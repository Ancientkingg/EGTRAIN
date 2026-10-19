#pragma once

#include "telemetry/TelemetryConsent.h"
#include "telemetry/TelemetryEvent.h"
#include <QDateTime>
#include <QSettings>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <functional>
#include <memory>

namespace telemetry {
/// Sends usage and diagnostic events to the compiled receiver from a worker thread of its own.
/// Producers on any thread hand events to an in-memory ring of 128 entries without waiting. Once a
/// second the worker re-reads consent through its own TelemetryConsent, stores events in a
/// TelemetryQueue and posts batches over HTTPS, handling each response as the HTTP outcomes of
/// docs/telemetry/README.md say. Only the process that owns the queue sends; in others every event
/// is dropped. A sender whose context is not capable, or whose construction fails, starts no thread
/// and accepts nothing.
class TelemetrySender {
public:
	/// Internal ingress state retained by the worker until it exits.
	struct Shared;
	/// Default-invalid authorization for one sender, independently per category.
	/// Copying preserves entry-time eligibility; it never acquires newer consent.
	class OperationToken {
	public:
		/// A token that authorizes nothing.
		OperationToken() noexcept = default;
	private:
		friend class TelemetrySender;
		std::weak_ptr<Shared> sender;
		quint64 epoch[2] = {0, 0};
		quint64 invalidation[2] = {0, 0};
		bool eligible[2] = {false, false};
	};
#ifdef EGTRAIN_SENDER_TEST_HOOK
	/// Test hooks. Only isolated numeric loopback, private settings and private storage are
	/// permitted: when any option is set, settingsFactory is required, the endpoint host must be
	/// 127.0.0.1 or ::1, and the settings file and the queue directory must lie apart in the system
	/// temporary directory.
	struct TestOptions {
		std::function<std::unique_ptr<QSettings>()> settingsFactory; ///< Creates the settings the worker reads consent from.
		std::function<QDateTime()> utcNow;							 ///< Replaces the UTC clock.
		std::function<qint64()> monotonicMs;						 ///< Replaces the monotonic clock, in milliseconds.
		std::function<int()> jitterPercent;							 ///< Replaces the random backoff jitter, in percent; kept within 50 to 150.
		/// Called after the durable reservation, before the final authorization check.
		std::function<void()> afterReservation;
		std::function<void()> onWorkerExit;	   ///< Called on the worker thread when it exits.
		std::function<void()> afterCompletion; ///< Called after each response has been handled.
		std::function<void()> afterPoll;	   ///< Called after each consent poll of the queue owner.
		/// Called in each pump just before the invalidation flags are read.
		std::function<void()> beforeFlagsRead;
		/// Called in each refresh after the usage observation, before the diagnostics one.
		std::function<void()> betweenObservations;
		std::function<void()> afterOperationCaptureLocked; ///< Isolated contention tests only.
		std::function<void()> beforeSessionAttempt;		   ///< After the lifetime latch, before allocation/storage.
		std::function<void(bool)> afterOwnershipAttempt;   ///< True only when this worker owns storage.
		bool failConstruction = false;					   ///< The constructor fails as on an allocation failure.
		QByteArray loopbackRootPem;						   ///< The root certificate that a loopback server is checked against.
		int requestTimeoutMs;							   ///< Time limit of one request in milliseconds, 1 to 10000.
		TestOptions() : requestTimeoutMs(10000) {}
		/// Worker-thread callback; reply ownership is transferred to the sender.
		std::function<QNetworkReply*(QNetworkAccessManager&, const QNetworkRequest&, const QByteArray&)> post;
	};
#endif
	/// Starts the worker when context is capable, application is valid once its platform and
	/// architecture are replaced by those of this build, privateDirectory is not empty and the
	/// endpoint has at most 2048 UTF-8 bytes. privateDirectory is the folder of the queue.
	TelemetrySender(TelemetryContext context, Application application, QString privateDirectory
#ifdef EGTRAIN_SENDER_TEST_HOOK
		,
		TestOptions tests = {}
#endif
	);
	/// Calls stop(). The worker exits by itself; nothing waits for it or for delivery.
	~TelemetrySender();
	TelemetrySender(const TelemetrySender&) = delete;
	TelemetrySender& operator=(const TelemetrySender&) = delete;
	/// The authorization of both categories at the start of an operation. It is invalid when the
	/// sender has stopped or its lock is busy, as it never waits. Product observations must carry
	/// entry-time authorization, never retry capture.
	OperationToken captureOperation() noexcept;
	/// Takes in input, dated now, when its category was authorized in token, consent has not been
	/// invalidated since, and the ring has room. Never waits; returns false when input is dropped.
	bool tryEnqueue(const TelemetryEventInput& input, const OperationToken& token) noexcept;
	/// Takes in input without a token, still only while its category is enabled and the ring has
	/// room. Only the sender tests use it.
	bool tryEnqueue(const TelemetryEventInput& input) noexcept;
	/// Explicit interactive lifetime request, attempted once at first enabled usage: the worker then
	/// stores one session.started event. Ignored when the context is not interactive.
	void requestInteractiveSession() noexcept;
	/// Closes the named categories at once, drops their waiting events and invalidates tokens taken
	/// before; the worker purges their queued events and opens them again after its next consent read.
	/// Connected directly to TelemetryConsent::revoked. Never waits.
	void invalidateConsent(bool usage, bool diagnostics) noexcept;
	/// Invalidates both categories; the worker purges every queued event and sends nothing more in this
	/// run. Connected directly to TelemetryConsent::receiverChanged. Never waits.
	void invalidateReceiver() noexcept;
	/// Asks the worker to re-read consent at its next tick, within about 50 ms, instead of at its next
	/// poll.
	void requestConsentRefresh() noexcept;
	/// Stops taking in events at once. The worker aborts a running request and exits; queued events
	/// stay on disk. Never waits.
	void stop() noexcept;
private:
	std::shared_ptr<Shared> m_shared;
};
}
