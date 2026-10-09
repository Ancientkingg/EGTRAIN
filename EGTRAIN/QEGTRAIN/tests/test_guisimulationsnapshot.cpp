#include "app/GuiSimulationSnapshot.h"
#include "app/GuiReplayHistory.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <set>

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
