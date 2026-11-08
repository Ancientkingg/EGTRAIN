#ifndef GUIREPLAYHISTORY_H
#define GUIREPLAYHISTORY_H

#include "simulation/GuiSimulationSnapshot.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct SimulationRun;

/// The samples of one train that the replay shares with the completed run, for makeGuiReplayTrains().
struct GuiReplayTrajectory {
	int firstSample = 0;								 ///< Step of the first element of both vectors.
	std::shared_ptr<const std::vector<double>> position; ///< Head position along the route, in m. May be null.
	std::shared_ptr<const std::vector<double>> speed;	 ///< Speed, in m/s. May be null.
};

/// One per-step series of a train, the head position or the speed, for every step that the vector of the train had.
/// Five regions follow each other: a value that repeats at the start, the samples that are kept here, the samples shared
/// with the completed run, the samples kept here after those, and a value that repeats to the end. Every sample is
/// reproduced bit for bit.
struct GuiReplaySeries {
	int size = 0;									   ///< Number of samples of the vector of the train.
	double first = 0.0;								   ///< Value of the samples before headStart.
	int headStart = 0;								   ///< First sample that is in head.
	std::vector<double> head;						   ///< The samples from headStart up to sharedStart.
	int sharedStart = 0;							   ///< First sample that is in shared.
	std::shared_ptr<const std::vector<double>> shared; ///< The samples from sharedStart on. May be null, which is empty.
	std::vector<double> tail;						   ///< The samples right after the shared ones.
	double last = 0.0;								   ///< Value of the samples after the tail, up to size.
	/// The sample at index `sample`, which is from 0 up to but not including size.
	double at(int sample) const;
};

/// What the replay needs of the trains of a finished run. Built once by makeGuiReplayTrains(), then only read.
struct GuiReplayTrains {
	/// The part of a route that the position of a vehicle reads, as the first sections of the route hold it.
	struct Route {
		bool reversed = false;		 ///< The route is driven against its definition.
		std::vector<double> startX;	 ///< X of the start node of each block section, in km.
		std::vector<double> endX;	 ///< X of the end node of each block section, in km.
		std::vector<double> geoXBeg; ///< Scene X of the start node of each block section, in m.
	};
	/// One train of the run.
	struct Train {
		bool validRoute = false;	 ///< The route index is a route of the run.
		int routeIndex = -1;		 ///< Index into routes.
		double numberOfWagons = 0.0; ///< Wagons without the traction unit, as the run holds it.
		double trainLength = 0.0;	 ///< Length of the train in m.
		GuiReplaySeries position;	 ///< Head position along the route, in m.
		GuiReplaySeries speed;		 ///< Speed in m/s.
	};
	std::vector<Route> routes;				///< By route index of the run.
	std::vector<Train> trains;				///< In the order of the run.
	std::vector<int> messageSteps;			///< The step of each virtual coupling message of the run.
	std::vector<std::string> messageTrains; ///< The train of each message.
	std::vector<std::string> messageTexts;	///< The text of each message.
	/// Accounted bytes of everything except the vectors shared with the completed run.
	std::size_t ownedBytes() const;
	/// Capacity bytes of the vectors shared with the completed run.
	std::size_t sharedBytes() const;
};

/// Builds the train source from the trains of `run`, which must still hold their samples, and the slices that the
/// completed run holds (one per train in run order; a missing or mismatching slice is copied instead). The result does
/// not refer to `run`.
std::shared_ptr<const GuiReplayTrains> makeGuiReplayTrains(const SimulationRun& run, const std::vector<GuiReplayTrajectory>& shared);

