#ifndef PLAYBACKPROFILER_H
#define PLAYBACKPROFILER_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/// \file
/// The playback profiler and its scope macro.

/// Opt-in timing of GUI playback, described in docs/development/playback-profiling.md. It works only
/// when the environment variable QEGTRAIN_PLAYBACK_PROFILE is "1". It times named scopes during one
/// measurement window and writes the results to standard output as lines that start with
/// "QEGTRAIN_PLAYBACK_PROFILE " followed by a JSON record. Durations are steady_clock nanoseconds.
class PlaybackProfiler {
public:
	/// The settings of a run, written unchanged into the "run" record.
	struct RunConfig {
		int trial = 0;				   ///< Trial number of the harness (QEGTRAIN_PLAYBACK_PROFILE_TRIAL).
		std::string view;			   ///< Requested view, "fit" or "dense" (QEGTRAIN_PLAYBACK_PROFILE_VIEW).
		std::string platform;		   ///< Operating system: "macOS", "Windows" or "Linux".
		std::string architecture;	   ///< CPU architecture as Qt names it.
		std::string qtPlatform;		   ///< Name of the Qt platform plugin, such as "cocoa" or "offscreen".
		std::string buildType;		   ///< CMake build type of the application.
		std::string caseName;		   ///< Name of the scene.
		std::string requestedScenario; ///< Scenario ID asked for (QEGTRAIN_PLAYBACK_PROFILE_SCENARIO).
		std::string scenario;		   ///< ID of the scenario that the run applies.
		std::string defaultScenario;   ///< ID of the default scenario of the scene.
		double targetZoom = 1.0;	   ///< Zoom ratio that the view asks for: 3 for "dense", 1 otherwise.
		double actualZoom = 1.0;	   ///< Zoom ratio of the view when measuring began, set by beginAfterPaint().
		int durationMs = 0;			   ///< Length of the measurement window, in milliseconds.
		int delayMs = 0;			   ///< Delay between simulation steps, in milliseconds.
		bool passengerGui = false;	   ///< True when the window shows passengers.
		bool tsm = false;			   ///< True when the traffic-state channel (-TSM) is on.
		bool routeChoice = false;	   ///< True when the route-choice channel (-RC) is on.
		int horizon = 0;			   ///< Length of the run in steps of 1 s of simulation time.
		bool structural = false;	   ///< True for a structural test, whose records have mode "structural".
	};

	/// Times the rest of the enclosing block as one call of a path while the profiler is measuring.
	/// The scopes of one thread form a tree. Their times are added to the totals when the root scope
	/// ends, and are dropped when the window ends or starts again before that.
	class Scope {
	public:
		/// Starts the timer. \p path names the measured path, such as "worker/playback_step/compute",
		/// \p lane the label of the timed work ("worker", "gui" or "render"), and \p parent the path
		/// of the enclosing scope, or "" for a root scope. A child is timed only when \p parent is the
		/// innermost open scope of the thread. The texts are not copied and must stay valid until the
		/// root scope ends.
		Scope(const char* path, const char* lane, const char* parent);
		/// Stops the timer and records the time.
		~Scope();
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
	private:
		const char* m_path = nullptr;
		const char* m_lane = nullptr;
		const char* m_parent = nullptr;
		std::chrono::steady_clock::time_point m_started;
		std::uint64_t m_epoch = 0;
		std::uint64_t m_rootGeneration = 0;
		bool m_active = false;
		bool m_root = false;
	};

	/// The profiler of the process, created at the first call.
	static PlaybackProfiler& instance();
	/// True when QEGTRAIN_PLAYBACK_PROFILE was "1" when the profiler was created.
	static bool enabled();
	/// True when profiling is enabled and QEGTRAIN_STARTUP_TIMING or QEGTRAIN_STARTUP_NATIVE_DETAIL is
	/// "1". Then arm() and beginAfterPaint() do nothing, so no window is measured.
	static bool startupTimingConflict();

