#include "app/GuiReplayHistory.h"
#include "simulation/SimulationRun.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <utility>

namespace {
constexpr std::int32_t kSectionPrepared = 1;
constexpr std::int32_t kSectionBlocked = 2;
constexpr std::size_t kStringsPerPassenger = 4;
// Entries of the table of handed-out snapshots at which expired ones are removed.
constexpr std::size_t kHandedOutLimit = 32;

// Each comparison below binds every field of its type. Adding or removing a field stops the
// build here: decide whether it is the same for the whole run or changes, then extend the
// comparison, the reset, the slots of GuiReplayHistory and the round-trip test.

// Compares what stays the same for a train during a run.
bool sameStatic(const GuiTrainState& a, const GuiTrainState& b) {
	[[maybe_unused]] const auto& [index, id, type, description, operatingCode, serviceId, occurrence, routeIndex, reversedDirection,
		wagonCount, length, departureTime, outOfSimulation, routeAxisPosition, speedKmh, currentOnboardPassengers, maxOnboardPassengers,
		wagonHeadPositions, wagonTailPositions, occupiedArcs] = a;
	return index == b.index && id == b.id && type == b.type && description == b.description && operatingCode == b.operatingCode
		&& serviceId == b.serviceId && occurrence == b.occurrence && routeIndex == b.routeIndex && reversedDirection == b.reversedDirection
		&& wagonCount == b.wagonCount && length == b.length && departureTime == b.departureTime
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

bool sameStatic(const GuiPassengerState& a, const GuiPassengerState& b) {
	[[maybe_unused]] const auto& [id, status, waitingPlatform, nextTrain, nextDestination] = a;
	return id == b.id;
}

template <class T> bool sameStaticList(const std::vector<T>& a, const std::vector<T>& b) {
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const T& x, const T& y) { return sameStatic(x, y); });
}

// Releases the memory of a vector, which clear() keeps.
template <class T> void release(std::vector<T>& values) {
	std::vector<T>().swap(values);
}

// The changing fields of a prototype go back to their defaults.
void resetChanging(GuiTrainState& train) {
	const GuiTrainState defaults;
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

// A string that is only cleared keeps its storage, so these are replaced by empty ones.
void resetChanging(GuiPassengerState& passenger) {
	passenger.status = std::string();
	passenger.waitingPlatform = std::string();
	passenger.nextTrain = std::string();
	passenger.nextDestination = std::string();
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

// Equal bytes, which tells 0.0 from -0.0 and takes no tolerance.
bool sameBits(double a, double b) {
	std::uint64_t x = 0;
	std::uint64_t y = 0;
	static_assert(sizeof(x) == sizeof(a), "a double has 64 bits");
	std::memcpy(&x, &a, sizeof(x));
	std::memcpy(&y, &b, sizeof(y));
	return x == y;
}

bool sameArcs(const std::vector<GuiOccupiedArc>& a, const std::vector<GuiOccupiedArc>& b) {
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const GuiOccupiedArc& x, const GuiOccupiedArc& y) {
		return x.trackId == y.trackId && sameBits(x.startX, y.startX);
	});
}

// One series of a train from its vector `s` and the slice `v` of it that starts at sample `start`. The slice is used only
// when it lies inside the vector and has the same bytes; otherwise the series copies what it needs.
GuiReplaySeries makeSeries(const std::vector<double>& s, const std::shared_ptr<const std::vector<double>>& v, int start) {
	GuiReplaySeries series;
	const std::size_t n = s.size();
	series.size = static_cast<int>(n);
	if (n == 0)
		return series;
	std::size_t a = n;
	std::shared_ptr<const std::vector<double>> slice;
	if (v && !v->empty() && start >= 0 && static_cast<std::size_t>(start) + v->size() <= n
		&& std::equal(v->begin(), v->end(), s.begin() + start, sameBits)) {
		a = static_cast<std::size_t>(start);
		slice = v;
	}
	series.first = s[0];
	std::size_t p = 0;
	while (p < a && sameBits(s[p], series.first)) ++p;
	series.headStart = static_cast<int>(p);
	series.head.assign(s.begin() + static_cast<std::ptrdiff_t>(p), s.begin() + static_cast<std::ptrdiff_t>(a));
	series.sharedStart = static_cast<int>(a);
	series.shared = std::move(slice);
	const std::size_t b = a + (series.shared ? series.shared->size() : 0);
	series.last = s[n - 1];
	std::size_t q = n;
	while (q > b && sameBits(s[q - 1], series.last)) --q;
	series.tail.assign(s.begin() + static_cast<std::ptrdiff_t>(b), s.begin() + static_cast<std::ptrdiff_t>(q));
	return series;
}

