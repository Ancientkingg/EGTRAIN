#include "app/GuiSimulationSnapshot.h"
#include "app/GuiReplayHistory.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <set>
#include <vector>

namespace {
void require(bool condition, const char* message) {
	if (!condition) {
		std::cerr << message << '\n';
		std::exit(1);
	}
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
		&& history.atOrBefore(99)->timestep == 12, "replay lookup failed to clamp or floor");
	history.clear();
	require(history.empty() && !history.truncated(), "replacement retained old frames");
	auto only = std::make_shared<GuiSimulationSnapshot>();
	only->timestep = 3;
	only->totalTimesteps = 4;
	history.record(only);
	require(history.size() == 1 && history.atOrBefore(0)->timestep == 3, "one-frame replay failed");

	history.clear();
	for (int t = 0; t < 8200; ++t) {
		auto frame = std::make_shared<GuiSimulationSnapshot>();
		frame->timestep = t * GuiReplayHistory::cadenceSeconds;
		frame->totalTimesteps = 50000;
		history.record(frame);
	}
	require(history.size() == GuiReplayHistory::frameLimit && history.truncated()
		&& history.firstTime() > 0 && history.payloadBytes() <= GuiReplayHistory::payloadLimit,
		"frame-count eviction failed");
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
	history.clear();
	auto large = std::make_shared<GuiSimulationSnapshot>();
	large->passengers.resize(10000);
	for (auto& passenger : large->passengers) passenger.id = std::string(1000, 'p');
	for (int t = 0; t < 12; ++t) {
		auto frame = std::make_shared<GuiSimulationSnapshot>(*large);
		frame->timestep = t * GuiReplayHistory::cadenceSeconds;
		history.record(frame);
	}
	require(history.truncated() && history.payloadBytes() <= GuiReplayHistory::payloadLimit
		&& history.firstTime() > 0, "payload eviction failed");
	large->passengers.front().id = std::string(GuiReplayHistory::payloadLimit, 'x');
	history.record(large);
	require(history.oversize() && history.empty() && history.payloadBytes() == 0,
		"oversize frame did not disable replay");
	return 0;
}
