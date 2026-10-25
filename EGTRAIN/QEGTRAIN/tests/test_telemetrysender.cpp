#include "telemetry/TelemetrySender.h"
#include "telemetry/TelemetryQueue.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSettings>
#include <QSet>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <new>

namespace {
// QVERIFY reports to the thread of the test and returns from the function that calls it. A check in a callback on the
// thread of a sender, or in a helper that returns a value, records its failure with RECORD_VERIFY instead, and cleanup()
// fails the slot that recorded one.
std::mutex recordedMutex;
QStringList recordedFailures;

void recordFailure(const QString& failure) {
	const std::lock_guard<std::mutex> lock(recordedMutex);
	recordedFailures.append(failure);
}
} // namespace

#define RECORD_VERIFY(condition)                                                                       \
	do {                                                                                               \
		if (!(condition))                                                                              \
			recordFailure(QStringLiteral("line %1: %2").arg(__LINE__).arg(QLatin1String(#condition))); \
	} while (false)

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
	void finish() {
		done = true;
		setFinished(true);
		emit finished();
	}
};

// A counting semaphore with the calls of QSemaphore that these tests use. QSemaphore waits inside the Qt library, which
// ThreadSanitizer does not instrument, so it would not see that a value a callback of the sender writes before release()
// is read by the test after tryAcquire(). It does see std::mutex and std::condition_variable.
class Semaphore {
public:
	void release() {
		const std::lock_guard<std::mutex> lock(m_mutex);
		++m_count;
		m_changed.notify_all();
	}
	bool tryAcquire(int n, int timeoutMs) {
		std::unique_lock<std::mutex> lock(m_mutex);
		if (!m_changed.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this, n] { return m_count >= n; })) return false;
		m_count -= n;
		return true;
	}
private:
	std::mutex m_mutex;
	std::condition_variable m_changed;
	int m_count = 0;
};

static QJsonObject readObject(const QString& path) {
	QFile file(path);
	const bool opened = file.open(QIODevice::ReadOnly);
	RECORD_VERIFY(opened);
	const auto doc = QJsonDocument::fromJson(file.readAll());
	RECORD_VERIFY(doc.isObject());
	return doc.object();
}

static QJsonArray readDurableEvents(const QString& path) {
	return QFile::exists(path) ? readObject(path).value(QStringLiteral("events")).toArray() : QJsonArray();
}

// The time in milliseconds that a test waits for the sender thread without a semaphore. A busy machine that runs other
// tests at the same time can take seconds to schedule that thread, so the limit is the 3 s of the semaphore waits below.
constexpr int kWaitMs = 3000;

// tryEnqueue never waits, so it also refuses an event while the worker briefly holds the ingress lock. An event that
// must be accepted while the worker runs is retried here until it is taken; one sent while a hook holds the worker needs
// no retry. qWaitFor stops calling the predicate once it returned true, so the event is taken once.
static bool waitForGate(telemetry::TelemetrySender& sender, const telemetry::TelemetryEventInput& input, int timeoutMs = kWaitMs) {
	return QTest::qWaitFor([&] { return sender.tryEnqueue(input); }, timeoutMs);
}

// The receiver of every sender in these tests. A test transport is accepted only for a numeric loopback address.
static TelemetryContext loopbackContext() {
	TelemetryContext context;
	context.packaged = true;
	context.interactive = true;
	context.enabled = true;
	context.endpoint = QStringLiteral("https://127.0.0.1:1/collect");
	return context;
}
#endif

class TestTelemetrySender : public QObject {
	Q_OBJECT

private slots:
	void cleanup();
	void incapableSenderRefusesEverything();
	void refusesEndpointsThatAreNotHttps();
#ifdef EGTRAIN_SENDER_TEST_HOOK
	void refusesTestTransportForPublicReceiver();
	void operationTokens();
	void tokenLifetimeAndContention();
	void interactiveSessions_data();
	void interactiveSessions();
	void sessionAttemptLoss_data();
	void sessionAttemptLoss();
	void disabledWorkerExits_data();
	void disabledWorkerExits();
	void pathIsolation_data();
	void pathIsolation();
	void pathIsolationResolvesSettingsParent();
	void backoffSaturation_data();
	void backoffSaturation();
	void outcomes_data();
	void outcomes();
	void startupIngress();
	void stopKeepsStoredEvents();
	void preparationBarrier_data();
	void preparationBarrier();
	void interruptedBackoff();
	void timeoutAndExceptions_data();
	void timeoutAndExceptions();
	void expiryAndActiveRevocation_data();
	void expiryAndActiveRevocation();
	void constructionAndWorkerExceptions();
	void followerTakeover();
	void retirementWithQueuedWork();
	void splitRetainsIds();
	void retryAfterAndRetirement();
#endif

private:
	const telemetry::Application m_metadata{QStringLiteral("1.2.3"), QString(), QString()};
#ifdef EGTRAIN_SENDER_TEST_HOOK
	const TelemetryContext m_context = loopbackContext();
#endif
};

void TestTelemetrySender::cleanup() {
	QStringList failures;
	{
		const std::lock_guard<std::mutex> lock(recordedMutex);
		failures.swap(recordedFailures);
	}
	QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral("; "))));
}

void TestTelemetrySender::incapableSenderRefusesEverything() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	TelemetryContext context;
	context.packaged = true;
	context.interactive = true;
	context.enabled = false;
	context.endpoint = QStringLiteral("https://127.0.0.1:1/collect");
	const telemetry::TelemetryEventInput input;
	telemetry::TelemetrySender sender(context, m_metadata, directory.path());
	for (int i = 0; i < 1000; ++i) QVERIFY(!sender.tryEnqueue(input));
	sender.invalidateConsent(true, true);
	sender.requestConsentRefresh();
	sender.invalidateReceiver();
	sender.stop();
	QVERIFY(!sender.tryEnqueue(input));
}

void TestTelemetrySender::refusesEndpointsThatAreNotHttps() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	TelemetryContext context;
	context.packaged = true;
	context.interactive = true;
	context.enabled = true;
	const telemetry::TelemetryEventInput input;
	telemetry::TelemetrySender emptyEndpoint(context, m_metadata, directory.path());
	QVERIFY(!emptyEndpoint.tryEnqueue(input));
	context.endpoint = QStringLiteral("http://127.0.0.1:1/collect");
	telemetry::TelemetrySender insecureEndpoint(context, m_metadata, directory.path());
	QVERIFY(!insecureEndpoint.tryEnqueue(input));
}

#ifdef EGTRAIN_SENDER_TEST_HOOK
void TestTelemetrySender::refusesTestTransportForPublicReceiver() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	TelemetryContext context = m_context;
	context.endpoint = QStringLiteral("https://example.org/collect");
	telemetry::TelemetrySender::TestOptions rejected;
	rejected.settingsFactory = [] { return std::unique_ptr<QSettings>(new QSettings); };
	rejected.post = [](QNetworkAccessManager&, const QNetworkRequest&, const QByteArray&) -> QNetworkReply* {
		recordFailure(QStringLiteral("non-loopback test transport must not run"));
		return nullptr;
	};
	telemetry::TelemetrySender publicOverride(context, m_metadata, directory.path(), rejected);
	QVERIFY(!publicOverride.tryEnqueue(telemetry::TelemetryEventInput()));
}

