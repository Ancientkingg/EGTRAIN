#include "app/GuiSimulationSnapshot.h"
#include "app/GuiReplayHistory.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {
void require(bool condition, const char* message) {
	if (!condition) {
		std::cerr << message << '\n';
		std::exit(1);
	}
}
}

// Counts the bytes requested from the allocator, so that the accounted replay bytes can be
// compared with what the history really holds.
namespace {
std::size_t liveHeapBytes = 0;
constexpr std::size_t headerBytes = 16;
}

void* operator new(std::size_t size) {
	void* raw = std::malloc(size + headerBytes);
	if (!raw)
		throw std::bad_alloc();
	*static_cast<std::size_t*>(raw) = size;
	liveHeapBytes += size;
	return static_cast<char*>(raw) + headerBytes;
}

void operator delete(void* pointer) noexcept {
	if (!pointer)
		return;
	void* raw = static_cast<char*>(pointer) - headerBytes;
	liveHeapBytes -= *static_cast<std::size_t*>(raw);
	std::free(raw);
}

void operator delete(void* pointer, std::size_t) noexcept {
	operator delete(pointer);
}

namespace {
// Every field of every snapshot type is bound by name. A field added to one of the types stops
// the build here: compare it, and make the sample snapshot below set it to a value that is not
// its default.
auto fields(const GuiOccupiedArc& v) {
	const auto& [trackId, startX] = v;
	return std::make_tuple(trackId, startX);
}

std::vector<std::tuple<int, double>> arcFields(const std::vector<GuiOccupiedArc>& arcs) {
	std::vector<std::tuple<int, double>> result;
	for (const GuiOccupiedArc& arc : arcs) result.push_back(fields(arc));
	return result;
}

auto fields(const GuiTrainState& v) {
	const auto& [index, id, type, description, operatingCode, serviceId, routeIndex, reversedDirection, wagonCount, length, departureTime,
		outOfSimulation, routeAxisPosition, speedKmh, currentOnboardPassengers, maxOnboardPassengers, wagonHeadPositions,
		wagonTailPositions, occupiedArcs] = v;
	return std::make_tuple(index, id, type, description, operatingCode, serviceId, routeIndex, reversedDirection, wagonCount, length,
		departureTime, outOfSimulation, routeAxisPosition, speedKmh, currentOnboardPassengers, maxOnboardPassengers, wagonHeadPositions,
		wagonTailPositions, arcFields(occupiedArcs));
}

auto fields(const GuiSignalState& v) {
	const auto& [sectionId, code, reversedDirection, level, failed] = v;
	return std::make_tuple(sectionId, code, reversedDirection, level, failed);
}

auto fields(const GuiSectionState& v) {
	const auto& [sectionId, prepared, blocked] = v;
	return std::make_tuple(sectionId, prepared, blocked);
}

auto fields(const GuiPlatformState& v) {
	const auto& [stationId, platformId, maxVolume, passengerIds] = v;
	return std::make_tuple(stationId, platformId, maxVolume, passengerIds);
}

auto fields(const GuiPassengerState& v) {
	const auto& [id, status, waitingPlatform, nextTrain, nextDestination] = v;
	return std::make_tuple(id, status, waitingPlatform, nextTrain, nextDestination);
}

auto fields(const GuiVirtualCouplingState& v) {
	const auto& [timestep, trainDescription, message] = v;
	return std::make_tuple(timestep, trainDescription, message);
}

template <class T> bool sameList(const std::vector<T>& a, const std::vector<T>& b) {
	if (a.size() != b.size())
		return false;
	for (std::size_t i = 0; i < a.size(); ++i)
		if (fields(a[i]) != fields(b[i]))
			return false;
	return true;
}

bool sameSnapshot(const GuiSimulationSnapshot& a, const GuiSimulationSnapshot& b) {
	const auto& [timestep, totalTimesteps, trains, signalStates, sectionStates, platforms, passengers, messages] = a;
	return timestep == b.timestep && totalTimesteps == b.totalTimesteps && sameList(trains, b.trains)
		&& sameList(signalStates, b.signalStates) && sameList(sectionStates, b.sectionStates) && sameList(platforms, b.platforms)
		&& sameList(passengers, b.passengers) && sameList(messages, b.virtualCouplingMessages);
}

// A snapshot whose every field is set, and whose changing fields depend on the time. The
// signal list repeats a section and direction with different codes.
GuiSimulationSnapshot sampleSnapshot(int t, std::size_t trainCount = 3, int total = 1000) {
	GuiSimulationSnapshot snapshot;
	snapshot.timestep = t;
	snapshot.totalTimesteps = total;
	const int step = t / 5;
	for (std::size_t i = 0; i < trainCount; ++i) {
		const int n = static_cast<int>(i);
		GuiTrainState train;
		train.index = n;
		train.id = 100.5 + n;
		train.type = "type " + std::to_string(n);
		train.description = "train " + std::to_string(n);
		train.operatingCode = "code " + std::to_string(n);
		train.serviceId = "service " + std::to_string(n);
		train.routeIndex = (n + step / 10) % 3;
		train.reversedDirection = (n + step / 20) % 2 == 1;
		train.wagonCount = 3 + n % 12;
		train.length = 75.25 * (n + 1);
		train.departureTime = 10 * n;
		train.outOfSimulation = n % 3 == 2 && t >= 300;
		train.speedKmh = 0.25 * t + n;
		train.currentOnboardPassengers = (t * 7 + n) % 40;
		train.maxOnboardPassengers = 200 + n;
		if (n % 3 != 1 || t >= 100) {
			train.routeAxisPosition = 1.5 * t + n;
			for (int wagon = 0; wagon <= train.wagonCount; ++wagon) {
				train.wagonHeadPositions.push_back(train.routeAxisPosition - 25.0 * wagon);
				train.wagonTailPositions.push_back(train.routeAxisPosition - 25.0 * (wagon + 1) + 0.125);
			}
			for (int arc = 0; arc < step % 3 + 1; ++arc) train.occupiedArcs.push_back({7 + arc, 0.5 * t + arc});
		}
		snapshot.trains.push_back(std::move(train));
	}
	const int codes[] = {0, 75, 180, 270, 751};
	for (int k = 0; k < 6; ++k) {
		GuiSignalState signal;
		signal.sectionId = "@" + std::to_string(k / 2 + 1) + "-B0@";
		signal.reversedDirection = k >= 4;
		signal.code = codes[(step + k) % 5];
		signal.level = k % 3 == 0 ? kGuiSignalNoLevel : 1 + (k + step / 10) % 5;
		signal.failed = (step / 4 + k) % 4 == 0;
		snapshot.signalStates.push_back(std::move(signal));
	}
	for (int k = 0; k < 4; ++k)
		snapshot.sectionStates.push_back({"section " + std::to_string(k), (step + k) % 3 == 0, (step + k) % 5 == 0});
	for (int p = 0; p < 2; ++p) {
		GuiPlatformState platform;
		platform.stationId = "station " + std::to_string(p);
		platform.platformId = "platform " + std::to_string(p);
		platform.maxVolume = 50 + p;
		for (int k = 0; k < (step + p) % 4; ++k) platform.passengerIds.push_back("pax " + std::to_string((step + k) % 7));
		snapshot.platforms.push_back(std::move(platform));
	}
	const char* statuses[] = {"waiting", "onboard", "arrived"};
	for (int k = 0; k < 3; ++k)
		snapshot.passengers.push_back({"pax " + std::to_string(k), statuses[(step + k) % 3], "platform " + std::to_string(k % 2),
			k == 1 ? std::string() : "train " + std::to_string(k), "station " + std::to_string((step + k) % 2)});
	if (step % 3 == 0) {
		snapshot.virtualCouplingMessages.push_back({t - 5, "train 0", "couple " + std::to_string(t)});
		snapshot.virtualCouplingMessages.push_back({t, "train 1", ""});
	}
	return snapshot;
}

// The same run, but with the signal, section and train counts of a large scene.
GuiSimulationSnapshot sceneSnapshot(int t, std::size_t trainCount, std::size_t signalCount, std::size_t sectionCount, int total) {
	GuiSimulationSnapshot snapshot = sampleSnapshot(t, trainCount, total);
	snapshot.signalStates.clear();
	snapshot.sectionStates.clear();
	const int codes[] = {0, 75, 180, 270, 751};
	for (std::size_t k = 0; k < signalCount; ++k) {
		GuiSignalState signal;
		signal.sectionId = "@" + std::to_string(k / 2) + "-B0@";
		signal.reversedDirection = k % 2 == 1;
		signal.code = codes[(static_cast<std::size_t>(t / 5) + k) % 5];
		signal.level = 2;
		snapshot.signalStates.push_back(std::move(signal));
	}
	for (std::size_t k = 0; k < sectionCount; ++k)
		snapshot.sectionStates.push_back({"section " + std::to_string(k), (t / 5 + k) % 3 == 0, false});
	return snapshot;
}

std::shared_ptr<const GuiSimulationSnapshot> shared(GuiSimulationSnapshot snapshot) {
	return std::make_shared<const GuiSimulationSnapshot>(std::move(snapshot));
}
}

