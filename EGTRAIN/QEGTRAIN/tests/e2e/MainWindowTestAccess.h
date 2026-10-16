#pragma once

#include "app/MainWindow.h"

// The friend of MainWindow for the scripted GUI runs. Each static member starts one scenario and is defined in
// the source of that scenario in this folder.
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
};