void TestTelemetrySender::operationTokens() {
	QTemporaryDir directory;
	const QString settingsFile = directory.filePath(QStringLiteral("consent.ini"));
	const QString storage = directory.filePath(QStringLiteral("queue"));
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	QVERIFY(consent.save(false, false));
	Semaphore polled, proceed, settled, exited;
	telemetry::TelemetrySender::TestOptions options;
	options.settingsFactory = [settingsFile] { return std::make_unique<QSettings>(settingsFile, QSettings::IniFormat); };
	options.monotonicMs = [] { return 0; }; // Never transmit; inspect durable records.
	options.afterPoll = [&] {
		polled.release();
		RECORD_VERIFY(proceed.tryAcquire(1, 3000));
		QTimer::singleShot(0, [&] { settled.release(); });
	};
	options.onWorkerExit = [&] { exited.release(); };
	telemetry::TelemetrySender sender(m_context, m_metadata, storage, options);
	QVERIFY(polled.tryAcquire(1, 3000));
	// Lets the worker finish its poll, asks for the next one and waits until the worker holds in it.
	const auto cycle = [&] {
		proceed.release();
		if (!settled.tryAcquire(1, 3000)) return false;
		sender.requestConsentRefresh();
		return polled.tryAcquire(1, 3000);
	};
	telemetry::TelemetryEventInput usage, diagnostic;
	usage.name = telemetry::Name::SceneOpened;
	diagnostic.name = telemetry::Name::OperationFailed;
	const auto disabled = sender.captureOperation();
	QVERIFY(consent.save(true, true));
	QVERIFY(cycle());
	QVERIFY(!sender.tryEnqueue(usage, {}));
	QVERIFY(!sender.tryEnqueue(diagnostic, {}));
	QVERIFY(!sender.tryEnqueue(usage, disabled));
	QVERIFY(!sender.tryEnqueue(diagnostic, disabled));
	auto token = sender.captureOperation();
	QVERIFY(sender.tryEnqueue(usage, token));
	QVERIFY(sender.tryEnqueue(diagnostic, token));
	QVERIFY(cycle());
	QCOMPARE(readDurableEvents(storage + QStringLiteral("/usage.json")).size(), 1);
	QCOMPARE(readDurableEvents(storage + QStringLiteral("/diagnostics.json")).size(), 1);
	for (int n = 0; n < 2; ++n) {
		// Diagnostics revocation must not invalidate a still-authorized usage operation.
		token = sender.captureOperation();
		QVERIFY(consent.save(true, false));
		sender.invalidateConsent(false, true);
		QVERIFY(cycle());
		QVERIFY(!sender.tryEnqueue(diagnostic, token));
		QVERIFY(sender.tryEnqueue(usage, token));
		QVERIFY(cycle());
		QVERIFY(consent.save(true, true));
		QVERIFY(cycle());
		QVERIFY(!sender.tryEnqueue(diagnostic, token));
		QVERIFY(sender.tryEnqueue(usage, token));
		QVERIFY(cycle());
		QCOMPARE(readDurableEvents(storage + QStringLiteral("/usage.json")).size(), 3 + 2 * n);
		// Usage revoke alone preserves diagnostics; creating a new usage ID genuinely
		// advances the persistent diagnostics stamp and then rejects that token too.
		token = sender.captureOperation();
		QVERIFY(consent.save(false, true));
		sender.invalidateConsent(true, false);
		QVERIFY(cycle());
		QVERIFY(!sender.tryEnqueue(usage, token));
		QVERIFY(sender.tryEnqueue(diagnostic, token));
		QVERIFY(cycle());
		QCOMPARE(readDurableEvents(storage + QStringLiteral("/diagnostics.json")).size(), 1);
		QVERIFY(consent.save(true, true));
		QVERIFY(cycle());
		QVERIFY(!sender.tryEnqueue(usage, token));
		QVERIFY(!sender.tryEnqueue(diagnostic, token));
		// Start the next iteration's usage count from the new installation stamp.
		if (n == 0) {
			const auto fresh = sender.captureOperation();
			for (int i = 0; i < 3; ++i) QVERIFY(sender.tryEnqueue(usage, fresh));
			QVERIFY(cycle());
		}
	}
	token = sender.captureOperation();
	QVERIFY(sender.tryEnqueue(usage, token));
	QVERIFY(cycle());
	const auto records = readDurableEvents(storage + QStringLiteral("/usage.json"));
	QCOMPARE(records.size(), 1);
	QCOMPARE(records.first().toObject().value(QStringLiteral("event")).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("scene.opened"));
	sender.invalidateReceiver();
	QVERIFY(!sender.tryEnqueue(usage, token));
	QVERIFY(!sender.tryEnqueue(diagnostic, token));
	proceed.release();
	QVERIFY(settled.tryAcquire(1, 3000));
	// The next pump purges the queue and the worker exits without a stop. Reading usage.json while the worker replaces
	// it makes the replacement fail on Windows, so the file is read once the worker has exited.
	QVERIFY(exited.tryAcquire(1, 3000));
	QVERIFY(readDurableEvents(storage + QStringLiteral("/usage.json")).isEmpty());
	sender.stop();
}

void TestTelemetrySender::tokenLifetimeAndContention() {
	QTemporaryDir directory;
	const QString settingsFile = directory.filePath(QStringLiteral("consent.ini"));
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	QVERIFY(consent.save(true, true));
	Semaphore polled, proceed, settled, exited, captured, unlock;
	std::atomic<bool> holdCapture{false}, throwCapture{false};
	telemetry::TelemetrySender::TestOptions options;
	options.settingsFactory = [settingsFile] { return std::make_unique<QSettings>(settingsFile, QSettings::IniFormat); };
	options.monotonicMs = [] { return 0; };
	options.afterPoll = [&] {
		polled.release();
		RECORD_VERIFY(proceed.tryAcquire(1, 3000));
		QTimer::singleShot(0, [&] { settled.release(); });
	};
	options.afterOperationCaptureLocked = [&] {
		if (throwCapture) throw std::bad_alloc();
		if (holdCapture) {
			captured.release();
			RECORD_VERIFY(unlock.tryAcquire(1, 3000));
		}
	};
	options.onWorkerExit = [&] { exited.release(); };
	// Placement reuse also exercises the sender identity without relying on allocator luck.
	alignas(telemetry::TelemetrySender) unsigned char facade[sizeof(telemetry::TelemetrySender)];
	auto* sender = new (facade) telemetry::TelemetrySender(m_context, m_metadata, directory.filePath(QStringLiteral("queue")), options);
	QVERIFY(polled.tryAcquire(1, 3000));
	telemetry::TelemetryEventInput usage;
	usage.name = telemetry::Name::SceneOpened;
	const auto original = sender->captureOperation();
	telemetry::TelemetrySender::OperationToken contended;
	holdCapture = true;
	std::thread producer([&] { contended = sender->captureOperation(); });
	QVERIFY(captured.tryAcquire(1, 3000));
	QElapsedTimer timer;
	timer.start();
	const auto dropped = sender->captureOperation();
	const bool admitted = sender->tryEnqueue(usage, original);
	const auto delay = timer.elapsed();
	// Invalidation must reject an old token even when its epoch cannot be updated yet.
	sender->invalidateConsent(true, false);
	unlock.release();
	producer.join();
	holdCapture = false;
	QCOMPARE_LT(delay, qint64(100));
	QVERIFY(!admitted);
	QVERIFY(!sender->tryEnqueue(usage, dropped));
	QVERIFY(!sender->tryEnqueue(usage, contended));
	throwCapture = true;
	const auto failed = sender->captureOperation();
	throwCapture = false;
	QVERIFY(!sender->tryEnqueue(usage, failed));
	proceed.release();
	QVERIFY(settled.tryAcquire(1, 3000));
	sender->requestConsentRefresh();
	QVERIFY(polled.tryAcquire(1, 3000));
	QVERIFY(!sender->tryEnqueue(usage, original));
	const auto fresh = sender->captureOperation();
	QVERIFY(sender->tryEnqueue(usage, fresh));
	proceed.release();
	QVERIFY(settled.tryAcquire(1, 3000));
	sender->stop();
	QVERIFY(exited.tryAcquire(1, 3000));
	sender->~TelemetrySender();
	QCOMPARE(readDurableEvents(directory.filePath(QStringLiteral("queue/usage.json"))).size(), 1);
	sender = new (facade) telemetry::TelemetrySender(m_context, m_metadata, directory.filePath(QStringLiteral("queue")), options);
	QVERIFY(polled.tryAcquire(1, 3000));
	QVERIFY(!sender->tryEnqueue(usage, original));
	QVERIFY(!sender->tryEnqueue(usage, fresh));
	const auto replacement = sender->captureOperation();
	// A different live capable sender also cannot accept the replacement's token.
	Semaphore foreignPolled, foreignProceed, foreignSettled, foreignExited;
	auto foreignOptions = options;
	foreignOptions.afterPoll = [&] {
		foreignPolled.release();
		RECORD_VERIFY(foreignProceed.tryAcquire(1, 3000));
		QTimer::singleShot(0, [&] { foreignSettled.release(); });
	};
	foreignOptions.onWorkerExit = [&] { foreignExited.release(); };
	telemetry::TelemetrySender foreign(m_context, m_metadata, directory.filePath(QStringLiteral("foreign")), foreignOptions);
	QVERIFY(foreignPolled.tryAcquire(1, 3000));
	QVERIFY(!foreign.tryEnqueue(usage, replacement));
	QVERIFY(!sender->tryEnqueue(usage, foreign.captureOperation()));
	foreignProceed.release();
	QVERIFY(foreignSettled.tryAcquire(1, 3000));
	foreign.stop();
	QVERIFY(foreignExited.tryAcquire(1, 3000));
	QVERIFY(readDurableEvents(directory.filePath(QStringLiteral("foreign/usage.json"))).isEmpty());
	QVERIFY(sender->tryEnqueue(usage, replacement));
	proceed.release();
	QVERIFY(settled.tryAcquire(1, 3000));
	sender->stop();
	QVERIFY(exited.tryAcquire(1, 3000));
	QVERIFY(!sender->tryEnqueue(usage, replacement));
	sender->~TelemetrySender();
	QCOMPARE(readDurableEvents(directory.filePath(QStringLiteral("queue/usage.json"))).size(), 2);
}

void TestTelemetrySender::interactiveSessions_data() {
	QTest::addColumn<int>("scenario");
	// Saved usage, late usage, diagnostics-only and ordinary standalone senders.
	QTest::newRow("saved usage") << 0;
	QTest::newRow("late usage") << 1;
	QTest::newRow("diagnostics only") << 2;
	QTest::newRow("standalone") << 3;
}

