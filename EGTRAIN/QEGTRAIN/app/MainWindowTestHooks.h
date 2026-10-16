#pragma once

#include <QString>
#include <functional>

class MainWindow;

// The entry points that the scripted runs in tests/e2e use. They are empty in a normal run.
struct MainWindowTestHooks {
	// A scripted run has no user to answer a dialog.
	bool dialogsSuppressed = false;
	// A scripted run that starts without a scene: the first show asks no startup question and starts nothing but afterFirstShow.
	bool suppressStartupPrompts = false;
	// Capacity analysis asks for its scope although dialogs are suppressed, for a scripted run that answers the dialog.
	bool askCapacityScope = false;
	// Called once, at the end of the first showEvent.
	std::function<void(MainWindow&)> afterFirstShow;
	// Called at the end of every frame that renderSnapshot draws, with whether the frame comes from a replay.
	// A plain pointer, so that a normal run pays one test per frame.
	void (*frameRendered)(MainWindow& window, bool historical) = nullptr;
	// The folder that Import Passengers reads in place of asking for one. An empty path asks as usual.
	std::function<QString()> passengerSource;
};

// The one set of hooks of the process: free functions read it, and main builds one MainWindow.
inline MainWindowTestHooks& mainWindowTestHooks() {
	static MainWindowTestHooks hooks;
	return hooks;
}