std::size_t seriesBytes(const GuiReplaySeries& series) {
	return arrayBytes(series.head) + arrayBytes(series.tail);
}

// The X coordinate of the point `wagon` wagon lengths behind the head of a train whose head is `position` metres along
// its route, in km, or -1. This is trainXPositionOf (simulation/RollingStock.cpp) on the copied route, with the same
// operand types and the same expressions in the same order, so that the result is the same bit for bit.
double wagonXPosition(const GuiReplayTrains::Route& route, const GuiReplayTrains::Train& train, double position, int wagon) {
	double X;
	double portion = 0;

	double total_nW = train.numberOfWagons + 1;

	for (std::size_t i = 0; i < route.startX.size(); i++) {
		if (((position - wagon * (train.trainLength / total_nW)) >= route.startX[i] * 1000) && (position - wagon * (train.trainLength / total_nW)) < (route.endX[i] * 1000)) {
			portion = (position - wagon * (train.trainLength / total_nW)) - route.startX[i] * 1000;

			if (route.reversed) {
				X = (route.geoXBeg[i] - portion) / 1000;
			} else {
				X = (route.geoXBeg[i] + portion) / 1000;
			}
			return X;
		}
	}

	return -1;
}
} // namespace

double GuiReplaySeries::at(int sample) const {
	const int sharedEnd = sharedStart + (shared ? static_cast<int>(shared->size()) : 0);
	const int tailEnd = sharedEnd + static_cast<int>(tail.size());
	if (sample < headStart)
		return first;
	if (sample < sharedStart)
		return head[static_cast<std::size_t>(sample - headStart)];
	if (sample < sharedEnd)
		return (*shared)[static_cast<std::size_t>(sample - sharedStart)];
	if (sample < tailEnd)
		return tail[static_cast<std::size_t>(sample - sharedEnd)];
	return last;
}

std::size_t GuiReplayTrains::ownedBytes() const {
	std::size_t bytes = sizeof(*this) + arrayBytes(routes) + arrayBytes(trains) + arrayBytes(messageSteps) + arrayBytes(messageTrains)
		+ arrayBytes(messageTexts);
	for (const Route& route : routes) bytes += arrayBytes(route.startX) + arrayBytes(route.endX) + arrayBytes(route.geoXBeg);
	for (const Train& train : trains) bytes += seriesBytes(train.position) + seriesBytes(train.speed);
	for (const std::string& text : messageTrains) bytes += textBytes(text);
	for (const std::string& text : messageTexts) bytes += textBytes(text);
	return bytes;
}

std::size_t GuiReplayTrains::sharedBytes() const {
	std::size_t bytes = 0;
	for (const Train& train : trains) {
		if (train.position.shared)
			bytes += arrayBytes(*train.position.shared);
		if (train.speed.shared)
			bytes += arrayBytes(*train.speed.shared);
	}
	return bytes;
}