void TestTelemetrySender::interactiveSessions() {
	QFETCH(int, scenario);
	QTemporaryDir directory;
	const QString settingsFile = directory.filePath(QStringLiteral("consent.ini"));
	const QString storage = directory.filePath(QStringLiteral("queue"));
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	QVERIFY(consent.save(scenario == 0 || scenario == 3, true));
	auto clock = std::make_shared<std::atomic<qint64>>(0);
	const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(10);
	Semaphore polled, proceed, settled, exited, posted, completed, opened, allowObservations;
	int sessionAttempts = 0;
	QString expectedInstallationId = consent.observeUsage().installationId;
	telemetry::TelemetrySender::TestOptions options;
	options.settingsFactory = [settingsFile] { return std::make_unique<QSettings>(settingsFile, QSettings::IniFormat); };
	options.utcNow = [base, clock] { return base.addMSecs(clock->load()); };
	options.monotonicMs = [clock] { return clock->load(); };
	options.afterPoll = [&] {
		polled.release();
		RECORD_VERIFY(proceed.tryAcquire(1, 3000));
		QTimer::singleShot(0, [&] { settled.release(); });
	};
	options.afterOwnershipAttempt = [&](bool owner) {
		RECORD_VERIFY(owner);
		opened.release();
		RECORD_VERIFY(allowObservations.tryAcquire(1, 3000));
	};
	options.beforeSessionAttempt = [&] { ++sessionAttempts; };
	options.onWorkerExit = [&] { exited.release(); };
	options.afterCompletion = [&] { completed.release(); };
	options.post = [&](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray& bytes) {
		const auto wire = QJsonDocument::fromJson(bytes).object();
		const auto events = wire.value(QStringLiteral("events")).toArray();
		RECORD_VERIFY(events.size() == 1);
		RECORD_VERIFY(events.first().toObject().value(QStringLiteral("name")).toString() == QStringLiteral("session.started"));
		RECORD_VERIFY(events.first().toObject().value(QStringLiteral("occurred_at")).toString() == base.addMSecs(scenario == 1 ? 1234 : 0).toString(Qt::ISODateWithMs));
		RECORD_VERIFY(wire.value(QStringLiteral("installation_id")).toString() == expectedInstallationId);
		posted.release();
		return new ScriptedReply(request, 202, &manager);
	};
	telemetry::TelemetrySender sender(m_context, m_metadata, storage, options);
	QVERIFY(opened.tryAcquire(1, 3000));
	QCOMPARE(sessionAttempts, 0);
	const auto beforeObservation = sender.captureOperation();
	QVERIFY(!sender.tryEnqueue({}, beforeObservation));
	if (scenario == 0) {
		sender.requestInteractiveSession();
		sender.requestInteractiveSession();
	}
	// A startup request precedes BOTH initial observations. Initial diagnostics
	// ring clearing must finish before the worker attempts the session.
	allowObservations.release();
	QVERIFY(polled.tryAcquire(1, 3000));
	QVERIFY(readDurableEvents(storage + QStringLiteral("/usage.json")).isEmpty());
	if (scenario != 3) {
		sender.requestInteractiveSession();
		sender.requestInteractiveSession();
	}
	// Lets the worker finish its poll, asks for the next one and waits until the worker holds in it.
	const auto cycle = [&] {
		proceed.release();
		if (!settled.tryAcquire(1, 3000)) return false;
		sender.requestConsentRefresh();
		return polled.tryAcquire(1, 3000);
	};
	QVERIFY(cycle());
	QCOMPARE(sessionAttempts, scenario == 0 ? 1 : 0);
	if (scenario == 1) {
		QVERIFY(readDurableEvents(storage + QStringLiteral("/usage.json")).isEmpty());
		QVERIFY(consent.save(true, true));
		expectedInstallationId = consent.observeUsage().installationId;
		*clock = 1234;
		QVERIFY(cycle());
		QVERIFY(cycle());
		QCOMPARE(sessionAttempts, 1);
	}
	auto records = readDurableEvents(storage + QStringLiteral("/usage.json"));
	QCOMPARE(records.size(), scenario < 2 ? 1 : 0);
	if (scenario < 2) {
		QCOMPARE(records.first().toObject().value(QStringLiteral("event")).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("session.started"));
		*clock = 60001;
		QVERIFY(cycle());
		QVERIFY(cycle());
		QVERIFY(posted.tryAcquire(1, 3000));
		QVERIFY(completed.tryAcquire(1, 3000));
		QVERIFY(readDurableEvents(storage + QStringLiteral("/usage.json")).isEmpty());
		QVERIFY(consent.save(false, true));
		sender.invalidateConsent(true, false);
		QVERIFY(cycle());
		QVERIFY(consent.save(true, true));
		sender.requestInteractiveSession();
		QVERIFY(cycle());
		QVERIFY(cycle());
		QCOMPARE(sessionAttempts, 1);
		QVERIFY(readDurableEvents(storage + QStringLiteral("/usage.json")).isEmpty());
	} else {
		QCOMPARE(sessionAttempts, 0);
		QVERIFY(!posted.tryAcquire(1, 0));
	}
	proceed.release();
	QVERIFY(settled.tryAcquire(1, 3000));
	sender.stop();
	QVERIFY(exited.tryAcquire(1, 3000));
}

void TestTelemetrySender::sessionAttemptLoss_data() {
	QTest::addColumn<int>("failure");
	QTest::newRow("consent lost") << 0;
	QTest::newRow("exception") << 1;
	QTest::newRow("storage failure") << 2;
}

void TestTelemetrySender::sessionAttemptLoss() {
	QFETCH(int, failure);
	QTemporaryDir directory;
	const QString settingsFile = directory.filePath(QStringLiteral("consent.ini"));
	const QString storage = directory.filePath(QStringLiteral("queue"));
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	QVERIFY(consent.save(true, false));
	Semaphore polled, proceed, settled, attempted, resume, exited;
	int attempts = 0;
	telemetry::TelemetrySender::TestOptions options;
	options.settingsFactory = [settingsFile] { return std::make_unique<QSettings>(settingsFile, QSettings::IniFormat); };
	options.monotonicMs = [] { return 0; };
	options.afterPoll = [&] {
		polled.release();
		RECORD_VERIFY(proceed.tryAcquire(1, 3000));
		QTimer::singleShot(0, [&] { settled.release(); });
	};
	options.beforeSessionAttempt = [&] {
		++attempts;
		attempted.release();
		RECORD_VERIFY(resume.tryAcquire(1, 3000));
		if (failure == 1) throw std::bad_alloc();
	};
	options.onWorkerExit = [&] { exited.release(); };
	telemetry::TelemetrySender sender(m_context, m_metadata, storage, options);
	QVERIFY(polled.tryAcquire(1, 3000));
	sender.requestInteractiveSession();
	proceed.release();
	QVERIFY(attempted.tryAcquire(1, 3000));
	if (failure == 0) {
		// Lose authorization after the latch and before storing. Re-enable is
		// not a reason to replay that session or any prior operation.
		QVERIFY(consent.save(false, false));
		sender.invalidateConsent(true, false);
	} else if (failure == 2) {
		// Force the atomic usage-store replacement to fail.
		if (QFile::exists(storage + QStringLiteral("/usage.json")))
			QVERIFY(QFile::remove(storage + QStringLiteral("/usage.json")));
		QVERIFY(QDir().mkdir(storage + QStringLiteral("/usage.json")));
	}
	resume.release();
	if (failure == 0) {
		QVERIFY(settled.tryAcquire(1, 3000));
		sender.requestConsentRefresh();
		QVERIFY(polled.tryAcquire(1, 3000));
		proceed.release();
		QVERIFY(settled.tryAcquire(1, 3000));
		QVERIFY(consent.save(true, false));
		sender.requestInteractiveSession();
		sender.requestConsentRefresh();
		QVERIFY(polled.tryAcquire(1, 3000));
		proceed.release();
		QVERIFY(settled.tryAcquire(1, 3000));
		sender.requestConsentRefresh();
		QVERIFY(polled.tryAcquire(1, 3000));
		QCOMPARE(attempts, 1);
		QVERIFY(readDurableEvents(storage + QStringLiteral("/usage.json")).isEmpty());
		proceed.release();
		QVERIFY(settled.tryAcquire(1, 3000));
		sender.stop();
		QVERIFY(exited.tryAcquire(1, 3000));
	} else {
		// The exception and the storage failure end this worker without a stop, so it cannot attempt again.
		QVERIFY(exited.tryAcquire(1, 3000));
		sender.requestInteractiveSession();
		sender.requestConsentRefresh();
		sender.stop();
	}
	QCOMPARE(attempts, 1);
	QVERIFY(!sender.tryEnqueue({}, sender.captureOperation()));
	if (failure == 1)
		QVERIFY(readDurableEvents(storage + QStringLiteral("/usage.json")).isEmpty());
}

