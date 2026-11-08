#include "simulation/GuiSimulationSnapshot.h"
#include "app/GuiReplayHistory.h"
#include "simulation/RollingStock.h"
#include "simulation/Signalling.h"
#include "simulation/SimulationRun.h"
#include "support/SnapshotCompare.h"

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
		train.routeIndex = n % 3;
		train.reversedDirection = n % 2 == 1;
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

// The fields that the history derives from the train source, and the messages it takes from it, go back to their
// defaults. Without a train source the replay leaves them at the defaults, so a recorded snapshot and a replay compare
// equal in everything else.
GuiSimulationSnapshot withoutDerived(GuiSimulationSnapshot snapshot) {
	for (GuiTrainState& train : snapshot.trains) {
		train.routeAxisPosition = -9999.0;
		train.speedKmh = 0.0;
		train.wagonHeadPositions.clear();
		train.wagonTailPositions.clear();
	}
	snapshot.virtualCouplingMessages.clear();
	return snapshot;
}

// Every field of the replay equals the recorded snapshot, but for the derived ones.
bool sameRecorded(const GuiSimulationSnapshot& replay, const GuiSimulationSnapshot& recorded) {
	return sameSnapshot(withoutDerived(replay), withoutDerived(recorded));
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

// A change of one field that is stored once for the whole run.
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
	{"train route", [](GuiSimulationSnapshot& s) { s.trains[1].routeIndex += 1; }},
	{"train direction", [](GuiSimulationSnapshot& s) { s.trains[1].reversedDirection = !s.trains[1].reversedDirection; }},
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
	{"passenger id", [](GuiSimulationSnapshot& s) { s.passengers[1].id += "x"; }},
	{"total steps", [](GuiSimulationSnapshot& s) { s.totalTimesteps += 1; }},
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
	void emptyAndSingleSecond();
	void secondsComeBackAsRecorded();
	void valueChangesBackWithinFiveSeconds();
	void seekBetweenKeyframes();
	void newRunReplacesHistory();
	void evictsWholeIntervals();
	void staticChangeRestarts_data();
	void staticChangeRestarts();
	void countChangeRestarts();
	void trainsDerivedFromSource();
	void virtualCouplingFromSource();
	void virtualCouplingFromRun();
	void keepsSecondHandedOutLast();
	void sameSecondSameObject();
	void accountedBytes();
	void copenhagenSizedRunFits();
	void storesStringsOnce();
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


// An empty history is empty, and a history of one second works.
void TestGuiSimulationSnapshot::emptyAndSingleSecond() {
	GuiReplayHistory history;
	QVERIFY2(!history.at(4), "empty replay selected a second");
	QVERIFY(history.empty());
	QCOMPARE(history.payloadBytes(), std::size_t(0));
	QCOMPARE(history.sharedBytes(), std::size_t(0));
	QCOMPARE(history.intervalCount(), std::size_t(0));
	QVERIFY(!history.truncated());
	QVERIFY(!history.oversize());
	QVERIFY(!history.trains());
	QCOMPARE(history.evictedBeforeTime(), 0);
	QCOMPARE(history.budgetBytes(), GuiReplayHistory::payloadLimit);
	auto only = std::make_shared<GuiSimulationSnapshot>();
	only->timestep = 3;
	only->totalTimesteps = 4;
	only->trains.push_back(displayedTrain());
	history.record(only);
	// A one-second replay works, and every time selects that second.
	QCOMPARE(history.intervalCount(), std::size_t(1));
	QCOMPARE(history.firstTime(), 3);
	QCOMPARE(history.lastTime(), 3);
	QCOMPARE(history.at(0)->timestep, 3);
	QCOMPARE(history.at(99)->timestep, 3);
	history.clear();
	QVERIFY(history.empty());
	QCOMPARE(history.payloadBytes(), std::size_t(0));
}

// Every second of a run with trains, signals, sections, platforms and passengers comes back as recorded, in both
// intervals, with changes of every kind in between.
void TestGuiSimulationSnapshot::secondsComeBackAsRecorded() {
	GuiReplayHistory history;
	std::vector<GuiSimulationSnapshot> recorded;
	for (int t = 0; t < 600; ++t) {
		recorded.push_back(sampleSnapshot(t, 3, 600));
		history.record(shared(recorded.back()));
	}
	// Two intervals, from 0 and from 300.
	QCOMPARE(history.intervalCount(), std::size_t(2));
	QCOMPARE(history.firstTime(), 0);
	QCOMPARE(history.lastTime(), 599);
	QVERIFY(!history.truncated());
	QCOMPARE(history.evictedBeforeTime(), 0);
	for (const GuiSimulationSnapshot& expected : recorded) {
		const auto back = history.at(expected.timestep);
		QVERIFY(back);
		QCOMPARE(back->timestep, expected.timestep);
		QVERIFY2(sameRecorded(*back, expected), qPrintable(QString("second %1 did not come back").arg(expected.timestep)));
	}
	// The sample sets every recorded field to something other than its default in some second.
	bool sawExited = false, sawArcs = false, sawLevel = false, sawNoLevel = false;
	bool sawFailed = false, sawPrepared = false, sawBlocked = false;
	bool sawPlatformIds = false, sawNextTrain = false, sawNoNextTrain = false;
	for (const GuiSimulationSnapshot& snapshot : recorded) {
		for (const GuiTrainState& train : snapshot.trains) {
			sawExited = sawExited || train.outOfSimulation;
			sawArcs = sawArcs || !train.occupiedArcs.empty();
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
	}
	QVERIFY(sawExited);
	QVERIFY(sawArcs);
	QVERIFY(sawLevel);
	QVERIFY(sawNoLevel);
	QVERIFY(sawFailed);
	QVERIFY(sawPrepared);
	QVERIFY(sawBlocked);
	QVERIFY(sawPlatformIds);
	QVERIFY(sawNextTrain);
	QVERIFY(sawNoNextTrain);
	QVERIFY(history.at(300)->trains[2].outOfSimulation);
	// Without a train source the derived fields keep their defaults.
	QCOMPARE(history.at(300)->trains[0].routeAxisPosition, -9999.0);
	QVERIFY(history.at(300)->trains[0].wagonHeadPositions.empty());
	QVERIFY(history.at(300)->virtualCouplingMessages.empty());
}

// A value that changes at second 7 and changes back at second 9 shows in the seconds 7 and 8 and nowhere else. A replay
// that keeps a snapshot only every few seconds would miss it.
void TestGuiSimulationSnapshot::valueChangesBackWithinFiveSeconds() {
	const GuiSimulationSnapshot base = sampleSnapshot(0, 3, 13);
	const auto stateAt = [&base](int t) {
		GuiSimulationSnapshot snapshot = base;
		snapshot.timestep = t;
		if (t == 7 || t == 8) {
			snapshot.signalStates[1].code = 751;
			snapshot.sectionStates[2].blocked = !snapshot.sectionStates[2].blocked;
			snapshot.platforms[0].passengerIds.push_back("late passenger");
			snapshot.passengers[0].status = "boarding";
			snapshot.trains[1].currentOnboardPassengers += 17;
		}
		return snapshot;
	};
	GuiReplayHistory history;
	for (int t = 0; t <= 12; ++t) history.record(shared(stateAt(t)));
	QVERIFY(history.at(6)->signalStates[1].code != 751);
	for (int t = 6; t <= 10; ++t) {
		const auto replay = history.at(t);
		QCOMPARE(replay->timestep, t);
		QVERIFY2(sameRecorded(*replay, stateAt(t)), qPrintable(QString("second %1 is not as recorded").arg(t)));
	}
	const auto changed = history.at(7);
	QCOMPARE(changed->signalStates[1].code, 751);
	QCOMPARE(changed->sectionStates[2].blocked, !base.sectionStates[2].blocked);
	QCOMPARE(changed->platforms[0].passengerIds.back(), std::string("late passenger"));
	QCOMPARE(changed->passengers[0].status, std::string("boarding"));
	QCOMPARE(changed->trains[1].currentOnboardPassengers, base.trains[1].currentOnboardPassengers + 17);
	QCOMPARE(history.at(9)->signalStates[1].code, base.signalStates[1].code);
	QCOMPARE(history.at(9)->trains[1].currentOnboardPassengers, base.trains[1].currentOnboardPassengers);
}

// With an interval of 10 seconds, a second between two keyframes, one on a keyframe and one before a keyframe are
// rebuilt right, also after seeking backwards.
void TestGuiSimulationSnapshot::seekBetweenKeyframes() {
	GuiReplayHistory history(GuiReplayHistory::payloadLimit, 10);
	std::vector<GuiSimulationSnapshot> recorded;
	for (int t = 0; t <= 35; ++t) {
		recorded.push_back(sampleSnapshot(t, 3, 36));
		history.record(shared(recorded.back()));
	}
	QCOMPARE(history.intervalCount(), std::size_t(4));
	for (const int t : {17, 23, 10, 19, 20, 35, 3, 29, 0, 30, 9, 31}) {
		const auto replay = history.at(t);
		QCOMPARE(replay->timestep, t);
		QVERIFY2(sameRecorded(*replay, recorded[static_cast<std::size_t>(t)]), qPrintable(QString("second %1 is not as recorded").arg(t)));
	}
}

// What the window does when the next run starts: the history is cleared, which releases its train source, and the next
// run shows only itself.
void TestGuiSimulationSnapshot::newRunReplacesHistory() {
	GuiReplayHistory history;
	auto source = std::make_shared<GuiReplayTrains>();
	const std::weak_ptr<const GuiReplayTrains> weakSource = source;
	for (int t = 0; t < 40; ++t) history.record(shared(sampleSnapshot(t, 3, 40)));
	history.setTrains(std::move(source));
	QVERIFY(history.trains());
	QVERIFY(!weakSource.expired());
	const auto held = history.at(10);
	history.clear();
	QVERIFY(history.empty());
	QVERIFY(!history.trains());
	QVERIFY(weakSource.expired());
	QCOMPARE(history.payloadBytes(), std::size_t(0));
	QVERIFY(!history.at(10));
	QCOMPARE(held->trains.size(), std::size_t(3));
	// A second run with other counts.
	std::vector<GuiSimulationSnapshot> next;
	for (int t = 0; t < 25; ++t) {
		next.push_back(sampleSnapshot(t, 5, 25));
		next.back().sectionStates.pop_back();
		history.record(shared(next.back()));
	}
	QCOMPARE(history.firstTime(), 0);
	QCOMPARE(history.lastTime(), 24);
	QVERIFY(!history.truncated());
	QCOMPARE(history.at(12)->trains.size(), std::size_t(5));
	QCOMPARE(history.at(12)->sectionStates.size(), std::size_t(3));
	QVERIFY(sameRecorded(*history.at(12), next[12]));
	QVERIFY(sameRecorded(*history.at(24), next[24]));
}

// A small budget drops whole intervals, from the oldest, and says what is left; an interval that cannot fit turns the
// replay off.
void TestGuiSimulationSnapshot::evictsWholeIntervals() {
	std::vector<GuiSimulationSnapshot> run;
	for (int t = 0; t < 100; ++t) run.push_back(sampleSnapshot(t, 3, 100));
	GuiReplayHistory reference(GuiReplayHistory::payloadLimit, 10);
	for (int t = 0; t < 35; ++t) reference.record(shared(run[static_cast<std::size_t>(t)]));
	GuiReplayHistory small(reference.payloadBytes(), 10);
	for (const GuiSimulationSnapshot& snapshot : run) small.record(shared(snapshot));
	QVERIFY(small.truncated());
	QVERIFY(!small.oversize());
	QCOMPARE_GE(small.intervalCount(), std::size_t(2));
	QCOMPARE_LT(small.intervalCount(), std::size_t(10));
	QCOMPARE_LE(small.payloadBytes(), small.budgetBytes());
	// Whole intervals went: the first second kept starts an interval, and the replay reaches the last second.
	QCOMPARE(small.lastTime(), 99);
	QCOMPARE_GT(small.firstTime(), 0);
	QCOMPARE(small.firstTime() % 10, 0);
	QCOMPARE(small.evictedBeforeTime(), small.firstTime());
	QCOMPARE(small.at(small.firstTime() - 1)->timestep, small.firstTime());
	QCOMPARE(small.at(0)->timestep, small.firstTime());
	QCOMPARE(small.at(500)->timestep, 99);
	for (int t = small.firstTime(); t < 100; ++t)
		QVERIFY2(sameRecorded(*small.at(t), run[static_cast<std::size_t>(t)]), qPrintable(QString("second %1 changed after eviction").arg(t)));

	GuiReplayHistory tiny(1024, 10);
	tiny.record(shared(run[0]));
	QVERIFY(tiny.oversize());
	QVERIFY(tiny.empty());
	QCOMPARE(tiny.payloadBytes(), std::size_t(0));
	QVERIFY(!tiny.at(0));
	tiny.record(shared(run[1]));
	QVERIFY(tiny.empty());
	QVERIFY(tiny.oversize());
}

// A change of one static field is found at the first second of the next interval, or at the last step, and the history
// starts again there. The seconds before are dropped with the old layout.
void TestGuiSimulationSnapshot::staticChangeRestarts_data() {
	QTest::addColumn<int>("change");
	QTest::addColumn<bool>("checkLastStep");
	for (int i = 0; i < static_cast<int>(std::size(kStaticChanges)); ++i)
		QTest::newRow(kStaticChanges[i].name) << i << (std::string(kStaticChanges[i].name) != "total steps");
}

void TestGuiSimulationSnapshot::staticChangeRestarts() {
	QFETCH(int, change);
	QFETCH(bool, checkLastStep);
	GuiReplayHistory history(GuiReplayHistory::payloadLimit, 10);
	std::vector<GuiSimulationSnapshot> run;
	for (int t = 0; t <= 30; ++t) {
		GuiSimulationSnapshot snapshot = sampleSnapshot(t, 3, 40);
		if (t >= 13)
			kStaticChanges[change].apply(snapshot);
		run.push_back(snapshot);
		history.record(shared(snapshot));
	}
	QVERIFY(history.truncated());
	QCOMPARE(history.firstTime(), 20);
	QCOMPARE(history.lastTime(), 30);
	QCOMPARE(history.intervalCount(), std::size_t(2));
	for (int t = 20; t <= 30; ++t)
		QVERIFY2(sameRecorded(*history.at(t), run[static_cast<std::size_t>(t)]), qPrintable(QString("second %1 is not as recorded").arg(t)));
	if (!checkLastStep)
		return;
	GuiReplayHistory last(GuiReplayHistory::payloadLimit, 10);
	GuiSimulationSnapshot final;
	for (int t = 0; t < 40; ++t) {
		final = sampleSnapshot(t, 3, 40);
		if (t == 39)
			kStaticChanges[change].apply(final);
		last.record(shared(final));
	}
	QVERIFY(last.truncated());
	QCOMPARE(last.firstTime(), 39);
	QCOMPARE(last.lastTime(), 39);
	QVERIFY(sameRecorded(*last.at(39), final));
}

// A change of a count is found at once.
void TestGuiSimulationSnapshot::countChangeRestarts() {
	using Change = void (*)(GuiSimulationSnapshot&);
	const Change changes[] = {
		[](GuiSimulationSnapshot& s) { s.trains.push_back(s.trains.back()); },
		[](GuiSimulationSnapshot& s) { s.signalStates.pop_back(); },
		[](GuiSimulationSnapshot& s) { s.sectionStates.push_back(s.sectionStates.back()); },
		[](GuiSimulationSnapshot& s) { s.platforms.pop_back(); },
		[](GuiSimulationSnapshot& s) { s.passengers.push_back(s.passengers.back()); },
	};
	for (const Change change : changes) {
		GuiReplayHistory history(GuiReplayHistory::payloadLimit, 10);
		GuiSimulationSnapshot changed;
		for (int t = 0; t <= 15; ++t) {
			changed = sampleSnapshot(t, 3, 40);
			if (t >= 13)
				change(changed);
			history.record(shared(changed));
		}
		QVERIFY(history.truncated());
		QCOMPARE(history.firstTime(), 13);
		QCOMPARE(history.lastTime(), 15);
		QVERIFY(sameRecorded(*history.at(15), changed));
	}
}

namespace {
// A run of this shape: a train A whose position has all five regions of a series, a train B on a forward route and a
// train C on a reversed route that both stand 1,500 m along their route, and the samples that the completed run would
// share for A. The route has one block section from 1 to 2 km, whose start is at 10,000 m in scene coordinates. Each
// train has one wagon behind the traction unit and is 100 m long. The samples number 12; the history may be longer.
struct DerivedRun {
	std::unique_ptr<SimulationRun> run = std::make_unique<SimulationRun>();
	std::vector<double> positionA = {5, 5, 5, 7, 8, 100, 200, 300, 400, 9, 9, 9};
	std::vector<double> speedA = {0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 8};
	std::vector<double> positionB = std::vector<double>(12, 1500.0);
	std::vector<double> speedB = std::vector<double>(12, 10.0);

	DerivedRun() {
		run->numTrains = 3;
		run->trains.resize(3);
		run->infra.routes.resize(2);
		for (std::size_t r = 0; r < 2; ++r) {
			Route& route = run->infra.routes[r];
			route.N_Block_Sections = 1;
			route.reversed_direction = r == 1;
			route.sequence_of_block_sections.resize(1);
			Section& section = route.sequence_of_block_sections[0];
			section.start_node.X = 1.0;
			section.end_node.X = 2.0;
			section.GeoXBegNode = 10000.0;
		}
		const int routes[] = {0, 0, 1};
		for (std::size_t i = 0; i < 3; ++i) {
			Train& train = run->trains[i];
			train.indexOfRoute = routes[i];
			train.number_of_wagons = 1.0;
			train.train_length = 100.0;
			train.instant_spatial_position = i == 0 ? positionA : positionB;
			train.instant_train_speed = i == 0 ? speedA : speedB;
		}
	}

	// What the window records for the run: three trains with one wagon each, with the route of each train and its
	// direction, for `seconds` seconds.
	GuiReplayHistory history(int seconds) const {
		GuiReplayHistory result;
		for (int t = 0; t < seconds; ++t) {
			GuiSimulationSnapshot snapshot = sampleSnapshot(t, 3, seconds);
			for (std::size_t i = 0; i < 3; ++i) {
				GuiTrainState& train = snapshot.trains[i];
				train.routeIndex = i == 2 ? 1 : 0;
				train.reversedDirection = i == 2;
				train.wagonCount = 1;
				train.length = 100.0;
			}
			result.record(shared(snapshot));
		}
		return result;
	}

	// The samples of A from step 5 on, as a completed run holds them: three values.
	std::vector<GuiReplayTrajectory> slices() const {
		std::vector<GuiReplayTrajectory> result(3);
		result[0].firstSample = 5;
		result[0].position = std::make_shared<const std::vector<double>>(positionA.begin() + 5, positionA.begin() + 8);
		result[0].speed = std::make_shared<const std::vector<double>>(speedA.begin() + 5, speedA.begin() + 8);
		return result;
	}
};
}

// The train fields come from the train source with the arithmetic of the live snapshot: hand-computed positions, the
// speed in km/h, the defaults outside the samples, every region of a series and a slice that does not match.
void TestGuiSimulationSnapshot::trainsDerivedFromSource() {
	DerivedRun data;
	GuiReplayHistory history = data.history(14);
	history.setTrains(makeGuiReplayTrains(*data.run, data.slices()));
	QVERIFY(history.trains());
	// A and its speeds share three values each.
	QCOMPARE(history.sharedBytes(), 2 * 3 * sizeof(double));
	const auto at = history.at(4);
	// B: head 1,500 m in the section, which is 500 m after its start: 10,500 m forward, 9,500 m reversed.
	QCOMPARE(at->trains[1].routeAxisPosition, 1500.0);
	QCOMPARE(at->trains[1].wagonHeadPositions.size(), std::size_t(2));
	QCOMPARE(at->trains[1].wagonTailPositions.size(), std::size_t(2));
	QCOMPARE(at->trains[1].wagonHeadPositions[0], 10.5);
	QCOMPARE(at->trains[2].wagonHeadPositions[0], 9.5);
	// The second vehicle is half the train length behind: 1,450 m, and the tail of it a whole length: 1,400 m.
	QCOMPARE(at->trains[1].wagonHeadPositions[1], (10000.0 + 450.0) / 1000);
	QCOMPARE(at->trains[1].wagonTailPositions[0], (10000.0 + 450.0) / 1000);
	QCOMPARE(at->trains[1].wagonTailPositions[1], (10000.0 + 400.0) / 1000);
	QCOMPARE(at->trains[2].wagonHeadPositions[1], (10000.0 - 450.0) / 1000);
	QCOMPARE(at->trains[2].wagonTailPositions[1], (10000.0 - 400.0) / 1000);
	// 10 m/s is 36 km/h.
	QCOMPARE(at->trains[1].speedKmh, 10.0 * 3.6);
	// A is at 7 m, which is on no section: -1 for every vehicle.
	QCOMPARE(at->trains[0].wagonHeadPositions[0], -1.0);
	// Every second of every sample is exact: the repeated start, the copied head, the shared samples, the copied tail and
	// the repeated end.
	for (int t = 0; t < 12; ++t) {
		const auto replay = history.at(t);
		QCOMPARE(replay->trains[0].routeAxisPosition, data.positionA[static_cast<std::size_t>(t)]);
		QCOMPARE(replay->trains[0].speedKmh, data.speedA[static_cast<std::size_t>(t)] * 3.6);
	}
	// Past the samples the train has no place: no position, no vehicles and no speed.
	for (const int t : {12, 13}) {
		const auto outside = history.at(t);
		for (const GuiTrainState& train : outside->trains) {
			QCOMPARE(train.routeAxisPosition, -9999.0);
			QVERIFY(train.wagonHeadPositions.empty());
			QVERIFY(train.wagonTailPositions.empty());
			QCOMPARE(train.speedKmh, 0.0);
		}
	}
	// The other fields are untouched.
	QVERIFY(sameRecorded(*history.at(4), withoutDerived(*history.at(4))));

	// A slice that does not match the samples is copied and still exact. Nothing is shared then. The history records as
	// many seconds as the one above, so that the two differ only in their train source: the growth of a vector differs
	// between standard libraries.
	std::vector<GuiReplayTrajectory> mismatch = data.slices();
	mismatch[0].position = std::make_shared<const std::vector<double>>(std::vector<double>{100.0, 201.0, 300.0});
	mismatch[0].speed = std::make_shared<const std::vector<double>>(std::vector<double>{4.0, 5.0, 6.5});
	GuiReplayHistory copied = data.history(14);
	copied.setTrains(makeGuiReplayTrains(*data.run, mismatch));
	QCOMPARE(copied.sharedBytes(), std::size_t(0));
	QCOMPARE_GT(copied.payloadBytes(), history.payloadBytes() - history.sharedBytes() - 1);
	for (int t = 0; t < 12; ++t) {
		const auto replay = copied.at(t);
		QCOMPARE(replay->trains[0].routeAxisPosition, data.positionA[static_cast<std::size_t>(t)]);
		QCOMPARE(replay->trains[0].speedKmh, data.speedA[static_cast<std::size_t>(t)] * 3.6);
	}
	// No slices at all: also exact.
	GuiReplayHistory plain = data.history(12);
	plain.setTrains(makeGuiReplayTrains(*data.run, {}));
	QCOMPARE(plain.sharedBytes(), std::size_t(0));
	QCOMPARE(plain.at(6)->trains[0].routeAxisPosition, 200.0);
	// The history reads nothing of the run once it has its source.
	data.run->trains.clear();
	data.run->infra.routes.clear();
	QCOMPARE(history.at(4)->trains[1].wagonHeadPositions[0], 10.5);
}

// A message of step 100 shows from step 100 up to and including step 140.
void TestGuiSimulationSnapshot::virtualCouplingFromSource() {
	GuiReplayHistory history;
	for (int t = 0; t < 150; ++t) history.record(shared(sampleSnapshot(t, 3, 150)));
	auto source = std::make_shared<GuiReplayTrains>();
	source->messageSteps = {100, 120};
	source->messageTrains = {"train 0", "train 1"};
	source->messageTexts = {"couple", "uncouple"};
	history.setTrains(source);
	QVERIFY(history.at(99)->virtualCouplingMessages.empty());
	QCOMPARE(history.at(100)->virtualCouplingMessages.size(), std::size_t(1));
	QCOMPARE(history.at(100)->virtualCouplingMessages[0].message, std::string("couple"));
	QCOMPARE(history.at(100)->virtualCouplingMessages[0].timestep, 100);
	QCOMPARE(history.at(120)->virtualCouplingMessages.size(), std::size_t(2));
	QCOMPARE(history.at(120)->virtualCouplingMessages[1].trainDescription, std::string("train 1"));
	QCOMPARE(history.at(140)->virtualCouplingMessages.size(), std::size_t(2));
	QCOMPARE(history.at(141)->virtualCouplingMessages.size(), std::size_t(1));
	QCOMPARE(history.at(141)->virtualCouplingMessages[0].message, std::string("uncouple"));
	QVERIFY(guiVirtualCouplingMessageShown(100, 140));
	QVERIFY(!guiVirtualCouplingMessageShown(100, 141));
	QVERIFY(!guiVirtualCouplingMessageShown(100, 99));
}

// The messages that a run holds, pushed in the steps that the simulation pushes them in, reach the replay through
// makeGuiReplayTrains and show from their step up to 40 seconds after it, and not outside that window.
void TestGuiSimulationSnapshot::virtualCouplingFromRun() {
	DerivedRun data;
	const int steps[] = {30, 30, 100};
	const char* trains[] = {"train 0", "train 1", "train 2"};
	const char* texts[] = {"Train is now virtually\ncoupled to the leader", "Train is now decoupled", "Train is now decoupled"};
	for (std::size_t i = 0; i < 3; ++i) {
		data.run->vcMsgTimestep.push_back(steps[i]);
		data.run->vcMsgTrain.push_back(trains[i]);
		data.run->vcMsgText.push_back(texts[i]);
	}
	GuiReplayHistory later;
	for (int t = 0; t < 160; ++t) later.record(shared(sampleSnapshot(t, 3, 160)));
	later.setTrains(makeGuiReplayTrains(*data.run, {}));
	data.run->vcMsgTimestep.clear();
	data.run->vcMsgTrain.clear();
	data.run->vcMsgText.clear();
	const auto shown = [&later](int t) { return later.at(t)->virtualCouplingMessages; };
	QVERIFY(shown(29).empty());
	QCOMPARE(shown(30).size(), std::size_t(2));
	QCOMPARE(shown(30)[0].timestep, 30);
	QCOMPARE(shown(30)[0].trainDescription, std::string("train 0"));
	QCOMPARE(shown(30)[0].message, std::string(texts[0]));
	QCOMPARE(shown(30)[1].trainDescription, std::string("train 1"));
	QCOMPARE(shown(70).size(), std::size_t(2));
	QVERIFY(shown(71).empty());
	QVERIFY(shown(99).empty());
	QCOMPARE(shown(100).size(), std::size_t(1));
	QCOMPARE(shown(100)[0].trainDescription, std::string("train 2"));
	QCOMPARE(shown(140).size(), std::size_t(1));
	QVERIFY(shown(141).empty());
}

// The history itself keeps the second it handed out last, so a caller that drops the result can ask again and gets the
// same object, and the cache moves with the latest request.
void TestGuiSimulationSnapshot::keepsSecondHandedOutLast() {
	GuiReplayHistory history;
	recordSampleRun(history);
	const std::weak_ptr<const GuiSimulationSnapshot> last = history.at(100);
	QVERIFY(!last.expired());
	QVERIFY(last.lock() == history.at(100));
	history.at(101);
	QVERIFY2(last.expired(), "the cache kept more than one snapshot");
}

// Asking for the same second twice returns the same object, another second another one.
void TestGuiSimulationSnapshot::sameSecondSameObject() {
	GuiReplayHistory history;
	recordSampleRun(history);
	const auto a = history.at(100);
	QVERIFY(a == history.at(100));
	const auto c = history.at(101);
	QVERIFY(c != a);
	QCOMPARE(c->timestep, 101);
	QVERIFY(history.at(101) == c);
	// A snapshot that the caller still holds is not rebuilt after other seconds were asked for.
	for (int t = 110; t < 300; t += 5) history.at(t);
	QVERIFY(history.at(100) == a);
	QVERIFY(history.at(101) == c);
	// A new train source drops the snapshots handed out.
	history.setTrains(std::make_shared<GuiReplayTrains>());
	QVERIFY(history.at(100) != a);
}

// The accounted bytes are close to what the history holds, and a second adds far less than the first, which also holds the
// layout and the keyframe.
void TestGuiSimulationSnapshot::accountedBytes() {
	GuiReplayHistory history;
	const std::size_t heapBefore = heapBytes();
	std::vector<std::shared_ptr<const GuiSimulationSnapshot>> large;
	for (int t = 0; t < 160; ++t) large.push_back(shared(sceneSnapshot(t, 40, 1350, 700, 160)));
	const std::size_t heapWithInput = heapBytes();
	std::size_t firstSecond = 0;
	for (const auto& snapshot : large) {
		history.record(snapshot);
		if (!firstSecond)
			firstSecond = history.payloadBytes();
	}
	const std::size_t held = heapBytes() - heapWithInput;
	const std::size_t accounted = history.payloadBytes();
	QCOMPARE(history.intervalCount(), std::size_t(1));
	// The accounted bytes differ from the bytes held by less than 10 percent.
	QCOMPARE_GT(accounted, held - held / 10);
	QCOMPARE_LT(accounted, held + held / 10);
	// A later second adds less than half of what the first did: the static data of the run is not repeated.
	QCOMPARE_LT(2 * ((accounted - firstSecond) / (large.size() - 1)), firstSecond);
	// The compact form is much smaller than the snapshots.
	QCOMPARE_GT(heapWithInput - heapBefore, 3 * held);
	large.clear();
	history.clear();
	QCOMPARE(history.payloadBytes(), std::size_t(0));
}

// The replay of a run with the shape of Copenhagen (196 trains, 1,331 signal entries, 1,265 sections, 8,000 seconds) holds
// less than 10 MiB of its own payload, with about 11 signal and 5 section changes a second.
void TestGuiSimulationSnapshot::copenhagenSizedRunFits() {
	constexpr int total = 8000;
	constexpr int probeSecond = 4321;
	const auto working = std::make_shared<GuiSimulationSnapshot>(sceneSnapshot(0, 196, 1331, 1265, total));
	const int codes[] = {0, 75, 180, 270, 751};
	GuiReplayHistory history;
	GuiSimulationSnapshot probe;
	std::size_t signalChanges = 0;
	for (int t = 0; t < total; ++t) {
		working->timestep = t;
		if (t > 0) {
			for (std::size_t k = 0; k < 11; ++k) {
				GuiSignalState& signal = working->signalStates[(static_cast<std::size_t>(t) * 11 + k) % working->signalStates.size()];
				const auto old = std::find(std::begin(codes), std::end(codes), signal.code);
				signal.code = codes[(static_cast<std::size_t>(old - std::begin(codes)) + 1) % 5];
				++signalChanges;
			}
			for (std::size_t k = 0; k < 5; ++k) {
				GuiSectionState& section = working->sectionStates[(static_cast<std::size_t>(t) * 5 + k) % working->sectionStates.size()];
				section.prepared = !section.prepared;
			}
			for (std::size_t k = 0; k < 3; ++k) {
				GuiTrainState& train = working->trains[(static_cast<std::size_t>(t) * 3 + k) % working->trains.size()];
				if (train.occupiedArcs.size() > 1)
					train.occupiedArcs.pop_back();
				else
					train.occupiedArcs.push_back({7, 0.5 * t});
			}
		}
		if (t == probeSecond)
			probe = *working;
		history.record(working);
	}
	QCOMPARE(signalChanges, std::size_t(11 * (total - 1)));
	QVERIFY(!history.truncated());
	QVERIFY(!history.oversize());
	QCOMPARE(history.firstTime(), 0);
	QCOMPARE(history.lastTime(), total - 1);
	QCOMPARE(history.intervalCount(), std::size_t(27));
	QCOMPARE_LT(history.payloadBytes(), std::size_t(10u << 20));
	QVERIFY(sameRecorded(*history.at(probeSecond), probe));
	QVERIFY(sameRecorded(*history.at(total - 1), *working));
}

// Strings that vary are stored once, so a crowd of passengers with the same status stays small over many seconds, and a
// second that cannot fit the budget turns replay off.
void TestGuiSimulationSnapshot::storesStringsOnce() {
	GuiReplayHistory passengersOnly(8u << 20);
	auto crowd = std::make_shared<GuiSimulationSnapshot>();
	crowd->totalTimesteps = 1000;
	crowd->passengers.resize(10000);
	for (std::size_t i = 0; i < crowd->passengers.size(); ++i) {
		crowd->passengers[i].id = "p" + std::to_string(i);
		crowd->passengers[i].status = std::string(1000, 'p');
	}
	for (int t = 0; t < 12; ++t) {
		auto second = std::make_shared<GuiSimulationSnapshot>(*crowd);
		second->timestep = t;
		passengersOnly.record(second);
	}
	// Equal strings are stored once, not once per passenger.
	QVERIFY(!passengersOnly.truncated());
	QCOMPARE(passengersOnly.lastTime(), 11);
	QCOMPARE_LE(passengersOnly.payloadBytes(), passengersOnly.budgetBytes());
	QCOMPARE(passengersOnly.at(5)->passengers[9999].status, std::string(1000, 'p'));
	crowd->passengers.front().status = std::string(passengersOnly.budgetBytes(), 'x');
	crowd->timestep = 12;
	passengersOnly.record(crowd);
	// An oversize second disables replay.
	QVERIFY(passengersOnly.oversize());
	QVERIFY(passengersOnly.empty());
	QCOMPARE(passengersOnly.payloadBytes(), std::size_t(0));
	QVERIFY(!passengersOnly.at(0));
	// A small second is refused too once replay is off for the run.
	auto tiny = std::make_shared<GuiSimulationSnapshot>();
	tiny->timestep = 500;
	passengersOnly.record(tiny);
	QVERIFY(passengersOnly.empty());
	QVERIFY(passengersOnly.oversize());
}

// The window receives the history by move. A moved history gives the same seconds back, keeps its train source, and still
// records: the string table of the new object serves a second that is recorded afterwards.
void TestGuiSimulationSnapshot::movedHistory() {
	GuiReplayHistory source;
	std::vector<GuiSimulationSnapshot> kept;
	for (int t = 0; t < 50; ++t) {
		kept.push_back(sampleSnapshot(t, 3, 600));
		source.record(shared(kept.back()));
	}
	const auto trains = std::make_shared<const GuiReplayTrains>();
	source.setTrains(trains);
	GuiReplayHistory moved(std::move(source));
	GuiReplayHistory assigned;
	assigned = std::move(moved);
	kept.push_back(sampleSnapshot(50, 3, 600));
	assigned.record(shared(kept.back()));
	QCOMPARE(assigned.lastTime(), 50);
	QVERIFY(assigned.trains() == trains);
	for (const GuiSimulationSnapshot& expected : kept) {
		const auto back = assigned.at(expected.timestep);
		QVERIFY(back);
		QVERIFY(sameRecorded(*back, expected));
	}
}

QTEST_APPLESS_MAIN(TestGuiSimulationSnapshot)
#include "test_guisimulationsnapshot.moc"