int main() {
	require(!guiSectionReportsPermissiveSignalling(0.0),
		"zero signalling code was reported as permissive signalling");
	require(guiSectionReportsPermissiveSignalling(1.0)
			&& guiSectionReportsPermissiveSignalling(75.0)
			&& guiSectionReportsPermissiveSignalling(180.0)
			&& guiSectionReportsPermissiveSignalling(270.0)
			&& guiSectionReportsPermissiveSignalling(751.0),
		"nonzero signalling codes did not report permissive signalling");
	GuiSimulationSnapshot operational;
	operational.sectionStates.push_back({"unused-nonzero", guiSectionReportsPermissiveSignalling(751.0), false});
	operational.sectionStates.push_back({"blocked", guiSectionReportsPermissiveSignalling(0.0), true});
	GuiTrainState occupiedTrain;
	occupiedTrain.occupiedArcs.push_back({7, 2.5});
	operational.trains.push_back(occupiedTrain);
	require(operational.sectionStates.at(0).prepared
			&& !operational.sectionStates.at(0).blocked
			&& operational.trains.front().occupiedArcs.size() == 1,
		"permissive signalling was conflated with occupied or blocked state");
	require(operational.sectionStates.at(1).blocked
			&& !operational.sectionStates.at(1).prepared,
		"blocked section did not remain independent of permissive signalling");

	// Route copies of one section and direction merge into the most restrictive
	// code, whatever the order they arrive in.
	require(guiSignalRestriction(0) < guiSignalRestriction(751)
			&& guiSignalRestriction(751) < guiSignalRestriction(75)
			&& guiSignalRestriction(75) < guiSignalRestriction(180)
			&& guiSignalRestriction(180) < guiSignalRestriction(270)
			&& guiSignalRestriction(270) < guiSignalRestriction(-1)
			&& guiSignalRestriction(-1) == guiSignalRestriction(1000),
		"signal codes are not ordered from stop to clear");
	require(guiSignalHasLevel(0) && guiSignalHasLevel(5) && !guiSignalHasLevel(6)
			&& !guiSignalHasLevel(kGuiSignalNoLevel) && !guiSignalHasLevel(-99999999),
		"signalling levels are not 0 to 5");
	std::array<int, 4> codes{270, 75, 180, 751};
	std::sort(codes.begin(), codes.end());
	do {
		GuiSignalStateList copies;
		for (const int code : codes)
			copies.merge("@1-B0@", false, code, 0);
		const auto merged = copies.take();
		require(merged.size() == 1 && merged.front().code == 751,
			"merge did not keep the most restrictive code in every order");
	} while (std::next_permutation(codes.begin(), codes.end()));
	// Directions and sections stay separate, in the order they first appear.
	GuiSignalStateList routes;
	routes.merge("@2-B0@", false, 75, 2);
	routes.merge("@1-B0@", false, 270, 2);
	routes.merge("@1-B0@", true, 0, 2);
	routes.merge("@1-B0@", false, 180, 2);
	routes.merge("@1-B0@", true, 270, 2);
	const auto separate = routes.take();
	require(separate.size() == 3, "directions or sections were merged");
	require(separate[0].sectionId == "@2-B0@" && separate[0].code == 75 && !separate[0].reversedDirection
			&& separate[1].sectionId == "@1-B0@" && separate[1].code == 180 && !separate[1].reversedDirection
			&& separate[2].sectionId == "@1-B0@" && separate[2].code == 0 && separate[2].reversedDirection,
		"a direction took the code of the other direction");
	// A clear copy does not hide a stop, and a failure marks both directions.
	GuiSignalStateList failure;
	failure.merge("@1-B0@", false, 0, 0);
	failure.merge("@1-B0@", false, 270, 0);
	failure.merge("@1-B0@", true, 270, 0);
	failure.merge("@2-B0@", false, 270, 0);
	failure.fail("@1-B0@");
	failure.fail("@3-B0@");
	const auto failed = failure.take();
	require(failed.size() == 3 && failed[0].code == 0 && failed[0].failed
			&& failed[1].failed && failed[1].reversedDirection && !failed[2].failed,
		"a clear copy hid a stop, or a failure missed a direction");
	// A copy without a level has no signalling and never hides one that has.
	GuiSignalStateList levels;
	levels.merge("@1-B0@", false, 270, kGuiSignalNoLevel);
	levels.merge("@1-B0@", false, 75, 1);
	levels.merge("@1-B0@", false, 0, kGuiSignalNoLevel);
	const auto withLevel = levels.take();
	require(withLevel.size() == 1 && withLevel.front().level == 1 && withLevel.front().code == 75,
		"a copy without a level replaced or hid one with a level");
	GuiSignalStateList noLevel;
	noLevel.merge("@1-B0@", false, 270, kGuiSignalNoLevel);
	noLevel.merge("@1-B0@", false, 270, 99999999);
	const auto without = noLevel.take();
	require(without.size() == 1 && without.front().level == kGuiSignalNoLevel,
		"a section without a level was given one");
	require(GuiSignalState().level == kGuiSignalNoLevel && !GuiSignalState().failed,
		"a signal state starts with a level or failed");
	require(GuiSignalStateList().take().empty(), "an empty list has entries");

	// A head shows the code of a section with a level, and unavailable without one.
	require(guiSignalDisplayCode({"@1-B0@", 75, false, 0, false}) == 75
			&& guiSignalDisplayCode({"@1-B0@", 751, true, 5, false}) == 751
			&& guiSignalDisplayCode({"@1-B0@", 270, false, kGuiSignalNoLevel, false}) == kGuiSignalUnavailable
			&& guiSignalDisplayCode({"@1-B0@", 0, false, 99999999, false}) == kGuiSignalUnavailable,
		"a head did not show the code of its level, or unavailable without one");

	GuiTrainState activeTrain;
	activeTrain.description = "Intercity northbound";
	require(guiTrainDisplayIdentifier(activeTrain) == activeTrain.description,
		"train description fallback was not used");
	activeTrain.operatingCode = "1725";
	require(guiTrainDisplayIdentifier(activeTrain) == activeTrain.operatingCode,
		"operating code was not preferred");
	activeTrain.occupiedArcs.push_back({7, 2.5});
	GuiTrainState exitedTrain = activeTrain;
	exitedTrain.outOfSimulation = true;
	require(guiTrainPublishesOccupiedArcs(activeTrain),
		"active train occupation was filtered");
	require(!guiTrainPublishesOccupiedArcs(exitedTrain),
		"out-of-simulation train occupation was retained");
	activeTrain.routeAxisPosition = 25.0;
	activeTrain.departureTime = 5;
	activeTrain.wagonHeadPositions.push_back(25.0);
	activeTrain.wagonTailPositions.push_back(24.0);
	exitedTrain = activeTrain;
	exitedTrain.outOfSimulation = true;
	require(!guiReplayTrainHasPosition(activeTrain, 4)
			&& guiReplayTrainHasPosition(activeTrain, 5)
			&& !guiReplayTrainHasPosition(exitedTrain, 5),
		"replay train visibility ignored departure or exit");
	activeTrain.routeAxisPosition = -9999.0;
	require(!guiReplayTrainHasPosition(activeTrain, 5), "replay accepted sentinel geometry");
	activeTrain.routeAxisPosition = 25.0;

	GuiSimulationSnapshot first;
	first.timestep = 1;
	first.trains.push_back(GuiTrainState{});
	first.trains.front().description = "first";
	first.sectionStates.push_back({"section-a", true, true});
	first.sectionStates.push_back({"section-b", false, false});
	std::set<std::string> sectionIds;
	for (const GuiSectionState& section : first.sectionStates)
		sectionIds.insert(section.sectionId);
	require(sectionIds.size() == first.sectionStates.size(), "section states contain duplicate IDs");
	require(first.sectionStates.front().prepared && first.sectionStates.front().blocked,
		"section state flags were not retained");
	auto firstSnapshot = std::make_shared<const GuiSimulationSnapshot>(first);

	GuiSimulationSnapshot second;
	second.timestep = 2;
	second.trains.push_back(GuiTrainState{});
	second.trains.front().description = "second";
	second.sectionStates.push_back({"section-a", false, true});
	auto secondSnapshot = std::make_shared<const GuiSimulationSnapshot>(second);

	GuiSimulationSnapshotMailbox mailbox;
	require(mailbox.publish(firstSnapshot), "first publish did not request a notification");
	require(!mailbox.publish(secondSnapshot), "second publish requested a duplicate notification");

	second.trains.front().description = "mutated source";
	const auto latest = mailbox.take();
	require(latest != nullptr, "take returned no snapshot");
	require(latest->timestep == 2, "take did not return the latest snapshot");
	require(latest->trains.front().description == "second", "snapshot changed after source mutation");
	require(latest->sectionStates.size() == 1 && latest->sectionStates.front().sectionId == "section-a",
		"section state payload was not copied into immutable snapshot");

	require(mailbox.publish(firstSnapshot), "publish after take did not request a notification");
	require(mailbox.take()->timestep == 1, "second take returned the wrong snapshot");

	GuiReplayHistory history;
	require(!history.atOrBefore(4), "empty replay selected a frame");
	require(history.empty() && history.payloadBytes() == 0 && history.size() == 0 && !history.truncated()
			&& history.evictedBeforeTime() == 0 && history.budgetBytes() == GuiReplayHistory::payloadLimit,
		"empty replay is not empty");
	for (int t = 0; t <= 12; ++t) {
		auto frame = std::make_shared<GuiSimulationSnapshot>();
		frame->timestep = t;
		frame->totalTimesteps = 13;
		frame->trains.push_back(activeTrain);
		history.record(frame);
	}
	require(history.size() == 4 && history.firstTime() == 0 && history.lastTime() == 12,
		"first/cadence/final samples not retained");
	require(history.atOrBefore(-1)->timestep == 0 && history.atOrBefore(7)->timestep == 5
			&& history.atOrBefore(99)->timestep == 12,
		"replay lookup failed to clamp or floor");
	history.clear();
	require(history.empty() && !history.truncated() && history.payloadBytes() == 0 && history.layoutCount() == 0,
		"replacement retained old frames");
	auto only = std::make_shared<GuiSimulationSnapshot>();
	only->timestep = 3;
	only->totalTimesteps = 4;
	history.record(only);
	require(history.size() == 1 && history.atOrBefore(0)->timestep == 3, "one-frame replay failed");

	// Every field of a frame with trains, signals, sections, platforms, passengers and messages
	// comes back, for many frames with changing values.
	history.clear();
	std::vector<GuiSimulationSnapshot> recorded;
	for (int t = 0; t < 600; ++t) {
		recorded.push_back(sampleSnapshot(t, 3, 600));
		history.record(shared(recorded.back()));
	}
	require(history.size() == 121 && history.firstTime() == 0 && history.lastTime() == 599 && !history.truncated()
			&& history.evictedBeforeTime() == 0 && history.layoutCount() == 1,
		"run did not keep every cadence frame under one layout");
	for (const GuiSimulationSnapshot& expected : recorded) {
		if (expected.timestep % 5 != 0 && expected.timestep != 599)
			continue;
		const auto back = history.atOrBefore(expected.timestep);
		require(back && sameSnapshot(*back, expected), "a frame did not come back as recorded");
		const auto between = history.atOrBefore(expected.timestep + 1);
		require(between && (expected.timestep == 599 || between->timestep == expected.timestep),
			"a time between frames did not select the frame before it");
	}
	// The sample sets every field to something other than its default in some frame.
	bool sawReversed = false, sawExited = false, sawArcs = false, sawLevel = false, sawNoLevel = false;
	bool sawFailed = false, sawPrepared = false, sawBlocked = false;
	bool sawPlatformIds = false, sawNextTrain = false, sawNoNextTrain = false, sawMessages = false, sawStale = false;
	for (const GuiSimulationSnapshot& snapshot : recorded) {
		for (const GuiTrainState& train : snapshot.trains) {
			sawReversed = sawReversed || train.reversedDirection;
			sawExited = sawExited || train.outOfSimulation;
			sawArcs = sawArcs || !train.occupiedArcs.empty();
			sawStale = sawStale || (train.outOfSimulation && !train.wagonHeadPositions.empty());
		}
		for (const GuiSignalState& signal : snapshot.signalStates) {
			sawLevel = sawLevel || signal.level != kGuiSignalNoLevel;
			sawNoLevel = sawNoLevel || signal.level == kGuiSignalNoLevel;
			sawFailed = sawFailed || signal.failed;
		}
		for (const GuiSectionState& section : snapshot.sectionStates) {
			sawPrepared = sawPrepared || section.prepared;
			sawBlocked = sawBlocked || section.blocked;
		}
		for (const GuiPlatformState& platform : snapshot.platforms) sawPlatformIds = sawPlatformIds || !platform.passengerIds.empty();
		for (const GuiPassengerState& passenger : snapshot.passengers) {
			sawNextTrain = sawNextTrain || !passenger.nextTrain.empty();
			sawNoNextTrain = sawNoNextTrain || passenger.nextTrain.empty();
		}
		sawMessages = sawMessages || !snapshot.virtualCouplingMessages.empty();
	}
	require(sawReversed && sawExited && sawArcs && sawStale && sawLevel && sawNoLevel && sawFailed && sawPrepared && sawBlocked
			&& sawPlatformIds && sawNextTrain && sawNoNextTrain && sawMessages,
		"the sample snapshot leaves a field at its default");
	require(history.atOrBefore(300)->trains[2].outOfSimulation && history.atOrBefore(300)->trains[2].wagonHeadPositions.size() == 6
			&& history.atOrBefore(300)->virtualCouplingMessages.size() == 2,
		"stale wagon positions of an exited train or the messages were lost");

	// The history itself keeps the frame it handed out last, so a caller that drops the result
	// can ask again and gets the same object, and the cache moves with the latest request.
	{
		const std::weak_ptr<const GuiSimulationSnapshot> last = history.atOrBefore(100);
		require(!last.expired() && last.lock() == history.atOrBefore(104), "the frame handed out last was not kept");
		history.atOrBefore(105);
		require(last.expired(), "the cache kept more than one frame");
	}

	// Asking for the same frame twice returns the same object, another frame another one.
	{
		const auto a = history.atOrBefore(100);
		const auto b = history.atOrBefore(104);
		require(a == b && a == history.atOrBefore(100), "the same frame was rebuilt twice");
		const auto c = history.atOrBefore(105);
		require(c != a && c->timestep == 105 && history.atOrBefore(105) == c, "a different frame returned the cached object");
		// A frame that the caller still holds is not rebuilt after other frames were asked for.
		for (int t = 110; t < 300; t += 5) history.atOrBefore(t);
		require(history.atOrBefore(100) == a && history.atOrBefore(105) == c, "a frame in use was rebuilt");
	}

	// A run in which the number of trains and a static field change starts a new layout; frames
	// of both layouts come back.
	history.clear();
	std::vector<GuiSimulationSnapshot> mixed;
	for (int t = 0; t < 40; t += 5) {
		mixed.push_back(sampleSnapshot(t, t < 15 ? 2 : 4, 40));
		if (t >= 30)
			mixed.back().trains[0].description = "renamed";
		history.record(shared(mixed.back()));
	}
	require(history.size() == 8 && history.layoutCount() == 3, "a change of the static data did not start a layout");
	for (const GuiSimulationSnapshot& expected : mixed)
		require(sameSnapshot(*history.atOrBefore(expected.timestep), expected), "a frame of a changed layout did not come back");
	require(history.atOrBefore(0)->trains.size() == 2 && history.atOrBefore(20)->trains.size() == 4
			&& history.atOrBefore(35)->trains.front().description == "renamed" && history.atOrBefore(25)->trains.front().description == "train 0",
		"a layout leaked into a frame of another");

	// A change of any one field that is stored once per layout starts a new layout, and both
	// frames come back as recorded. A field that is missing from the comparison would be restored
	// from the layout of the first frame.
	{
		struct StaticChange {
			const char* name;
			void (*apply)(GuiSimulationSnapshot&);
		};
		const StaticChange changes[] = {
			{"train index", [](GuiSimulationSnapshot& s) { s.trains[1].index += 10; }},
			{"train id", [](GuiSimulationSnapshot& s) { s.trains[1].id += 1.0; }},
			{"train type", [](GuiSimulationSnapshot& s) { s.trains[1].type += "x"; }},
			{"train description", [](GuiSimulationSnapshot& s) { s.trains[1].description += "x"; }},
			{"train operating code", [](GuiSimulationSnapshot& s) { s.trains[1].operatingCode += "x"; }},
			{"train service", [](GuiSimulationSnapshot& s) { s.trains[1].serviceId += "x"; }},
			{"train wagon count", [](GuiSimulationSnapshot& s) { s.trains[1].wagonCount += 1; }},
			{"train length", [](GuiSimulationSnapshot& s) { s.trains[1].length += 1.0; }},
			{"train departure time", [](GuiSimulationSnapshot& s) { s.trains[1].departureTime += 1; }},
			{"train capacity", [](GuiSimulationSnapshot& s) { s.trains[1].maxOnboardPassengers += 1; }},
			{"signal section", [](GuiSimulationSnapshot& s) { s.signalStates[2].sectionId += "x"; }},
			{"signal direction", [](GuiSimulationSnapshot& s) { s.signalStates[2].reversedDirection = !s.signalStates[2].reversedDirection; }},
			{"section id", [](GuiSimulationSnapshot& s) { s.sectionStates[2].sectionId += "x"; }},
			{"platform station", [](GuiSimulationSnapshot& s) { s.platforms[1].stationId += "x"; }},
			{"platform id", [](GuiSimulationSnapshot& s) { s.platforms[1].platformId += "x"; }},
			{"platform capacity", [](GuiSimulationSnapshot& s) { s.platforms[1].maxVolume += 1; }},
		};
		for (const StaticChange& change : changes) {
			GuiReplayHistory single;
			GuiSimulationSnapshot before = sampleSnapshot(0, 3, 40);
			GuiSimulationSnapshot after = sampleSnapshot(5, 3, 40);
			change.apply(after);
			single.record(shared(before));
			single.record(shared(after));
			const bool restored = single.size() == 2 && sameSnapshot(*single.atOrBefore(0), before) && sameSnapshot(*single.atOrBefore(5), after);
			if (!restored || single.layoutCount() != 2) {
				std::cerr << "a change of the " << change.name << " was not kept by the replay\n";
				return 1;
			}
		}
	}

	// The accounted bytes grow with every frame and are close to what the history holds.
	history.clear();
	const std::size_t heapBefore = liveHeapBytes;
	std::vector<std::shared_ptr<const GuiSimulationSnapshot>> large;
	for (int t = 0; t < 400; t += 5) large.push_back(shared(sceneSnapshot(t, 40, 1350, 700, 400)));
	const std::size_t heapWithInput = liveHeapBytes;
	std::size_t previous = history.payloadBytes();
	std::size_t firstStep = 0;
	std::size_t lastStep = 0;
	bool grows = true;
	for (const auto& snapshot : large) {
		history.record(snapshot);
		const std::size_t now = history.payloadBytes();
		grows = grows && now > previous;
		if (!firstStep)
			firstStep = now - previous;
		lastStep = now - previous;
		previous = now;
	}
	const std::size_t held = liveHeapBytes - heapWithInput;
	const std::size_t accounted = history.payloadBytes();
	require(grows && history.size() == large.size(), "accounted bytes do not grow with the frames");
	require(accounted > held - held / 10 && accounted < held + held / 10, "accounted bytes differ from the bytes held by more than 10 percent");
	// One frame is much smaller than the first, which also holds the layout, and a frame of a
	// scene of this size needs far less than the snapshot it was made from.
	require(history.layoutCount() == 1 && 2 * lastStep < firstStep, "frames repeat the static data of the run");
	require(heapWithInput - heapBefore > 3 * held, "the compact form is not much smaller than the snapshots");
	large.clear();
	history.clear();
	require(history.payloadBytes() == 0, "clearing did not reset the accounted bytes");

	// Eviction under a small budget keeps the newest frames and says what it covers.
	history.clear();
	std::vector<int> times;
	std::vector<GuiSimulationSnapshot> run;
	for (int t = 0; t < 500; ++t) {
		run.push_back(sampleSnapshot(t, 3, 500));
		if (t % 5 == 0 || t == 499)
			times.push_back(t);
	}
	GuiReplayHistory reference;
	for (int t = 0; t < 50; ++t) reference.record(shared(run[static_cast<std::size_t>(t)]));
	GuiReplayHistory small(reference.payloadBytes());
	for (const GuiSimulationSnapshot& snapshot : run) small.record(shared(snapshot));
	require(small.truncated() && small.size() >= 3 && small.size() < times.size() / 2 && small.payloadBytes() <= small.budgetBytes(),
		"a small budget did not evict");
	require(small.lastTime() == 499 && small.firstTime() == times[times.size() - small.size()]
			&& small.evictedBeforeTime() == small.firstTime() && small.firstTime() > 0,
		"eviction did not keep the newest frames or did not report the covered interval");
	require(small.atOrBefore(0)->timestep == small.firstTime() && small.atOrBefore(499)->timestep == 499,
		"a time before the covered interval did not select its first frame");
	for (std::size_t k = times.size() - small.size(); k < times.size(); ++k)
		require(sameSnapshot(*small.atOrBefore(times[k]), run[static_cast<std::size_t>(times[k])]), "a frame kept after eviction changed");
	require(small.layoutCount() == 1, "eviction multiplied the layouts");

	// A layout of evicted frames is released and no longer counted.
	GuiReplayHistory later;
	for (int t = 50; t < 100; t += 5) later.record(shared(sampleSnapshot(t, 6, 100)));
	GuiReplayHistory trade(later.payloadBytes());
	for (int t = 0; t < 100; t += 5) trade.record(shared(sampleSnapshot(t, t < 50 ? 2 : 6, 100)));
	require(trade.truncated() && trade.layoutCount() == 1 && trade.firstTime() >= 50 && trade.size() >= 5 && trade.payloadBytes() <= trade.budgetBytes()
			&& trade.atOrBefore(0)->trains.size() == 6 && trade.atOrBefore(95)->trains.size() == 6,
		"the layout of evicted frames was kept");

	// Frames with the shape of Copenhagen and of the Netherlands scene fit the default budget.
	struct Shape {
		std::size_t trains, signals, sections;
		int frames;
	};
	for (const Shape shape : {Shape{196, 1331, 1265, 1601}, Shape{40, 1353, 700, 1601}}) {
		GuiReplayHistory wholeRun;
		const int total = (shape.frames - 1) * GuiReplayHistory::cadenceSeconds + 1;
		for (int t = 0; t < total; t += GuiReplayHistory::cadenceSeconds)
			wholeRun.record(shared(sceneSnapshot(t, shape.trains, shape.signals, shape.sections, total)));
		require(!wholeRun.truncated() && wholeRun.firstTime() == 0 && wholeRun.size() == static_cast<std::size_t>(shape.frames)
				&& wholeRun.payloadBytes() < GuiReplayHistory::payloadLimit && wholeRun.layoutCount() == 1,
			"a full run of a large scene did not fit the replay budget");
		require(sameSnapshot(*wholeRun.atOrBefore(0), sceneSnapshot(0, shape.trains, shape.signals, shape.sections, total))
				&& sameSnapshot(*wholeRun.atOrBefore(4000), sceneSnapshot(4000, shape.trains, shape.signals, shape.sections, total)),
			"a frame of a large scene did not come back as recorded");
	}

	history.clear();
	auto withoutService = std::make_shared<GuiSimulationSnapshot>();
	withoutService->trains.push_back(GuiTrainState{});
	history.record(withoutService);
	const std::size_t bytesWithoutService = history.payloadBytes();
	history.clear();
	auto withService = std::make_shared<GuiSimulationSnapshot>();
	withService->trains.push_back(GuiTrainState{});
	withService->trains.front().serviceId = std::string(1000, 's');
	history.record(withService);
	require(history.atOrBefore(0)->trains.front().serviceId == std::string(1000, 's')
			&& history.payloadBytes() >= bytesWithoutService + 900,
		"service id is not part of the train state or the replay byte count");
	history.clear();
	auto withSignals = std::make_shared<GuiSimulationSnapshot>();
	withSignals->signalStates.resize(1000);
	for (auto& signal : withSignals->signalStates)
		signal.sectionId = std::string(100, 'x');
	history.record(withSignals);
	require(history.payloadBytes() >= 1000 * (sizeof(GuiSignalState) + 100),
		"signal states are not part of the replay byte count");

	// Strings that vary are stored once, so repeated passenger frames stay small, and a frame
	// that cannot fit the budget turns replay off.
	GuiReplayHistory passengersOnly(8u << 20);
	auto crowd = std::make_shared<GuiSimulationSnapshot>();
	crowd->passengers.resize(10000);
	for (auto& passenger : crowd->passengers) passenger.id = std::string(1000, 'p');
	for (int t = 0; t < 12; ++t) {
		auto frame = std::make_shared<GuiSimulationSnapshot>(*crowd);
		frame->timestep = t * GuiReplayHistory::cadenceSeconds;
		passengersOnly.record(frame);
	}
	require(!passengersOnly.truncated() && passengersOnly.size() == 12 && passengersOnly.payloadBytes() <= passengersOnly.budgetBytes(),
		"equal strings were stored once per passenger");
	crowd->passengers.front().id = std::string(passengersOnly.budgetBytes(), 'x');
	passengersOnly.record(crowd);
	require(passengersOnly.oversize() && passengersOnly.empty() && passengersOnly.payloadBytes() == 0 && !passengersOnly.atOrBefore(0),
		"oversize frame did not disable replay");
	// A small frame is refused too once replay is off for the run.
	auto tiny = std::make_shared<GuiSimulationSnapshot>();
	tiny->timestep = 500;
	passengersOnly.record(tiny);
	require(passengersOnly.empty() && passengersOnly.oversize(), "a history without replay recorded a frame");

	// There is no frame count limit: far more frames than any fixed limit stay.
	GuiReplayHistory many;
	for (int t = 0; t < 9000 * GuiReplayHistory::cadenceSeconds; t += GuiReplayHistory::cadenceSeconds) {
		auto frame = std::make_shared<GuiSimulationSnapshot>();
		frame->timestep = t;
		frame->totalTimesteps = 9000 * GuiReplayHistory::cadenceSeconds + 1;
		many.record(frame);
	}
	require(many.size() == 9000 && !many.truncated() && many.firstTime() == 0, "the number of frames is limited");
	return 0;
}