void TestTelemetrySender::disabledWorkerExits_data() {
	// A worker that can send nothing more in this run leaves its loop without a stop: when its first pump finds the
	// queue disabled, when a pump takes a receiver change, and after a 410 response. No pump follows.
	QTest::addColumn<int>("cause");
	QTest::newRow("disabled queue") << 0;
	QTest::newRow("receiver change") << 1;
	QTest::newRow("410 response") << 2;
}

void TestTelemetrySender::disabledWorkerExits() {
	QFETCH(int, cause);
	QTemporaryDir directory;
	const QString settingsFile = directory.filePath(QStringLiteral("consent.ini"));
	const QString storage = directory.filePath(QStringLiteral("queue"));
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	QVERIFY(consent.save(true, false));
	if (cause == 0) {
		QVERIFY(QDir().mkpath(storage));
		QFile marker(storage + QStringLiteral("/disabled"));
		QVERIFY(marker.open(QIODevice::WriteOnly));
		QCOMPARE(marker.write("disabled\n"), qint64(9));
	}
	auto clock = std::make_shared<std::atomic<qint64>>(0);
	const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(10);
	Semaphore polled, atPump, resume, exited;
	std::atomic<int> pumps{0}, lastPump{1};
	std::atomic<bool> hold{false};
	telemetry::TelemetrySender::TestOptions options;
	options.settingsFactory = [settingsFile] { return std::make_unique<QSettings>(settingsFile, QSettings::IniFormat); };
	options.utcNow = [clock, base] { return base.addMSecs(clock->load()); };
	options.monotonicMs = [clock] { return clock->load(); };
	options.afterPoll = [&] { polled.release(); };
	options.beforeFlagsRead = [&] {
		++pumps;
		if (!hold.exchange(false)) return;
		atPump.release();
		RECORD_VERIFY(resume.tryAcquire(1, 3000));
	};
	options.afterCompletion = [&] { lastPump = pumps.load(); };
	options.onWorkerExit = [&] { exited.release(); };
	options.post = [](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray&) {
		return new ScriptedReply(request, 410, &manager);
	};
	telemetry::TelemetrySender sender(m_context, m_metadata, storage, options);
	if (cause == 1) {
		QVERIFY(polled.tryAcquire(1, 3000));
		hold = true;
		QVERIFY(atPump.tryAcquire(1, 3000));
		sender.invalidateReceiver();
		lastPump = pumps.load();
		resume.release();
	} else if (cause == 2) {
		QVERIFY(waitForGate(sender, {}));
		*clock = 60001;
	}
	QVERIFY(exited.tryAcquire(1, 3000));
	QCOMPARE(pumps.load(), lastPump.load());
	QVERIFY(!sender.tryEnqueue({}));
	sender.stop();
}

void TestTelemetrySender::pathIsolation_data() {
	// The index of a pair of paths in the table of pathIsolation().
	QTest::addColumn<int>("index");
	QTest::newRow("queue in the home folder through ..") << 0;
	QTest::newRow("queue in a linked folder") << 1;
	QTest::newRow("queue through a link and ..") << 2;
	QTest::newRow("queue below a dangling link") << 3;
	QTest::newRow("settings are the queue") << 4;
	QTest::newRow("settings in a linked folder") << 5;
	QTest::newRow("settings in the home folder through ..") << 6;
	QTest::newRow("settings in the queue through a link") << 7;
	QTest::newRow("settings in the queue through ..") << 8;
}

void TestTelemetrySender::pathIsolation() {
	QFETCH(int, index);
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString outside = QDir::homePath() + QStringLiteral("/egtrain-must-not-create-") + QFileInfo(directory.path()).fileName();
	QVERIFY(!QFile::exists(outside));
	const QString link = directory.filePath(QStringLiteral("outside-link"));
	QVERIFY(QFile::link(QDir::homePath(), link));
	const QString dangling = directory.filePath(QStringLiteral("dangling-link"));
	QVERIFY(QFile::link(outside, dangling));
	const QString queue = directory.filePath(QStringLiteral("queue"));
	const QString queueAlias = directory.filePath(QStringLiteral("queue-alias"));
	QVERIFY(QFile::link(queue, queueAlias));
	const QString traversed = QDir::tempPath() + QLatin1Char('/') + QDir(QDir::tempPath()).relativeFilePath(outside);
	const QString redirected = link + QLatin1Char('/') + QFileInfo(outside).fileName();
	const QString symlinkTraversal = link + QStringLiteral("/../") + QFileInfo(QDir::homePath()).fileName() + QLatin1Char('/') + QFileInfo(outside).fileName();
	const QString goodSettings = directory.filePath(QStringLiteral("consent.ini"));
	struct Paths {
		QString queue, settings;
		bool worker;
	};
	const Paths cases[] = {
		{traversed, goodSettings, false},
		{redirected, goodSettings, false},
		{symlinkTraversal, goodSettings, false},
		{dangling + QStringLiteral("/queue"), goodSettings, false},
		{queue, queue, true},
		{queue, redirected + QStringLiteral(".ini"), true},
		{queue, traversed + QStringLiteral(".ini"), true},
		{queue, queueAlias + QStringLiteral("/consent.ini"), true},
		{queue, queue + QStringLiteral("/../queue/consent.ini"), true}};
	const Paths& paths = cases[index];
	Semaphore exited;
	telemetry::TelemetrySender::TestOptions options;
	options.settingsFactory = [paths] { return std::unique_ptr<QSettings>(new QSettings(paths.settings, QSettings::IniFormat)); };
	options.onWorkerExit = [&] { exited.release(); };
	options.post = [](QNetworkAccessManager&, const QNetworkRequest&, const QByteArray&) -> QNetworkReply* {
		recordFailure(QStringLiteral("invalid isolation must not dispatch"));
		return nullptr;
	};
	telemetry::TelemetrySender sender(m_context, m_metadata, paths.queue, options);
	if (paths.worker) QVERIFY(exited.tryAcquire(1, 3000));
	else QThread::msleep(100); // No worker starts; the checks below see that nothing is created in this time.
	QVERIFY(!sender.tryEnqueue({}));
	QVERIFY(!QFile::exists(queue));
	QVERIFY(!QFile::exists(outside));
	QVERIFY(!QFile::exists(outside + QStringLiteral(".ini")));
	sender.stop();
}

