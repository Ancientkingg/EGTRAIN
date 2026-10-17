#ifndef UPDATETELEMETRYCONTROLLER_H
#define UPDATETELEMETRYCONTROLLER_H

#include "scene/SceneDiagnostic.h"
#include "telemetry/TelemetryOperation.h"

#include <QObject>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QAction;
class QMenu;
class QProgressDialog;
class QSettings;
class QString;
class QWidget;
class SelfUpdater;
class TelemetryConsent;
class UpdateChecker;
struct StableRelease;
struct UpdateCheckResult;

// The telemetry error that the error diagnostics of a scene stand for, or none when no error has a code that maps to one.
// The most serious error wins.
std::optional<telemetry::Error> telemetryDiagnosticError(const std::vector<SceneDiagnostic>& diagnostics);

// The update checks and the self update, the consent and privacy dialogs, and the telemetry sender of the window. The
// dialogs open over the window.
class UpdateTelemetryController : public QObject {
	Q_OBJECT

public:
	// askToSaveScene asks about unsaved changes before an update starts and returns false when the user cancels.
	// beforeQuit runs when an installed update ends the application.
	UpdateTelemetryController(QWidget* window, std::function<bool()> askToSaveScene, std::function<void()> beforeQuit);
	~UpdateTelemetryController() override;

	// The window that the dialogs open over.
	QWidget* window() const { return m_window; }

	// Adds the update entries to the Help menu. Without a menu, or when the environment suppresses updates, there are none.
	void setupUpdateActions(QMenu* helpMenu);
	// Opens the privacy dialog. The first prompt opens only when the consent is still to be given.
	void showPrivacySettings(bool initialPrompt);
	// Makes the consent and the sender when this build can send telemetry. Does nothing when there is a sender already.
	void startTelemetryAfterInitialConsent();
	// Asks once whether to check for updates automatically, and checks when the answer or the setting is yes.
	void maybePromptForUpdateChecks();
	// A manual check always runs. An automatic one runs only when the setting is on and no check is going. Nothing runs
	// without update entries.
	void startUpdateCheck(bool manual);

	// The sender, or null while telemetry is off.
	telemetry::TelemetrySender* sender() const { return m_telemetrySender.get(); }
	// The observation of an operation that starts now.
	telemetry::OperationObservation captureOperation() const noexcept;
	// Stops the sender, if there is one.
	void stopTelemetry();

private:
	// The scripted runs in tests/e2e replace the telemetry.
	friend class MainWindowTestAccess;

	void handleUpdateCheckFinished(const UpdateCheckResult& result);
	void startSelfUpdate(const StableRelease& release);
	void handleSelfUpdateFinished(bool success, const QString& error);

	QWidget* const m_window;
	std::function<bool()> m_askToSaveScene;
	std::function<void()> m_beforeQuit;
	QAction* m_checkForUpdatesAction = nullptr;
	QAction* m_automaticUpdateChecksAction = nullptr;
	std::unique_ptr<QSettings> m_telemetrySettings;
	std::unique_ptr<TelemetryConsent> m_telemetryConsent;
	std::unique_ptr<telemetry::TelemetrySender> m_telemetrySender;
	UpdateChecker* m_updateChecker = nullptr;
	SelfUpdater* m_selfUpdater = nullptr;
	QProgressDialog* m_updateProgress = nullptr;
	bool m_manualUpdateCheck = false;
};

#endif // UPDATETELEMETRYCONTROLLER_H
