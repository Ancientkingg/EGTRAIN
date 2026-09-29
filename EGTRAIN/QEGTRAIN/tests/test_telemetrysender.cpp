#include "telemetry/TelemetrySender.h"
#include "telemetry/TelemetryQueue.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSettings>
#include <QSemaphore>
#include <QSet>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <atomic>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#ifdef EGTRAIN_SENDER_TEST_HOOK
class ScriptedReply : public QNetworkReply {
public:
    explicit ScriptedReply(const QNetworkRequest& request, int code, QObject* owner,
                           QByteArray retry = {}, QNetworkReply::NetworkError error = QNetworkReply::NoError,
                           QByteArray body = {}, bool held = false)
        : QNetworkReply(owner), m_code(code), m_error(error), m_retry(std::move(retry)), m_body(std::move(body)) {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        if (!held) QTimer::singleShot(0, this, [this] { complete(); });
    }
    void complete() {
        if (done) return;
        if (m_code) setAttribute(QNetworkRequest::HttpStatusCodeAttribute, m_code);
        if (!m_retry.isEmpty()) setRawHeader("Retry-After", m_retry);
        if (m_error != QNetworkReply::NoError) setError(m_error, QString());
        else if (m_code >= 400) setError(QNetworkReply::ContentAccessDenied, QString());
        if (!m_body.isEmpty()) emit readyRead();
        if (!done) finish();
    }
    void abort() override {
        if (done) return;
        setError(QNetworkReply::OperationCanceledError, QString());
        finish();
    }
    qint64 readData(char* data, qint64 maxSize) override {
        if (m_offset >= m_body.size()) return -1;
        const qint64 count = qMin(maxSize, qint64(m_body.size() - m_offset));
        memcpy(data, m_body.constData() + m_offset, size_t(count));
        m_offset += int(count);
        return count;
    }
private:
    bool done = false;
    int m_code;
    QNetworkReply::NetworkError m_error;
    QByteArray m_retry, m_body;
    int m_offset = 0;
    void finish() { done = true; setFinished(true); emit finished(); }
};

static QJsonObject readObject(const QString& path) {
    QFile file(path);
    assert(file.open(QIODevice::ReadOnly));
    const auto doc = QJsonDocument::fromJson(file.readAll());
    assert(doc.isObject());
    return doc.object();
}

static bool waitForGate(telemetry::TelemetrySender& sender, const telemetry::TelemetryEventInput& input) {
    for (int i = 0; i < 100; ++i) {
        if (sender.tryEnqueue(input)) return true;
        QThread::msleep(10); // Startup readiness only; interleaving tests use semaphores.
    }
    return false;
}

static void testBackoffSaturation(const telemetry::Application& metadata, TelemetryContext context) {
    for (int attempt : {12, 13, 31}) for (int jitter : {50, 100, 150}) {
        QTemporaryDir directory;
        const QString settingsFile = directory.filePath(QStringLiteral("consent.ini"));
        const QString storage = directory.filePath(QStringLiteral("queue"));
        QSettings settings(settingsFile, QSettings::IniFormat);
        TelemetryConsent consent(settings, context);
        assert(consent.save(true, false));
        const auto base = QDateTime::currentDateTimeUtc().addSecs(10);
        {
            telemetry::TelemetryQueue seed;
            assert(seed.open(storage, context.endpoint, context.termsVersion, base) == telemetry::TelemetryQueue::OpenResult::Owner);
            assert(seed.enqueueUsage({telemetry::createEvent({}, base), consent.observeUsage().installationId}, base));
            telemetry::RetryState retry; retry.failures = attempt - 1;
            assert(seed.commitRetry(retry, base));
        }
        auto clock = std::make_shared<std::atomic<qint64>>(0);
        QSemaphore ready, completed, exited;
        telemetry::TelemetrySender::TestOptions options;
        options.settingsFactory = [settingsFile] { return std::unique_ptr<QSettings>(new QSettings(settingsFile, QSettings::IniFormat)); };
        options.utcNow = [clock, base] { return base.addMSecs(clock->load()); };
        options.monotonicMs = [clock] { return clock->load(); };
        options.jitterPercent = [jitter] { return jitter; };
        options.afterOwnershipAttempt = [&](bool owner) { assert(owner); ready.release(); };
        options.afterReservation = [&] {
            const auto retry = readObject(storage + QStringLiteral("/control.json")).value(QStringLiteral("retry")).toObject();
            assert(retry.value(QStringLiteral("failures")).toInt() == attempt);
            const auto next = QDateTime::fromString(retry.value(QStringLiteral("next")).toString(), Qt::ISODateWithMs);
            assert(base.addMSecs(clock->load()).msecsTo(next) == 86400000);
        };
        options.afterCompletion = [&] { completed.release(); };
        options.onWorkerExit = [&] { exited.release(); };
        options.post = [](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray&) {
            return new ScriptedReply(request, 503, &manager);
        };
        telemetry::TelemetrySender sender(context, metadata, storage, options);
        assert(ready.tryAcquire(1, 3000));
        *clock = 60001;
        assert(completed.tryAcquire(1, 3000));
        const auto retry = readObject(storage + QStringLiteral("/control.json")).value(QStringLiteral("retry")).toObject();
        const auto next = QDateTime::fromString(retry.value(QStringLiteral("next")).toString(), Qt::ISODateWithMs);
        assert(base.addMSecs(clock->load()).msecsTo(next) == qMin(86400000LL, 86400000LL * jitter / 100));
        sender.stop(); assert(exited.tryAcquire(1, 3000));
    }
}