std::shared_ptr<const GuiReplayTrains> makeGuiReplayTrains(const SimulationRun& run, const std::vector<GuiReplayTrajectory>& shared) {
	auto source = std::make_shared<GuiReplayTrains>();
	source->routes.reserve(run.infra.routes.size());
	for (const Route& route : run.infra.routes) {
		GuiReplayTrains::Route copy;
		copy.reversed = route.reversed_direction;
		const std::size_t count = static_cast<std::size_t>(std::max(0, route.N_Block_Sections));
		const std::size_t sections = std::min(count, route.sequence_of_block_sections.size());
		copy.startX.reserve(sections);
		copy.endX.reserve(sections);
		copy.geoXBeg.reserve(sections);
		for (std::size_t i = 0; i < sections; ++i) {
			const Section& section = route.sequence_of_block_sections[i];
			copy.startX.push_back(section.start_node.X);
			copy.endX.push_back(section.end_node.X);
			copy.geoXBeg.push_back(section.GeoXBegNode);
		}
		source->routes.push_back(std::move(copy));
	}

	const std::size_t trainCount = static_cast<std::size_t>(std::max(0, std::min(run.numTrains, static_cast<int>(run.trains.size()))));
	source->trains.reserve(trainCount);
	for (std::size_t i = 0; i < trainCount; ++i) {
		const ::Train& train = run.trains[i];
		GuiReplayTrains::Train copy;
		copy.validRoute = train.indexOfRoute >= 0 && train.indexOfRoute < static_cast<int>(run.infra.routes.size());
		copy.routeIndex = train.indexOfRoute;
		copy.numberOfWagons = train.number_of_wagons;
		copy.trainLength = train.train_length;
		const GuiReplayTrajectory* slice = i < shared.size() ? &shared[i] : nullptr;
		copy.position = makeSeries(train.instant_spatial_position, slice ? slice->position : nullptr, slice ? slice->firstSample : 0);
		copy.speed = makeSeries(train.instant_train_speed, slice ? slice->speed : nullptr, slice ? slice->firstSample : 0);
		source->trains.push_back(std::move(copy));
	}

	const std::size_t messages = std::min({run.vcMsgTimestep.size(), run.vcMsgTrain.size(), run.vcMsgText.size()});
	source->messageSteps.assign(run.vcMsgTimestep.begin(), run.vcMsgTimestep.begin() + static_cast<std::ptrdiff_t>(messages));
	source->messageTrains.assign(run.vcMsgTrain.begin(), run.vcMsgTrain.begin() + static_cast<std::ptrdiff_t>(messages));
	source->messageTexts.assign(run.vcMsgText.begin(), run.vcMsgText.begin() + static_cast<std::ptrdiff_t>(messages));
	return source;
}

void GuiReplayHistory::clear() {
	resetRecording();
	trains_.reset();
	trainsBytes_ = 0;
	sharedBytes_ = 0;
	truncated_ = false;
	oversize_ = false;
}

void GuiReplayHistory::resetRecording() {
	intervals_.clear();
	layout_ = Layout();
	slots_ = Slots();
	release(current_);
	release(currentArcs_);
	release(currentPlatforms_);
	stringNumbers_.clear();
	strings_.clear();
	layoutBytes_ = 0;
	stringBytes_ = 0;
	intervalBytes_ = 0;
	stateBytes_ = 0;
	cached_.reset();
	handedOut_.clear();
}

void GuiReplayHistory::setTrains(std::shared_ptr<const GuiReplayTrains> trains) {
	trains_ = std::move(trains);
	trainsBytes_ = trains_ ? trains_->ownedBytes() : 0;
	sharedBytes_ = trains_ ? trains_->sharedBytes() : 0;
	cached_.reset();
	handedOut_.clear();
}

int GuiReplayHistory::lastTime() const {
	return intervals_.empty() ? 0 : intervals_.back().startTime + static_cast<int>(intervals_.back().secondEnds.size());
}

bool GuiReplayHistory::sameCounts(const Layout& layout, const GuiSimulationSnapshot& snapshot) {
	return layout.trains.size() == snapshot.trains.size() && layout.signalStates.size() == snapshot.signalStates.size()
		&& layout.sectionStates.size() == snapshot.sectionStates.size() && layout.platforms.size() == snapshot.platforms.size()
		&& layout.passengers.size() == snapshot.passengers.size();
}

