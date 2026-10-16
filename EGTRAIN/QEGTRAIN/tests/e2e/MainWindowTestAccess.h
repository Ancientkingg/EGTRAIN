#pragma once

#include "app/MainWindow.h"
#include <QString>
#include <QStringList>
#include <functional>
#include <string>

class CaseChooserDialog;
class QDialog;

// The friend of MainWindow for the scripted GUI runs. Each scenario member starts one scenario and is defined in
// the source of that scenario in this folder. The hook members are what E2ERunner.cpp installs in MainWindowTestHooks.
class MainWindowTestAccess {
public:
	static void runVisualPolish(MainWindow& w);
	static void visualPolishFrame(MainWindow& w, bool historical);
	static void runStationOverlay(MainWindow& w);
	static void runEditorSmoke(MainWindow& w);
	static void runSceneRender(MainWindow& w);
	static void runTrackPreview(MainWindow& w);
	static void runLegacyImport(MainWindow& w);
	static void runSceneDrop(MainWindow& w);
	static void runOutputFolder(MainWindow& w);
	static void runCreatorAcceptance(MainWindow& w);
	// The checks of the preview that the creator acceptance scenario makes, defined with it.
	static bool checkPreviewInfrastructure(MainWindow& w, QString& failure);
	static bool checkPreviewRuntimeParity(MainWindow& w, QString& failure, bool measuredPresentation = false);

	// The autostart and the checks of a run and of what follows it, defined in PostRun.cpp.
	static void autostart(MainWindow& w);
	static void snapshotDelivered(MainWindow& w);
	static void checkSignalHeads(MainWindow& w);
	static void runResultsRefreshed(MainWindow& w);
	static bool runFinished(MainWindow& w);
	static void runSettled(MainWindow& w);
	static void routeDiagramBuilt(const std::function<std::string(const QStringList&)>& csv, const QStringList& trainIds,
		bool timetable);

	// The scripts for the dialogs that the window opens, defined in Dialogs.cpp. Each acts on the dialogs for which the
	// variables of its scenario are set.
	static void dialogShown(MainWindow& w, QDialog& dialog, const char* kind);
	static void dialogClosed(MainWindow& w, QDialog& dialog, const char* kind);

#ifdef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
	// The telemetry smoke, defined in TelemetrySmoke.cpp: the isolated start of telemetry, the script that follows it, and the
	// checks that a run and the end of the window report.
	static bool startTelemetrySmoke(MainWindow& w);
	static void telemetryObservationStep(int step);
	static void telemetryRunStarting(MainWindow& w);
	static void telemetryWindowDestroyed();
#endif

	// Whether a scenario has begun its checks, which it does once, and how often it has waited for the first train to be
	// drawn. A run starts one scenario.
	static inline bool scenarioStarted = false;
	static inline int scenarioAttempts = 0;
	// Whether the editor smoke has begun its checks.
	static inline bool editorSmokeStarted = false;

private:
	static void chooserShown(MainWindow& w, CaseChooserDialog& dialog);
	static void chooserClosed(MainWindow& w, CaseChooserDialog& dialog);
	static void reviewShown(MainWindow& w, QDialog& review);
	static void reviewClosed(MainWindow& w, QDialog& review);
	static void runReplaySignals(MainWindow& w);
	static void checkOperationalCompletion(MainWindow& w);
	static bool checkReplayWholeRun(MainWindow& w, int middle, QString& failure);
	static bool checkReplayRow(MainWindow& w, int middle, QString& failure);
	static bool replaySliderCovers(const MainWindow& w, int from, int to);
	static QSize replayRowSizeAt(MainWindow& w, int time);
#ifdef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
	static void runTelemetrySmoke(MainWindow& w, int stage);
#endif
};
