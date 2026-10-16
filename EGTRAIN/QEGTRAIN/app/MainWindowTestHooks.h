#pragma once

#include <functional>

class MainWindow;

// The entry points that the scripted runs in tests/e2e use. They are empty in a normal run.
struct MainWindowTestHooks {
	// A scripted run has no user to answer a dialog.
	bool dialogsSuppressed = false;
	// Called once, at the end of the first showEvent.
	std::function<void(MainWindow&)> afterFirstShow;
};

// The one set of hooks of the process: free functions read it, and main builds one MainWindow.
inline MainWindowTestHooks& mainWindowTestHooks() {
	static MainWindowTestHooks hooks;
	return hooks;
}