static void testPathIsolation(const telemetry::Application& metadata, TelemetryContext context) {
    QTemporaryDir directory;
    assert(directory.isValid());
    const QString outside = QDir::homePath() + QStringLiteral("/egtrain-must-not-create-") + QFileInfo(directory.path()).fileName();
    assert(!QFile::exists(outside));
    const QString link = directory.filePath(QStringLiteral("outside-link"));
    assert(QFile::link(QDir::homePath(), link));
    const QString dangling = directory.filePath(QStringLiteral("dangling-link"));
    assert(QFile::link(outside, dangling));
    const QString queue = directory.filePath(QStringLiteral("queue"));
    const QString queueAlias = directory.filePath(QStringLiteral("queue-alias"));
    assert(QFile::link(queue, queueAlias));
    const QString traversed = QDir::tempPath() + QLatin1Char('/') + QDir(QDir::tempPath()).relativeFilePath(outside);
    const QString redirected = link + QLatin1Char('/') + QFileInfo(outside).fileName();
    const QString symlinkTraversal = link + QStringLiteral("/../") + QFileInfo(QDir::homePath()).fileName() +
        QLatin1Char('/') + QFileInfo(outside).fileName();
    const QString goodSettings = directory.filePath(QStringLiteral("consent.ini"));
    struct Paths { QString queue, settings; bool worker; };
    const Paths cases[] = {
        {traversed, goodSettings, false},
        {redirected, goodSettings, false},
        {symlinkTraversal, goodSettings, false},
        {dangling + QStringLiteral("/queue"), goodSettings, false},
        {queue, queue, true},
        {queue, redirected + QStringLiteral(".ini"), true},
        {queue, traversed + QStringLiteral(".ini"), true},
        {queue, queueAlias + QStringLiteral("/consent.ini"), true},
        {queue, queue + QStringLiteral("/../queue/consent.ini"), true}
    };
    for (const auto& paths : cases) {
        QSemaphore exited;
        telemetry::TelemetrySender::TestOptions options;
        options.settingsFactory = [paths] { return std::unique_ptr<QSettings>(new QSettings(paths.settings, QSettings::IniFormat)); };
        options.onWorkerExit = [&] { exited.release(); };
        options.post = [](QNetworkAccessManager&, const QNetworkRequest&, const QByteArray&) -> QNetworkReply* {
            assert(false && "invalid isolation must not dispatch"); return nullptr;
        };
        telemetry::TelemetrySender sender(context, metadata, paths.queue, options);
        if (paths.worker) assert(exited.tryAcquire(1, 3000));
        else QThread::msleep(100);
        assert(!sender.tryEnqueue({}));
        assert(!QFile::exists(queue) && !QFile::exists(outside) && !QFile::exists(outside + QStringLiteral(".ini")));
        sender.stop();
    }
    // An existing symlinked settings parent must resolve before separation is checked.
    assert(QDir().mkpath(queue));
    QSemaphore exited;
    telemetry::TelemetrySender::TestOptions options;
    options.settingsFactory = [queueAlias] {
        return std::unique_ptr<QSettings>(new QSettings(queueAlias + QStringLiteral("/consent.ini"), QSettings::IniFormat));
    };
    options.onWorkerExit = [&] { exited.release(); };
    telemetry::TelemetrySender sender(context, metadata, queue, options);
    assert(exited.tryAcquire(1, 3000));
    assert(QDir(queue).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
}

static void testOutcomes(const telemetry::Application& metadata, TelemetryContext context) {
    struct Case {
        int status;
        QNetworkReply::NetworkError error;
        QByteArray retry;
        qint64 delay;
        bool retained;
        int failures;
        int events;
        int limit;
    };
    const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(10);
    const QByteArray future = base.addMSecs(60001 + 300000).toUTC().toString(Qt::RFC2822Date).toLatin1();
    const Case cases[] = {
        {202, QNetworkReply::NoError, {}, 60000, false, 0, 1, 50},
        {400, QNetworkReply::ContentAccessDenied, {}, 60000, false, 0, 1, 50},
        {422, QNetworkReply::ContentAccessDenied, {}, 60000, false, 0, 1, 50},
        {413, QNetworkReply::ContentAccessDenied, {}, 30000, false, 0, 1, 1},
        {413, QNetworkReply::ContentAccessDenied, {}, 30000, true, 0, 2, 1},
        {429, QNetworkReply::ContentAccessDenied, "120", 120000, true, 1, 1, 50},
        {429, QNetworkReply::ContentAccessDenied, future, 300000, true, 1, 1, 50},
        {429, QNetworkReply::ContentAccessDenied, "invalid", 60000, true, 1, 1, 50},
        {429, QNetworkReply::ContentAccessDenied, "99999999999999999999999", 86400000, true, 1, 1, 50},
        {503, QNetworkReply::ServiceUnavailableError, {}, 60000, true, 1, 1, 50},
        {302, QNetworkReply::NoError, {}, 86400000, true, 0, 1, 50},
        {401, QNetworkReply::AuthenticationRequiredError, {}, 86400000, true, 0, 1, 50},
        {403, QNetworkReply::ContentAccessDenied, {}, 86400000, true, 0, 1, 50},
        {404, QNetworkReply::ContentNotFoundError, {}, 86400000, true, 0, 1, 50},
        {0, QNetworkReply::HostNotFoundError, {}, 60000, true, 1, 1, 50},
        {0, QNetworkReply::ConnectionRefusedError, {}, 60000, true, 1, 1, 50},
        {0, QNetworkReply::SslHandshakeFailedError, {}, 60000, true, 1, 1, 50}
    };
    for (const auto& scenario : cases) {
        QTemporaryDir directory;
        assert(directory.isValid());
        const QString settingsFile = directory.path() + QStringLiteral("/consent.ini");
        QSettings settings(settingsFile, QSettings::IniFormat);
        TelemetryConsent consent(settings, context);
        assert(consent.save(true, false));
        auto clock = std::make_shared<std::atomic<qint64>>(0);
        QSemaphore posted, completed, exited;
        int calls = 0;
        const QString storage = directory.path() + QStringLiteral("/queue");
        telemetry::TelemetrySender::TestOptions options;
        options.settingsFactory = [settingsFile] { return std::unique_ptr<QSettings>(new QSettings(settingsFile, QSettings::IniFormat)); };
        options.utcNow = [clock, base] { return base.addMSecs(clock->load()); };
        options.monotonicMs = [clock] { return clock->load(); };
        options.jitterPercent = [] { return 100; };
        options.afterCompletion = [&completed] { completed.release(); };
        options.onWorkerExit = [&exited] { exited.release(); };
        options.post = [&](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray& bytes) {
            assert(++calls == 1 && bytes.size() <= 65536);
            assert(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::ManualRedirectPolicy);
            assert(request.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt() == QNetworkRequest::Manual);
            assert(request.attribute(QNetworkRequest::CookieSaveControlAttribute).toInt() == QNetworkRequest::Manual);
            posted.release();
            return new ScriptedReply(request, scenario.status, &manager, scenario.retry, scenario.error);
        };
        telemetry::TelemetrySender sender(context, metadata, storage, options);
        telemetry::TelemetryEventInput input;
        for (int i = 0; i < scenario.events; ++i) assert(waitForGate(sender, input));
        *clock = 60001;
        assert(posted.tryAcquire(1, 3000) && completed.tryAcquire(1, 3000));
        const auto control = readObject(storage + QStringLiteral("/control.json"));
        const auto retry = control.value(QStringLiteral("retry")).toObject();
        assert(retry.value(QStringLiteral("failures")).toInt() == scenario.failures);
        assert(retry.value(QStringLiteral("usage_limit")).toInt() == scenario.limit);
        const auto next = QDateTime::fromString(retry.value(QStringLiteral("next")).toString(), Qt::ISODateWithMs);
        const qint64 observedDelay = base.addMSecs(60001).msecsTo(next);
        assert(scenario.retry == future ? observedDelay > scenario.delay - 1000 && observedDelay <= scenario.delay :
               observedDelay == scenario.delay);
        const auto usage = readObject(storage + QStringLiteral("/usage.json"));
        assert(usage.value(QStringLiteral("events")).toArray().size() ==
               (scenario.retained ? scenario.events : 0));
        sender.stop();
        assert(exited.tryAcquire(1, 3000));
    }
}

static void testPreparationBarrier(const telemetry::Application& metadata, TelemetryContext context) {
    for (int action = 0; action < 3; ++action) {
        QTemporaryDir directory;
        const QString settingsFile = directory.path() + QStringLiteral("/consent.ini");
        QSettings settings(settingsFile, QSettings::IniFormat);
        TelemetryConsent consent(settings, context);
        assert(consent.save(true, false));
        auto clock = std::make_shared<std::atomic<qint64>>(0);
        const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(10);
        QSemaphore reserved, proceed, settled, exited;
        std::atomic<int> posts{0};
        telemetry::TelemetrySender::TestOptions options;
        options.settingsFactory = [settingsFile] { return std::unique_ptr<QSettings>(new QSettings(settingsFile, QSettings::IniFormat)); };
        options.utcNow = [clock, base] { return base.addMSecs(clock->load()); };
        options.monotonicMs = [clock] { return clock->load(); };
        options.afterReservation = [&] {
            QTimer::singleShot(0, [&] { settled.release(); });
            reserved.release(); assert(proceed.tryAcquire(1, 3000));
        };
        options.onWorkerExit = [&] { exited.release(); };
        options.post = [&](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray&) {
            ++posts;
            return new ScriptedReply(request, 202, &manager);
        };
        telemetry::TelemetrySender sender(context, metadata, directory.path() + QStringLiteral("/queue"), options);
        assert(waitForGate(sender, {}));
        *clock = 60001;
        assert(reserved.tryAcquire(1, 3000));
        if (action == 0) sender.invalidateConsent(true, false);
        if (action == 1) sender.invalidateReceiver();
        if (action == 2) {
            QElapsedTimer timer; timer.start(); sender.stop();
            assert(timer.elapsed() < 100);
        }
        proceed.release();
        if (action != 2) {
            assert(settled.tryAcquire(1, 3000)); // Dispatch boundary ran without stop masking invalidation.
            assert(posts == 0);
            sender.stop();
        }
        assert(exited.tryAcquire(1, 3000));
        assert(posts == 0);
    }
}

static void testInterruptedBackoff(const telemetry::Application& metadata, TelemetryContext context) {
    QTemporaryDir directory;
    const QString settingsFile = directory.path() + QStringLiteral("/consent.ini");
    const QString storage = directory.path() + QStringLiteral("/queue");
    QSettings settings(settingsFile, QSettings::IniFormat);
    TelemetryConsent consent(settings, context);
    assert(consent.save(true, false));
    auto clock = std::make_shared<std::atomic<qint64>>(0);
    const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(10);
    const auto optionsBase = [&] {
        telemetry::TelemetrySender::TestOptions options;
        options.settingsFactory = [settingsFile] { return std::unique_ptr<QSettings>(new QSettings(settingsFile, QSettings::IniFormat)); };
        options.utcNow = [clock, base] { return base.addMSecs(clock->load()); };
        options.monotonicMs = [clock] { return clock->load(); };
        return options;
    };
    QString originalId;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        QSemaphore reserved, proceed, exited, posted, completed;
        auto options = optionsBase();
        if (attempt < 3) {
            options.afterReservation = [&] { reserved.release(); assert(proceed.tryAcquire(1, 3000)); };
            options.post = [](QNetworkAccessManager&, const QNetworkRequest&, const QByteArray&) -> QNetworkReply* {
                assert(false && "interrupted preparation must not transmit"); return nullptr;
            };
        } else {
            options.post = [&](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray& bytes) {
                const QString id = QJsonDocument::fromJson(bytes).object().value(QStringLiteral("events"))
                    .toArray().first().toObject().value(QStringLiteral("event_id")).toString();
                assert(id == originalId);
                posted.release();
                return new ScriptedReply(request, 202, &manager);
            };
            options.afterCompletion = [&] { completed.release(); };
        }
        options.onWorkerExit = [&] { exited.release(); };
        telemetry::TelemetrySender sender(context, metadata, storage, options);
        if (attempt == 1) assert(waitForGate(sender, {}));
        *clock = attempt == 1 ? 60001 : attempt == 2 ? 120002 : 300003;
        if (attempt < 3) {
            assert(reserved.tryAcquire(1, 3000));
            const auto retry = readObject(storage + QStringLiteral("/control.json")).value(QStringLiteral("retry")).toObject();
            assert(retry.value(QStringLiteral("failures")).toInt() == attempt);
            const auto deadline = QDateTime::fromString(retry.value(QStringLiteral("next")).toString(), Qt::ISODateWithMs);
            assert(base.addMSecs(clock->load()).msecsTo(deadline) == (attempt == 1 ? 60000 : 120000));
            const auto records = readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray();
            assert(records.size() == 1);
            const QString id = records.first().toObject().value(QStringLiteral("event")).toObject()
                .value(QStringLiteral("event_id")).toString();
            if (attempt == 1) originalId = id; else assert(id == originalId);
            sender.stop(); proceed.release();
        } else {
            assert(posted.tryAcquire(1, 3000) && completed.tryAcquire(1, 3000));
            assert(readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray().isEmpty());
            sender.stop();
        }
        assert(exited.tryAcquire(1, 3000));
    }
}