void TestTelemetrySender::pathIsolationResolvesSettingsParent() {
	// An existing symlinked settings parent must resolve before separation is checked.
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString queue = directory.filePath(QStringLiteral("queue"));
	const QString queueAlias = directory.filePath(QStringLiteral("queue-alias"));
	QVERIFY(QFile::link(queue, queueAlias));
	QVERIFY(QDir().mkpath(queue));
	Semaphore exited;
	telemetry::TelemetrySender::TestOptions options;
	options.settingsFactory = [queueAlias] {
		return std::unique_ptr<QSettings>(new QSettings(queueAlias + QStringLiteral("/consent.ini"), QSettings::IniFormat));
	};
	options.onWorkerExit = [&] { exited.release(); };
	telemetry::TelemetrySender sender(m_context, m_metadata, queue, options);
	QVERIFY(exited.tryAcquire(1, 3000));
	QVERIFY(QDir(queue).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
}

void TestTelemetrySender::backoffSaturation_data() {
	QTest::addColumn<int>("attempt");
	QTest::addColumn<int>("jitter");
	for (int attempt : {12, 13, 31})
		for (int jitter : {50, 100, 150})
			QTest::addRow("attempt %d, jitter %d", attempt, jitter) << attempt << jitter;
}

void TestTelemetrySender::backoffSaturation() {
	QFETCH(int, attempt);
	QFETCH(int, jitter);
	QTemporaryDir directory;
	const QString settingsFile = directory.filePath(QStringLiteral("consent.ini"));
	const QString storage = directory.filePath(QStringLiteral("queue"));
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	QVERIFY(consent.save(true, false));
	const auto base = QDateTime::currentDateTimeUtc().addSecs(10);
	{
		telemetry::TelemetryQueue seed;
		QCOMPARE(seed.open(storage, m_context.endpoint, m_context.termsVersion, base), telemetry::TelemetryQueue::OpenResult::Owner);
		QVERIFY(seed.enqueueUsage({telemetry::createEvent({}, base), consent.observeUsage().installationId}, base));
		telemetry::RetryState retry;
		retry.failures = attempt - 1;
		QVERIFY(seed.commitRetry(retry, base));
	}
	auto clock = std::make_shared<std::atomic<qint64>>(0);
	Semaphore ready, completed, exited;
	telemetry::TelemetrySender::TestOptions options;
	options.settingsFactory = [settingsFile] { return std::unique_ptr<QSettings>(new QSettings(settingsFile, QSettings::IniFormat)); };
	options.utcNow = [clock, base] { return base.addMSecs(clock->load()); };
	options.monotonicMs = [clock] { return clock->load(); };
	options.jitterPercent = [jitter] { return jitter; };
	options.afterOwnershipAttempt = [&](bool owner) {
		RECORD_VERIFY(owner);
		ready.release();
	};
	options.afterReservation = [&] {
		const auto retry = readObject(storage + QStringLiteral("/control.json")).value(QStringLiteral("retry")).toObject();
		RECORD_VERIFY(retry.value(QStringLiteral("failures")).toInt() == attempt);
		const auto next = QDateTime::fromString(retry.value(QStringLiteral("next")).toString(), Qt::ISODateWithMs);
		RECORD_VERIFY(base.addMSecs(clock->load()).msecsTo(next) == 86400000);
	};
	options.afterCompletion = [&] { completed.release(); };
	options.onWorkerExit = [&] { exited.release(); };
	options.post = [](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray&) {
		return new ScriptedReply(request, 503, &manager);
	};
	telemetry::TelemetrySender sender(m_context, m_metadata, storage, options);
	QVERIFY(ready.tryAcquire(1, 3000));
	*clock = 60001;
	QVERIFY(completed.tryAcquire(1, 3000));
	const auto retry = readObject(storage + QStringLiteral("/control.json")).value(QStringLiteral("retry")).toObject();
	const auto next = QDateTime::fromString(retry.value(QStringLiteral("next")).toString(), Qt::ISODateWithMs);
	QCOMPARE(base.addMSecs(clock->load()).msecsTo(next), qMin(86400000LL, 86400000LL * jitter / 100));
	sender.stop();
	QVERIFY(exited.tryAcquire(1, 3000));
}

void TestTelemetrySender::outcomes_data() {
	QTest::addColumn<int>("status");
	QTest::addColumn<QNetworkReply::NetworkError>("error");
	QTest::addColumn<QByteArray>("retry");
	// The Retry-After header is the date 300 s after the moment of the request instead of retry.
	QTest::addColumn<bool>("retryDate");
	QTest::addColumn<qint64>("delay");
	QTest::addColumn<bool>("retained");
	QTest::addColumn<int>("failures");
	QTest::addColumn<int>("events");
	QTest::addColumn<int>("limit");
	QTest::newRow("202") << 202 << QNetworkReply::NoError << QByteArray() << false << qint64(60000) << false << 0 << 1 << 50;
	QTest::newRow("400") << 400 << QNetworkReply::ContentAccessDenied << QByteArray() << false << qint64(60000) << false << 0 << 1 << 50;
	QTest::newRow("422") << 422 << QNetworkReply::ContentAccessDenied << QByteArray() << false << qint64(60000) << false << 0 << 1 << 50;
	QTest::newRow("413 one event") << 413 << QNetworkReply::ContentAccessDenied << QByteArray() << false << qint64(30000) << false << 0 << 1 << 1;
	QTest::newRow("413 two events") << 413 << QNetworkReply::ContentAccessDenied << QByteArray() << false << qint64(30000) << true << 0 << 2 << 1;
	QTest::newRow("429 seconds") << 429 << QNetworkReply::ContentAccessDenied << QByteArray("120") << false << qint64(120000) << true << 1 << 1 << 50;
	QTest::newRow("429 date") << 429 << QNetworkReply::ContentAccessDenied << QByteArray() << true << qint64(300000) << true << 1 << 1 << 50;
	QTest::newRow("429 invalid") << 429 << QNetworkReply::ContentAccessDenied << QByteArray("invalid") << false << qint64(60000) << true << 1 << 1 << 50;
	QTest::newRow("429 too long") << 429 << QNetworkReply::ContentAccessDenied << QByteArray("99999999999999999999999") << false << qint64(86400000) << true << 1 << 1 << 50;
	QTest::newRow("503") << 503 << QNetworkReply::ServiceUnavailableError << QByteArray() << false << qint64(60000) << true << 1 << 1 << 50;
	QTest::newRow("302") << 302 << QNetworkReply::NoError << QByteArray() << false << qint64(86400000) << true << 0 << 1 << 50;
	QTest::newRow("401") << 401 << QNetworkReply::AuthenticationRequiredError << QByteArray() << false << qint64(86400000) << true << 0 << 1 << 50;
	QTest::newRow("403") << 403 << QNetworkReply::ContentAccessDenied << QByteArray() << false << qint64(86400000) << true << 0 << 1 << 50;
	QTest::newRow("404") << 404 << QNetworkReply::ContentNotFoundError << QByteArray() << false << qint64(86400000) << true << 0 << 1 << 50;
	QTest::newRow("host not found") << 0 << QNetworkReply::HostNotFoundError << QByteArray() << false << qint64(60000) << true << 1 << 1 << 50;
	QTest::newRow("connection refused") << 0 << QNetworkReply::ConnectionRefusedError << QByteArray() << false << qint64(60000) << true << 1 << 1 << 50;
	QTest::newRow("TLS handshake failed") << 0 << QNetworkReply::SslHandshakeFailedError << QByteArray() << false << qint64(60000) << true << 1 << 1 << 50;
}

void TestTelemetrySender::outcomes() {
	QFETCH(int, status);
	QFETCH(QNetworkReply::NetworkError, error);
	QFETCH(QByteArray, retry);
	QFETCH(bool, retryDate);
	QFETCH(qint64, delay);
	QFETCH(bool, retained);
	QFETCH(int, failures);
	QFETCH(int, events);
	QFETCH(int, limit);
	const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(10);
	if (retryDate) retry = base.addMSecs(60001 + 300000).toUTC().toString(Qt::RFC2822Date).toLatin1();
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString settingsFile = directory.path() + QStringLiteral("/consent.ini");
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	QVERIFY(consent.save(true, false));
	auto clock = std::make_shared<std::atomic<qint64>>(0);
	Semaphore posted, completed, exited;
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
		RECORD_VERIFY(++calls == 1);
		RECORD_VERIFY(bytes.size() <= 65536);
		RECORD_VERIFY(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::ManualRedirectPolicy);
		RECORD_VERIFY(request.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt() == QNetworkRequest::Manual);
		RECORD_VERIFY(request.attribute(QNetworkRequest::CookieSaveControlAttribute).toInt() == QNetworkRequest::Manual);
		posted.release();
		return new ScriptedReply(request, status, &manager, retry, error);
	};
	telemetry::TelemetrySender sender(m_context, m_metadata, storage, options);
	telemetry::TelemetryEventInput input;
	for (int i = 0; i < events; ++i) QVERIFY(waitForGate(sender, input));
	*clock = 60001;
	QVERIFY(posted.tryAcquire(1, 3000));
	QVERIFY(completed.tryAcquire(1, 3000));
	const auto control = readObject(storage + QStringLiteral("/control.json"));
	const auto retryState = control.value(QStringLiteral("retry")).toObject();
	QCOMPARE(retryState.value(QStringLiteral("failures")).toInt(), failures);
	QCOMPARE(retryState.value(QStringLiteral("usage_limit")).toInt(), limit);
	const auto next = QDateTime::fromString(retryState.value(QStringLiteral("next")).toString(), Qt::ISODateWithMs);
	const qint64 observedDelay = base.addMSecs(60001).msecsTo(next);
	if (retryDate) {
		QCOMPARE_GT(observedDelay, delay - 1000);
		QCOMPARE_LE(observedDelay, delay);
	} else {
		QCOMPARE(observedDelay, delay);
	}
	const auto usage = readObject(storage + QStringLiteral("/usage.json"));
	QCOMPARE(usage.value(QStringLiteral("events")).toArray().size(), retained ? events : 0);
	sender.stop();
	QVERIFY(exited.tryAcquire(1, 3000));
}

void TestTelemetrySender::startupIngress() {
	// The usage gate opens before the first diagnostics observation. An event
	// accepted in between is still sent.
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString settingsFile = directory.path() + QStringLiteral("/consent.ini");
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	QVERIFY(consent.save(true, false));
	auto clock = std::make_shared<std::atomic<qint64>>(0);
	const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(10);
	Semaphore opened, proceed, posted, completed, exited;
	std::atomic<bool> first{true};
	telemetry::TelemetrySender::TestOptions options;
	options.settingsFactory = [settingsFile] { return std::unique_ptr<QSettings>(new QSettings(settingsFile, QSettings::IniFormat)); };
	options.utcNow = [clock, base] { return base.addMSecs(clock->load()); };
	options.monotonicMs = [clock] { return clock->load(); };
	options.betweenObservations = [&] {
		if (!first.exchange(false)) return;
		opened.release();
		RECORD_VERIFY(proceed.tryAcquire(1, 3000));
	};
	options.afterCompletion = [&completed] { completed.release(); };
	options.onWorkerExit = [&exited] { exited.release(); };
	options.post = [&](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray&) {
		posted.release();
		return new ScriptedReply(request, 202, &manager);
	};
	telemetry::TelemetrySender sender(m_context, m_metadata, directory.path() + QStringLiteral("/queue"), options);
	QVERIFY(opened.tryAcquire(1, 3000));
	QVERIFY(sender.tryEnqueue({}));
	proceed.release();
	*clock = 60001;
	QVERIFY(posted.tryAcquire(1, 3000));
	QVERIFY(completed.tryAcquire(1, 3000));
	sender.stop();
	QVERIFY(exited.tryAcquire(1, 3000));
}

