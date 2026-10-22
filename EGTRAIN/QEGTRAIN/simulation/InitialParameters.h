#ifndef INITIALPARAMETERS_H
#define INITIALPARAMETERS_H

#include <string>

/// The default seed of the run generator.
constexpr unsigned long kDefaultRandomSeed = 789350715;

/// The options of the process: the command line, the values of the scene that was built last and the live stream.
///
/// The process holds one object, the global initial_variables (declared in RollingStock.h). main and the window write it,
/// and buildOperationsFromScene writes the values of the scene into it when a build succeeds. DispatchController::prepareScene
/// copies it into SimulationRun::settings, and the run reads only that copy.
class InitialParameters {
public:
	/// Name of the scene that was built last, or of the case study that set_case chose before that. Empty when no case is chosen.
	std::string name;
	/// Base time of the scene in seconds since midnight. Step t of a run is at startingSimulationTime + t seconds since midnight.
	int startingSimulationTime;
	/// True when the process shows the window (-g 1), false for a run without a window (-g 0).
	bool GUI;
	/// Shows the passengers in the window (-pax, or the passenger view of the window). With it on, every step refreshes the waiting list of every platform.
	bool PAX_GUI = false;
	/// Length of the run in seconds. Steps t run from 0 while t < times, so with the step of 1 s it is also the number of steps.
	/// It is the value of -h when durationOverride is set, and otherwise the duration of the scene once it is built.
	double times;
	/// Number of tracks of the scene that was built last. The window shows it in its ready message.
	int numTrackLines;
	/// Number of routes of the scene that was built last. The window shows it in its ready message.
	int N_Routes;
	/// Set to 0 by the operations builder. Nothing else reads it.
	int num_OrderLists;
	/// The buffer time of the blocking times in seconds: the value of -b, or else the buffer time of the scene rounded to whole
	/// seconds, or 0.
	double bufferTime;
	/// The running time recovery in percent: the value of -c, or else the recovery time of the scene rounded to a whole number, or 0.
	double recoveryTimePercentage;
	/// A folder of legacy input files. main clears it before a run, and nothing in the program reads it.
	std::string InputMainFolder;
	/// Folder that a run writes its output files to. prepareScene sets it to the folder chosen in the window, if any, and to
	/// "Output" when it is empty.
	std::string OutputMainFolder;
	/// True when the case was named on the command line (-n, --scene or a file to open), or later in the window. The window then
	/// does not open the case chooser at startup.
	bool nArgProvided = false;
	/// True when -h gave times. The operations builder then keeps times instead of the duration of the scene.
	bool durationOverride = false;
	/// True when -b gave bufferTime. The operations builder then keeps it.
	bool bufferTimeOverride = false;
	/// True when -c gave recoveryTimePercentage. The operations builder then keeps it.
	bool recoveryTimeOverride = false;
	/// Nothing reads this flag.
	bool enabled_log = true;
	/// Nothing reads this flag.
	bool log_to_file = true;
	/// Writes the per-train files in TEMP and TrainTrajectories/TrainPathDiagram.txt (--detailed-trajectories).
	bool exportDetailedTrajectories = false;
	/// Seeds the passenger sampling of a run (--seed, 1 to kMaxRandomSeed). Equal seeds give equal runs.
	unsigned long randomSeed = kDefaultRandomSeed;
	/// The live stream (--live): the port of its state socket, 0 for a free port, or -1 when the stream is off.
	int livePort = -1;
	/// A state frame for every liveEvery-th step (--live-every).
	int liveEvery = 1;
	/// The stream also takes requests (--live-commands).
	bool liveCommands = false;

	/// Sets the numbers to 0 and GUI to true. A caseStudy above 0 is chosen with set_case.
	explicit InitialParameters(int caseStudy);
	/// Sets name to that of case study caseStudy: 1 Netherlands, 2 Paimpol, 3 Copenhagen, 4 Milano_Brescia,
	/// 5 Assignment_Gvc_Gdg_Ut. Another number clears name and logs a warning.
	void set_case(int caseStudy);
};

#endif