static void testTimeoutAndExceptions(const telemetry::Application& metadata, TelemetryContext context) {
    for (int scenario = 0; scenario < 5; ++scenario) {
        QTemporaryDir directory;
        const QString settingsFile = directory.path() + QStringLiteral("/consent.ini");
        const QString storage = directory.path() + QStringLiteral("/queue");
        QSettings settings(settingsFile, QSettings::IniFormat);
        TelemetryConsent consent(settings, context);
        assert(consent.save(true, false));
        auto clock = std::make_shared<std::atomic<qint64>>(0);
        const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(10);
        QSemaphore posted, completed, exited;
        int requests = 0;
        QString firstId;
        ScriptedReply* delayed = nullptr;
        telemetry::TelemetrySender::TestOptions options;
        options.settingsFactory = [settingsFile] { return std::unique_ptr<QSettings>(new QSettings(settingsFile, QSettings::IniFormat)); };
        options.utcNow = [clock, base] { return base.addMSecs(clock->load()); };
        options.monotonicMs = [clock] { return clock->load(); };
        options.onWorkerExit = [&] { exited.release(); };
        options.afterCompletion = [&] { completed.release(); };
        options.jitterPercent = [] { return 100; };
        if (scenario == 0) options.requestTimeoutMs = 30;
        if (scenario == 3) options.afterReservation = [] { throw std::bad_alloc(); };
        options.post = [&](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray& bytes) {
            const QString id = QJsonDocument::fromJson(bytes).object().value(QStringLiteral("events"))
                .toArray().first().toObject().value(QStringLiteral("event_id")).toString();
            ++requests;
            if (scenario == 0) { if (requests == 1) firstId = id; else assert(id == firstId); }
            auto* scripted = new ScriptedReply(request, 202, &manager, {}, QNetworkReply::NoError,
                                     scenario == 1 ? QByteArray(8192, 'x') :
                                     scenario == 2 ? QByteArray(8193, 'x') : QByteArray(),
                                     scenario == 4 || (scenario == 0 && requests == 1));
            if (scenario == 4) delayed = scripted;
            posted.release();
            return scripted;
        };
        telemetry::TelemetrySender sender(context, metadata, storage, options);
        assert(waitForGate(sender, {}));
        *clock = 60001;
        if (scenario == 3) {
            assert(exited.tryAcquire(1, 3000));
            assert(!posted.tryAcquire(1, 0));
            assert(!sender.tryEnqueue({}));
            continue;
        }
        assert(posted.tryAcquire(1, 3000));
        if (scenario == 4) {
            assert(delayed && !completed.tryAcquire(1, 0));
            *clock = 60501;
            QMetaObject::invokeMethod(delayed, [delayed] { delayed->complete(); }, Qt::QueuedConnection);
        }
        assert(completed.tryAcquire(1, 3000));
        const auto records = readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray();
        assert(records.size() == (scenario == 0 ? 1 : 0));
        const auto retry = readObject(storage + QStringLiteral("/control.json")).value(QStringLiteral("retry")).toObject();
        assert(retry.value(QStringLiteral("failures")).toInt() == (scenario == 0 ? 1 : 0));
        if (scenario == 0) {
            *clock = 120002;
            assert(posted.tryAcquire(1, 3000) && completed.tryAcquire(1, 3000));
            assert(requests == 2);
            assert(readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray().isEmpty());
            assert(readObject(storage + QStringLiteral("/control.json")).value(QStringLiteral("retry"))
                   .toObject().value(QStringLiteral("failures")).toInt() == 0);
        }
        sender.stop();
        assert(exited.tryAcquire(1, 3000));
    }
}