void TestTelemetrySender::stopKeepsStoredEvents() {
	// stop() raises the same flag as a receiver change. A stop that arrives while
	// a pump starts must not purge the stored events.
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString settingsFile = directory.filePath(QStringLiteral("consent.ini"));
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	QVERIFY(consent.save(true, false));
	Semaphore polled, proceed, settled, atPump, resume, exited;
	std::atomic<bool> hold{false};
	telemetry::TelemetrySender::TestOptions options;
	options.settingsFactory = [settingsFile] { return std::make_unique<QSettings>(settingsFile, QSettings::IniFormat); };
	options.monotonicMs = [] { return 0; }; // Never transmit; inspect durable records.
	options.afterPoll = [&] {
		polled.release();
		RECORD_VERIFY(proceed.tryAcquire(1, 3000));
		QTimer::singleShot(0, [&] { settled.release(); });
	};
	options.beforeFlagsRead = [&] {
		if (!hold.exchange(false)) return;
		atPump.release();
		RECORD_VERIFY(resume.tryAcquire(1, 3000));
	};
	options.onWorkerExit = [&] { exited.release(); };
	telemetry::TelemetrySender sender(m_context, m_metadata, directory.filePath(QStringLiteral("queue")), options);
	QVERIFY(polled.tryAcquire(1, 3000));
	QVERIFY(sender.tryEnqueue({}));
	proceed.release();
	QVERIFY(settled.tryAcquire(1, 3000));
	QCOMPARE(readDurableEvents(directory.filePath(QStringLiteral("queue/usage.json"))).size(), 1);
	hold = true;
	QVERIFY(atPump.tryAcquire(1, 3000));
	sender.stop();
	resume.release();
	QVERIFY(exited.tryAcquire(1, 3000));
	QCOMPARE(readDurableEvents(directory.filePath(QStringLiteral("queue/usage.json"))).size(), 1);
}

void TestTelemetrySender::preparationBarrier_data() {
	QTest::addColumn<int>("action");
	QTest::newRow("consent revoked") << 0;
	QTest::newRow("receiver changed") << 1;
	QTest::newRow("stopped") << 2;
}

void TestTelemetrySender::preparationBarrier() {
	QFETCH(int, action);
	QTemporaryDir directory;
	const QString settingsFile = directory.path() + QStringLiteral("/consent.ini");
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	QVERIFY(consent.save(true, false));
	auto clock = std::make_shared<std::atomic<qint64>>(0);
	const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(10);
	Semaphore reserved, proceed, settled, exited;
	std::atomic<int> posts{0};
	telemetry::TelemetrySender::TestOptions options;
	options.settingsFactory = [settingsFile] { return std::unique_ptr<QSettings>(new QSettings(settingsFile, QSettings::IniFormat)); };
	options.utcNow = [clock, base] { return base.addMSecs(clock->load()); };
	options.monotonicMs = [clock] { return clock->load(); };
	options.afterReservation = [&] {
		QTimer::singleShot(0, [&] { settled.release(); });
		reserved.release();
		RECORD_VERIFY(proceed.tryAcquire(1, 3000));
	};
	options.onWorkerExit = [&] { exited.release(); };
	options.post = [&](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray&) {
		++posts;
		return new ScriptedReply(request, 202, &manager);
	};
	telemetry::TelemetrySender sender(m_context, m_metadata, directory.path() + QStringLiteral("/queue"), options);
	QVERIFY(waitForGate(sender, {}));
	*clock = 60001;
	QVERIFY(reserved.tryAcquire(1, 3000));
	if (action == 0) sender.invalidateConsent(true, false);
	if (action == 1) sender.invalidateReceiver();
	if (action == 2) {
		QElapsedTimer timer;
		timer.start();
		sender.stop();
		QCOMPARE_LT(timer.elapsed(), qint64(100));
	}
	proceed.release();
	if (action != 2) {
		QVERIFY(settled.tryAcquire(1, 3000)); // Dispatch boundary ran without stop masking invalidation.
		QCOMPARE(posts.load(), 0);
		sender.stop();
	}
	QVERIFY(exited.tryAcquire(1, 3000));
	QCOMPARE(posts.load(), 0);
}

void TestTelemetrySender::interruptedBackoff() {
	QTemporaryDir directory;
	const QString settingsFile = directory.path() + QStringLiteral("/consent.ini");
	const QString storage = directory.path() + QStringLiteral("/queue");
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	QVERIFY(consent.save(true, false));
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
		Semaphore reserved, proceed, exited, posted, completed;
		auto options = optionsBase();
		if (attempt < 3) {
			options.afterReservation = [&] {
				reserved.release();
				RECORD_VERIFY(proceed.tryAcquire(1, 3000));
			};
			options.post = [](QNetworkAccessManager&, const QNetworkRequest&, const QByteArray&) -> QNetworkReply* {
				recordFailure(QStringLiteral("interrupted preparation must not transmit"));
				return nullptr;
			};
		} else {
			options.post = [&](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray& bytes) {
				const QString id = QJsonDocument::fromJson(bytes).object().value(QStringLiteral("events")).toArray().first().toObject().value(QStringLiteral("event_id")).toString();
				RECORD_VERIFY(id == originalId);
				posted.release();
				return new ScriptedReply(request, 202, &manager);
			};
			options.afterCompletion = [&] { completed.release(); };
		}
		options.onWorkerExit = [&] { exited.release(); };
		telemetry::TelemetrySender sender(m_context, m_metadata, storage, options);
		if (attempt == 1) QVERIFY(waitForGate(sender, {}));
		*clock = attempt == 1 ? 60001 : attempt == 2 ? 120002
													 : 300003;
		if (attempt < 3) {
			QVERIFY(reserved.tryAcquire(1, 3000));
			const auto retry = readObject(storage + QStringLiteral("/control.json")).value(QStringLiteral("retry")).toObject();
			QCOMPARE(retry.value(QStringLiteral("failures")).toInt(), attempt);
			const auto deadline = QDateTime::fromString(retry.value(QStringLiteral("next")).toString(), Qt::ISODateWithMs);
			QCOMPARE(base.addMSecs(clock->load()).msecsTo(deadline), attempt == 1 ? 60000 : 120000);
			const auto records = readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray();
			QCOMPARE(records.size(), 1);
			const QString id = records.first().toObject().value(QStringLiteral("event")).toObject().value(QStringLiteral("event_id")).toString();
			if (attempt == 1) originalId = id;
			else QCOMPARE(id, originalId);
			sender.stop();
			proceed.release();
		} else {
			QVERIFY(posted.tryAcquire(1, 3000));
			QVERIFY(completed.tryAcquire(1, 3000));
			QVERIFY(readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray().isEmpty());
			sender.stop();
		}
		QVERIFY(exited.tryAcquire(1, 3000));
	}
}

void TestTelemetrySender::timeoutAndExceptions_data() {
	QTest::addColumn<int>("scenario");
	QTest::newRow("timeout") << 0;
	QTest::newRow("body of 8192 bytes") << 1;
	QTest::newRow("body of 8193 bytes") << 2;
	QTest::newRow("exception after the reservation") << 3;
	QTest::newRow("late reply") << 4;
}