bool GuiReplayHistory::sameStatic(const Layout& layout, const GuiSimulationSnapshot& snapshot) {
	return layout.totalTimesteps == snapshot.totalTimesteps && sameStaticList(layout.trains, snapshot.trains)
		&& sameStaticList(layout.signalStates, snapshot.signalStates) && sameStaticList(layout.sectionStates, snapshot.sectionStates)
		&& sameStaticList(layout.platforms, snapshot.platforms) && sameStaticList(layout.passengers, snapshot.passengers);
}

std::size_t GuiReplayHistory::layoutPayload(const Layout& layout) {
	std::size_t bytes = sizeof(layout);
	bytes += arrayBytes(layout.trains) + arrayBytes(layout.signalStates) + arrayBytes(layout.sectionStates) + arrayBytes(layout.platforms)
		+ arrayBytes(layout.passengers);
	for (const GuiTrainState& train : layout.trains)
		bytes += textBytes(train.type) + textBytes(train.description) + textBytes(train.operatingCode) + textBytes(train.serviceId);
	for (const GuiSignalState& signal : layout.signalStates) bytes += textBytes(signal.sectionId);
	for (const GuiSectionState& section : layout.sectionStates) bytes += textBytes(section.sectionId);
	for (const GuiPlatformState& platform : layout.platforms) bytes += textBytes(platform.stationId) + textBytes(platform.platformId);
	for (const GuiPassengerState& passenger : layout.passengers)
		bytes += textBytes(passenger.id) + textBytes(passenger.status) + textBytes(passenger.waitingPlatform) + textBytes(passenger.nextTrain)
			+ textBytes(passenger.nextDestination);
	return bytes;
}

std::size_t GuiReplayHistory::intervalPayload(const Interval& interval) {
	return sizeof(interval) + arrayBytes(interval.keyframe) + arrayBytes(interval.changes) + arrayBytes(interval.secondEnds)
		+ arrayBytes(interval.arcs) + arrayBytes(interval.arcEnds) + arrayBytes(interval.platformIds) + arrayBytes(interval.platformEnds);
}