/// The changes of a completed run, kept for replay.
///
/// What is the same for the whole run (identity, route and direction of each train, capacities, signal, section and
/// platform ids, passenger ids) is stored once in a layout. What changes is stored as the difference to the second
/// before, one record of 8 bytes for each value that changed, with a full copy of the values (a keyframe) at the start of
/// every interval of keyframeSeconds. at() starts from the keyframe of the interval and applies the changes up to the
/// second asked for. Lists and strings that change are stored once in the table of their interval or in the string table.
///
/// The positions, speeds and wagon positions of the trains are not recorded. setTrains() hands the history the samples
/// that the completed run holds, shared, and at() derives these fields from them with the arithmetic of
/// buildGuiSimulationSnapshot. Without a train source those fields keep their defaults and the trains are not drawn.
///
/// Accounted payload (payloadBytes) counts the capacities of the layout, the string table, the keyframes, changes and
/// lists of every interval, the state of the last second and the part of the train source that the history owns. It does
/// not count allocator bookkeeping, shared_ptr control blocks, container nodes or the snapshots that callers hold. This is
/// not RSS. sharedBytes() reports the samples shared with the completed run apart: the completed run holds them whether
/// or not there is a replay, so they are not in the budget. The budget bounds what record() keeps: when it is reached,
/// the oldest interval is dropped as a whole, and firstTime() and lastTime() tell what is kept.
///
/// Threads: record() is called only on the thread that steps the run, while the worker runs, and nothing else touches the
/// history then. The history is moved out of the producer after the worker has joined. setTrains(), at() and every
/// getter are used on the GUI thread only. The train source is immutable, so histories may share it.
class GuiReplayHistory {
public:
	/// Length of an interval in seconds of simulated time: the distance between two keyframes.
	static constexpr int keyframeSeconds = 300;
	/// Default budget of accounted payload, in bytes (128 MiB).
	static constexpr std::size_t payloadLimit = 128u * 1024u * 1024u;

	/// An empty history that keeps at most `budgetBytes` bytes of accounted payload and starts an interval every
	/// `keyframeInterval` seconds.
	explicit GuiReplayHistory(std::size_t budgetBytes = payloadLimit, int keyframeInterval = keyframeSeconds)
		: budget_(budgetBytes), keyframeInterval_(keyframeInterval > 0 ? keyframeInterval : 1) {}
	/// The string table keys point into the table of the same object, so a copy would dangle.
	GuiReplayHistory(const GuiReplayHistory&) = delete;
	/// Deleted, as the copy constructor.
	GuiReplayHistory& operator=(const GuiReplayHistory&) = delete;
	/// Not noexcept: the move of std::unordered_map may allocate in some standard libraries.
	GuiReplayHistory(GuiReplayHistory&&) = default; // NOLINT(performance-noexcept-move-constructor)
	/// Not noexcept, as the move constructor.
	GuiReplayHistory& operator=(GuiReplayHistory&&) = default; // NOLINT(performance-noexcept-move-constructor)

	/// Drops everything, including the train source, and resets the truncated and oversize flags. The budget and the
	/// interval stay.
	void clear();
	/// Records the state of the second `snapshot->timestep`. Ignores null, a second that is not later than lastTime() and
	/// everything after the history became oversize. A skipped second shows the state of the second before it. Drops the
	/// oldest interval while the budget is exceeded. When a count or a static field of the run changes, which no run does,
	/// the history starts again at that second and truncated() is true.
	void record(const std::shared_ptr<const GuiSimulationSnapshot>& snapshot);
	/// Sets the source of the train positions and speeds and drops the snapshots handed out. May be null.
	void setTrains(std::shared_ptr<const GuiReplayTrains> trains);
	/// The source of the train fields, or null.
	const std::shared_ptr<const GuiReplayTrains>& trains() const { return trains_; }

	/// No second is kept.
	bool empty() const { return intervals_.empty(); }
	/// One interval did not fit in the budget on its own. The history was cleared and records nothing more until clear().
	bool oversize() const { return oversize_; }
	/// True when the start of the run was dropped to stay within the budget.
	bool truncated() const { return truncated_; }
	/// The number of intervals kept.
	std::size_t intervalCount() const { return intervals_.size(); }
	/// Accounted payload in bytes: the recorded part and the part of the train source that the history owns.
	std::size_t payloadBytes() const { return layoutBytes_ + stringBytes_ + intervalBytes_ + stateBytes_ + trainsBytes_; }
	/// Bytes of the samples shared with the completed run, which are not in payloadBytes().
	std::size_t sharedBytes() const { return sharedBytes_; }
	/// The budget of accounted payload, in bytes.
	std::size_t budgetBytes() const { return budget_; }
	/// The oldest second kept, from the start of the run. 0 when the history is empty.
	int firstTime() const { return intervals_.empty() ? 0 : intervals_.front().startTime; }
	/// The newest second kept, from the start of the run. 0 when the history is empty.
	int lastTime() const;
	/// Seconds before this one were dropped, or 0 when none were.
	int evictedBeforeTime() const { return truncated_ ? firstTime() : 0; }
	/// The state of the second `time`, or of the nearest second kept when it is outside the interval kept. Null when the
	/// history is empty. Asking again for the same second returns the same object while the caller or the history holds it.
	std::shared_ptr<const GuiSimulationSnapshot> at(int time) const;

private:
	// The data of a run that does not change. The fields of a train, signal, section, platform and passenger that
	// change hold their default values here.
	struct Layout {
		std::vector<GuiTrainState> trains;
		std::vector<GuiSignalState> signalStates;
		std::vector<GuiSectionState> sectionStates;
		std::vector<GuiPlatformState> platforms;
		std::vector<GuiPassengerState> passengers;
		int totalTimesteps = 0;
	};