static void testExpiryAndActiveRevocation(const telemetry::Application& metadata, TelemetryContext context) {
    for (int scenario = 0; scenario < 3; ++scenario) {
        QTemporaryDir directory;
        const QString settingsFile = directory.path() + QStringLiteral("/consent.ini");
        const QString storage = directory.path() + QStringLiteral("/queue");
        QSettings settings(settingsFile, QSettings::IniFormat);
        TelemetryConsent consent(settings, context);
        assert(consent.save(true, false));
        auto clock = std::make_shared<std::atomic<qint64>>(0);
        const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(10);
        auto utcClock = std::make_shared<std::atomic<qint64>>(0);
        if (scenario == 0) {
            telemetry::TelemetryQueue seed;
            assert(seed.open(storage, context.endpoint, context.termsVersion, base) == telemetry::TelemetryQueue::OpenResult::Owner);
            assert(seed.enqueueUsage({telemetry::createEvent({}, base.addSecs(-6 * 86400 - 23 * 3600)),
                                      consent.observeUsage().installationId}, base));
        }
        QSemaphore posted, completed, polled, exited;
        std::atomic<int> calls{0};
        telemetry::TelemetrySender::TestOptions options;
        options.settingsFactory = [settingsFile] { return std::unique_ptr<QSettings>(new QSettings(settingsFile, QSettings::IniFormat)); };
        options.utcNow = [utcClock, base] { return base.addMSecs(utcClock->load()); };
        options.monotonicMs = [clock] { return clock->load(); };
        options.onWorkerExit = [&] { exited.release(); };
        options.afterCompletion = [&] { completed.release(); };
        options.afterPoll = [&] { polled.release(); };
        options.post = [&](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray&) {
            ++calls; posted.release();
            return new ScriptedReply(request, scenario == 0 ? 403 : 202, &manager, {},
                                     QNetworkReply::NoError, {}, scenario != 0);
        };
        telemetry::TelemetrySender sender(context, metadata, storage, options);
        if (scenario != 0) assert(waitForGate(sender, {}));
        else assert(polled.tryAcquire(1, 3000));
        *utcClock = 60001;
        *clock = 60001;
        assert(posted.tryAcquire(1, 3000));
        if (scenario == 0) {
            assert(completed.tryAcquire(1, 3000));
            const auto retry = readObject(storage + QStringLiteral("/control.json")).value(QStringLiteral("retry")).toObject();
            assert(retry.value(QStringLiteral("failures")).toInt() == 0);
            const auto uploadDeadline = QDateTime::fromString(retry.value(QStringLiteral("next")).toString(), Qt::ISODateWithMs);
            assert(base.addMSecs(60001).msecsTo(uploadDeadline) == 86400000);
            assert(readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray().size() == 1);
            *utcClock = 3600001; // Old row crosses seven days, but neither upload deadline has elapsed.
            *clock = 61002;
            assert(base.addMSecs(utcClock->load()) < uploadDeadline);
            assert(clock->load() < 60001 + 86400000);
            sender.requestConsentRefresh();
            assert(polled.tryAcquire(1, 3000));
            // Poll notification can include the startup poll; drain and observe the actual file condition.
            for (int i = 0; i < 100 && !readObject(storage + QStringLiteral("/usage.json"))
                     .value(QStringLiteral("events")).toArray().isEmpty(); ++i) {
                assert(polled.tryAcquire(1, 3000));
                *clock += 1001;
            }
            assert(readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray().isEmpty());
            assert(calls == 1 && base.addMSecs(utcClock->load()) < uploadDeadline);
            assert(clock->load() < 60001 + 86400000);
        } else {
            if (scenario == 1) sender.invalidateConsent(true, false);
            else {
                // Worker misses the disabled interval; the new stamp still removes the old row.
                QSettings other(settingsFile, QSettings::IniFormat);
                TelemetryConsent otherConsent(other, context);
                assert(otherConsent.save(false, false));
                assert(otherConsent.save(true, false));
                sender.requestConsentRefresh();
            }
            assert(completed.tryAcquire(1, 3000));
            sender.requestConsentRefresh();
            *clock = 61002;
            bool removed = false;
            for (int i = 0; i < 100; ++i) {
                assert(polled.tryAcquire(1, 3000));
                removed = readObject(storage + QStringLiteral("/usage.json"))
                    .value(QStringLiteral("events")).toArray().isEmpty();
                if (removed) break;
                *clock += 1001;
            }
            assert(removed && calls == 1);
        }
        sender.stop();
        assert(exited.tryAcquire(1, 3000));
    }
}

