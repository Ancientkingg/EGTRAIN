#include "app/GuiReplayHistory.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace {
// Layout of GuiReplayHistory::Frame::trainInts.
constexpr std::size_t kRouteIndex = 0;
constexpr std::size_t kOnboardPassengers = 1;
constexpr std::size_t kHeadCount = 2;
constexpr std::size_t kTailCount = 3;
constexpr std::size_t kArcCount = 4;
constexpr std::size_t kTrainFlags = 5;
constexpr std::size_t kIntsPerTrain = 6;
constexpr std::int32_t kTrainReversed = 1;
constexpr std::int32_t kTrainOut = 2;
// Layout of GuiReplayHistory::Frame::trainDoubles.
constexpr std::size_t kAxisPosition = 0;
constexpr std::size_t kSpeed = 1;
constexpr std::size_t kDoublesPerTrain = 2;
constexpr std::size_t kStringsPerPassenger = 5;
constexpr std::uint8_t kSectionPrepared = 1;
constexpr std::uint8_t kSectionBlocked = 2;
// Entries of the table of handed-out snapshots at which expired ones are removed.
constexpr std::size_t kHandedOutLimit = 32;

// Each comparison below binds every field of its type. Adding or removing a field stops the
// build here: decide whether it is the same for the whole run or changes per frame, then extend
// the comparison, the reset, GuiReplayHistory::compact and expand, and the round-trip test.

// Compares what stays the same for a train during a run.
bool sameStatic(const GuiTrainState& a, const GuiTrainState& b) {
	[[maybe_unused]] const auto& [index, id, type, description, operatingCode, serviceId, routeIndex, reversedDirection, wagonCount,
		length, departureTime, outOfSimulation, routeAxisPosition, speedKmh, currentOnboardPassengers, maxOnboardPassengers,
		wagonHeadPositions, wagonTailPositions, occupiedArcs] = a;
	return index == b.index && id == b.id && type == b.type && description == b.description && operatingCode == b.operatingCode
		&& serviceId == b.serviceId && wagonCount == b.wagonCount && length == b.length && departureTime == b.departureTime
		&& maxOnboardPassengers == b.maxOnboardPassengers;
}

bool sameStatic(const GuiSignalState& a, const GuiSignalState& b) {
	[[maybe_unused]] const auto& [sectionId, code, reversedDirection, level, failed] = a;
	return sectionId == b.sectionId && reversedDirection == b.reversedDirection;
}

bool sameStatic(const GuiSectionState& a, const GuiSectionState& b) {
	[[maybe_unused]] const auto& [sectionId, prepared, blocked] = a;
	return sectionId == b.sectionId;
}

bool sameStatic(const GuiPlatformState& a, const GuiPlatformState& b) {
	[[maybe_unused]] const auto& [stationId, platformId, maxVolume, passengerIds] = a;
	return stationId == b.stationId && platformId == b.platformId && maxVolume == b.maxVolume;
}

template <class T> bool sameStaticList(const std::vector<T>& a, const std::vector<T>& b) {
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const T& x, const T& y) { return sameStatic(x, y); });
}

// Releases the memory of a vector, which clear() keeps.
template <class T> void release(std::vector<T>& values) {
	std::vector<T>().swap(values);
}

// The per-frame fields of a prototype go back to their defaults.
void resetChanging(GuiTrainState& train) {
	const GuiTrainState defaults;
	train.routeIndex = defaults.routeIndex;
	train.reversedDirection = defaults.reversedDirection;
	train.outOfSimulation = defaults.outOfSimulation;
	train.routeAxisPosition = defaults.routeAxisPosition;
	train.speedKmh = defaults.speedKmh;
	train.currentOnboardPassengers = defaults.currentOnboardPassengers;
	release(train.wagonHeadPositions);
	release(train.wagonTailPositions);
	release(train.occupiedArcs);
}