void TestTelemetrySender::timeoutAndExceptions() {
	QFETCH(int, scenario);
	QTemporaryDir directory;
	const QString settingsFile = directory.path() + QStringLiteral("/consent.ini");
	const QString storage = directory.path() + QStringLiteral("/queue");
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	QVERIFY(consent.save(true, false));
	auto clock = std::make_shared<std::atomic<qint64>>(0);
	const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(10);
	Semaphore posted, completed, exited;
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
		const QString id = QJsonDocument::fromJson(bytes).object().value(QStringLiteral("events")).toArray().first().toObject().value(QStringLiteral("event_id")).toString();
		++requests;
		if (scenario == 0) {
			if (requests == 1) firstId = id;
			else RECORD_VERIFY(id == firstId);
		}
		auto* scripted = new ScriptedReply(request, 202, &manager, {}, QNetworkReply::NoError,
			scenario == 1 ? QByteArray(8192, 'x') : scenario == 2 ? QByteArray(8193, 'x')
																  : QByteArray(),
			scenario == 4 || (scenario == 0 && requests == 1));
		if (scenario == 4) delayed = scripted;
		posted.release();
		return scripted;
	};
	telemetry::TelemetrySender sender(m_context, m_metadata, storage, options);
	QVERIFY(waitForGate(sender, {}));
	*clock = 60001;
	if (scenario == 3) {
		QVERIFY(exited.tryAcquire(1, 3000));
		QVERIFY(!posted.tryAcquire(1, 0));
		QVERIFY(!sender.tryEnqueue({}));
		return;
	}
	QVERIFY(posted.tryAcquire(1, 3000));
	if (scenario == 4) {
		QVERIFY(delayed);
		QVERIFY(!completed.tryAcquire(1, 0));
		*clock = 60501;
		QMetaObject::invokeMethod(delayed, [delayed] { delayed->complete(); }, Qt::QueuedConnection);
	}
	QVERIFY(completed.tryAcquire(1, 3000));
	const auto records = readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray();
	QCOMPARE(records.size(), scenario == 0 ? 1 : 0);
	const auto retry = readObject(storage + QStringLiteral("/control.json")).value(QStringLiteral("retry")).toObject();
	QCOMPARE(retry.value(QStringLiteral("failures")).toInt(), scenario == 0 ? 1 : 0);
	if (scenario == 0) {
		*clock = 120002;
		QVERIFY(posted.tryAcquire(1, 3000));
		QVERIFY(completed.tryAcquire(1, 3000));
		QCOMPARE(requests, 2);
		QVERIFY(readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray().isEmpty());
		QCOMPARE(
			readObject(storage + QStringLiteral("/control.json")).value(QStringLiteral("retry")).toObject().value(QStringLiteral("failures")).toInt(), 0);
	}
	sender.stop();
	QVERIFY(exited.tryAcquire(1, 3000));
}

void TestTelemetrySender::expiryAndActiveRevocation_data() {
	QTest::addColumn<int>("scenario");
	QTest::newRow("expiry while the upload waits") << 0;
	QTest::newRow("consent revoked") << 1;
	QTest::newRow("consent disabled and enabled in another instance") << 2;
}

void TestTelemetrySender::expiryAndActiveRevocation() {
	QFETCH(int, scenario);
	QTemporaryDir directory;
	const QString settingsFile = directory.path() + QStringLiteral("/consent.ini");
	const QString storage = directory.path() + QStringLiteral("/queue");
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	QVERIFY(consent.save(true, false));
	auto clock = std::make_shared<std::atomic<qint64>>(0);
	const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(10);
	auto utcClock = std::make_shared<std::atomic<qint64>>(0);
	if (scenario == 0) {
		telemetry::TelemetryQueue seed;
		QCOMPARE(seed.open(storage, m_context.endpoint, m_context.termsVersion, base), telemetry::TelemetryQueue::OpenResult::Owner);
		QVERIFY(seed.enqueueUsage({telemetry::createEvent({}, base.addSecs(-6 * 86400 - 23 * 3600)),
									  consent.observeUsage().installationId},
			base));
	}
	Semaphore posted, completed, polled, exited;
	std::atomic<int> calls{0};
	telemetry::TelemetrySender::TestOptions options;
	options.settingsFactory = [settingsFile] { return std::unique_ptr<QSettings>(new QSettings(settingsFile, QSettings::IniFormat)); };
	options.utcNow = [utcClock, base] { return base.addMSecs(utcClock->load()); };
	options.monotonicMs = [clock] { return clock->load(); };
	options.onWorkerExit = [&] { exited.release(); };
	options.afterCompletion = [&] { completed.release(); };
	options.afterPoll = [&] { polled.release(); };
	options.post = [&](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray&) {
		++calls;
		posted.release();
		return new ScriptedReply(request, scenario == 0 ? 403 : 202, &manager, {},
			QNetworkReply::NoError, {}, scenario != 0);
	};
	telemetry::TelemetrySender sender(m_context, m_metadata, storage, options);
	if (scenario != 0) QVERIFY(waitForGate(sender, {}));
	else QVERIFY(polled.tryAcquire(1, 3000));
	*utcClock = 60001;
	*clock = 60001;
	QVERIFY(posted.tryAcquire(1, 3000));
	if (scenario == 0) {
		QVERIFY(completed.tryAcquire(1, 3000));
		const auto retry = readObject(storage + QStringLiteral("/control.json")).value(QStringLiteral("retry")).toObject();
		QCOMPARE(retry.value(QStringLiteral("failures")).toInt(), 0);
		const auto uploadDeadline = QDateTime::fromString(retry.value(QStringLiteral("next")).toString(), Qt::ISODateWithMs);
		QCOMPARE(base.addMSecs(60001).msecsTo(uploadDeadline), 86400000);
		QCOMPARE(readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray().size(), 1);
		*utcClock = 3600001; // Old row crosses seven days, but neither upload deadline has elapsed.
		*clock = 61002;
		QVERIFY(base.addMSecs(utcClock->load()) < uploadDeadline);
		QCOMPARE_LT(clock->load(), 60001 + 86400000);
		sender.requestConsentRefresh();
		QVERIFY(polled.tryAcquire(1, 3000));
		// Poll notification can include the startup poll; drain and observe the actual file condition.
		for (int i = 0; i < 100 && !readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray().isEmpty(); ++i) {
			QVERIFY(polled.tryAcquire(1, 3000));
			*clock += 1001;
		}
		QVERIFY(readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray().isEmpty());
		QCOMPARE(calls.load(), 1);
		QVERIFY(base.addMSecs(utcClock->load()) < uploadDeadline);
		QCOMPARE_LT(clock->load(), 60001 + 86400000);
	} else {
		if (scenario == 1) sender.invalidateConsent(true, false);
		else {
			// Worker misses the disabled interval; the new stamp still removes the old row.
			QSettings other(settingsFile, QSettings::IniFormat);
			TelemetryConsent otherConsent(other, m_context);
			QVERIFY(otherConsent.save(false, false));
			QVERIFY(otherConsent.save(true, false));
			sender.requestConsentRefresh();
		}
		QVERIFY(completed.tryAcquire(1, 3000));
		sender.requestConsentRefresh();
		*clock = 61002;
		bool removed = false;
		for (int i = 0; i < 100; ++i) {
			QVERIFY(polled.tryAcquire(1, 3000));
			removed = readObject(storage + QStringLiteral("/usage.json"))
						  .value(QStringLiteral("events"))
						  .toArray()
						  .isEmpty();
			if (removed) break;
			*clock += 1001;
		}
		QVERIFY(removed);
		QCOMPARE(calls.load(), 1);
	}
	sender.stop();
	QVERIFY(exited.tryAcquire(1, 3000));
}

void TestTelemetrySender::constructionAndWorkerExceptions() {
	QTemporaryDir directory;
	telemetry::TelemetrySender::TestOptions options;
	options.settingsFactory = [] { return std::unique_ptr<QSettings>(new QSettings); };
	options.failConstruction = true;
	telemetry::TelemetrySender failed(m_context, m_metadata, directory.path() + QStringLiteral("/construction"), options);
	QVERIFY(!failed.tryEnqueue({}));
	options.failConstruction = false;
	Semaphore invoked, exited;
	options.settingsFactory = [&]() -> std::unique_ptr<QSettings> {
		invoked.release();
		throw std::bad_alloc();
	};
	options.onWorkerExit = [&] { exited.release(); };
	telemetry::TelemetrySender worker(m_context, m_metadata, directory.path() + QStringLiteral("/worker"), options);
	QVERIFY(invoked.tryAcquire(1, 3000));
	QVERIFY(exited.tryAcquire(1, 3000));
	QVERIFY(!worker.tryEnqueue({}));
	worker.stop();
}

void TestTelemetrySender::followerTakeover() {
	QTemporaryDir directory;
	const QString settingsFile = directory.path() + QStringLiteral("/consent.ini");
	const QString storage = directory.path() + QStringLiteral("/queue");
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	QVERIFY(consent.save(true, false));
	auto clock = std::make_shared<std::atomic<qint64>>(0);
	const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(10);
	Semaphore firstPosted, secondPosted, secondCompleted, firstExited, secondExited, followerAttempt;
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
		id = QJsonDocument::fromJson(body).object().value(QStringLiteral("events")).toArray().first().toObject().value(QStringLiteral("event_id")).toString();
		firstPosted.release();
		return new ScriptedReply(request, 202, &manager, {}, QNetworkReply::NoError, {}, true);
	};
	telemetry::TelemetrySender first(m_context, m_metadata, storage, firstOptions);
	QVERIFY(waitForGate(first, {}));
	*clock = 60001;
	QVERIFY(firstPosted.tryAcquire(1, 3000));
	QVERIFY(!id.isEmpty());
	auto secondOptions = optionsBase();
	secondOptions.onWorkerExit = [&] { secondExited.release(); };
	secondOptions.afterOwnershipAttempt = [&](bool owner) {
		if (!owner) followerAttempt.release();
	};
	secondOptions.afterCompletion = [&] { secondCompleted.release(); };
	secondOptions.post = [&](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray& body) {
		const auto reused =
			QJsonDocument::fromJson(body).object().value(QStringLiteral("events")).toArray().first().toObject().value(QStringLiteral("event_id")).toString();
		RECORD_VERIFY(reused == id);
		secondPosted.release();
		return new ScriptedReply(request, 202, &manager);
	};
	telemetry::TelemetrySender second(m_context, m_metadata, storage, secondOptions);
	QVERIFY(followerAttempt.tryAcquire(1, 3000));
	QVERIFY(!second.tryEnqueue({})); // The follower does not buffer producer intents.
	first.stop();
	QVERIFY(firstExited.tryAcquire(1, 3000));
	*clock = 120002;
	QVERIFY(secondPosted.tryAcquire(1, 3000));
	QVERIFY(secondCompleted.tryAcquire(1, 3000));
	QVERIFY(readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray().isEmpty());
	second.stop();
	QVERIFY(secondExited.tryAcquire(1, 3000));
}