	// One recorded value that differs from the second before.
	struct Change {
		std::uint32_t slot;
		std::int32_t value;
	};

	// A keyframe, the changes of the seconds after it, and the lists that its values refer to.
	struct Interval {
		int startTime = 0;
		std::vector<std::int32_t> keyframe;
		std::vector<Change> changes;
		// End in changes of second startTime + 1 + i.
		std::vector<std::uint32_t> secondEnds;
		// List number k holds [ends[k - 1], ends[k]) of the values, from 0 for k = 0.
		std::vector<GuiOccupiedArc> arcs;
		std::vector<std::uint32_t> arcEnds;
		std::vector<std::uint32_t> platformIds;
		std::vector<std::uint32_t> platformEnds;
		std::size_t bytes = 0;
	};

	// First slot of each group of values, in the order of the layout.
	struct Slots {
		std::size_t signalCode = 0, signalLevel = 0, signalFailed = 0, sectionFlags = 0, trainOut = 0, trainOnboard = 0, trainArcs = 0,
					platformList = 0, passenger = 0, total = 0;
	};

	static bool sameCounts(const Layout& layout, const GuiSimulationSnapshot& snapshot);
	static bool sameStatic(const Layout& layout, const GuiSimulationSnapshot& snapshot);
	static std::size_t layoutPayload(const Layout& layout);
	static std::size_t intervalPayload(const Interval& interval);
	void makeLayout(const GuiSimulationSnapshot& snapshot);
	void startInterval(const GuiSimulationSnapshot& snapshot);
	void addChanges(const GuiSimulationSnapshot& snapshot);
	void putValue(std::size_t slot, std::int32_t value);
	std::int32_t addArcList(Interval& interval, const std::vector<GuiOccupiedArc>& arcs);
	std::int32_t addPlatformList(Interval& interval, const std::vector<std::uint32_t>& ids);
	std::size_t statePayload() const;
	void refreshBytes();
	void enforceBudget();
	void evictOldest();
	void resetRecording();
	std::uint32_t intern(const std::string& text);
	std::shared_ptr<const GuiSimulationSnapshot> expand(const Interval& interval, const std::vector<std::int32_t>& values, int time) const;
	void deriveTrains(GuiSimulationSnapshot& snapshot) const;

	std::size_t budget_;
	int keyframeInterval_;
	Layout layout_;
	Slots slots_;
	std::deque<Interval> intervals_;
	// The values of the last recorded second, and the lists they stand for, which the next second is compared with.
	std::vector<std::int32_t> current_;
	std::vector<std::vector<GuiOccupiedArc>> currentArcs_;
	std::vector<std::vector<std::uint32_t>> currentPlatforms_;
	std::deque<std::string> strings_;
	std::unordered_map<std::string_view, std::uint32_t> stringNumbers_;
	std::size_t layoutBytes_ = 0;
	std::size_t stringBytes_ = 0;
	std::size_t intervalBytes_ = 0;
	std::size_t stateBytes_ = 0;
	std::size_t trainsBytes_ = 0;
	std::size_t sharedBytes_ = 0;
	std::shared_ptr<const GuiReplayTrains> trains_;
	bool truncated_ = false;
	bool oversize_ = false;
	// The snapshot handed out last, and the ones handed out since that are still in use.
	mutable std::shared_ptr<const GuiSimulationSnapshot> cached_;
	mutable int cachedTime_ = 0;
	mutable std::unordered_map<int, std::weak_ptr<const GuiSimulationSnapshot>> handedOut_;
};

#endif