void resetChanging(GuiSignalState& signal) {
	const GuiSignalState defaults;
	signal.code = defaults.code;
	signal.level = defaults.level;
	signal.failed = defaults.failed;
}

void resetChanging(GuiSectionState& section) {
	const GuiSectionState defaults;
	section.prepared = defaults.prepared;
	section.blocked = defaults.blocked;
}

void resetChanging(GuiPlatformState& platform) {
	release(platform.passengerIds);
}

template <class T> std::vector<T> prototypes(const std::vector<T>& states) {
	std::vector<T> result = states;
	for (T& state : result) resetChanging(state);
	return result;
}

template <class T> std::size_t arrayBytes(const std::vector<T>& values) {
	return values.capacity() * sizeof(T);
}

std::size_t textBytes(const std::string& text) {
	return text.capacity() + 1;
}
}

void GuiReplayHistory::clear() {
	frames_.clear();
	layouts_.clear();
	stringNumbers_.clear();
	strings_.clear();
	bytes_ = 0;
	layoutBytes_ = 0;
	stringBytes_ = 0;
	truncated_ = false;
	oversize_ = false;
	cached_.reset();
	handedOut_.clear();
}

bool GuiReplayHistory::matches(const Layout& layout, const GuiSimulationSnapshot& snapshot) {
	return sameStaticList(layout.trains, snapshot.trains) && sameStaticList(layout.signalStates, snapshot.signalStates)
		&& sameStaticList(layout.sectionStates, snapshot.sectionStates) && sameStaticList(layout.platforms, snapshot.platforms);
}

std::shared_ptr<const GuiReplayHistory::Layout> GuiReplayHistory::makeLayout(const GuiSimulationSnapshot& snapshot) {
	auto layout = std::make_shared<Layout>();
	layout->trains = prototypes(snapshot.trains);
	layout->signalStates = prototypes(snapshot.signalStates);
	layout->sectionStates = prototypes(snapshot.sectionStates);
	layout->platforms = prototypes(snapshot.platforms);
	layout->bytes = layoutPayload(*layout);
	return layout;
}

std::size_t GuiReplayHistory::layoutPayload(const Layout& layout) {
	std::size_t bytes = sizeof(layout);
	bytes += arrayBytes(layout.trains) + arrayBytes(layout.signalStates) + arrayBytes(layout.sectionStates) + arrayBytes(layout.platforms);
	for (const GuiTrainState& train : layout.trains)
		bytes += textBytes(train.type) + textBytes(train.description) + textBytes(train.operatingCode) + textBytes(train.serviceId);
	for (const GuiSignalState& signal : layout.signalStates) bytes += textBytes(signal.sectionId);
	for (const GuiSectionState& section : layout.sectionStates) bytes += textBytes(section.sectionId);
	for (const GuiPlatformState& platform : layout.platforms) bytes += textBytes(platform.stationId) + textBytes(platform.platformId);
	return bytes;
}

std::size_t GuiReplayHistory::framePayload(const Frame& frame) {
	return sizeof(frame) + arrayBytes(frame.trainInts) + arrayBytes(frame.trainDoubles) + arrayBytes(frame.wagonPositions)
		+ arrayBytes(frame.occupiedArcs) + arrayBytes(frame.signalCodes) + arrayBytes(frame.signalLevels)
		+ arrayBytes(frame.signalFailed) + arrayBytes(frame.sectionFlags) + arrayBytes(frame.platformPassengerCounts)
		+ arrayBytes(frame.platformPassengerIds) + arrayBytes(frame.passengerStrings) + arrayBytes(frame.messageTimes)
		+ arrayBytes(frame.messageStrings);
}

std::uint32_t GuiReplayHistory::intern(const std::string& text) {
	const auto found = stringNumbers_.find(std::string_view(text));
	if (found != stringNumbers_.end())
		return found->second;
	const auto number = static_cast<std::uint32_t>(strings_.size());
	strings_.push_back(text);
	stringNumbers_.emplace(std::string_view(strings_.back()), number);
	stringBytes_ += sizeof(std::string) + textBytes(strings_.back()) + sizeof(std::string_view) + sizeof(std::uint32_t);
	return number;
}