static void testConstructionAndWorkerExceptions(const telemetry::Application& metadata, TelemetryContext context) {
    QTemporaryDir directory;
    telemetry::TelemetrySender::TestOptions options;
    options.settingsFactory = [] { return std::unique_ptr<QSettings>(new QSettings); };
    options.failConstruction = true;
    telemetry::TelemetrySender failed(context, metadata, directory.path() + QStringLiteral("/construction"), options);
    assert(!failed.tryEnqueue({}));
    options.failConstruction = false;
    QSemaphore invoked, exited;
    options.settingsFactory = [&]() -> std::unique_ptr<QSettings> {
        invoked.release();
        throw std::bad_alloc();
    };
    options.onWorkerExit = [&] { exited.release(); };
    telemetry::TelemetrySender worker(context, metadata, directory.path() + QStringLiteral("/worker"), options);
    assert(invoked.tryAcquire(1, 3000) && exited.tryAcquire(1, 3000));
    assert(!worker.tryEnqueue({}));
    worker.stop();
}

static void testFollowerTakeover(const telemetry::Application& metadata, TelemetryContext context) {
    QTemporaryDir directory;
    const QString settingsFile = directory.path() + QStringLiteral("/consent.ini");
    const QString storage = directory.path() + QStringLiteral("/queue");
    QSettings settings(settingsFile, QSettings::IniFormat);
    TelemetryConsent consent(settings, context);
    assert(consent.save(true, false));
    auto clock = std::make_shared<std::atomic<qint64>>(0);
    const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(10);
    QSemaphore firstPosted, secondPosted, secondCompleted, firstExited, secondExited, followerAttempt;
    QString id;
    const auto optionsBase = [&] {
        telemetry::TelemetrySender::TestOptions options;
        options.settingsFactory = [settingsFile] { return std::unique_ptr<QSettings>(new QSettings(settingsFile, QSettings::IniFormat)); };
        options.utcNow = [clock, base] { return base.addMSecs(clock->load()); };
        options.monotonicMs = [clock] { return clock->load(); };
        return options;
    };
    auto firstOptions = optionsBase();
    firstOptions.onWorkerExit = [&] { firstExited.release(); };
    firstOptions.post = [&](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray& body) {
        id = QJsonDocument::fromJson(body).object().value(QStringLiteral("events"))
            .toArray().first().toObject().value(QStringLiteral("event_id")).toString();
        firstPosted.release();
        return new ScriptedReply(request, 202, &manager, {}, QNetworkReply::NoError, {}, true);
    };
    telemetry::TelemetrySender first(context, metadata, storage, firstOptions);
    assert(waitForGate(first, {}));
    *clock = 60001;
    assert(firstPosted.tryAcquire(1, 3000) && !id.isEmpty());
    auto secondOptions = optionsBase();
    secondOptions.onWorkerExit = [&] { secondExited.release(); };
    secondOptions.afterOwnershipAttempt = [&](bool owner) { if (!owner) followerAttempt.release(); };
    secondOptions.afterCompletion = [&] { secondCompleted.release(); };
    secondOptions.post = [&](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray& body) {
        const auto reused = QJsonDocument::fromJson(body).object().value(QStringLiteral("events"))
            .toArray().first().toObject().value(QStringLiteral("event_id")).toString();
        assert(reused == id);
        secondPosted.release();
        return new ScriptedReply(request, 202, &manager);
    };
    telemetry::TelemetrySender second(context, metadata, storage, secondOptions);
    assert(followerAttempt.tryAcquire(1, 3000));
    assert(!second.tryEnqueue({})); // The follower does not buffer producer intents.
    first.stop();
    assert(firstExited.tryAcquire(1, 3000));
    *clock = 120002;
    assert(secondPosted.tryAcquire(1, 3000) && secondCompleted.tryAcquire(1, 3000));
    assert(readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray().isEmpty());
    second.stop();
    assert(secondExited.tryAcquire(1, 3000));
}