	/// Stores the settings of the run and its window length. Does nothing unless enabled().
	void configure(const RunConfig& config);
	/// Lets the next beginAfterPaint() start the window. \p timestep is the step of the run, in
	/// seconds from its start, written as start_timestep.
	void arm(int timestep);
	/// True between arm() and the beginAfterPaint() that starts the window.
	bool armed() const { return m_armed.load(std::memory_order_relaxed); }
	/// Starts the window after a completed paint of the view when the profiler is armed: clears the
	/// totals and counters and stores \p actualZoom, the zoom ratio of the view. Returns true when it
	/// started the window.
	bool beginAfterPaint(double actualZoom);
	/// Ends the window. \p timestep is the step of the run shown last, in seconds from its start, or
	/// -1 when none is shown. Returns false when no window was running.
	bool freeze(int timestep);
	/// True after freeze() has ended a window, until beginAfterPaint() starts another.
	bool frozen() const { return m_frozen.load(std::memory_order_relaxed); }
	/// True while the window runs.
	bool measuring() const { return m_measuring.load(std::memory_order_relaxed); }
	/// Length of the window from configure(), in milliseconds.
	int durationMs() const { return m_durationMs.load(std::memory_order_relaxed); }

	/// Counts one computed step while measuring and keeps \p timestep, in seconds from the start of
	/// the run, as the last step.
	void noteTimestep(int timestep);
	/// Counts one snapshot delivered to the window while measuring.
	void noteDelivery();
	/// Counts one delivered snapshot that the window renders while measuring.
	void noteRenderedUpdate();
	/// Counts one completed paint of the view while measuring.
	void notePaint();
	/// Writes the "run" record, one "aggregate" record per lane and path sorted by path, and the
	/// "completion" record, then flushes standard output. Only the first call of the process can
	/// write, and it writes nothing unless freeze() has ended a window. \p cleanStop is written as
	/// clean_stop.
	void emitRecords(bool cleanStop);

private:
	friend class Scope;
	friend class PlaybackProfilerTestAccess;
	struct Aggregate {
		std::string path;
		std::string lane;
		std::string parent;
		std::uint64_t calls = 0;
		std::uint64_t total = 0;
		std::uint64_t minimum = 0;
		std::uint64_t maximum = 0;
		std::uint64_t reservoirState = 0x9e3779b97f4a7c15ULL;
		std::vector<std::uint64_t> samples;

		void add(std::uint64_t nanoseconds);
	};

	struct PendingRecord {
		const char* path;
		const char* lane;
		const char* parent;
		std::uint64_t nanoseconds;
	};
	struct PendingTree {
		std::uint64_t epoch = 0;
		std::uint64_t rootGeneration = 0;
		bool active = false;
		std::vector<PendingRecord> records;
		std::vector<const char*> scopes;
	};

	PlaybackProfiler();
	static PendingTree& pendingTree();
	void commitTree(std::uint64_t epoch, const std::vector<PendingRecord>& records);

	bool m_enabled = false;
	bool m_conflict = false;
	std::atomic<bool> m_armed{false};
	std::atomic<bool> m_measuring{false};
	std::atomic<bool> m_frozen{false};
	std::atomic<bool> m_emitted{false};
	std::atomic<int> m_durationMs{0};
	std::atomic<std::uint64_t> m_epoch{0};
	mutable std::mutex m_mutex;
	RunConfig m_config;
	std::unordered_map<std::string, Aggregate> m_aggregates;
	std::chrono::steady_clock::time_point m_started;
	std::uint64_t m_observedNs = 0;
	int m_startTimestep = -1;
	int m_endTimestep = -1;
	std::uint64_t m_timesteps = 0;
	std::uint64_t m_deliveries = 0;
	std::uint64_t m_renderedUpdates = 0;
	std::uint64_t m_paints = 0;
};

/// Pastes the line number into the name of the variable of QEGTRAIN_PROFILE_SCOPE.
#define QEGTRAIN_PROFILE_SCOPE_NAME2(line) playbackProfilerScope##line
/// Name of the variable of QEGTRAIN_PROFILE_SCOPE, unique per line.
#define QEGTRAIN_PROFILE_SCOPE_NAME(line) QEGTRAIN_PROFILE_SCOPE_NAME2(line)
/// Declares a PlaybackProfiler::Scope that times the rest of the enclosing block.
#define QEGTRAIN_PROFILE_SCOPE(path, lane, parent) \
	PlaybackProfiler::Scope QEGTRAIN_PROFILE_SCOPE_NAME(__LINE__)(path, lane, parent)

#endif