GuiReplayHistory::Frame GuiReplayHistory::compact(const GuiSimulationSnapshot& snapshot, std::shared_ptr<const Layout> layout) {
	const auto& [timestep, totalTimesteps, trains, signalStates, sectionStates, platforms, passengers, messages] = snapshot;
	Frame frame;
	frame.timestep = timestep;
	frame.totalTimesteps = totalTimesteps;
	frame.layout = std::move(layout);

	std::size_t wagonValues = 0;
	std::size_t arcCount = 0;
	for (const GuiTrainState& train : trains) {
		wagonValues += train.wagonHeadPositions.size() + train.wagonTailPositions.size();
		arcCount += train.occupiedArcs.size();
	}
	frame.trainInts.reserve(trains.size() * kIntsPerTrain);
	frame.trainDoubles.reserve(trains.size() * kDoublesPerTrain);
	frame.wagonPositions.reserve(wagonValues);
	frame.occupiedArcs.reserve(arcCount);
	for (const GuiTrainState& train : trains) {
		const std::size_t at = frame.trainInts.size();
		frame.trainInts.resize(at + kIntsPerTrain);
		std::int32_t* ints = frame.trainInts.data() + at;
		ints[kRouteIndex] = train.routeIndex;
		ints[kOnboardPassengers] = train.currentOnboardPassengers;
		ints[kHeadCount] = static_cast<std::int32_t>(train.wagonHeadPositions.size());
		ints[kTailCount] = static_cast<std::int32_t>(train.wagonTailPositions.size());
		ints[kArcCount] = static_cast<std::int32_t>(train.occupiedArcs.size());
		ints[kTrainFlags] = (train.reversedDirection ? kTrainReversed : 0) | (train.outOfSimulation ? kTrainOut : 0);
		frame.trainDoubles.push_back(train.routeAxisPosition);
		frame.trainDoubles.push_back(train.speedKmh);
		frame.wagonPositions.insert(frame.wagonPositions.end(), train.wagonHeadPositions.begin(), train.wagonHeadPositions.end());
		frame.wagonPositions.insert(frame.wagonPositions.end(), train.wagonTailPositions.begin(), train.wagonTailPositions.end());
		frame.occupiedArcs.insert(frame.occupiedArcs.end(), train.occupiedArcs.begin(), train.occupiedArcs.end());
	}

	frame.signalCodes.reserve(signalStates.size());
	frame.signalLevels.reserve(signalStates.size());
	frame.signalFailed.reserve(signalStates.size());
	for (const GuiSignalState& signal : signalStates) {
		frame.signalCodes.push_back(signal.code);
		frame.signalLevels.push_back(signal.level);
		frame.signalFailed.push_back(signal.failed ? 1 : 0);
	}

	frame.sectionFlags.reserve(sectionStates.size());
	for (const GuiSectionState& section : sectionStates)
		frame.sectionFlags.push_back(static_cast<std::uint8_t>((section.prepared ? kSectionPrepared : 0) | (section.blocked ? kSectionBlocked : 0)));

	frame.platformPassengerCounts.reserve(platforms.size());
	std::size_t platformPassengers = 0;
	for (const GuiPlatformState& platform : platforms) platformPassengers += platform.passengerIds.size();
	frame.platformPassengerIds.reserve(platformPassengers);
	for (const GuiPlatformState& platform : platforms) {
		frame.platformPassengerCounts.push_back(static_cast<std::uint32_t>(platform.passengerIds.size()));
		for (const std::string& id : platform.passengerIds) frame.platformPassengerIds.push_back(intern(id));
	}

	frame.passengerStrings.reserve(passengers.size() * kStringsPerPassenger);
	for (const GuiPassengerState& passenger : passengers) {
		const auto& [id, status, waitingPlatform, nextTrain, nextDestination] = passenger;
		frame.passengerStrings.push_back(intern(id));
		frame.passengerStrings.push_back(intern(status));
		frame.passengerStrings.push_back(intern(waitingPlatform));
		frame.passengerStrings.push_back(intern(nextTrain));
		frame.passengerStrings.push_back(intern(nextDestination));
	}

	frame.messageTimes.reserve(messages.size());
	frame.messageStrings.reserve(messages.size() * 2);
	for (const GuiVirtualCouplingState& message : messages) {
		const auto& [messageTime, trainDescription, text] = message;
		frame.messageTimes.push_back(messageTime);
		frame.messageStrings.push_back(intern(trainDescription));
		frame.messageStrings.push_back(intern(text));
	}
	frame.bytes = framePayload(frame);
	return frame;
}

