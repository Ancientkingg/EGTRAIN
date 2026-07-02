#include "../VisualPolish.h"

#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

int main() {
	bool ok = true;
	ok &= expect(classifyTrackSpeed(83.4).kind == TrackVisualKind::HighSpeed, "high-speed track classification");
	ok &= expect(classifyTrackSpeed(44.5).kind == TrackVisualKind::Mainline, "mainline track classification");
	ok &= expect(classifyTrackSpeed(19.0).kind == TrackVisualKind::Local, "local track classification");

	ok &= expect(classifyTrainType("freight", "F01").kind == TrainVisualKind::Freight, "freight train classification");
	ok &= expect(classifyTrainType("IC", "IC 2201").kind == TrainVisualKind::Intercity, "intercity train classification");
	ok &= expect(classifyTrainType("", "sprinter 301").kind == TrainVisualKind::Sprinter, "sprinter train classification");
	ok &= expect(classifyTrainType("", "ICE 10").kind == TrainVisualKind::HighSpeed, "high-speed train classification");

	ok &= expect(classifyStation(true, 4).kind == StationVisualKind::Interchange, "interchange station classification");
	ok &= expect(classifyStation(true, 1).kind == StationVisualKind::Platform, "platform station classification");
	ok &= expect(classifyStation(false, 0).kind == StationVisualKind::StopMarker, "stop marker classification");

	ok &= expect(simulationSpeedLabel(0) == "Speed: fastest", "fastest speed label");
	ok &= expect(simulationSpeedLabel(250) == "Speed: +250 ms", "delayed speed label");
	ok &= expect(simulationSpeedMode(0) == "Compressed", "compressed speed mode");
	ok &= expect(simulationSpeedMode(500) == "Slowed", "slowed speed mode");

	if (!ok)
		return 1;

	std::cout << "all VisualPolish tests passed\n";
	return 0;
}
