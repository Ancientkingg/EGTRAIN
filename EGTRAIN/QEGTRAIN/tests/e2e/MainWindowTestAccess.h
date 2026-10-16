#pragma once

#include "app/MainWindow.h"

// The friend of MainWindow for the scripted GUI runs. Each static member starts one scenario.
class MainWindowTestAccess {
public:
	static void runVisualPolish(MainWindow& w) { w.runVisualPolishE2E(); }
	static void runStationOverlay(MainWindow& w) { w.runStationOverlayE2E(); }
	static void runEditorSmoke(MainWindow& w) { w.runEditorSmokeE2E(); }
	static void runSceneRender(MainWindow& w) { w.runSceneRenderE2E(); }
	static void runTrackPreview(MainWindow& w) { w.runTrackPreviewE2E(); }
	static void runLegacyImport(MainWindow& w) { w.runLegacyImportE2E(); }
	static void runSceneDrop(MainWindow& w) { w.runSceneDropE2E(); }
};