std::size_t GuiReplayHistory::statePayload() const {
	std::size_t bytes = arrayBytes(current_) + arrayBytes(currentArcs_) + arrayBytes(currentPlatforms_);
	for (const auto& arcs : currentArcs_) bytes += arrayBytes(arcs);
	for (const auto& ids : currentPlatforms_) bytes += arrayBytes(ids);
	return bytes;
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

void GuiReplayHistory::makeLayout(const GuiSimulationSnapshot& snapshot) {
	layout_.trains = prototypes(snapshot.trains);
	layout_.signalStates = prototypes(snapshot.signalStates);
	layout_.sectionStates = prototypes(snapshot.sectionStates);
	layout_.platforms = prototypes(snapshot.platforms);
	layout_.passengers = prototypes(snapshot.passengers);
	layout_.totalTimesteps = snapshot.totalTimesteps;
	layoutBytes_ = layoutPayload(layout_);
	const std::size_t signals = layout_.signalStates.size();
	const std::size_t trains = layout_.trains.size();
	slots_.signalCode = 0;
	slots_.signalLevel = slots_.signalCode + signals;
	slots_.signalFailed = slots_.signalLevel + signals;
	slots_.sectionFlags = slots_.signalFailed + signals;
	slots_.trainOut = slots_.sectionFlags + layout_.sectionStates.size();
	slots_.trainOnboard = slots_.trainOut + trains;
	slots_.trainArcs = slots_.trainOnboard + trains;
	slots_.platformList = slots_.trainArcs + trains;
	slots_.passenger = slots_.platformList + layout_.platforms.size();
	slots_.total = slots_.passenger + kStringsPerPassenger * layout_.passengers.size();
}

std::int32_t GuiReplayHistory::addArcList(Interval& interval, const std::vector<GuiOccupiedArc>& arcs) {
	interval.arcs.insert(interval.arcs.end(), arcs.begin(), arcs.end());
	interval.arcEnds.push_back(static_cast<std::uint32_t>(interval.arcs.size()));
	return static_cast<std::int32_t>(interval.arcEnds.size() - 1);
}

std::int32_t GuiReplayHistory::addPlatformList(Interval& interval, const std::vector<std::uint32_t>& ids) {
	interval.platformIds.insert(interval.platformIds.end(), ids.begin(), ids.end());
	interval.platformEnds.push_back(static_cast<std::uint32_t>(interval.platformIds.size()));
	return static_cast<std::int32_t>(interval.platformEnds.size() - 1);
}

// Starts an interval at the second of `snapshot`: its values become the state, the keyframe, and the lists that they
// refer to are added to the new interval, so that an interval never refers to another one.
void GuiReplayHistory::startInterval(const GuiSimulationSnapshot& snapshot) {
	if (!intervals_.empty()) {
		// The interval before is complete: give back what its vectors did not use.
		Interval& done = intervals_.back();
		done.changes.shrink_to_fit();
		done.secondEnds.shrink_to_fit();
		done.arcs.shrink_to_fit();
		done.arcEnds.shrink_to_fit();
		done.platformIds.shrink_to_fit();
		done.platformEnds.shrink_to_fit();
		refreshBytes();
	}
	Interval interval;
	interval.startTime = snapshot.timestep;
	current_.assign(slots_.total, 0);
	for (std::size_t i = 0; i < snapshot.signalStates.size(); ++i) {
		const GuiSignalState& signal = snapshot.signalStates[i];
		current_[slots_.signalCode + i] = signal.code;
		current_[slots_.signalLevel + i] = signal.level;
		current_[slots_.signalFailed + i] = signal.failed ? 1 : 0;
	}
	for (std::size_t i = 0; i < snapshot.sectionStates.size(); ++i) {
		const GuiSectionState& section = snapshot.sectionStates[i];
		current_[slots_.sectionFlags + i] = (section.prepared ? kSectionPrepared : 0) | (section.blocked ? kSectionBlocked : 0);
	}
	currentArcs_.assign(snapshot.trains.size(), {});
	for (std::size_t i = 0; i < snapshot.trains.size(); ++i) {
		const GuiTrainState& train = snapshot.trains[i];
		current_[slots_.trainOut + i] = train.outOfSimulation ? 1 : 0;
		current_[slots_.trainOnboard + i] = train.currentOnboardPassengers;
		currentArcs_[i] = train.occupiedArcs;
		current_[slots_.trainArcs + i] = addArcList(interval, currentArcs_[i]);
	}
	currentPlatforms_.assign(snapshot.platforms.size(), {});
	for (std::size_t i = 0; i < snapshot.platforms.size(); ++i) {
		for (const std::string& id : snapshot.platforms[i].passengerIds) currentPlatforms_[i].push_back(intern(id));
		current_[slots_.platformList + i] = addPlatformList(interval, currentPlatforms_[i]);
	}
	for (std::size_t i = 0; i < snapshot.passengers.size(); ++i) {
		const GuiPassengerState& passenger = snapshot.passengers[i];
		const std::size_t at = slots_.passenger + kStringsPerPassenger * i;
		current_[at] = static_cast<std::int32_t>(intern(passenger.status));
		current_[at + 1] = static_cast<std::int32_t>(intern(passenger.waitingPlatform));
		current_[at + 2] = static_cast<std::int32_t>(intern(passenger.nextTrain));
		current_[at + 3] = static_cast<std::int32_t>(intern(passenger.nextDestination));
	}
	interval.keyframe = current_;
	intervals_.push_back(std::move(interval));
	refreshBytes();
}

void GuiReplayHistory::putValue(std::size_t slot, std::int32_t value) {
	if (current_[slot] == value)
		return;
	current_[slot] = value;
	intervals_.back().changes.push_back({static_cast<std::uint32_t>(slot), value});
}

// Appends the difference between `snapshot` and the state of the second before to the last interval.
void GuiReplayHistory::addChanges(const GuiSimulationSnapshot& snapshot) {
	Interval& interval = intervals_.back();
	// A second that was not recorded shows the state of the second before it.
	while (lastTime() + 1 < snapshot.timestep) interval.secondEnds.push_back(static_cast<std::uint32_t>(interval.changes.size()));
	for (std::size_t i = 0; i < snapshot.signalStates.size(); ++i) {
		const GuiSignalState& signal = snapshot.signalStates[i];
		putValue(slots_.signalCode + i, signal.code);
		putValue(slots_.signalLevel + i, signal.level);
		putValue(slots_.signalFailed + i, signal.failed ? 1 : 0);
	}
	for (std::size_t i = 0; i < snapshot.sectionStates.size(); ++i) {
		const GuiSectionState& section = snapshot.sectionStates[i];
		putValue(slots_.sectionFlags + i, (section.prepared ? kSectionPrepared : 0) | (section.blocked ? kSectionBlocked : 0));
	}
	for (std::size_t i = 0; i < snapshot.trains.size(); ++i) {
		const GuiTrainState& train = snapshot.trains[i];
		putValue(slots_.trainOut + i, train.outOfSimulation ? 1 : 0);
		putValue(slots_.trainOnboard + i, train.currentOnboardPassengers);
		if (!sameArcs(currentArcs_[i], train.occupiedArcs)) {
			currentArcs_[i] = train.occupiedArcs;
			putValue(slots_.trainArcs + i, addArcList(interval, currentArcs_[i]));
		}
	}
	for (std::size_t i = 0; i < snapshot.platforms.size(); ++i) {
		const std::vector<std::string>& ids = snapshot.platforms[i].passengerIds;
		std::vector<std::uint32_t>& known = currentPlatforms_[i];
		bool same = ids.size() == known.size();
		for (std::size_t k = 0; same && k < ids.size(); ++k) same = strings_[known[k]] == ids[k];
		if (same)
			continue;
		known.clear();
		for (const std::string& id : ids) known.push_back(intern(id));
		putValue(slots_.platformList + i, addPlatformList(interval, known));
	}
	for (std::size_t i = 0; i < snapshot.passengers.size(); ++i) {
		const GuiPassengerState& passenger = snapshot.passengers[i];
		const std::size_t at = slots_.passenger + kStringsPerPassenger * i;
		const std::string* texts[kStringsPerPassenger] = {&passenger.status, &passenger.waitingPlatform, &passenger.nextTrain,
			&passenger.nextDestination};
		for (std::size_t k = 0; k < kStringsPerPassenger; ++k)
			if (strings_[static_cast<std::uint32_t>(current_[at + k])] != *texts[k])
				putValue(at + k, static_cast<std::int32_t>(intern(*texts[k])));
	}
	interval.secondEnds.push_back(static_cast<std::uint32_t>(interval.changes.size()));
	refreshBytes();
}

void GuiReplayHistory::refreshBytes() {
	Interval& last = intervals_.back();
	intervalBytes_ -= last.bytes;
	last.bytes = intervalPayload(last);
	intervalBytes_ += last.bytes;
	stateBytes_ = statePayload();
}

void GuiReplayHistory::evictOldest() {
	intervalBytes_ -= intervals_.front().bytes;
	intervals_.pop_front();
	truncated_ = true;
	const int first = firstTime();
	if (cached_ && cachedTime_ < first)
		cached_.reset();
	for (auto entry = handedOut_.begin(); entry != handedOut_.end();)
		entry = entry->first < first ? handedOut_.erase(entry) : std::next(entry);
}

void GuiReplayHistory::enforceBudget() {
	while (payloadBytes() > budget_ && intervals_.size() > 1) evictOldest();
	if (payloadBytes() > budget_) {
		clear();
		oversize_ = true;
	}
}

void GuiReplayHistory::record(const std::shared_ptr<const GuiSimulationSnapshot>& snapshot) {
	if (!snapshot || oversize_)
		return;
	const GuiSimulationSnapshot& now = *snapshot;
	bool newInterval = false;
	if (!intervals_.empty()) {
		if (now.timestep <= lastTime())
			return;
		newInterval = now.timestep >= intervals_.back().startTime + keyframeInterval_;
		const bool finalStep = now.timestep >= now.totalTimesteps - 1;
		// The counts are compared at every step, the static fields at the first second of an interval and at the last step
		// of the run. A change is detected late, and the seconds before the detection are dropped with the old layout.
		if (!sameCounts(layout_, now) || ((newInterval || finalStep) && !sameStatic(layout_, now))) {
			resetRecording();
			truncated_ = true;
		}
	}
	if (intervals_.empty()) {
		makeLayout(now);
		startInterval(now);
	} else if (newInterval) {
		startInterval(now);
	} else {
		addChanges(now);
	}
	enforceBudget();
}

std::shared_ptr<const GuiSimulationSnapshot> GuiReplayHistory::at(int time) const {
	if (intervals_.empty())
		return {};
	const int second = std::clamp(time, firstTime(), lastTime());
	if (cached_ && cachedTime_ == second)
		return cached_;
	std::shared_ptr<const GuiSimulationSnapshot> snapshot;
	const auto known = handedOut_.find(second);
	if (known != handedOut_.end())
		snapshot = known->second.lock();
	if (!snapshot) {
		const auto after = std::upper_bound(intervals_.begin(), intervals_.end(), second, [](int value, const Interval& entry) { return value < entry.startTime; });
		const Interval& interval = *std::prev(after);
		std::vector<std::int32_t> values = interval.keyframe;
		const std::size_t seconds = std::min(static_cast<std::size_t>(second - interval.startTime), interval.secondEnds.size());
		const std::size_t end = seconds == 0 ? 0 : interval.secondEnds[seconds - 1];
		for (std::size_t i = 0; i < end; ++i) values[interval.changes[i].slot] = interval.changes[i].value;
		snapshot = expand(interval, values, second);
		if (handedOut_.size() >= kHandedOutLimit) {
			for (auto entry = handedOut_.begin(); entry != handedOut_.end();)
				entry = entry->second.expired() ? handedOut_.erase(entry) : std::next(entry);
		}
		handedOut_[second] = snapshot;
	}
	cached_ = std::move(snapshot);
	cachedTime_ = second;
	return cached_;
}

std::shared_ptr<const GuiSimulationSnapshot> GuiReplayHistory::expand(const Interval& interval, const std::vector<std::int32_t>& values,
	int time) const {
	auto snapshot = std::make_shared<GuiSimulationSnapshot>();
	snapshot->timestep = time;
	snapshot->totalTimesteps = layout_.totalTimesteps;

	snapshot->trains = layout_.trains;
	for (std::size_t i = 0; i < snapshot->trains.size(); ++i) {
		GuiTrainState& train = snapshot->trains[i];
		train.outOfSimulation = values[slots_.trainOut + i] != 0;
		train.currentOnboardPassengers = values[slots_.trainOnboard + i];
		const auto list = static_cast<std::size_t>(values[slots_.trainArcs + i]);
		const std::size_t from = list == 0 ? 0 : interval.arcEnds[list - 1];
		train.occupiedArcs.assign(interval.arcs.begin() + static_cast<std::ptrdiff_t>(from),
			interval.arcs.begin() + static_cast<std::ptrdiff_t>(interval.arcEnds[list]));
	}
	deriveTrains(*snapshot);

	snapshot->signalStates = layout_.signalStates;
	for (std::size_t i = 0; i < snapshot->signalStates.size(); ++i) {
		GuiSignalState& signal = snapshot->signalStates[i];
		signal.code = values[slots_.signalCode + i];
		signal.level = values[slots_.signalLevel + i];
		signal.failed = values[slots_.signalFailed + i] != 0;
	}

	snapshot->sectionStates = layout_.sectionStates;
	for (std::size_t i = 0; i < snapshot->sectionStates.size(); ++i) {
		GuiSectionState& section = snapshot->sectionStates[i];
		section.prepared = (values[slots_.sectionFlags + i] & kSectionPrepared) != 0;
		section.blocked = (values[slots_.sectionFlags + i] & kSectionBlocked) != 0;
	}

	snapshot->platforms = layout_.platforms;
	for (std::size_t i = 0; i < snapshot->platforms.size(); ++i) {
		const auto list = static_cast<std::size_t>(values[slots_.platformList + i]);
		const std::size_t from = list == 0 ? 0 : interval.platformEnds[list - 1];
		GuiPlatformState& platform = snapshot->platforms[i];
		platform.passengerIds.reserve(interval.platformEnds[list] - from);
		for (std::size_t k = from; k < interval.platformEnds[list]; ++k) platform.passengerIds.push_back(strings_[interval.platformIds[k]]);
	}

	snapshot->passengers = layout_.passengers;
	for (std::size_t i = 0; i < snapshot->passengers.size(); ++i) {
		GuiPassengerState& passenger = snapshot->passengers[i];
		const std::size_t at = slots_.passenger + kStringsPerPassenger * i;
		passenger.status = strings_[static_cast<std::uint32_t>(values[at])];
		passenger.waitingPlatform = strings_[static_cast<std::uint32_t>(values[at + 1])];
		passenger.nextTrain = strings_[static_cast<std::uint32_t>(values[at + 2])];
		passenger.nextDestination = strings_[static_cast<std::uint32_t>(values[at + 3])];
	}

	if (trains_) {
		for (std::size_t i = 0; i < trains_->messageSteps.size(); ++i)
			if (guiVirtualCouplingMessageShown(trains_->messageSteps[i], time))
				snapshot->virtualCouplingMessages.push_back({trains_->messageSteps[i], trains_->messageTrains[i], trains_->messageTexts[i]});
	}
	return snapshot;
}

// Sets the position, the speed and the wagon positions of the trains the way buildGuiSimulationSnapshot does, from the
// train source. Without one they keep their defaults.
void GuiReplayHistory::deriveTrains(GuiSimulationSnapshot& snapshot) const {
	if (!trains_)
		return;
	const int time = snapshot.timestep;
	const std::size_t count = std::min(snapshot.trains.size(), trains_->trains.size());
	for (std::size_t i = 0; i < count; ++i) {
		const GuiReplayTrains::Train& source = trains_->trains[i];
		GuiTrainState& train = snapshot.trains[i];
		if (time >= 0 && time < source.speed.size)
			train.speedKmh = source.speed.at(time) * 3.6;
		if (!source.validRoute || static_cast<std::size_t>(source.routeIndex) >= trains_->routes.size() || time < 0 || time >= source.position.size)
			continue;
		const GuiReplayTrains::Route& route = trains_->routes[static_cast<std::size_t>(source.routeIndex)];
		const double position = source.position.at(time);
		train.routeAxisPosition = position;
		train.wagonHeadPositions.reserve(static_cast<std::size_t>(train.wagonCount + 1));
		train.wagonTailPositions.reserve(static_cast<std::size_t>(train.wagonCount + 1));
		for (int wagon = 0; wagon <= train.wagonCount; ++wagon) {
			train.wagonHeadPositions.push_back(wagonXPosition(route, source, position, wagon));
			train.wagonTailPositions.push_back(wagonXPosition(route, source, position, wagon + 1));
		}
	}
}
