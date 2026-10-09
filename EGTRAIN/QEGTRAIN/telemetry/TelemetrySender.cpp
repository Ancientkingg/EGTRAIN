#include "telemetry/TelemetrySender.h"
#include "telemetry/TelemetryQueue.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <atomic>
#include <array>
#include <algorithm>
#include <limits>
#include <new>
#include <mutex>
#ifdef EGTRAIN_SENDER_TEST_HOOK
#include <QFileInfo>
#include <filesystem>
#endif

namespace telemetry {
namespace {
constexpr qint64 kDay = 86400000;
constexpr qint64 kMaxDelay = kDay;
constexpr int kIngress = 128;
qint64 backoffBase(int failures) {
    return std::min(kMaxDelay, 60000LL << std::max(0, std::min(11, failures - 1)));
}
#ifdef EGTRAIN_SENDER_TEST_HOOK
QString isolatedPath(const QString& value) {
    namespace fs = std::filesystem;
    const auto path = fs::u8path(value.toUtf8().toStdString());
    if (!path.is_absolute() || path.filename().empty() || path.filename() == "." || path.filename() == "..") return {};
    std::error_code error;
    // Resolve the raw parent before normalizing '..', including symlinked ancestors.
    // Missing/unresolvable parents fail closed rather than being created during validation.
    const auto parent = fs::canonical(path.parent_path(), error);
    if (error || !fs::is_directory(parent, error) || error) return {};
    const auto resolved = parent / path.filename();
    const QString result = QString::fromUtf8(resolved.generic_u8string().c_str());
    if (QFileInfo(result).isSymLink()) return {};
    const auto temporary = fs::canonical(fs::u8path(QDir::tempPath().toUtf8().toStdString()), error);
    if (error) return {};
    const QString root = QString::fromUtf8(temporary.generic_u8string().c_str());
    return result.startsWith(root + QLatin1Char('/')) ? result : QString();
}
#endif
Category categoryOf(const TelemetryEventInput& input) {
    return input.name == Name::OperationFailed ? Category::Diagnostics : Category::Usage;
}
QDateTime utcNow() { return QDateTime::currentDateTimeUtc(); }
qint64 retryAfter(const QByteArray& raw, const QDateTime& now) {
    if (raw.size() > 96 || raw.isEmpty()) return 0;
    for (char c : raw) if (static_cast<unsigned char>(c) > 127) return 0;
    bool digits = true;
    for (char c : raw) if (c < '0' || c > '9') digits = false;
    if (digits) {
        qint64 seconds = 0;
        for (char c : raw) {
            if (seconds > 86400) return kDay;
            seconds = seconds * 10 + (c - '0');
        }
        return std::min(seconds, qint64(86400)) * 1000;
    }
    const QDateTime date = QDateTime::fromString(QString::fromLatin1(raw), Qt::RFC2822Date);
    return date.isValid() ? std::max(qint64(0), std::min(kDay, now.msecsTo(date.toUTC()))) : 0;
}
class EmptyCookies : public QNetworkCookieJar {
public:
    QList<QNetworkCookie> cookiesForUrl(const QUrl&) const override { return {}; }
    bool setCookiesFromUrl(const QList<QNetworkCookie>&, const QUrl&) override { return false; }
};
Application normalized(Application app) {
#ifdef Q_OS_MACOS
    app.platform = QStringLiteral("macos");
#elif defined(Q_OS_WIN)
    app.platform = QStringLiteral("windows");
#elif defined(Q_OS_LINUX)
    app.platform = QStringLiteral("linux");
#else
    app.platform.clear();
#endif
    const QString arch = QSysInfo::buildCpuArchitecture();
    app.architecture = arch == QStringLiteral("arm64") || arch == QStringLiteral("aarch64")
        ? QStringLiteral("arm64")
        : arch == QStringLiteral("x86_64")
            ? QStringLiteral("x86_64")
            : QString();
    return app;
}
}
struct TelemetrySender::Shared {
    struct Intent { TelemetryEventInput input; QDateTime occurred; quint64 epoch = 0; };
    QMutex mutex;
    std::array<Intent, kIngress> ring;
    int head = 0, count = 0;
    bool gate[2] = {false, false};
    quint64 epoch[2] = {1, 1};
    std::atomic<bool> stop{false};
    std::atomic<bool> interactiveSessionRequested{false};
    // Category-specific serials close the gap when invalidation cannot take ingress.
    // The existing global permit version still guards worker/network reservations.
    std::atomic<quint64> invalidation[2]{};
    std::atomic<unsigned> flags{0}; // usage, diagnostics, receiver, refresh
    // High bits count invalidations; low bits publish gates in the same atomic word.
    std::atomic<quint64> permits{0};
    TelemetryContext context;
    Application application;
    QString directory;
#ifdef EGTRAIN_SENDER_TEST_HOOK
    TestOptions tests;
#endif
};
class SenderThread : public QThread {
public:
    explicit SenderThread(std::shared_ptr<TelemetrySender::Shared> state) : state(std::move(state)) {}
    std::shared_ptr<TelemetrySender::Shared> state;
    void run() override;
};
// The self-owned thread is never joined or destroyed while running. Its shared
// ingress survives facade destruction, including a stalled filesystem operation.
TelemetrySender::TelemetrySender(TelemetryContext context, Application application, QString directory
#ifdef EGTRAIN_SENDER_TEST_HOOK
    , TestOptions tests
#endif
    ) {
    try {
    application = normalized(std::move(application));
    if (!context.capable() || !validApplication(application) || directory.isEmpty() ||
        context.endpoint.toUtf8().size() > 2048) return;
#ifdef EGTRAIN_SENDER_TEST_HOOK
    if (tests.settingsFactory || tests.utcNow || tests.monotonicMs || tests.jitterPercent || tests.afterReservation || tests.onWorkerExit || tests.afterCompletion || tests.afterPoll || tests.afterOperationCaptureLocked || tests.beforeSessionAttempt || tests.afterOwnershipAttempt || tests.failConstruction || tests.post || tests.loopbackTrust || tests.requestTimeoutMs != 10000) {
        const QUrl url(context.endpoint);
        const QString host = url.host();
        if (!tests.settingsFactory || (host != QStringLiteral("127.0.0.1") && host != QStringLiteral("::1"))) return;
        // A test transport still requires a valid HTTPS context and independently isolated storage.
        directory = isolatedPath(directory);
        if (directory.isEmpty() || tests.requestTimeoutMs < 1 || tests.requestTimeoutMs > 10000) return;
    }
#endif
#ifdef EGTRAIN_SENDER_TEST_HOOK
    if (tests.failConstruction) throw std::bad_alloc();
#endif
    m_shared = std::make_shared<Shared>();
    m_shared->context = std::move(context);
    m_shared->application = std::move(application);
    m_shared->directory = std::move(directory);
#ifdef EGTRAIN_SENDER_TEST_HOOK
    m_shared->tests = std::move(tests);
#endif
    auto* thread = new SenderThread(m_shared);
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    } catch (...) { m_shared.reset(); } // Telemetry must not prevent application startup.
}
TelemetrySender::~TelemetrySender() { stop(); }
TelemetrySender::OperationToken TelemetrySender::captureOperation() noexcept {
    OperationToken token;
    auto s = m_shared;
    if (!s || s->stop.load(std::memory_order_acquire)) return token;
    try {
        if (!s->mutex.tryLock()) return token;
        std::unique_lock<QMutex> lock(s->mutex, std::adopt_lock);
        token.sender = s;
        for (int i = 0; i < 2; ++i) {
            token.invalidation[i] = s->invalidation[i].load(std::memory_order_acquire);
            token.epoch[i] = s->epoch[i];
            token.eligible[i] = !s->stop.load(std::memory_order_acquire) && s->gate[i]
                && (s->permits.load(std::memory_order_acquire) & (quint64(1) << i))
                && !(s->flags.load(std::memory_order_acquire) & ((1u << i) | 4u));
        }
#ifdef EGTRAIN_SENDER_TEST_HOOK
        if (s->tests.afterOperationCaptureLocked) s->tests.afterOperationCaptureLocked();
#endif
        return token;
    } catch (...) { return {}; }
}
bool TelemetrySender::tryEnqueue(const TelemetryEventInput& input, const OperationToken& token) noexcept {
    auto s = m_shared;
    if (!s || token.sender.lock() != s || s->stop.load(std::memory_order_acquire)) return false;
    try {
        const Category category = categoryOf(input);
        const int idx = category == Category::Usage ? 0 : 1;
        if (!token.eligible[idx] || !validInput(input, category) || !s->mutex.tryLock()) return false;
        std::unique_lock<QMutex> lock(s->mutex, std::adopt_lock);
        if (s->stop.load(std::memory_order_acquire) || !s->gate[idx]
            || !(s->permits.load(std::memory_order_acquire) & (quint64(1) << idx))
            || (s->flags.load(std::memory_order_acquire) & ((1u << idx) | 4u))
            || token.epoch[idx] != s->epoch[idx]
            || token.invalidation[idx] != s->invalidation[idx].load(std::memory_order_acquire)
            || s->count >= kIngress) return false;
        s->ring[(s->head + s->count) % kIngress] = {input, utcNow(), s->epoch[idx]};
        ++s->count;
        return true;
    } catch (...) { return false; }
}
void TelemetrySender::requestInteractiveSession() noexcept {
    if (m_shared && m_shared->context.interactive)
        m_shared->interactiveSessionRequested.store(true, std::memory_order_release);
}
bool TelemetrySender::tryEnqueue(const TelemetryEventInput& input) noexcept {
    auto s = m_shared;
    if (!s || s->stop.load(std::memory_order_acquire)) return false;
    const Category category = categoryOf(input);
    try {
        if (!validInput(input, category) || !s->mutex.tryLock()) return false;
        try {
            const int idx = category == Category::Usage ? 0 : 1;
            bool accepted = false;
            if (!s->stop.load(std::memory_order_relaxed) && (s->permits.load(std::memory_order_acquire) & (quint64(1) << idx))
                && s->gate[idx] && s->count < kIngress) {
                s->ring[(s->head + s->count) % kIngress] = {input, utcNow(), s->epoch[idx]};
                ++s->count;
                accepted = true;
            }
            s->mutex.unlock();
            return accepted;
        } catch (...) { s->mutex.unlock(); throw; }
    } catch (...) { return false; }
}
void TelemetrySender::invalidateConsent(bool usage, bool diagnostics) noexcept {
    auto s = m_shared;
    if (!s) return;
    for (int i = 0; i < 2; ++i) if (i == 0 ? usage : diagnostics)
        s->invalidation[i].fetch_add(1, std::memory_order_acq_rel);
    s->permits.fetch_add(4, std::memory_order_acq_rel);
    s->permits.fetch_and(~quint64((usage ? 1u : 0u) | (diagnostics ? 2u : 0u)), std::memory_order_release);
    if (s->mutex.tryLock()) {
        for (int i = 0; i < 2; ++i) if (i == 0 ? usage : diagnostics) {
            s->gate[i] = false;
            ++s->epoch[i];
        }
        s->mutex.unlock();
    } else {
        // The worker never holds this lock during storage or network operations.
        // Fail closed immediately even if the short producer lock is contended.
        s->flags.fetch_or((usage ? 1u : 0u) | (diagnostics ? 2u : 0u), std::memory_order_release);
    }
    s->flags.fetch_or((usage ? 1u : 0u) | (diagnostics ? 2u : 0u), std::memory_order_release);
}
void TelemetrySender::invalidateReceiver() noexcept {
    auto s = m_shared;
    if (s) { s->flags.fetch_or(4u, std::memory_order_release); invalidateConsent(true, true); }
}
void TelemetrySender::requestConsentRefresh() noexcept {
    if (m_shared) m_shared->flags.fetch_or(8u, std::memory_order_release);
}
void TelemetrySender::stop() noexcept {
    auto s = m_shared;
    if (!s) return;
    // The stop request comes first: pump() reads the flags before it.
    s->stop.store(true, std::memory_order_release);
    s->flags.fetch_or(4u, std::memory_order_release);
}
class Worker {
public:
    explicit Worker(std::shared_ptr<TelemetrySender::Shared> state) : s(std::move(state)) {}
    std::shared_ptr<TelemetrySender::Shared> s;
    TelemetryQueue queue;
    std::unique_ptr<QSettings> settings;
    std::unique_ptr<TelemetryConsent> consent;
    QNetworkAccessManager network;
    QNetworkReply* reply = nullptr;
    QTimer tick, timeout;
    QEventLoop loop;
    QElapsedTimer elapsed;
    qint64 deadline = 0;
    qint64 nextPoll = 0, nextOwnership = 0;
    bool owner = false, disabled = false;
    QString stamp[2];
    quint64 mappedEpoch[2] = {0, 0};
    bool observed[2] = {false, false};
    bool sessionAttempted = false;
    PreparedBatch active;
    QString requestEndpoint;
    qint64 responseBytes = 0;
    bool timedOut = false, oversized = false;
    qint64 monotonic() const {
#ifdef EGTRAIN_SENDER_TEST_HOOK
        if (s->tests.monotonicMs) return s->tests.monotonicMs();
#endif
        return elapsed.elapsed();
    }
    QDateTime now() const {
#ifdef EGTRAIN_SENDER_TEST_HOOK
        if (s->tests.utcNow) return s->tests.utcNow();
#endif
        return utcNow();
    }
    void close() {
        { QMutexLocker lock(&s->mutex); s->gate[0] = s->gate[1] = false; s->count = 0; }
        if (reply) { reply->disconnect(); reply->abort(); reply->deleteLater(); reply = nullptr; }
        queue.close(); owner = false;
    }
    void disable() { close(); disabled = true; }
    void failClosed() noexcept {
        s->stop.store(true, std::memory_order_release);
        s->permits.fetch_and(~quint64(3), std::memory_order_release);
        try { disable(); tick.stop(); loop.quit(); } catch (...) { loop.quit(); }
    }
    void refresh() {
        if (!owner) return;
        for (int i = 0; i < 2; ++i) {
#ifdef EGTRAIN_SENDER_TEST_HOOK
            if (i && s->tests.betweenObservations) s->tests.betweenObservations();
#endif
            const quint64 version = s->permits.load(std::memory_order_acquire) & ~quint64(3);
            QString value;
            TelemetryConsent::ObservationStatus status;
            if (i) {
                auto snap = consent->observeDiagnostics(); status = snap.status;
                if (status == TelemetryConsent::ObservationStatus::Enabled) value = QString::number(snap.generation);
            } else {
                auto snap = consent->observeUsage(); status = snap.status;
                if (status == TelemetryConsent::ObservationStatus::Enabled) value = snap.installationId;
            }
            if (status == TelemetryConsent::ObservationStatus::Mismatch) {
                queue.purgeEndpointData(); disable(); return;
            }
            if (status == TelemetryConsent::ObservationStatus::Error || status == TelemetryConsent::ObservationStatus::Unavailable) {
                disable(); return;
            }
            QMutexLocker lock(&s->mutex);
            if (!observed[i] || stamp[i] != value) {
                // A category's gate stays closed until its first observation, so the ring
                // then holds only intents of the other category. They stay queued.
                if (observed[i]) s->count = 0;
                observed[i] = true;
                stamp[i] = value; ++s->epoch[i];
                s->gate[i] = false;
                lock.unlock();
                const Category category = i ? Category::Diagnostics : Category::Usage;
                const bool purged = value.isEmpty() ? queue.purgeCategory(category) : queue.purgeOtherStamps(category, value);
                if (!purged) { disable(); return; }
                if (reply && active.token.category == (i ? Category::Diagnostics : Category::Usage)) { reply->abort(); }
                lock.relock();
            }
            mappedEpoch[i] = s->epoch[i];
            s->gate[i] = !value.isEmpty();
            if (!value.isEmpty() && !(s->flags.load(std::memory_order_acquire) & (1u << i))) {
                quint64 expected = s->permits.load(std::memory_order_acquire);
                while ((expected & ~quint64(3)) == version
                       && !s->permits.compare_exchange_weak(expected, expected | (quint64(1) << i),
                                                          std::memory_order_release, std::memory_order_acquire)) {}
            }
        }
    }
    void attemptInteractiveSession() {
        if (sessionAttempted || !s->interactiveSessionRequested.load(std::memory_order_acquire) ||
            !owner || !observed[0] || !observed[1]) return;
        quint64 epoch;
        QString selectedStamp;
        {
            QMutexLocker lock(&s->mutex);
            if (s->stop.load(std::memory_order_acquire) || !s->gate[0] || stamp[0].isEmpty()
                || !(s->permits.load(std::memory_order_acquire) & 1u)
                || (s->flags.load(std::memory_order_acquire) & 5u)) return;
            // This is a lifetime attempt, not a retryable producer intent.
            sessionAttempted = true;
            epoch = s->epoch[0];
            selectedStamp = stamp[0];
        }
#ifdef EGTRAIN_SENDER_TEST_HOOK
        if (s->tests.beforeSessionAttempt) s->tests.beforeSessionAttempt();
#endif
        const auto occurred = now();
        auto event = createEvent({}, occurred);
        refresh();
        if (!owner) return;
        {
            QMutexLocker lock(&s->mutex);
            if (s->stop.load(std::memory_order_acquire) || !s->gate[0] || s->epoch[0] != epoch
                || stamp[0] != selectedStamp || !(s->permits.load(std::memory_order_acquire) & 1u)
                || (s->flags.load(std::memory_order_acquire) & 5u)) return;
        }
        if (!queue.enqueueUsage({event, selectedStamp}, now()) && !queue.healthy()) disable();
    }
    bool authorized(int index, const QString& selectedStamp, quint64 epoch, quint64 permitVersion) {
        refresh(); // Read-only cross-process consent observation after all storage operations.
        if (!owner || s->stop.load(std::memory_order_acquire) ||
            (s->flags.load(std::memory_order_acquire) & 7u) || stamp[index] != selectedStamp) return false;
        QMutexLocker lock(&s->mutex);
        return s->gate[index] && s->epoch[index] == epoch
            && (s->permits.load(std::memory_order_acquire) & ~quint64(3)) == permitVersion
            && (s->permits.load(std::memory_order_acquire) & (quint64(1) << index));
    }
    bool reserve(qint64 delay, int failures, int usageLimit = -1, int diagnosticLimit = -1) {
        auto state = queue.retryState();
        state.failures = failures;
        if (usageLimit > 0) state.usageBatchLimit = usageLimit;
        if (diagnosticLimit > 0) state.diagnosticsBatchLimit = diagnosticLimit;
        state.nextEligibleUtc = now().addMSecs(delay);
        if (!queue.commitRetry(state, now())) { disable(); return false; }
        deadline = monotonic() + delay;
        return true;
    }
    void onFinished() {
#ifdef EGTRAIN_SENDER_TEST_HOOK
        struct CompletionSignal {
            const std::function<void()>* callback;
            ~CompletionSignal() noexcept { try { if (*callback) (*callback)(); } catch (...) {} }
        } completion{&s->tests.afterCompletion};
#endif
        if (!reply) return;
        auto* finished = reply;
        reply = nullptr;
        timeout.stop();
        int status = finished->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray retryHeader = finished->rawHeader("Retry-After").left(97);
        const auto error = finished->error();
        finished->deleteLater();
        // A partial 202 header followed by a failed transport is not an acknowledgement.
        if (status == 202 && (timedOut || (error != QNetworkReply::NoError && !oversized))) status = 0;
        if (!owner || !queue.healthy()) return;
        // A known retirement applies to the original request even after a local revoke.
        if (status == 410) { if (!queue.retireExactEndpoint(requestEndpoint)) disable(); else disable(); return; }
        refresh();
        if (!owner) return;
        const int index = active.token.category == Category::Usage ? 0 : 1;
        if (active.token.stamp != stamp[index]) return;
        if ((status == 202 && (error == QNetworkReply::NoError || oversized) && !timedOut) ||
            status == 400 || status == 422) {
            if (!queue.discardBatch(active.token)) { disable(); return; }
            reserve(60000, 0);
        } else if (status == 413) {
            const auto previous = queue.retryState();
            const int limit = index ? previous.diagnosticsBatchLimit : previous.usageBatchLimit;
            if (active.token.ids.size() == 1) {
                if (!queue.discardBatch(active.token)) { disable(); return; }
            }
            const int reduced = std::max(1, std::min(limit - 1, active.token.ids.size() / 2));
            reserve(30000, 0, index ? -1 : reduced, index ? reduced : -1);
        } else if (status == 429 || (status >= 500 && status <= 599) || status == 0) {
            // Preflight already counted this attempt. Completion must not count it twice.
            const int failures = queue.retryState().failures;
            const qint64 base = backoffBase(failures);
            int jitter = QRandomGenerator::global()->bounded(50, 151);
#ifdef EGTRAIN_SENDER_TEST_HOOK
            if (s->tests.jitterPercent) jitter = std::max(50, std::min(150, s->tests.jitterPercent()));
#endif
            const qint64 backoff = std::max(qint64(30000), std::min(kMaxDelay, base * jitter / 100));
            reserve(status == 429 ? std::max(backoff, retryAfter(retryHeader, now())) : backoff, failures);
        } else reserve(kDay, 0); // Every other completed HTTP response, including Qt HTTP errors.
    }
    void send() {
        if (!owner || reply || !queue.prune(now())) { if (!queue.healthy()) disable(); return; }
        const QDateTime eligible = queue.retryState().nextEligibleUtc;
        if (eligible.isValid() && now() < eligible) {
            deadline = monotonic() + std::max(qint64(30000), std::min(kDay, now().msecsTo(eligible)));
            return;
        }
        refresh();
        if (!owner) return;
        for (int i = 0; i < 2; ++i) {
            if (stamp[i].isEmpty()) continue;
            const Category category = i ? Category::Diagnostics : Category::Usage;
            const auto state = queue.retryState();
            const QString selectedStamp = stamp[i];
            const quint64 selectedEpoch = mappedEpoch[i];
            const quint64 permitVersion = s->permits.load(std::memory_order_acquire) & ~quint64(3);
            active = queue.prepareBatch(category, selectedStamp, s->application, now(), i ? state.diagnosticsBatchLimit : state.usageBatchLimit);
            if (active.bytes.isEmpty()) continue;
            const int attemptFailures = std::min(31, state.failures + 1);
            const qint64 preflight = backoffBase(attemptFailures);
            if (!reserve(preflight, attemptFailures)) return;
#ifdef EGTRAIN_SENDER_TEST_HOOK
            if (s->tests.afterReservation) s->tests.afterReservation();
#endif
            if (!authorized(i, selectedStamp, selectedEpoch, permitVersion)) return;
            QNetworkRequest request(QUrl(s->context.endpoint));
            request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json; charset=utf-8"));
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
#ifdef EGTRAIN_SENDER_TEST_HOOK
            if (s->tests.loopbackTrust) {
                QSslConfiguration configuration = *s->tests.loopbackTrust;
                configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
                request.setSslConfiguration(configuration);
            }
#endif
            request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
            request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
            request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
            request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
            request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
            requestEndpoint = s->context.endpoint;
            responseBytes = 0;
            timedOut = oversized = false;
#ifdef EGTRAIN_SENDER_TEST_HOOK
            if (s->tests.post) {
                try { reply = s->tests.post(network, request, active.bytes); }
                catch (...) { disable(); return; }
                if (!reply || reply->thread() != QThread::currentThread()) { disable(); return; }
            } else
#endif
                reply = network.post(request, active.bytes);
            reply->setReadBufferSize(8193);
            QObject::connect(reply, &QIODevice::readyRead, &tick, [this] {
                try {
                    if (!reply) return;
                    responseBytes += reply->read(8193).size();
                    if (responseBytes > 8192 || reply->bytesAvailable() > 8192) { oversized = true; reply->abort(); }
                } catch (...) { failClosed(); }
            });
            QObject::connect(reply, &QNetworkReply::finished, &tick, [this] {
                try { onFinished(); } catch (...) { failClosed(); }
            });
#ifdef EGTRAIN_SENDER_TEST_HOOK
            timeout.start(s->tests.requestTimeoutMs);
#else
            timeout.start(10000);
#endif
            return;
        }
        reserve(60000, 0);
    }
    void pump() {
#ifdef EGTRAIN_SENDER_TEST_HOOK
        if (s->tests.beforeFlagsRead) s->tests.beforeFlagsRead();
#endif
        // stop() raises the receiver flag after its stop request. The flags are read
        // first, so a pump that sees that flag also sees the stop and purges nothing.
        const unsigned flags = s->flags.exchange(0, std::memory_order_acq_rel);
        if (s->stop.load(std::memory_order_acquire)) { close(); tick.stop(); loop.quit(); return; }
        if (flags & 7u) {
            { QMutexLocker lock(&s->mutex);
              for (int i = 0; i < 2; ++i) if (flags & (1u << i) || flags & 4u) {
                  s->gate[i] = false; ++s->epoch[i]; stamp[i].clear();
              }
              s->count = 0;
            }
            if (reply && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 410) {
                const QString retired = requestEndpoint;
                reply->disconnect(); reply->abort(); reply->deleteLater(); reply = nullptr;
                if (!queue.retireExactEndpoint(retired)) { disable(); return; }
                disable(); return;
            }
            if (reply) reply->abort();
            if (owner
                && (flags & 4u
                    ? !queue.purgeEndpointData()
                    : (flags & 1u && !queue.purgeCategory(Category::Usage))
                        || (flags & 2u && !queue.purgeCategory(Category::Diagnostics)))) { disable(); return; }
            if (flags & 4u) { close(); disabled = true; return; }
        }
        if (disabled) return;
        const qint64 time = monotonic();
        if (!owner) {
            if (time < nextOwnership) return;
            nextOwnership = time + 60000;
#ifdef EGTRAIN_SENDER_TEST_HOOK
            if (s->tests.settingsFactory && !settings) {
                settings = s->tests.settingsFactory();
                const QString settingsPath = settings ? isolatedPath(settings->fileName()) : QString();
                // Validate both paths before queue.open can create, chmod or lock storage.
                const QString directory = isolatedPath(s->directory);
                if (settingsPath.isEmpty() || directory.isEmpty() || settingsPath == directory
                    || settingsPath.startsWith(directory + QLatin1Char('/'))
                    || directory.startsWith(settingsPath + QLatin1Char('/'))) { failClosed(); return; }
                s->directory = directory;
            }
#endif
            const auto result = queue.open(s->directory, s->context.endpoint, s->context.termsVersion, now());
#ifdef EGTRAIN_SENDER_TEST_HOOK
            if (s->tests.afterOwnershipAttempt) s->tests.afterOwnershipAttempt(result == TelemetryQueue::OpenResult::Owner);
#endif
            if (result == TelemetryQueue::OpenResult::Disabled || result == TelemetryQueue::OpenResult::Retired) { disable(); return; }
            owner = result == TelemetryQueue::OpenResult::Owner;
            if (!owner) return;
            if (!settings) settings.reset(new QSettings);
            if (!settings) { disable(); return; }
            consent.reset(new TelemetryConsent(*settings, s->context));
            auto retry = queue.retryState();
            deadline = time + (retry.nextEligibleUtc.isValid() ?
                std::max(qint64(0), std::min(kDay, now().msecsTo(retry.nextEligibleUtc))) : 60000);
            if (retry.nextEligibleUtc.isValid() && now() < retry.nextEligibleUtc && deadline == time) deadline = time + kDay;
            refresh();
        }
        if (!owner) return;
        if (time >= nextPoll || flags & 8u) {
            nextPoll = time + 1000;
            refresh();
            if (owner && !queue.prune(now())) { disable(); return; }
#ifdef EGTRAIN_SENDER_TEST_HOOK
            if (owner && s->tests.afterPoll) s->tests.afterPoll();
#endif
        }
        if (!owner) return;
        // Sessions bypass ingress, and only after a complete successful
        // two-category refresh.
        attemptInteractiveSession();
        if (!owner) return;
        // Move at most the fixed ring capacity per tick. No producer event creates a Qt event.
        for (int n = 0; n < kIngress; ++n) {
            TelemetrySender::Shared::Intent item;
            int index = 0;
            { QMutexLocker lock(&s->mutex);
              if (!s->count) break;
              item = s->ring[s->head]; s->head = (s->head + 1) % kIngress; --s->count;
              index = categoryOf(item.input) == Category::Usage ? 0 : 1;
              if (!s->gate[index] || item.epoch != mappedEpoch[index] || stamp[index].isEmpty()) continue;
            }
            refresh();
            if (!owner) return;
            if (item.epoch != mappedEpoch[index] || stamp[index].isEmpty()) continue;
            auto event = createEvent(item.input, item.occurred);
            if (!event.occurredAt.isValid()) continue;
            const bool ok = index ? queue.enqueueDiagnostic({event, stamp[index].toInt()}, now()) :
                                    queue.enqueueUsage({event, stamp[index]}, now());
            if (!ok && !queue.healthy()) { disable(); return; }
        }
        if (!reply && time >= deadline) {
            const QDateTime next = queue.retryState().nextEligibleUtc;
            if (next.isValid() && now() < next)
                deadline = time + std::max(qint64(30000), std::min(kDay, now().msecsTo(next)));
            else send();
        }
    }
    void run() {
        elapsed.start();
        network.setCookieJar(new EmptyCookies);
        tick.setInterval(50);
        timeout.setSingleShot(true);
        QObject::connect(&tick, &QTimer::timeout, &tick, [this] { try { pump(); } catch (...) { failClosed(); } });
        QObject::connect(&timeout, &QTimer::timeout, &tick, [this] {
            try { if (reply) { timedOut = true; reply->abort(); } } catch (...) { failClosed(); }
        });
        tick.start();
        pump();
        if (!s->stop.load(std::memory_order_acquire)) loop.exec();
        close();
    }
};
void SenderThread::run() {
    try { Worker worker(state); worker.run(); }
    catch (...) {
        state->stop.store(true, std::memory_order_release);
        state->permits.fetch_and(~quint64(3), std::memory_order_release);
    }
#ifdef EGTRAIN_SENDER_TEST_HOOK
    try { if (state->tests.onWorkerExit) state->tests.onWorkerExit(); } catch (...) {}
#endif
    state.reset();
}
}
