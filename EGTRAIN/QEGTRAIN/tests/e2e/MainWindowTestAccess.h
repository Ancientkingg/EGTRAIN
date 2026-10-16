#pragma once

#include "app/MainWindow.h"
#include <QString>
#include <QStringList>
#include <functional>
#include <string>

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
	static void routeDiagramBuilt(const std::function<std::string(const QStringList&)>& csv, bool timetable);

private:
	static void runReplaySignals(MainWindow& w);
	static void checkOperationalCompletion(MainWindow& w);
	static bool checkReplayWholeRun(MainWindow& w, int middle, QString& failure);
	static bool checkReplayRow(MainWindow& w, int middle, QString& failure);
	static bool replaySliderCovers(const MainWindow& w, int from, int to);
	static QSize replayRowSizeAt(MainWindow& w, int time);
};
