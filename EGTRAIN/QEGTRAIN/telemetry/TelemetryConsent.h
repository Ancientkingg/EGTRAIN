#pragma once

#include <QObject>
#include <QSettings>
#include <QProcessEnvironment>
#include <QString>
#include <QUrl>
#ifdef EGTRAIN_CONSENT_TEST_HOOK
#include <functional>
#endif

/// What this build and run allow for telemetry. Consent can be asked and events sent only when
/// capable() is true. See the consent boundary in docs/telemetry/README.md.
struct TelemetryContext {
	bool packaged = false;						///< Built as a distributed package (EGTRAIN_PACKAGED_BUILD).
	bool enabled = false;						///< Telemetry compiled in (EGTRAIN_ENABLE_TELEMETRY).
	bool interactive = false;					///< An ordinary interactive GUI run; see telemetryInteractiveEnvironment().
	QString endpoint;							///< Exact HTTPS URL of the receiver (EGTRAIN_TELEMETRY_URL); empty when none.
	QString termsVersion = QStringLiteral("1"); ///< Version of the collection terms. A change resets the saved choices.
	/// True when packaged, enabled and interactive, with non-empty terms and a valid exact HTTPS endpoint.
	bool capable() const;
	/// Host of endpoint, shown as the receiving domain; empty when endpoint has none.
	QString domain() const;
};

class QLockFile;

/// The saved choices for usage statistics and diagnostic reports, for the receiver and terms of the
/// context. They are one JSON value in the given QSettings, re-read and written under a lock file
/// that is tried for at most 100 ms.
///
/// promptRequired(), usageEnabled(), diagnosticsEnabled(), usageInstallationId() and save() re-read
/// the settings, so they see choices changed by another process. After a lock or storage failure
/// they fail closed and report no choice as enabled.
/// observeUsage() and observeDiagnostics() read the settings again on each call and return Error
/// when the lock or read fails. A saved choice for another endpoint or terms version is reset on
/// the next re-read.
class TelemetryConsent : public QObject {
	Q_OBJECT
public:
	/// settings is not owned and must outlive this object. Reads the saved choices once, without the
	/// lock.
	TelemetryConsent(QSettings& settings, TelemetryContext context, QObject* parent = nullptr);
	/// True when the context is capable, so the choices can be asked for and saved.
	bool available() const;
	/// True when the consent dialog should be shown at start-up: no valid choice is saved for this
	/// receiver and terms. False when unavailable or after a storage failure.
	bool promptRequired();
	/// True when usage statistics are saved as enabled for this receiver and terms, with a valid
	/// installation ID.
	bool usageEnabled();
	/// True when diagnostic reports are saved as enabled for this receiver and terms.
	bool diagnosticsEnabled();
	/// The random UUIDv4 that identifies this installation in usage batches; empty when usage is not
	/// enabled. Diagnostics never use it.
	QString usageInstallationId();
	/// Result of one observation of a category by the sender's worker.
	enum class ObservationStatus { Enabled, ///< The category is enabled for this receiver and terms.
		Disabled,							///< Nothing is saved, or the category is not enabled.
		Mismatch,							///< The saved choices belong to another endpoint or terms version.
		Unavailable,						///< The context is not capable.
		Error								///< The lock was not taken, or the saved value cannot be read or is malformed.
	};
	/// Usage consent as observeUsage() read it.
	struct UsageObservation {
		ObservationStatus status; ///< Result of the observation.
		QString installationId;	  ///< Installation UUID when status is Enabled; empty otherwise.
	};
	/// Diagnostics consent as observeDiagnostics() read it.
	struct DiagnosticsObservation {
		ObservationStatus status; ///< Result of the observation.
		/// Diagnostics consent generation when status is Enabled, 0 otherwise. It changes whenever
		/// earlier diagnostic records must no longer be sent, such as after a revocation, so they stay
		/// unsent after a later re-enablement. It is never sent itself.
		int generation = 0;
	};
	/// Reads usage consent under the lock without changing it or emitting a signal.
	UsageObservation observeUsage();
	/// Reads diagnostics consent under the lock without changing it or emitting a signal.
	DiagnosticsObservation observeDiagnostics();
	/// Saves both choices for this receiver and terms. Enabling usage keeps a valid installation ID or
	/// creates a new one; disabling usage removes it. Emits revoked for a choice that is turned off.
	/// Returns false when unavailable, when the lock is not taken or when the write fails; no choice
	/// is then active in this object.
	bool save(bool usage, bool diagnostics);
	/// Saves both choices as disabled, as Not now does on the first prompt.
	void dismiss();
#ifdef EGTRAIN_CONSENT_TEST_HOOK
	/// Test hook: callback runs in save() before the lock is taken for the write.
	void setBeforeWriteForTesting(std::function<void()> callback) { m_beforeWriteForTesting = std::move(callback); }
#endif

signals:
	/// The named categories may no longer be sent under the consent seen before: a choice was turned
	/// off, the installation ID or diagnostics generation changed, the receiver or terms changed, or
	/// storage failed (both true). Emitted synchronously after the lock is released.
	void revoked(bool usage, bool diagnostics);
	/// The saved receiver endpoint, or the terms of a receiver seen before, changed. Emitted right
	/// after revoked(true, true).
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
	ObservationStatus observeState(State& state);
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

/// False when the environment marks a CI, automation, profiling or test run, or the Qt platform
/// (QT_QPA_PLATFORM or platform) is offscreen or minimal.
bool telemetryInteractiveEnvironment(const QProcessEnvironment& environment, const QString& platform);
/// The context of this build from its compile definitions, interactive when a QGuiApplication runs
/// in an interactive environment.
TelemetryContext applicationTelemetryContext();