void TestTelemetrySender::retirementWithQueuedWork() {
	QTemporaryDir directory;
	const QString settingsFile = directory.path() + QStringLiteral("/consent.ini");
	const QString storage = directory.path() + QStringLiteral("/queue");
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	QVERIFY(consent.save(true, false));
	auto clock = std::make_shared<std::atomic<qint64>>(0);
	const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(10);
	Semaphore posted, completed, exited;
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
	telemetry::TelemetrySender sender(m_context, m_metadata, storage, options);
	QVERIFY(waitForGate(sender, {}));
	*clock = 60001;
	QVERIFY(posted.tryAcquire(1, 3000));
	QVERIFY(pending);
	QVERIFY(waitForGate(sender, {})); // A second event arrives while the first request is outstanding.
	QMetaObject::invokeMethod(pending, [pending] { pending->complete(); }, Qt::QueuedConnection);
	QVERIFY(completed.tryAcquire(1, 3000));
	sender.stop();
	QVERIFY(exited.tryAcquire(1, 3000));
	QVERIFY(readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray().isEmpty());
	telemetry::TelemetryQueue reopened;
	QCOMPARE(reopened.open(storage, m_context.endpoint, m_context.termsVersion, base.addMSecs(60001)), telemetry::TelemetryQueue::OpenResult::Retired);
	reopened.close();
}

void TestTelemetrySender::splitRetainsIds() {
	QTemporaryDir directory;
	const QString settingsFile = directory.path() + QStringLiteral("/consent.ini");
	const QString storage = directory.path() + QStringLiteral("/queue");
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	QVERIFY(consent.save(true, false));
	auto clock = std::make_shared<std::atomic<qint64>>(0);
	const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(10);
	Semaphore posted, completed, exited;
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
			RECORD_VERIFY(events.size() == 2);
			for (const auto& event : events) ids.insert(event.toObject().value(QStringLiteral("event_id")).toString());
			RECORD_VERIFY(ids.size() == 2);
		} else {
			RECORD_VERIFY(events.size() == 1);
			const QString id = events.first().toObject().value(QStringLiteral("event_id")).toString();
			RECORD_VERIFY(ids.contains(id));
			RECORD_VERIFY(!acknowledged.contains(id));
			acknowledged.insert(id);
		}
		posted.release();
		return new ScriptedReply(request, attempts == 1 ? 413 : 202, &manager);
	};
	telemetry::TelemetrySender sender(m_context, m_metadata, storage, options);
	for (int i = 0; i < 2; ++i) QVERIFY(waitForGate(sender, {}));
	for (qint64 moment : {60001LL, 90002LL, 150003LL}) {
		*clock = moment;
		QVERIFY(posted.tryAcquire(1, 3000));
		QVERIFY(completed.tryAcquire(1, 3000));
	}
	QCOMPARE(attempts, 3);
	QVERIFY(acknowledged == ids);
	QVERIFY(readObject(storage + QStringLiteral("/usage.json")).value(QStringLiteral("events")).toArray().isEmpty());
	sender.stop();
	QVERIFY(exited.tryAcquire(1, 3000));
}

void TestTelemetrySender::retryAfterAndRetirement() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString settingsFile = directory.path() + QStringLiteral("/settings.ini");
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	QVERIFY(consent.save(true, false));
	const telemetry::TelemetryEventInput input;
	auto clock = std::make_shared<std::atomic<qint64>>(0);
	const QDateTime base = QDateTime::currentDateTimeUtc().addSecs(2);
	auto posts = std::make_shared<std::atomic<int>>(0);
	auto stableId = std::make_shared<std::atomic<bool>>(false);
	auto firstId = std::make_shared<QString>();
	// The pumps that began, the responses handled and the workers that exited.
	auto pumps = std::make_shared<std::atomic<int>>(0);
	auto completions = std::make_shared<std::atomic<int>>(0);
	auto exits = std::make_shared<std::atomic<int>>(0);
	telemetry::TelemetrySender::TestOptions tests;
	tests.settingsFactory = [settingsFile] {
		return std::unique_ptr<QSettings>(new QSettings(settingsFile, QSettings::IniFormat));
	};
	tests.monotonicMs = [clock] { return clock->load(); };
	tests.utcNow = [clock, base] { return base.addMSecs(clock->load()); };
	tests.jitterPercent = [] { return 100; };
	tests.beforeFlagsRead = [pumps] { ++*pumps; };
	tests.afterCompletion = [completions] { ++*completions; };
	tests.onWorkerExit = [exits] { ++*exits; };
	tests.post = [posts, stableId, firstId](QNetworkAccessManager& manager, const QNetworkRequest& request, const QByteArray& body) {
		RECORD_VERIFY(request.url().host() == QStringLiteral("127.0.0.1"));
		RECORD_VERIFY(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::ManualRedirectPolicy);
		RECORD_VERIFY(request.attribute(QNetworkRequest::AuthenticationReuseAttribute).toInt() == QNetworkRequest::Manual);
		RECORD_VERIFY(body.size() <= 65536);
		RECORD_VERIFY(QJsonDocument::fromJson(body).isObject());
		const QString id =
			QJsonDocument::fromJson(body).object().value(QStringLiteral("events")).toArray().first().toObject().value(QStringLiteral("event_id")).toString();
		const int attempt = ++*posts;
		if (attempt == 1) *firstId = id;
		if (attempt == 2) *stableId = id == *firstId;
		return new ScriptedReply(request, attempt == 1 ? 429 : attempt == 2 ? 202
																			: 410,
			&manager,
			attempt == 1 ? QByteArray("120") : QByteArray());
	};
	telemetry::TelemetrySender sender(m_context, m_metadata, directory.path() + QStringLiteral("/queue"), tests);
	QVERIFY(waitForGate(sender, input, 2 * kWaitMs));
	sender.invalidateConsent(true, false);
	QVERIFY(!sender.tryEnqueue(input));
	// The next whole pump takes the revocation before the refresh is asked for.
	const int revokedAt = pumps->load();
	QTRY_VERIFY_WITH_TIMEOUT(pumps->load() >= revokedAt + 2, kWaitMs);
	sender.requestConsentRefresh();
	QVERIFY(waitForGate(sender, input, 2 * kWaitMs));
	*clock = 60001;
	QTRY_COMPARE_WITH_TIMEOUT(posts->load(), 1, 2 * kWaitMs);
	// The 429 response is handled at this time, so its Retry-After counts from it.
	QTRY_COMPARE_WITH_TIMEOUT(completions->load(), 1, kWaitMs);
	*clock = 120002;
	QThread::msleep(100);		// Checks that no request follows in this time.
	QCOMPARE(posts->load(), 1); // Retry-After wins over one-minute backoff.
	*clock = 180003;
	QTRY_COMPARE_WITH_TIMEOUT(posts->load(), 2, 2 * kWaitMs);
	QTRY_COMPARE_WITH_TIMEOUT(completions->load(), 2, kWaitMs);
	// The post callback has returned once its reply completed, so it has written stableId.
	QVERIFY(stableId->load());
	QVERIFY(waitForGate(sender, input));
	*clock = 240004;
	QTRY_COMPARE_WITH_TIMEOUT(posts->load(), 3, 2 * kWaitMs);
	// The 410 response retires the receiver before the stop.
	QTRY_COMPARE_WITH_TIMEOUT(completions->load(), 3, kWaitMs);
	sender.stop();
	QVERIFY(!sender.tryEnqueue(input));
	QTRY_COMPARE_WITH_TIMEOUT(exits->load(), 1, kWaitMs);
	telemetry::TelemetrySender restarted(m_context, m_metadata, directory.path() + QStringLiteral("/queue"), tests);
	// Checks for a second that the retired queue takes nothing.
	for (int i = 0; i < 50; ++i) {
		QThread::msleep(20);
		QVERIFY(!restarted.tryEnqueue(input));
	}
	restarted.stop();
	QCOMPARE(posts->load(), 3);
}
#endif

QTEST_GUILESS_MAIN(TestTelemetrySender)
#include "test_telemetrysender.moc"
