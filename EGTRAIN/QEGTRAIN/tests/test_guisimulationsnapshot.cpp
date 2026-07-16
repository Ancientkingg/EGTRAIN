#include "app/GuiSimulationSnapshot.h"

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
	return 0;
}