std::shared_ptr<const GuiSimulationSnapshot> GuiReplayHistory::expand(const Frame& frame) const {
	const Layout& layout = *frame.layout;
	auto snapshot = std::make_shared<GuiSimulationSnapshot>();
	snapshot->timestep = frame.timestep;
	snapshot->totalTimesteps = frame.totalTimesteps;

	snapshot->trains = layout.trains;
	auto wagonAt = frame.wagonPositions.begin();
	auto arcAt = frame.occupiedArcs.begin();
	for (std::size_t i = 0; i < snapshot->trains.size(); ++i) {
		GuiTrainState& train = snapshot->trains[i];
		const std::int32_t* ints = frame.trainInts.data() + i * kIntsPerTrain;
		const double* doubles = frame.trainDoubles.data() + i * kDoublesPerTrain;
		train.routeIndex = ints[kRouteIndex];
		train.currentOnboardPassengers = ints[kOnboardPassengers];
		train.reversedDirection = (ints[kTrainFlags] & kTrainReversed) != 0;
		train.outOfSimulation = (ints[kTrainFlags] & kTrainOut) != 0;
		train.routeAxisPosition = doubles[kAxisPosition];
		train.speedKmh = doubles[kSpeed];
		train.wagonHeadPositions.assign(wagonAt, wagonAt + ints[kHeadCount]);
		wagonAt += ints[kHeadCount];
		train.wagonTailPositions.assign(wagonAt, wagonAt + ints[kTailCount]);
		wagonAt += ints[kTailCount];
		train.occupiedArcs.assign(arcAt, arcAt + ints[kArcCount]);
		arcAt += ints[kArcCount];
	}

	snapshot->signalStates = layout.signalStates;
	for (std::size_t i = 0; i < snapshot->signalStates.size(); ++i) {
		GuiSignalState& signal = snapshot->signalStates[i];
		signal.code = frame.signalCodes[i];
		signal.level = frame.signalLevels[i];
		signal.failed = frame.signalFailed[i] != 0;
	}

	snapshot->sectionStates = layout.sectionStates;
	for (std::size_t i = 0; i < snapshot->sectionStates.size(); ++i) {
		GuiSectionState& section = snapshot->sectionStates[i];
		section.prepared = (frame.sectionFlags[i] & kSectionPrepared) != 0;
		section.blocked = (frame.sectionFlags[i] & kSectionBlocked) != 0;
	}

	snapshot->platforms = layout.platforms;
	auto idAt = frame.platformPassengerIds.begin();
	for (std::size_t i = 0; i < snapshot->platforms.size(); ++i) {
		GuiPlatformState& platform = snapshot->platforms[i];
		const std::uint32_t count = frame.platformPassengerCounts[i];
		platform.passengerIds.reserve(count);
		for (std::uint32_t k = 0; k < count; ++k, ++idAt) platform.passengerIds.push_back(strings_[*idAt]);
	}

	const std::size_t passengerCount = frame.passengerStrings.size() / kStringsPerPassenger;
	snapshot->passengers.reserve(passengerCount);
	for (std::size_t i = 0; i < passengerCount; ++i) {
		const std::uint32_t* numbers = frame.passengerStrings.data() + i * kStringsPerPassenger;
		snapshot->passengers.push_back({strings_[numbers[0]], strings_[numbers[1]], strings_[numbers[2]], strings_[numbers[3]], strings_[numbers[4]]});
	}

	snapshot->virtualCouplingMessages.reserve(frame.messageTimes.size());
	for (std::size_t i = 0; i < frame.messageTimes.size(); ++i)
		snapshot->virtualCouplingMessages.push_back(
			{frame.messageTimes[i], strings_[frame.messageStrings[2 * i]], strings_[frame.messageStrings[2 * i + 1]]});
	return snapshot;
}

