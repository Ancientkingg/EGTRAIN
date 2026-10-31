#include "simulation/GuiSimulationSnapshot.h"
#include "app/GuiReplayHistory.h"

#include <QObject>
#include <QTest>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#if defined(__SANITIZE_ADDRESS__)
#define TEST_ADDRESS_SANITIZER 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TEST_ADDRESS_SANITIZER 1
#endif
#endif

// Counts the bytes requested from the allocator, so that the accounted replay bytes can be
// compared with what the history really holds. AddressSanitizer keeps its own count: with it, a
// system library on macOS frees with the operator delete of the sanitizer what a replaced operator
// new returned. GCC has no <sanitizer/allocator_interface.h>, so the function is declared here.
#ifdef TEST_ADDRESS_SANITIZER
extern "C" std::size_t __sanitizer_get_current_allocated_bytes(); // NOLINT(bugprone-reserved-identifier): the name the runtime exports

namespace {
std::size_t heapBytes() {
	return __sanitizer_get_current_allocated_bytes();
}
}
#else
// GCC 15 follows the header arithmetic and the free of the replaced operators into its callers and reports an access
// before an array and a mismatched free, although every pointer here comes from the operator new below.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
namespace {
std::size_t liveHeapBytes = 0;
constexpr std::size_t headerBytes = 16;

std::size_t heapBytes() {
	return liveHeapBytes;
}
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
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif

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
	const auto& [index, id, type, description, operatingCode, serviceId, occurrence, routeIndex, reversedDirection, wagonCount, length,
		departureTime, outOfSimulation, routeAxisPosition, speedKmh, currentOnboardPassengers, maxOnboardPassengers, wagonHeadPositions,
		wagonTailPositions, occupiedArcs] = v;
	return std::make_tuple(index, id, type, description, operatingCode, serviceId, occurrence, routeIndex, reversedDirection, wagonCount,
		length, departureTime, outOfSimulation, routeAxisPosition, speedKmh, currentOnboardPassengers, maxOnboardPassengers,
		wagonHeadPositions, wagonTailPositions, arcFields(occupiedArcs));
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
		train.occurrence = 2 + n;
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


namespace {
// A train with an operating code, one occupied arc, a position and one wagon, due at 5 s.
GuiTrainState displayedTrain() {
	GuiTrainState train;
	train.description = "Intercity northbound";
	train.operatingCode = "1725";
	train.occupiedArcs.push_back({7, 2.5});
	train.routeAxisPosition = 25.0;
	train.departureTime = 5;
	train.wagonHeadPositions.push_back(25.0);
	train.wagonTailPositions.push_back(24.0);
	return train;
}

// Records the 600 steps of a run of sample snapshots.
void recordSampleRun(GuiReplayHistory& history) {
	for (int t = 0; t < 600; ++t)
		history.record(shared(sampleSnapshot(t, 3, 600)));
}

// A change of one field that is stored once per layout.
struct StaticChange {
	const char* name;
	void (*apply)(GuiSimulationSnapshot&);
};

const StaticChange kStaticChanges[] = {
	{"train index", [](GuiSimulationSnapshot& s) { s.trains[1].index += 10; }},
	{"train id", [](GuiSimulationSnapshot& s) { s.trains[1].id += 1.0; }},
	{"train type", [](GuiSimulationSnapshot& s) { s.trains[1].type += "x"; }},
	{"train description", [](GuiSimulationSnapshot& s) { s.trains[1].description += "x"; }},
	{"train operating code", [](GuiSimulationSnapshot& s) { s.trains[1].operatingCode += "x"; }},
	{"train service", [](GuiSimulationSnapshot& s) { s.trains[1].serviceId += "x"; }},
	{"train occurrence", [](GuiSimulationSnapshot& s) { s.trains[1].occurrence += 1; }},
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
}

class TestGuiSimulationSnapshot : public QObject {
	Q_OBJECT

private slots:
	void permissiveSignalling();
	void ordersSignalCodes();
	void mergeKeepsMostRestrictiveCode();
	void mergeKeepsDirectionsAndSections();
	void failureMarksBothDirections();
	void copyWithoutLevel();
	void displayCode();
	void trainDisplay();
	void mailbox();
	void replayLookup();
	void framesComeBackAsRecorded();
	void keepsFrameHandedOutLast();
	void sameFrameSameObject();
	void layoutChanges();
	void staticChange_data();
	void staticChange();
	void accountedBytes();
	void evictionKeepsNewestFrames();
	void evictedLayoutIsReleased();
	void largeSceneFits_data();
	void largeSceneFits();
	void countsServiceId();
	void countsSignalStates();
	void storesStringsOnce();
	void noFrameCountLimit();
	void movedHistory();
};

void TestGuiSimulationSnapshot::permissiveSignalling() {
	QVERIFY2(!guiSectionReportsPermissiveSignalling(0.0),
		"zero signalling code was reported as permissive signalling");
	// Nonzero signalling codes report permissive signalling.
	QVERIFY(guiSectionReportsPermissiveSignalling(1.0));
	QVERIFY(guiSectionReportsPermissiveSignalling(75.0));
	QVERIFY(guiSectionReportsPermissiveSignalling(180.0));
	QVERIFY(guiSectionReportsPermissiveSignalling(270.0));
	QVERIFY(guiSectionReportsPermissiveSignalling(751.0));
	GuiSimulationSnapshot operational;
	operational.sectionStates.push_back({"unused-nonzero", guiSectionReportsPermissiveSignalling(751.0), false});
	operational.sectionStates.push_back({"blocked", guiSectionReportsPermissiveSignalling(0.0), true});
	GuiTrainState occupiedTrain;
	occupiedTrain.occupiedArcs.push_back({7, 2.5});
	operational.trains.push_back(occupiedTrain);
	// Permissive signalling is not conflated with occupied or blocked state.
	QVERIFY(operational.sectionStates.at(0).prepared);
	QVERIFY(!operational.sectionStates.at(0).blocked);
	QCOMPARE(operational.trains.front().occupiedArcs.size(), std::size_t(1));
	// A blocked section remains independent of permissive signalling.
	QVERIFY(operational.sectionStates.at(1).blocked);
	QVERIFY(!operational.sectionStates.at(1).prepared);
}

// Route copies of one section and direction merge into the most restrictive
// code, whatever the order they arrive in.
void TestGuiSimulationSnapshot::ordersSignalCodes() {
	// Signal codes are ordered from stop to clear.
	QCOMPARE_LT(guiSignalRestriction(0), guiSignalRestriction(751));
	QCOMPARE_LT(guiSignalRestriction(751), guiSignalRestriction(75));
	QCOMPARE_LT(guiSignalRestriction(75), guiSignalRestriction(180));
	QCOMPARE_LT(guiSignalRestriction(180), guiSignalRestriction(270));
	QCOMPARE_LT(guiSignalRestriction(270), guiSignalRestriction(-1));
	QCOMPARE(guiSignalRestriction(-1), guiSignalRestriction(1000));
	// Signalling levels are 0 to 5.
	QVERIFY(guiSignalHasLevel(0));
	QVERIFY(guiSignalHasLevel(5));
	QVERIFY(!guiSignalHasLevel(6));
	QVERIFY(!guiSignalHasLevel(kGuiSignalNoLevel));
	QVERIFY(!guiSignalHasLevel(-99999999));
}

void TestGuiSimulationSnapshot::mergeKeepsMostRestrictiveCode() {
	std::array<int, 4> codes{270, 75, 180, 751};
	std::sort(codes.begin(), codes.end());
	do {
		GuiSignalStateList copies;
		for (const int code : codes)
			copies.merge("@1-B0@", false, code, 0);
		const auto merged = copies.take();
		// Merge keeps the most restrictive code in every order.
		QCOMPARE(merged.size(), std::size_t(1));
		QCOMPARE(merged.front().code, 751);
	} while (std::next_permutation(codes.begin(), codes.end()));
}

// Directions and sections stay separate, in the order they first appear.
void TestGuiSimulationSnapshot::mergeKeepsDirectionsAndSections() {
	GuiSignalStateList routes;
	routes.merge("@2-B0@", false, 75, 2);
	routes.merge("@1-B0@", false, 270, 2);
	routes.merge("@1-B0@", true, 0, 2);
	routes.merge("@1-B0@", false, 180, 2);
	routes.merge("@1-B0@", true, 270, 2);
	const auto separate = routes.take();
	// Directions and sections are not merged.
	QCOMPARE(separate.size(), std::size_t(3));
	// A direction does not take the code of the other direction.
	QCOMPARE(separate[0].sectionId, std::string("@2-B0@"));
	QCOMPARE(separate[0].code, 75);
	QVERIFY(!separate[0].reversedDirection);
	QCOMPARE(separate[1].sectionId, std::string("@1-B0@"));
	QCOMPARE(separate[1].code, 180);
	QVERIFY(!separate[1].reversedDirection);
	QCOMPARE(separate[2].sectionId, std::string("@1-B0@"));
	QCOMPARE(separate[2].code, 0);
	QVERIFY(separate[2].reversedDirection);
}

// A clear copy does not hide a stop, and a failure marks both directions.
void TestGuiSimulationSnapshot::failureMarksBothDirections() {
	GuiSignalStateList failure;
	failure.merge("@1-B0@", false, 0, 0);
	failure.merge("@1-B0@", false, 270, 0);
	failure.merge("@1-B0@", true, 270, 0);
	failure.merge("@2-B0@", false, 270, 0);
	failure.fail("@1-B0@");
	failure.fail("@3-B0@");
	const auto failed = failure.take();
	// A clear copy does not hide a stop, and a failure does not miss a direction.
	QCOMPARE(failed.size(), std::size_t(3));
	QCOMPARE(failed[0].code, 0);
	QVERIFY(failed[0].failed);
	QVERIFY(failed[1].failed);
	QVERIFY(failed[1].reversedDirection);
	QVERIFY(!failed[2].failed);
}

// A copy without a level has no signalling and never hides one that has.
void TestGuiSimulationSnapshot::copyWithoutLevel() {
	GuiSignalStateList levels;
	levels.merge("@1-B0@", false, 270, kGuiSignalNoLevel);
	levels.merge("@1-B0@", false, 75, 1);
	levels.merge("@1-B0@", false, 0, kGuiSignalNoLevel);
	const auto withLevel = levels.take();
	// A copy without a level does not replace or hide one with a level.
	QCOMPARE(withLevel.size(), std::size_t(1));
	QCOMPARE(withLevel.front().level, 1);
	QCOMPARE(withLevel.front().code, 75);
	GuiSignalStateList noLevel;
	noLevel.merge("@1-B0@", false, 270, kGuiSignalNoLevel);
	noLevel.merge("@1-B0@", false, 270, 99999999);
	const auto without = noLevel.take();
	// A section without a level is not given one.
	QCOMPARE(without.size(), std::size_t(1));
	QCOMPARE(without.front().level, kGuiSignalNoLevel);
	// A signal state starts without a level and not failed.
	QCOMPARE(GuiSignalState().level, kGuiSignalNoLevel);
	QVERIFY(!GuiSignalState().failed);
	QVERIFY2(GuiSignalStateList().take().empty(), "an empty list has entries");
}

// A head shows the code of a section with a level, and unavailable without one.
void TestGuiSimulationSnapshot::displayCode() {
	QCOMPARE(guiSignalDisplayCode({"@1-B0@", 75, false, 0, false}), 75);
	QCOMPARE(guiSignalDisplayCode({"@1-B0@", 751, true, 5, false}), 751);
	QCOMPARE(guiSignalDisplayCode({"@1-B0@", 270, false, kGuiSignalNoLevel, false}), kGuiSignalUnavailable);
	QCOMPARE(guiSignalDisplayCode({"@1-B0@", 0, false, 99999999, false}), kGuiSignalUnavailable);
}

void TestGuiSimulationSnapshot::trainDisplay() {
	GuiTrainState activeTrain;
	activeTrain.description = "Intercity northbound";
	// The train description is the fallback.
	QCOMPARE(guiTrainDisplayIdentifier(activeTrain), activeTrain.description);
	activeTrain.operatingCode = "1725";
	// The operating code is preferred.
	QCOMPARE(guiTrainDisplayIdentifier(activeTrain), activeTrain.operatingCode);
	activeTrain.occupiedArcs.push_back({7, 2.5});
	GuiTrainState exitedTrain = activeTrain;
	exitedTrain.outOfSimulation = true;
	QVERIFY2(guiTrainPublishesOccupiedArcs(activeTrain),
		"active train occupation was filtered");
	QVERIFY2(!guiTrainPublishesOccupiedArcs(exitedTrain),
		"out-of-simulation train occupation was retained");
	activeTrain.routeAxisPosition = 25.0;
	activeTrain.departureTime = 5;
	activeTrain.wagonHeadPositions.push_back(25.0);
	activeTrain.wagonTailPositions.push_back(24.0);
	exitedTrain = activeTrain;
	exitedTrain.outOfSimulation = true;
	// Replay train visibility follows departure and exit.
	QVERIFY(!guiReplayTrainHasPosition(activeTrain, 4));
	QVERIFY(guiReplayTrainHasPosition(activeTrain, 5));
	QVERIFY(!guiReplayTrainHasPosition(exitedTrain, 5));
	activeTrain.routeAxisPosition = -9999.0;
	QVERIFY2(!guiReplayTrainHasPosition(activeTrain, 5), "replay accepted sentinel geometry");
}

void TestGuiSimulationSnapshot::mailbox() {
	GuiSimulationSnapshot first;
	first.timestep = 1;
	first.trains.push_back(GuiTrainState{});
	first.trains.front().description = "first";
	first.sectionStates.push_back({"section-a", true, true});
	first.sectionStates.push_back({"section-b", false, false});
	std::set<std::string> sectionIds;
	for (const GuiSectionState& section : first.sectionStates)
		sectionIds.insert(section.sectionId);
	// Section states contain no duplicate IDs.
	QCOMPARE(sectionIds.size(), first.sectionStates.size());
	// Section state flags are retained.
	QVERIFY(first.sectionStates.front().prepared);
	QVERIFY(first.sectionStates.front().blocked);
	auto firstSnapshot = std::make_shared<const GuiSimulationSnapshot>(first);

	GuiSimulationSnapshot second;
	second.timestep = 2;
	second.trains.push_back(GuiTrainState{});
	second.trains.front().description = "second";
	second.sectionStates.push_back({"section-a", false, true});
	auto secondSnapshot = std::make_shared<const GuiSimulationSnapshot>(second);

	GuiSimulationSnapshotMailbox mailbox;
	QVERIFY2(mailbox.publish(firstSnapshot), "first publish did not request a notification");
	QVERIFY2(!mailbox.publish(secondSnapshot), "second publish requested a duplicate notification");

	second.trains.front().description = "mutated source";
	const auto latest = mailbox.take();
	if (latest == nullptr)
		QFAIL("take returned no snapshot");
	// Take returns the latest snapshot.
	QCOMPARE(latest->timestep, 2);
	// The snapshot does not change after the source is changed.
	QCOMPARE(latest->trains.front().description, std::string("second"));
	// The section state payload is copied into the immutable snapshot.
	QCOMPARE(latest->sectionStates.size(), std::size_t(1));
	QCOMPARE(latest->sectionStates.front().sectionId, std::string("section-a"));

	QVERIFY2(mailbox.publish(firstSnapshot), "publish after take did not request a notification");
	// The second take returns the snapshot published after the first.
	QCOMPARE(mailbox.take()->timestep, 1);
}

void TestGuiSimulationSnapshot::replayLookup() {
	GuiReplayHistory history;
	QVERIFY2(!history.atOrBefore(4), "empty replay selected a frame");
	// An empty replay is empty.
	QVERIFY(history.empty());
	QCOMPARE(history.payloadBytes(), std::size_t(0));
	QCOMPARE(history.size(), std::size_t(0));
	QVERIFY(!history.truncated());
	QCOMPARE(history.evictedBeforeTime(), 0);
	QCOMPARE(history.budgetBytes(), GuiReplayHistory::payloadLimit);
	const GuiTrainState activeTrain = displayedTrain();
	for (int t = 0; t <= 12; ++t) {
		auto frame = std::make_shared<GuiSimulationSnapshot>();
		frame->timestep = t;
		frame->totalTimesteps = 13;
		frame->trains.push_back(activeTrain);
		history.record(frame);
	}
	// The first, cadence and final samples are retained.
	QCOMPARE(history.size(), std::size_t(4));
	QCOMPARE(history.firstTime(), 0);
	QCOMPARE(history.lastTime(), 12);
	// Replay lookup clamps and floors.
	QCOMPARE(history.atOrBefore(-1)->timestep, 0);
	QCOMPARE(history.atOrBefore(7)->timestep, 5);
	QCOMPARE(history.atOrBefore(99)->timestep, 12);
	history.clear();
	// A replacement retains no old frames.
	QVERIFY(history.empty());
	QVERIFY(!history.truncated());
	QCOMPARE(history.payloadBytes(), std::size_t(0));
	QCOMPARE(history.layoutCount(), std::size_t(0));
	auto only = std::make_shared<GuiSimulationSnapshot>();
	only->timestep = 3;
	only->totalTimesteps = 4;
	history.record(only);
	// A one-frame replay works.
	QCOMPARE(history.size(), std::size_t(1));
	QCOMPARE(history.atOrBefore(0)->timestep, 3);
}

// Every field of a frame with trains, signals, sections, platforms, passengers and messages
// comes back, for many frames with changing values.
void TestGuiSimulationSnapshot::framesComeBackAsRecorded() {
	GuiReplayHistory history;
	std::vector<GuiSimulationSnapshot> recorded;
	for (int t = 0; t < 600; ++t) {
		recorded.push_back(sampleSnapshot(t, 3, 600));
		history.record(shared(recorded.back()));
	}
	// The run keeps every cadence frame under one layout.
	QCOMPARE(history.size(), std::size_t(121));
	QCOMPARE(history.firstTime(), 0);
	QCOMPARE(history.lastTime(), 599);
	QVERIFY(!history.truncated());
	QCOMPARE(history.evictedBeforeTime(), 0);
	QCOMPARE(history.layoutCount(), std::size_t(1));
	for (const GuiSimulationSnapshot& expected : recorded) {
		if (expected.timestep % 5 != 0 && expected.timestep != 599)
			continue;
		const auto back = history.atOrBefore(expected.timestep);
		// A frame comes back as recorded.
		QVERIFY(back);
		QVERIFY(sameSnapshot(*back, expected));
		const auto between = history.atOrBefore(expected.timestep + 1);
		// A time between frames selects the frame before it.
		QVERIFY(between);
		QVERIFY(expected.timestep == 599 || between->timestep == expected.timestep);
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
	// The sample snapshot leaves no field at its default.
	QVERIFY(sawReversed);
	QVERIFY(sawExited);
	QVERIFY(sawArcs);
	QVERIFY(sawStale);
	QVERIFY(sawLevel);
	QVERIFY(sawNoLevel);
	QVERIFY(sawFailed);
	QVERIFY(sawPrepared);
	QVERIFY(sawBlocked);
	QVERIFY(sawPlatformIds);
	QVERIFY(sawNextTrain);
	QVERIFY(sawNoNextTrain);
	QVERIFY(sawMessages);
	// The stale wagon positions of an exited train and the messages are kept.
	QVERIFY(history.atOrBefore(300)->trains[2].outOfSimulation);
	QCOMPARE(history.atOrBefore(300)->trains[2].wagonHeadPositions.size(), std::size_t(6));
	QCOMPARE(history.atOrBefore(300)->virtualCouplingMessages.size(), std::size_t(2));
}

// The history itself keeps the frame it handed out last, so a caller that drops the result
// can ask again and gets the same object, and the cache moves with the latest request.
void TestGuiSimulationSnapshot::keepsFrameHandedOutLast() {
	GuiReplayHistory history;
	recordSampleRun(history);
	const std::weak_ptr<const GuiSimulationSnapshot> last = history.atOrBefore(100);
	// The frame handed out last is kept.
	QVERIFY(!last.expired());
	QVERIFY(last.lock() == history.atOrBefore(104));
	history.atOrBefore(105);
	QVERIFY2(last.expired(), "the cache kept more than one frame");
}

// Asking for the same frame twice returns the same object, another frame another one.
void TestGuiSimulationSnapshot::sameFrameSameObject() {
	GuiReplayHistory history;
	recordSampleRun(history);
	const auto a = history.atOrBefore(100);
	const auto b = history.atOrBefore(104);
	// The same frame is not rebuilt twice.
	QVERIFY(a == b);
	QVERIFY(a == history.atOrBefore(100));
	const auto c = history.atOrBefore(105);
	// A different frame does not return the cached object.
	QVERIFY(c != a);
	QCOMPARE(c->timestep, 105);
	QVERIFY(history.atOrBefore(105) == c);
	// A frame that the caller still holds is not rebuilt after other frames were asked for.
	for (int t = 110; t < 300; t += 5) history.atOrBefore(t);
	QVERIFY(history.atOrBefore(100) == a);
	QVERIFY(history.atOrBefore(105) == c);
}

// A run in which the number of trains and a static field change starts a new layout; frames
// of both layouts come back.
void TestGuiSimulationSnapshot::layoutChanges() {
	GuiReplayHistory history;
	std::vector<GuiSimulationSnapshot> mixed;
	for (int t = 0; t < 40; t += 5) {
		mixed.push_back(sampleSnapshot(t, t < 15 ? 2 : 4, 40));
		if (t >= 30)
			mixed.back().trains[0].description = "renamed";
		history.record(shared(mixed.back()));
	}
	// A change of the static data starts a layout.
	QCOMPARE(history.size(), std::size_t(8));
	QCOMPARE(history.layoutCount(), std::size_t(3));
	for (const GuiSimulationSnapshot& expected : mixed)
		QVERIFY2(sameSnapshot(*history.atOrBefore(expected.timestep), expected), "a frame of a changed layout did not come back");
	// A layout does not leak into a frame of another.
	QCOMPARE(history.atOrBefore(0)->trains.size(), std::size_t(2));
	QCOMPARE(history.atOrBefore(20)->trains.size(), std::size_t(4));
	QCOMPARE(history.atOrBefore(35)->trains.front().description, std::string("renamed"));
	QCOMPARE(history.atOrBefore(25)->trains.front().description, std::string("train 0"));
}

// A change of any one field that is stored once per layout starts a new layout, and both
// frames come back as recorded. A field that is missing from the comparison would be restored
// from the layout of the first frame.
void TestGuiSimulationSnapshot::staticChange_data() {
	QTest::addColumn<int>("change");
	for (int i = 0; i < static_cast<int>(std::size(kStaticChanges)); ++i)
		QTest::newRow(kStaticChanges[i].name) << i;
}

void TestGuiSimulationSnapshot::staticChange() {
	QFETCH(int, change);
	GuiReplayHistory single;
	GuiSimulationSnapshot before = sampleSnapshot(0, 3, 40);
	GuiSimulationSnapshot after = sampleSnapshot(5, 3, 40);
	kStaticChanges[change].apply(after);
	single.record(shared(before));
	single.record(shared(after));
	// The replay keeps the change.
	QCOMPARE(single.size(), std::size_t(2));
	QVERIFY(sameSnapshot(*single.atOrBefore(0), before));
	QVERIFY(sameSnapshot(*single.atOrBefore(5), after));
	QCOMPARE(single.layoutCount(), std::size_t(2));
}

// The accounted bytes grow with every frame and are close to what the history holds.
void TestGuiSimulationSnapshot::accountedBytes() {
	GuiReplayHistory history;
	const std::size_t heapBefore = heapBytes();
	std::vector<std::shared_ptr<const GuiSimulationSnapshot>> large;
	for (int t = 0; t < 400; t += 5) large.push_back(shared(sceneSnapshot(t, 40, 1350, 700, 400)));
	const std::size_t heapWithInput = heapBytes();
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
	const std::size_t held = heapBytes() - heapWithInput;
	const std::size_t accounted = history.payloadBytes();
	// The accounted bytes grow with the frames.
	QVERIFY(grows);
	QCOMPARE(history.size(), large.size());
	// The accounted bytes differ from the bytes held by less than 10 percent.
	QCOMPARE_GT(accounted, held - held / 10);
	QCOMPARE_LT(accounted, held + held / 10);
	// One frame is much smaller than the first, which also holds the layout: frames do not repeat
	// the static data of the run.
	QCOMPARE(history.layoutCount(), std::size_t(1));
	QCOMPARE_LT(2 * lastStep, firstStep);
	// A frame of a scene of this size needs far less than the snapshot it was made from: the compact
	// form is much smaller than the snapshots.
	QCOMPARE_GT(heapWithInput - heapBefore, 3 * held);
	large.clear();
	history.clear();
	// Clearing resets the accounted bytes.
	QCOMPARE(history.payloadBytes(), std::size_t(0));
}

// Eviction under a small budget keeps the newest frames and says what it covers.
void TestGuiSimulationSnapshot::evictionKeepsNewestFrames() {
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
	// A small budget evicts.
	QVERIFY(small.truncated());
	QCOMPARE_GE(small.size(), std::size_t(3));
	QCOMPARE_LT(small.size(), times.size() / 2);
	QCOMPARE_LE(small.payloadBytes(), small.budgetBytes());
	// Eviction keeps the newest frames and reports the covered interval.
	QCOMPARE(small.lastTime(), 499);
	QCOMPARE(small.firstTime(), times[times.size() - small.size()]);
	QCOMPARE(small.evictedBeforeTime(), small.firstTime());
	QCOMPARE_GT(small.firstTime(), 0);
	// A time before the covered interval selects its first frame.
	QCOMPARE(small.atOrBefore(0)->timestep, small.firstTime());
	QCOMPARE(small.atOrBefore(499)->timestep, 499);
	for (std::size_t k = times.size() - small.size(); k < times.size(); ++k)
		QVERIFY2(sameSnapshot(*small.atOrBefore(times[k]), run[static_cast<std::size_t>(times[k])]), "a frame kept after eviction changed");
	// Eviction does not multiply the layouts.
	QCOMPARE(small.layoutCount(), std::size_t(1));
}

// A layout of evicted frames is released and no longer counted.
void TestGuiSimulationSnapshot::evictedLayoutIsReleased() {
	GuiReplayHistory later;
	for (int t = 50; t < 100; t += 5) later.record(shared(sampleSnapshot(t, 6, 100)));
	GuiReplayHistory trade(later.payloadBytes());
	for (int t = 0; t < 100; t += 5) trade.record(shared(sampleSnapshot(t, t < 50 ? 2 : 6, 100)));
	// The layout of evicted frames is not kept.
	QVERIFY(trade.truncated());
	QCOMPARE(trade.layoutCount(), std::size_t(1));
	QCOMPARE_GE(trade.firstTime(), 50);
	QCOMPARE_GE(trade.size(), std::size_t(5));
	QCOMPARE_LE(trade.payloadBytes(), trade.budgetBytes());
	QCOMPARE(trade.atOrBefore(0)->trains.size(), std::size_t(6));
	QCOMPARE(trade.atOrBefore(95)->trains.size(), std::size_t(6));
}

// Frames with the shape of Copenhagen and of the Netherlands scene fit the default budget.
void TestGuiSimulationSnapshot::largeSceneFits_data() {
	QTest::addColumn<int>("trainCount");
	QTest::addColumn<int>("signalCount");
	QTest::addColumn<int>("sectionCount");
	QTest::addColumn<int>("frames");
	QTest::newRow("Copenhagen") << 196 << 1331 << 1265 << 1601;
	QTest::newRow("Netherlands") << 40 << 1353 << 700 << 1601;
}

void TestGuiSimulationSnapshot::largeSceneFits() {
	QFETCH(int, trainCount);
	QFETCH(int, signalCount);
	QFETCH(int, sectionCount);
	QFETCH(int, frames);
	const int total = (frames - 1) * GuiReplayHistory::cadenceSeconds + 1;
	const auto scene = [&](int t) {
		return sceneSnapshot(t, static_cast<std::size_t>(trainCount), static_cast<std::size_t>(signalCount), static_cast<std::size_t>(sectionCount), total);
	};
	GuiReplayHistory wholeRun;
	for (int t = 0; t < total; t += GuiReplayHistory::cadenceSeconds)
		wholeRun.record(shared(scene(t)));
	// A full run of a large scene fits the replay budget.
	QVERIFY(!wholeRun.truncated());
	QCOMPARE(wholeRun.firstTime(), 0);
	QCOMPARE(wholeRun.size(), static_cast<std::size_t>(frames));
	QCOMPARE_LT(wholeRun.payloadBytes(), GuiReplayHistory::payloadLimit);
	QCOMPARE(wholeRun.layoutCount(), std::size_t(1));
	// A frame of a large scene comes back as recorded.
	QVERIFY(sameSnapshot(*wholeRun.atOrBefore(0), scene(0)));
	QVERIFY(sameSnapshot(*wholeRun.atOrBefore(4000), scene(4000)));
}

void TestGuiSimulationSnapshot::countsServiceId() {
	GuiReplayHistory history;
	auto withoutService = std::make_shared<GuiSimulationSnapshot>();
	withoutService->trains.push_back(GuiTrainState{});
	history.record(withoutService);
	const std::size_t bytesWithoutService = history.payloadBytes();
	history.clear();
	auto withService = std::make_shared<GuiSimulationSnapshot>();
	withService->trains.push_back(GuiTrainState{});
	withService->trains.front().serviceId = std::string(1000, 's');
	history.record(withService);
	// The service id is part of the train state and of the replay byte count.
	QCOMPARE(history.atOrBefore(0)->trains.front().serviceId, std::string(1000, 's'));
	QCOMPARE_GE(history.payloadBytes(), bytesWithoutService + 900);
}

void TestGuiSimulationSnapshot::countsSignalStates() {
	GuiReplayHistory history;
	auto withSignals = std::make_shared<GuiSimulationSnapshot>();
	withSignals->signalStates.resize(1000);
	for (auto& signal : withSignals->signalStates)
		signal.sectionId = std::string(100, 'x');
	history.record(withSignals);
	// The signal states are part of the replay byte count.
	QCOMPARE_GE(history.payloadBytes(), 1000 * (sizeof(GuiSignalState) + 100));
}

// Strings that vary are stored once, so repeated passenger frames stay small, and a frame
// that cannot fit the budget turns replay off.
void TestGuiSimulationSnapshot::storesStringsOnce() {
	GuiReplayHistory passengersOnly(8u << 20);
	auto crowd = std::make_shared<GuiSimulationSnapshot>();
	crowd->passengers.resize(10000);
	for (auto& passenger : crowd->passengers) passenger.id = std::string(1000, 'p');
	for (int t = 0; t < 12; ++t) {
		auto frame = std::make_shared<GuiSimulationSnapshot>(*crowd);
		frame->timestep = t * GuiReplayHistory::cadenceSeconds;
		passengersOnly.record(frame);
	}
	// Equal strings are stored once, not once per passenger.
	QVERIFY(!passengersOnly.truncated());
	QCOMPARE(passengersOnly.size(), std::size_t(12));
	QCOMPARE_LE(passengersOnly.payloadBytes(), passengersOnly.budgetBytes());
	crowd->passengers.front().id = std::string(passengersOnly.budgetBytes(), 'x');
	passengersOnly.record(crowd);
	// An oversize frame disables replay.
	QVERIFY(passengersOnly.oversize());
	QVERIFY(passengersOnly.empty());
	QCOMPARE(passengersOnly.payloadBytes(), std::size_t(0));
	QVERIFY(!passengersOnly.atOrBefore(0));
	// A small frame is refused too once replay is off for the run.
	auto tiny = std::make_shared<GuiSimulationSnapshot>();
	tiny->timestep = 500;
	passengersOnly.record(tiny);
	// A history without replay records no frame.
	QVERIFY(passengersOnly.empty());
	QVERIFY(passengersOnly.oversize());
}

// There is no frame count limit: far more frames than any fixed limit stay.
void TestGuiSimulationSnapshot::noFrameCountLimit() {
	GuiReplayHistory many;
	for (int t = 0; t < 9000 * GuiReplayHistory::cadenceSeconds; t += GuiReplayHistory::cadenceSeconds) {
		auto frame = std::make_shared<GuiSimulationSnapshot>();
		frame->timestep = t;
		frame->totalTimesteps = 9000 * GuiReplayHistory::cadenceSeconds + 1;
		many.record(frame);
	}
	// The number of frames is not limited.
	QCOMPARE(many.size(), std::size_t(9000));
	QVERIFY(!many.truncated());
	QCOMPARE(many.firstTime(), 0);
}

// The window receives the history by move. A moved history gives the same frames back, and it
// still records: the string table of the new object serves a frame that is recorded afterwards.
void TestGuiSimulationSnapshot::movedHistory() {
	GuiReplayHistory source;
	std::vector<GuiSimulationSnapshot> kept;
	for (int t = 0; t < 50; t += GuiReplayHistory::cadenceSeconds) {
		kept.push_back(sampleSnapshot(t, 3, 600));
		source.record(shared(kept.back()));
	}
	GuiReplayHistory moved(std::move(source));
	GuiReplayHistory assigned;
	assigned = std::move(moved);
	kept.push_back(sampleSnapshot(50, 3, 600));
	assigned.record(shared(kept.back()));
	// A moved history keeps its frames and takes a new one.
	QCOMPARE(assigned.size(), kept.size());
	for (const GuiSimulationSnapshot& expected : kept) {
		const auto back = assigned.atOrBefore(expected.timestep);
		// A frame of a moved history comes back as recorded.
		QVERIFY(back);
		QVERIFY(sameSnapshot(*back, expected));
	}
}

QTEST_APPLESS_MAIN(TestGuiSimulationSnapshot)
#include "test_guisimulationsnapshot.moc"
