#ifndef STARTUPTIMING_H
#define STARTUPTIMING_H

// SceneModel has a member named signals, which the Qt keyword hides. Include the scene model before any Qt header,
// or through app/MainWindow.h.
#include "scene/SceneModel.h"

#include <QString>
#include <QtGlobal>
#include <array>
#include <cstddef>
#include <string>

class RunSelection;

/// \file
/// Startup timing, which writes how long the window takes to open and first paint a scene, and the part of the
/// playback profiler that belongs to a run. Both work only when their environment variable switches them on.

/// What startup timing compares to tell that a later step still works on the scene that it expects.
struct StartupIdentity {
	QString path;								   ///< Absolute path of the scene.
	int schemaVersion = 0;						   ///< Schema version of the scene.
	std::string name;							   ///< Name of the scene.
	std::string baseTime;						   ///< Base time of the scene.
	std::string defaultScenarioId;				   ///< ID of the default scenario of the scene.
	SceneSimulationSettings settings;			   ///< Simulation settings of the scene.
	std::array<std::size_t, 21> collectionSizes{}; ///< Sizes of the collections of the scene.
	std::string inputSnapshot;					   ///< Canonical input snapshot, empty when the caller has none.
	bool set = false;							   ///< False for an identity that names no scene.
};

/// Startup timing is on: the environment variable QEGTRAIN_STARTUP_TIMING is "1".
bool startupTimingEnabled();
/// Starts the clock of startup timing and forgets the expected scene. Does nothing when startup timing is off.
void beginStartupTiming();
/// Nanoseconds since beginStartupTiming() started the clock.
qint64 startupTimingNowNanoseconds();
/// Writes one QEGTRAIN_TIMING line of JSON to standard output when startup timing is on. `elapsedNanoseconds` is
/// written in milliseconds. `iteration` 0 is the fresh process and higher ones are warm reloads; a negative
/// `generation` and empty `invocation` and `source` are left out.
void recordStartupTiming(const QString& phase, int iteration, int generation,
	qint64 elapsedNanoseconds, const QString& invocation = {}, const QString& source = {},
	bool identityOk = true, bool canonicalPreloadNested = false);
/// Writes why startup timing failed as a QEGTRAIN_TIMING_ERROR line to standard error and ends the application with
/// exit code 2.
void failStartupTiming(const QString& message);
/// Records `identity` as the scene that startup timing expects.
void setStartupTimingPreloadIdentity(const StartupIdentity& identity);
/// True when `current` is the scene that startup timing expects. The input snapshot counts only with
/// `compareInputSnapshot`, because the open document has none.
bool matchesExpectedStartupIdentity(const StartupIdentity& current, bool compareInputSnapshot = false);

/// The trials of startup timing in one window: iteration 0 is the fresh process and each higher iteration a warm reload
/// of the scene that the window opened first.
class StartupTimingTrials {
public:
	/// Reads the number of warm reloads from QEGTRAIN_STARTUP_WARM_TRIALS when startup timing is on. A value that is not
	/// a number, or is negative, means none.
	StartupTimingTrials();
	/// The current iteration.
	int iteration() const { return m_iteration; }
	/// The scene that the warm reloads open; empty until rememberScene() is called.
	const QString& scenePath() const { return m_scenePath; }
	/// Keeps `path` as the scene of the warm reloads, unless a scene is kept already.
	void rememberScene(const QString& path);
	/// Moves to the next warm reload. Returns false, and stays, when the last trial is done.
	bool nextIteration();

private:
	int m_iteration = 0;
	int m_warmTrials = 0;
	QString m_scenePath;
};

/// Selects in `selection` the scenario that QEGTRAIN_PLAYBACK_PROFILE_SCENARIO names in `model`, when the playback
/// profiler is on. A scene without it gets the error record, ends the application with exit code 2 and returns false.
bool selectPlaybackProfileScenario(const SceneModel& model, RunSelection& selection);
/// Hands the playback profiler the settings of the run, when it is on. `scenario` is the ID that the run applies,
/// `passengerGui` tells whether the window shows passengers and `horizon` is the length of the run in steps.
void configurePlaybackProfile(const SceneModel& model, const std::string& scenario, bool passengerGui, int horizon);

#endif // STARTUPTIMING_H