void GuiReplayHistory::releaseUnusedLayouts() {
	while (layouts_.size() > 1 && (frames_.empty() || frames_.front().layout != layouts_.front())) {
		layoutBytes_ -= layouts_.front()->bytes;
		layouts_.pop_front();
	}
}

void GuiReplayHistory::evictOldest() {
	if (cached_ && cachedTime_ == frames_.front().timestep)
		cached_.reset();
	handedOut_.erase(frames_.front().timestep);
	bytes_ -= frames_.front().bytes;
	frames_.pop_front();
	truncated_ = true;
	releaseUnusedLayouts();
}

void GuiReplayHistory::record(std::shared_ptr<const GuiSimulationSnapshot> snapshot) {
	if (!snapshot || oversize_)
		return;
	const bool final = snapshot->timestep >= snapshot->totalTimesteps - 1;
	if (!frames_.empty() && !final && snapshot->timestep % cadenceSeconds != 0)
		return;
	if (!frames_.empty() && frames_.back().timestep == snapshot->timestep)
		return;
	std::shared_ptr<const Layout> layout;
	if (!layouts_.empty() && matches(*layouts_.back(), *snapshot))
		layout = layouts_.back();
	const bool added = !layout;
	if (added)
		layout = makeLayout(*snapshot);
	Frame frame = compact(*snapshot, layout);
	const std::size_t needed = frame.bytes + (added ? layout->bytes : 0);
	while (!frames_.empty() && payloadBytes() + needed > budget_) evictOldest();
	if (added && frames_.empty()) {
		layouts_.clear();
		layoutBytes_ = 0;
	}
	if (payloadBytes() + needed > budget_) {
		clear();
		oversize_ = true;
		return;
	}
	if (added) {
		layouts_.push_back(layout);
		layoutBytes_ += layout->bytes;
	}
	bytes_ += frame.bytes;
	frames_.push_back(std::move(frame));
	releaseUnusedLayouts();
}

std::shared_ptr<const GuiSimulationSnapshot> GuiReplayHistory::atOrBefore(int time) const {
	if (frames_.empty())
		return {};
	const auto it = std::upper_bound(frames_.begin(), frames_.end(), time, [](int value, const Frame& entry) { return value < entry.timestep; });
	const Frame& frame = *(it == frames_.begin() ? it : std::prev(it));
	if (cached_ && cachedTime_ == frame.timestep)
		return cached_;
	std::shared_ptr<const GuiSimulationSnapshot> snapshot;
	const auto known = handedOut_.find(frame.timestep);
	if (known != handedOut_.end())
		snapshot = known->second.lock();
	if (!snapshot) {
		snapshot = expand(frame);
		if (handedOut_.size() >= kHandedOutLimit) {
			for (auto entry = handedOut_.begin(); entry != handedOut_.end();)
				entry = entry->second.expired() ? handedOut_.erase(entry) : std::next(entry);
		}
		handedOut_[frame.timestep] = snapshot;
	}
	cached_ = snapshot;
	cachedTime_ = frame.timestep;
	return cached_;
}