static void testRetirementWithQueuedWork(const telemetry::Application& metadata, TelemetryContext context) {
    QTemporaryDir directory;
    const QString settingsFile = directory.path() + QStringLiteral("/consent.ini");
    const QString storage = directory.path() + QStringLiteral("/queue");
    QSettings settings(settingsFile, QSettings::IniFormat);
    TelemetryConsent consent(settings, context);
    assert(consent.save(true, false));
    auto clock = std::make_shared<std::atomic<qint64>>(0);
    const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(10);
    QSemaphore posted, completed, exited;
    ScriptedReply* pending = nullptr;
    telemetry::TelemetrySender::TestOptions options;
    options.settingsFactory = [settingsFile] { return std::unique_ptr<QSettings>(new QSettings(settingsFile, QSettings::IniFormat)); };
    options.utcNow = [clock, base] { return base.addMSecs(clock->load()); };
    options.monotonicMs = [clock] { return clock->load(); };
    options.onWorkerExit = [&] { exited.release(); };
    options.afterCompletion = [&] { completed.release(); };
    options.post = [&](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray&) {
        pending = new ScriptedReply(request, 410, &manager, {}, QNetworkReply::NoError, {}, true);
        posted.release();
        return pending;
    };
    telemetry::TelemetrySender sender(context, metadata, storage, options);
    assert(waitForGate(sender, {}));
    *clock = 60001;
    assert(posted.tryAcquire(1, 3000) && pending);
    assert(sender.tryEnqueue({})); // A second event arrives while the first request is outstanding.
    QMetaObject::invokeMethod(pending, [pending] { pending->complete(); }, Qt::QueuedConnection);
    assert(completed.tryAcquire(1, 3000));
    sender.stop();
    assert(exited.tryAcquire(1, 3000));
    assert(readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray().isEmpty());
    telemetry::TelemetryQueue reopened;
    assert(reopened.open(storage, context.endpoint, context.termsVersion, base.addMSecs(60001)) ==
           telemetry::TelemetryQueue::OpenResult::Retired);
    reopened.close();
}

