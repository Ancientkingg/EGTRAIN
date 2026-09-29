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
    bool tryEnqueue(const TelemetryEventInput& input) noexcept;
    void invalidateConsent(bool usage, bool diagnostics) noexcept;
    void invalidateReceiver() noexcept;
    void requestConsentRefresh() noexcept;
    void stop() noexcept;
private:
    std::shared_ptr<Shared> m_shared;
};
}
