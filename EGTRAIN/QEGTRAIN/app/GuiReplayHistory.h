#ifndef GUIREPLAYHISTORY_H
#define GUIREPLAYHISTORY_H

#include "app/GuiSimulationSnapshot.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The frames of a completed run, kept for replay.
//
// What is the same for the whole run (identity and static data of trains, signal and section
// ids, platforms) is stored once in a layout. A frame stores only flat arrays of the values that
// change. Strings that vary (passengers, virtual coupling messages) are stored once in a string
// table and referred to by number. atOrBefore() rebuilds an ordinary snapshot from the compact
// form. A frame that is still in use, and the last one handed out, is not rebuilt.
//
// Accounted payload bounds include vector and string capacities, but not allocator
// bookkeeping, shared_ptr control blocks or container-node overhead. This is not RSS. When the
// budget is reached the oldest frames are dropped; firstTime() and lastTime() tell what is kept.
//
// record() is called on the simulation thread. The history is read on the GUI thread after it
// has been moved out of the producer, so no member is shared between threads.
class GuiReplayHistory {
public:
	static constexpr int cadenceSeconds = 5;
	// Default budget of accounted payload bytes.
	static constexpr std::size_t payloadLimit = 128u * 1024u * 1024u;

	explicit GuiReplayHistory(std::size_t budgetBytes = payloadLimit) : budget_(budgetBytes) {}
	// The string table keys point into the table of the same object, so a copy would dangle.
	GuiReplayHistory(const GuiReplayHistory&) = delete;
	GuiReplayHistory& operator=(const GuiReplayHistory&) = delete;
	GuiReplayHistory(GuiReplayHistory&&) = default;
	GuiReplayHistory& operator=(GuiReplayHistory&&) = default;

	void clear();
	void record(std::shared_ptr<const GuiSimulationSnapshot> frame);

	bool empty() const { return frames_.empty(); }
	bool oversize() const { return oversize_; }
	// True when frames were dropped to stay within the budget.
	bool truncated() const { return truncated_; }
	std::size_t size() const { return frames_.size(); }
	// Frames, layouts and string table.
	std::size_t payloadBytes() const { return bytes_ + layoutBytes_ + stringBytes_; }
	std::size_t budgetBytes() const { return budget_; }
	// Number of layouts in use. It stays 1 while the static data of a run does not change.
	std::size_t layoutCount() const { return layouts_.size(); }
	int firstTime() const { return frames_.empty() ? 0 : frames_.front().timestep; }
	int lastTime() const { return frames_.empty() ? 0 : frames_.back().timestep; }
	// Frames before this time were dropped, or 0 when none were.
	int evictedBeforeTime() const { return truncated_ ? firstTime() : 0; }
	// The last frame at or before the time, or the first frame when the time is earlier. Asking
	// again for the same frame returns the same object while the caller or the history holds it.
	std::shared_ptr<const GuiSimulationSnapshot> atOrBefore(int time) const;

private:
	// The data of a run that does not change from frame to frame. The fields of a train, signal,
	// section and platform that change per frame hold their default values here.
	struct Layout {
		std::vector<GuiTrainState> trains;
		std::vector<GuiSignalState> signalStates;
		std::vector<GuiSectionState> sectionStates;
		std::vector<GuiPlatformState> platforms;
		std::size_t bytes = 0;
	};

	// The values that change per frame, as flat arrays in the order of the layout.
	struct Frame {
		int timestep = 0;
		int totalTimesteps = 0;
		std::shared_ptr<const Layout> layout;
		// Per train: route index, onboard passengers, counts of head positions, tail positions and
		// occupied arcs, flags.
		std::vector<std::int32_t> trainInts;
		// Per train: route axis position, speed.
		std::vector<double> trainDoubles;
		// Head positions, then tail positions, of every train in turn.
		std::vector<double> wagonPositions;
		std::vector<GuiOccupiedArc> occupiedArcs;
		// Per signal state.
		std::vector<std::int32_t> signalCodes;
		std::vector<std::int32_t> signalLevels;
		std::vector<std::uint8_t> signalFailed;
		// Per section state: bit 0 prepared, bit 1 blocked.
		std::vector<std::uint8_t> sectionFlags;
		// Per platform: number of passenger ids; the ids (string numbers) of all platforms in turn.
		std::vector<std::uint32_t> platformPassengerCounts;
		std::vector<std::uint32_t> platformPassengerIds;
		// Per passenger: string numbers of id, status, waiting platform, next train, next destination.
		std::vector<std::uint32_t> passengerStrings;
		// Per virtual coupling message: its time, and the string numbers of train and text.
		std::vector<std::int32_t> messageTimes;
		std::vector<std::uint32_t> messageStrings;
		std::size_t bytes = 0;
	};

	static bool matches(const Layout& layout, const GuiSimulationSnapshot& snapshot);
	static std::shared_ptr<const Layout> makeLayout(const GuiSimulationSnapshot& snapshot);
	static std::size_t layoutPayload(const Layout& layout);
	static std::size_t framePayload(const Frame& frame);
	Frame compact(const GuiSimulationSnapshot& snapshot, std::shared_ptr<const Layout> layout);
	std::shared_ptr<const GuiSimulationSnapshot> expand(const Frame& frame) const;
	std::uint32_t intern(const std::string& text);
	void evictOldest();
	void releaseUnusedLayouts();

	std::size_t budget_;
	std::deque<Frame> frames_;
	// Oldest first. A frame uses the layout it was recorded with, and layouts follow the frames.
	std::deque<std::shared_ptr<const Layout>> layouts_;
	std::deque<std::string> strings_;
	std::unordered_map<std::string_view, std::uint32_t> stringNumbers_;
	std::size_t bytes_ = 0;
	std::size_t layoutBytes_ = 0;
	std::size_t stringBytes_ = 0;
	bool truncated_ = false;
	bool oversize_ = false;
	// The snapshot handed out last, and the ones handed out since that are still in use.
	mutable std::shared_ptr<const GuiSimulationSnapshot> cached_;
	mutable int cachedTime_ = 0;
	mutable std::unordered_map<int, std::weak_ptr<const GuiSimulationSnapshot>> handedOut_;
};

#endif