static void testSplitRetainsIds(const telemetry::Application& metadata, TelemetryContext context) {
    QTemporaryDir directory;
    const QString settingsFile = directory.path() + QStringLiteral("/consent.ini");
    const QString storage = directory.path() + QStringLiteral("/queue");
    QSettings settings(settingsFile, QSettings::IniFormat);
    TelemetryConsent consent(settings, context);
    assert(consent.save(true, false));
    auto clock = std::make_shared<std::atomic<qint64>>(0);
    const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(10);
    QSemaphore posted, completed, exited;
    int attempts = 0;
    QSet<QString> ids, acknowledged;
    telemetry::TelemetrySender::TestOptions options;
    options.settingsFactory = [settingsFile] { return std::unique_ptr<QSettings>(new QSettings(settingsFile, QSettings::IniFormat)); };
    options.utcNow = [clock, base] { return base.addMSecs(clock->load()); };
    options.monotonicMs = [clock] { return clock->load(); };
    options.onWorkerExit = [&] { exited.release(); };
    options.afterCompletion = [&] { completed.release(); };
    options.post = [&](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray& bytes) {
        const auto events = QJsonDocument::fromJson(bytes).object().value(QStringLiteral("events")).toArray();
        ++attempts;
        if (attempts == 1) {
            assert(events.size() == 2);
            for (const auto& event : events) ids.insert(event.toObject().value(QStringLiteral("event_id")).toString());
            assert(ids.size() == 2);
        } else {
            assert(events.size() == 1);
            const QString id = events.first().toObject().value(QStringLiteral("event_id")).toString();
            assert(ids.contains(id) && !acknowledged.contains(id));
            acknowledged.insert(id);
        }
        posted.release();
        return new ScriptedReply(request, attempts == 1 ? 413 : 202, &manager);
    };
    telemetry::TelemetrySender sender(context, metadata, storage, options);
    assert(waitForGate(sender, {}));
    assert(sender.tryEnqueue({}));
    for (qint64 moment : {60001LL, 90002LL, 150003LL}) {
        *clock = moment;
        assert(posted.tryAcquire(1, 3000) && completed.tryAcquire(1, 3000));
    }
    assert(attempts == 3 && acknowledged == ids);
    assert(readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray().isEmpty());
    sender.stop();
    assert(exited.tryAcquire(1, 3000));
}
#endif

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    assert(directory.isValid());
    telemetry::Application metadata{QStringLiteral("1.2.3"), QString(), QString()};
    TelemetryContext context;
    context.packaged = true;
    context.interactive = true;
    context.enabled = false;
    context.endpoint = QStringLiteral("https://127.0.0.1:1/collect");
    telemetry::TelemetryEventInput input;
    {
        telemetry::TelemetrySender sender(context, metadata, directory.path());
        for (int i = 0; i < 1000; ++i) assert(!sender.tryEnqueue(input));
        sender.invalidateConsent(true, true);
        sender.requestConsentRefresh();
        sender.invalidateReceiver();
        sender.stop();
        assert(!sender.tryEnqueue(input));
    }
    context.enabled = true;
    context.endpoint.clear();
    telemetry::TelemetrySender emptyEndpoint(context, metadata, directory.path());
    assert(!emptyEndpoint.tryEnqueue(input));
    context.endpoint = QStringLiteral("http://127.0.0.1:1/collect");
    telemetry::TelemetrySender insecureEndpoint(context, metadata, directory.path());
    assert(!insecureEndpoint.tryEnqueue(input));
