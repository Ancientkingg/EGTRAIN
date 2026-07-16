#include "graphics/VisualPolish.h"

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

	const TrackStateVisual freeTrack = classifyTrackState(TrackOperationalState::Free);
	const TrackStateVisual preparedTrack = classifyTrackState(TrackOperationalState::Prepared);
	const TrackStateVisual occupiedTrack = classifyTrackState(TrackOperationalState::Occupied);
	const TrackStateVisual blockedTrack = classifyTrackState(TrackOperationalState::Blocked);
	ok &= expect(freeTrack.style == Qt::NoPen && freeTrack.width == 0, "free track has no underlay");
	ok &= expect(preparedTrack.style == Qt::SolidLine && preparedTrack.width > freeTrack.width, "prepared track underlay");
	ok &= expect(occupiedTrack.style == Qt::SolidLine && occupiedTrack.width > freeTrack.width, "occupied track underlay");
	ok &= expect(blockedTrack.style != Qt::SolidLine && blockedTrack.width > freeTrack.width, "blocked track has non-color cue");
	ok &= expect(preparedTrack.color == QColor("#3aa675"), "prepared track color");
	ok &= expect(occupiedTrack.color == QColor("#d95550"), "occupied track color");
	ok &= expect(blockedTrack.color == QColor("#e4a23a"), "blocked track color");
	ok &= expect(trackStatePriority(TrackOperationalState::Free) < trackStatePriority(TrackOperationalState::Prepared), "free track priority");
	ok &= expect(trackStatePriority(TrackOperationalState::Prepared) < trackStatePriority(TrackOperationalState::Occupied), "prepared track priority");
	ok &= expect(trackStatePriority(TrackOperationalState::Occupied) < trackStatePriority(TrackOperationalState::Blocked), "occupied track priority");

	ok &= expect(classifySignalAspect(0).lamp == QColor(Qt::red), "red signal aspect");
	ok &= expect(classifySignalAspect(75).lamp == QColor(Qt::yellow), "yellow signal aspect");
	ok &= expect(classifySignalAspect(180).lamp == QColor(Qt::green), "green signal aspect 180");
	ok &= expect(classifySignalAspect(270).lamp == QColor(Qt::green), "green signal aspect 270");

	ok &= expect(classifyTrainType("freight", "F01").kind == TrainVisualKind::Freight, "freight train classification");
	ok &= expect(classifyTrainType("IC", "IC 2201").kind == TrainVisualKind::Intercity, "intercity train classification");
	ok &= expect(classifyTrainType("", "sprinter 301").kind == TrainVisualKind::Sprinter, "sprinter train classification");
	ok &= expect(classifyTrainType("", "ICE 10").kind == TrainVisualKind::HighSpeed, "high-speed train classification");
	const TrainVisual intercity = classifyTrainType("IC", "IC 2201");
	const TrainVisual sprinter = classifyTrainType("", "sprinter 301");
	const TrainVisual freight = classifyTrainType("freight", "F01");
	ok &= expect(intercity.fill != sprinter.fill && intercity.fill != freight.fill && sprinter.fill != freight.fill,
		"train category contrast");

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
