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
	ok &= expect(preparedTrack.style == Qt::SolidLine && preparedTrack.width == 8, "prepared track underlay");
	ok &= expect(occupiedTrack.style == Qt::SolidLine && occupiedTrack.width == 10, "occupied track underlay");
	ok &= expect(blockedTrack.style == Qt::DashLine && blockedTrack.width == 8, "blocked track has non-color cue");
	ok &= expect(preparedTrack.color == QColor("#2ECC71"), "prepared track color");
	ok &= expect(occupiedTrack.color == QColor("#EF5350"), "occupied track color");
	ok &= expect(blockedTrack.color == QColor("#F2A516"), "blocked track color");
	ok &= expect(trackStatePriority(TrackOperationalState::Free) < trackStatePriority(TrackOperationalState::Prepared), "free track priority");
	ok &= expect(trackStatePriority(TrackOperationalState::Prepared) < trackStatePriority(TrackOperationalState::Occupied), "prepared track priority");
	ok &= expect(trackStatePriority(TrackOperationalState::Occupied) < trackStatePriority(TrackOperationalState::Blocked), "occupied track priority");

	const SignalVisual stopSignal = classifySignalAspect(0);
	const SignalVisual cautionSignal = classifySignalAspect(75);
	const SignalVisual proceed180Signal = classifySignalAspect(180);
	const SignalVisual proceed270Signal = classifySignalAspect(270);
	ok &= expect(stopSignal.lamp == QColor(Qt::red) && stopSignal.cue == SignalCueKind::Stop, "red stop signal cue");
	ok &= expect(cautionSignal.lamp == QColor(Qt::yellow) && cautionSignal.cue == SignalCueKind::Caution, "yellow caution signal cue");
	ok &= expect(proceed180Signal.lamp == QColor(Qt::green) && proceed180Signal.cue == SignalCueKind::Proceed, "green proceed signal cue 180");
	ok &= expect(proceed270Signal.lamp == QColor(Qt::green) && proceed270Signal.cue == SignalCueKind::Proceed, "green proceed signal cue 270");

	ok &= expect(classifyTrainType("freight", "F01").kind == TrainVisualKind::Freight, "freight train classification");
	ok &= expect(classifyTrainType("IC", "IC 2201").kind == TrainVisualKind::Intercity, "intercity train classification");
	ok &= expect(classifyTrainType("", "sprinter 301").kind == TrainVisualKind::Sprinter, "sprinter train classification");
	ok &= expect(classifyTrainType("", "ICE 10").kind == TrainVisualKind::HighSpeed, "high-speed train classification");
	const TrainVisual intercity = classifyTrainType("IC", "IC 2201");
	const TrainVisual sprinter = classifyTrainType("", "sprinter 301");
	const TrainVisual freight = classifyTrainType("freight", "F01");
	ok &= expect(intercity.fill != sprinter.fill && intercity.fill != freight.fill && sprinter.fill != freight.fill,
		"train category contrast");
	ok &= expect(intercity.shape == TrainBadgeShape::Capsule, "intercity badge shape");
	ok &= expect(sprinter.shape == TrainBadgeShape::Rounded, "sprinter badge shape");
	ok &= expect(freight.shape == TrainBadgeShape::Square, "freight badge shape");
	ok &= expect(intercity.shape != sprinter.shape && intercity.shape != freight.shape && sprinter.shape != freight.shape,
		"train category silhouettes");

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