#ifdef EGTRAIN_SENDER_TEST_HOOK
    context.endpoint = QStringLiteral("https://example.org/collect");
    telemetry::TelemetrySender::TestOptions rejected;
    rejected.settingsFactory = [] { return std::unique_ptr<QSettings>(new QSettings); };
    rejected.post = [](QNetworkAccessManager&, const QNetworkRequest&, const QByteArray&) -> QNetworkReply* {
        assert(false && "non-loopback test transport must not run");
        return nullptr;
    };
    telemetry::TelemetrySender publicOverride(context, metadata, directory.path(), rejected);
    assert(!publicOverride.tryEnqueue(input));

    context.endpoint = QStringLiteral("https://127.0.0.1:1/collect");
    if (argc == 2) {
        const QByteArray selected(argv[1]);
        if (selected == "isolation") testPathIsolation(metadata, context);
        else if (selected == "backoff") testBackoffSaturation(metadata, context);
        else if (selected == "barrier") testPreparationBarrier(metadata, context);
        else if (selected == "expiry") testExpiryAndActiveRevocation(metadata, context);
        else { assert(selected == "factory"); testConstructionAndWorkerExceptions(metadata, context); }
        return 0;
    }
    testPathIsolation(metadata, context);
    testBackoffSaturation(metadata, context);
    testOutcomes(metadata, context);
    testPreparationBarrier(metadata, context);
    testInterruptedBackoff(metadata, context);
    testTimeoutAndExceptions(metadata, context);
    testExpiryAndActiveRevocation(metadata, context);
    testConstructionAndWorkerExceptions(metadata, context);
    testFollowerTakeover(metadata, context);
    testRetirementWithQueuedWork(metadata, context);
    testSplitRetainsIds(metadata, context);
    const QString settingsFile = directory.path() + QStringLiteral("/settings.ini");
    QSettings settings(settingsFile, QSettings::IniFormat);
    TelemetryConsent consent(settings, context);
    assert(consent.save(true, false));
    auto clock = std::make_shared<std::atomic<qint64>>(0);
    const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(2);
    auto posts = std::make_shared<std::atomic<int>>(0);
    auto stableId = std::make_shared<std::atomic<bool>>(false);
    auto firstId = std::make_shared<QString>();
    telemetry::TelemetrySender::TestOptions tests;
    tests.settingsFactory = [settingsFile] {
        return std::unique_ptr<QSettings>(new QSettings(settingsFile, QSettings::IniFormat));
    };
    tests.monotonicMs = [clock] { return clock->load(); };
    tests.utcNow = [clock, base] { return base.addMSecs(clock->load()); };
    tests.jitterPercent = [] { return 100; };
    tests.post = [posts, stableId, firstId](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray& body) {
        assert(request.url().host() == QStringLiteral("127.0.0.1"));
        assert(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::ManualRedirectPolicy);
        assert(request.attribute(QNetworkRequest::AuthenticationReuseAttribute).toInt() == QNetworkRequest::Manual);
        assert(body.size() <= 65536 && QJsonDocument::fromJson(body).isObject());
        const QString id = QJsonDocument::fromJson(body).object().value(QStringLiteral("events"))
            .toArray().first().toObject().value(QStringLiteral("event_id")).toString();
        const int attempt = ++*posts;
        if (attempt == 1) *firstId = id;
        if (attempt == 2) *stableId = id == *firstId;
        return new ScriptedReply(request, attempt == 1 ? 429 : attempt == 2 ? 202 : 410, &manager,
                                 attempt == 1 ? QByteArray("120") : QByteArray());
    };
    telemetry::TelemetrySender sender(context, metadata, directory.path() + QStringLiteral("/queue"), tests);
    bool accepted = false;
    for (int i = 0; i < 100 && !accepted; ++i) {
        QThread::msleep(20);
        accepted = sender.tryEnqueue(input);
    }
    assert(accepted);
    sender.invalidateConsent(true, false);
    assert(!sender.tryEnqueue(input));
    QThread::msleep(100);
    sender.requestConsentRefresh();
    accepted = false;
    for (int i = 0; i < 100 && !accepted; ++i) {
        QThread::msleep(20);
        accepted = sender.tryEnqueue(input);
    }
    assert(accepted);
    *clock = 60001;
    for (int i = 0; i < 100 && *posts < 1; ++i) QThread::msleep(20);
    assert(*posts == 1);
    QThread::msleep(100);
    *clock = 120002;
    QThread::msleep(100);
    assert(*posts == 1); // Retry-After wins over one-minute backoff.
    *clock = 180003;
    for (int i = 0; i < 100 && *posts < 2; ++i) QThread::msleep(20);
    assert(*posts == 2 && *stableId);
    QThread::msleep(100);
    assert(sender.tryEnqueue(input));
    *clock = 240004;
    for (int i = 0; i < 100 && *posts < 3; ++i) QThread::msleep(20);
    assert(*posts == 3);
    QThread::msleep(100);
    sender.stop();
    assert(!sender.tryEnqueue(input));
    QThread::msleep(100);
    telemetry::TelemetrySender restarted(context, metadata, directory.path() + QStringLiteral("/queue"), tests);
    for (int i = 0; i < 50; ++i) {
        QThread::msleep(20);
        assert(!restarted.tryEnqueue(input));
    }
    restarted.stop();
    assert(*posts == 3);
#endif
}
