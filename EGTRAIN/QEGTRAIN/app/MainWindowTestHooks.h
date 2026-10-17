#pragma once

#include <QString>
#include <QStringList>
#include <functional>
#include <string>

class MainWindow;
class QDialog;
class UpdateTelemetryController;
struct TelemetryContext;

// The entry points that the scripted runs in tests/e2e use. They are empty in a normal run.
struct MainWindowTestHooks {
	// A scripted run has no user to answer a dialog.
	bool dialogsSuppressed = false;
	// A scripted run that starts without a scene: the first show asks no startup question and starts nothing but afterFirstShow.
	bool suppressStartupPrompts = false;
	// Capacity analysis asks for its scope although dialogs are suppressed, for a scripted run that answers the dialog.
	bool askCapacityScope = false;
	// The run review opens although dialogs are suppressed, for a scripted run that answers the dialog.
	bool askRunReview = false;
	// Called once, at the end of the first showEvent.
	std::function<void(MainWindow&)> afterFirstShow;
	// Called at the end of every frame that renderSnapshot draws, with whether the frame comes from a replay.
	// A plain pointer, so that a normal run pays one test per frame.
	void (*frameRendered)(MainWindow& window, bool historical) = nullptr;
	// The folder that Import Passengers reads in place of asking for one. An empty path asks as usual.
	std::function<QString()> passengerSource;
	// Called with a dialog just before it opens. kind names the dialog: startupChooser, runReview, privacySettings or recoveryPrompt.
	std::function<void(QDialog& dialog, const char* kind)> dialogShown;
	// Called with a startupChooser or runReview dialog once it has closed and the window has read its answer. The chooser
	// has by then opened the case that it chose. The dialog is still alive.
	std::function<void(QDialog& dialog, const char* kind)> dialogClosed;
	// Called with each live snapshot once the window holds it and before it is drawn.
	void (*snapshotDelivered)(MainWindow& window) = nullptr;
	// Called after each live snapshot is drawn and kept as the last one drawn.
	void (*snapshotDrawn)(MainWindow& window) = nullptr;
	// Called when the Run Results table of a completed run has been written.
	void (*runResultsRefreshed)(MainWindow& window) = nullptr;
	// Called when a run is over and its results are kept and shown, before the window does the rest for the run.
	// It returns true when it has ended the run for the window, which then does nothing more.
	bool (*runFinished)(MainWindow& window) = nullptr;
	// Called at the end of that work, when the window has done everything for the run.
	void (*runSettled)(MainWindow& window) = nullptr;
	// Called with the context that the privacy dialog is built for and whether it is the first prompt, which it may change.
	// It returns true when the dialog is only a preview, which has no sender and sends nothing.
	std::function<bool(TelemetryContext& context, bool& initialPrompt)> privacyPreview;
	// Called in place of the telemetry start of the window. It returns true when it has set the settings, the consent and
	// the sender of the controller, and false when telemetry does not start.
	bool (*telemetryStart)(UpdateTelemetryController& controller) = nullptr;
	// Called before each of the four steps that install the telemetry observation of a run, with the number of the step.
	// A hook that throws makes the step fail.
	void (*observationStep)(int step) = nullptr;
	// Called when the worker of a run is set up and before its thread starts.
	void (*runStarting)(MainWindow& window) = nullptr;
	// Called at the end of the destructor of the window.
	void (*windowDestroyed)() = nullptr;
	// Called once the window of a route diagram is built, with the function that gives its CSV for the trains shown and
	// the ids of the trains of the run.
	void (*routeDiagramBuilt)(const std::function<std::string(const QStringList&)>& csv, const QStringList& trainIds,
		bool timetable) = nullptr;
};

// The one set of hooks of the process: free functions read it, and main builds one MainWindow.
inline MainWindowTestHooks& mainWindowTestHooks() {
	static MainWindowTestHooks hooks;
	return hooks;
}
