#include "app/GuiSimulationSnapshot.h"

#include <cstdlib>
#include <iostream>
#include <memory>

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
	auto firstSnapshot = std::make_shared<const GuiSimulationSnapshot>(first);

	GuiSimulationSnapshot second;
	second.timestep = 2;
	second.trains.push_back(GuiTrainState{});
	second.trains.front().description = "second";
	auto secondSnapshot = std::make_shared<const GuiSimulationSnapshot>(second);

	GuiSimulationSnapshotMailbox mailbox;
	require(mailbox.publish(firstSnapshot), "first publish did not request a notification");
	require(!mailbox.publish(secondSnapshot), "second publish requested a duplicate notification");

	second.trains.front().description = "mutated source";
	const auto latest = mailbox.take();
	require(latest != nullptr, "take returned no snapshot");
	require(latest->timestep == 2, "take did not return the latest snapshot");
	require(latest->trains.front().description == "second", "snapshot changed after source mutation");

	require(mailbox.publish(firstSnapshot), "publish after take did not request a notification");
	require(mailbox.take()->timestep == 1, "second take returned the wrong snapshot");
	return 0;
}
