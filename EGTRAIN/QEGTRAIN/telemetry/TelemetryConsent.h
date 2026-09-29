#pragma once

#include <QObject>
#include <QSettings>
#include <QProcessEnvironment>
#include <QString>
#include <QUrl>
#ifdef EGTRAIN_CONSENT_TEST_HOOK
#include <functional>
#endif

struct TelemetryContext {
    bool packaged = false;
    bool enabled = false;
    bool interactive = false;
    QString endpoint;
    QString termsVersion = QStringLiteral("1");
    bool capable() const;
    QString domain() const;
};

// All collection and upload accessors re-read QSettings. Connect revoked and
// receiverChanged directly to a future sender's queue-clear/abort slots.
class QLockFile;

class TelemetryConsent : public QObject {
    Q_OBJECT
public:
    TelemetryConsent(QSettings& settings, TelemetryContext context, QObject* parent = nullptr);
    bool available() const;
    bool promptRequired();
    bool usageEnabled();
    bool diagnosticsEnabled();
    QString usageInstallationId();
    bool save(bool usage, bool diagnostics);
    void dismiss();
#ifdef EGTRAIN_CONSENT_TEST_HOOK
    void setBeforeWriteForTesting(std::function<void()> callback) { m_beforeWriteForTesting = std::move(callback); }
#endif

signals:
    void revoked(bool usage, bool diagnostics);
    void receiverChanged();

private:
    struct State {
        QString endpoint;
        QString terms;
        bool handled = false;
        bool usage = false;
        bool diagnostics = false;
        QString id;
        int diagnosticsGeneration = 0;
    };
    struct Transition {
        bool usage = false;
        bool diagnostics = false;
        bool receiver = false;
    };
    State read();
    bool persist(const State& state);
    QString lockPath() const;
    bool acquire(QLockFile& lock, const QString& path);
    void refreshLocked(Transition& change);
    void notify(const Transition& change);
    void refresh();
    bool valid(const State& state) const;
    QSettings& m_settings;
    TelemetryContext m_context;
    State m_last;
    bool m_failed = false;
#ifdef EGTRAIN_CONSENT_TEST_HOOK
    std::function<void()> m_beforeWriteForTesting;
#endif
};

bool telemetryInteractiveEnvironment(const QProcessEnvironment& environment, const QString& platform);
TelemetryContext applicationTelemetryContext();
