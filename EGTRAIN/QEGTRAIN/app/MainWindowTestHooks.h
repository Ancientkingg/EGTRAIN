#pragma once

#include <QString>
#include <QStringList>
#include <functional>
#include <string>

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
	// Called once the window of a route diagram is built, with the function that gives its CSV for the trains shown.
	void (*routeDiagramBuilt)(const std::function<std::string(const QStringList&)>& csv, bool timetable) = nullptr;
};

// The one set of hooks of the process: free functions read it, and main builds one MainWindow.
inline MainWindowTestHooks& mainWindowTestHooks() {
	static MainWindowTestHooks hooks;
	return hooks;
}
