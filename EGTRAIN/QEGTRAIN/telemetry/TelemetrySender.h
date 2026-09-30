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
#ifdef EGTRAIN_SENDER_TEST_HOOK
#include <QSslConfiguration>
#include <optional>
#endif
#include <memory>

namespace telemetry {
class TelemetrySender {
public:
    struct Shared; // Internal ingress state retained by the worker until it exits.
    // Default-invalid authorization for one sender, independently per category.
    // Copying preserves entry-time eligibility; it never acquires newer consent.
    class OperationToken {
    public:
        OperationToken() noexcept = default;
    private:
        friend class TelemetrySender;
        std::weak_ptr<Shared> sender;
        quint64 epoch[2] = {0, 0};
        quint64 invalidation[2] = {0, 0};
        bool eligible[2] = {false, false};
    };
#ifdef EGTRAIN_SENDER_TEST_HOOK
    // Only isolated numeric loopback, private settings and private storage are permitted.
    struct TestOptions {
        std::function<std::unique_ptr<QSettings>()> settingsFactory;
        std::function<QDateTime()> utcNow;
        std::function<qint64()> monotonicMs;
        std::function<int()> jitterPercent;
        // Called after the durable reservation, before the final authorization check.
        std::function<void()> afterReservation;
        std::function<void()> onWorkerExit;
        std::function<void()> afterCompletion;
        std::function<void()> afterPoll;
        std::function<void()> afterOperationCaptureLocked; // Isolated contention tests only.
        std::function<void()> beforeSessionAttempt; // After the lifetime latch, before allocation/storage.
        std::function<void(bool)> afterOwnershipAttempt; // True only when this worker owns storage.
        bool failConstruction = false;
        std::optional<QSslConfiguration> loopbackTrust;
        int requestTimeoutMs;
        TestOptions() : requestTimeoutMs(10000) {}
        // Worker-thread callback; reply ownership is transferred to the sender.
        std::function<QNetworkReply*(QNetworkAccessManager&, const QNetworkRequest&, const QByteArray&)> post;
    };
#endif
    TelemetrySender(TelemetryContext context, Application application, QString privateDirectory
#ifdef EGTRAIN_SENDER_TEST_HOOK
                    , TestOptions tests = {}
#endif
                    );
    ~TelemetrySender();
    TelemetrySender(const TelemetrySender&) = delete;
    TelemetrySender& operator=(const TelemetrySender&) = delete;
    // Product observations must carry entry-time authorization, never retry capture.
    OperationToken captureOperation() noexcept;
    bool tryEnqueue(const TelemetryEventInput& input, const OperationToken& token) noexcept;
    // Unconditional admission is retained for low-level sender tests.
    bool tryEnqueue(const TelemetryEventInput& input) noexcept;
    // Explicit interactive lifetime request, attempted once at first enabled usage.
    void requestInteractiveSession() noexcept;
    void invalidateConsent(bool usage, bool diagnostics) noexcept;
    void invalidateReceiver() noexcept;
    void requestConsentRefresh() noexcept;
    void stop() noexcept;
private:
    std::shared_ptr<Shared> m_shared;
};
}
