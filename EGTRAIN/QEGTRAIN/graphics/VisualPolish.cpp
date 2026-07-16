#include "graphics/VisualPolish.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>

namespace {

std::string lower(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
	return text;
}

bool containsAny(const std::string& text, std::initializer_list<const char*> needles) {
	for (const char* needle : needles) {
		if (text.find(needle) != std::string::npos)
			return true;
	}
	return false;
}

} // namespace

TrackVisual classifyTrackSpeed(double speedLimitMetersPerSecond) {
	double kmh = speedLimitMetersPerSecond * 3.6;
	if (kmh >= 200.0)
		return {TrackVisualKind::HighSpeed, QColor(30, 130, 210), 4};
	if (kmh >= 120.0)
		return {TrackVisualKind::Mainline, QColor(80, 80, 80), 3};
	return {TrackVisualKind::Local, QColor(120, 120, 120), 2};
}

TrackStateVisual classifyTrackState(TrackOperationalState state) {
	switch (state) {
	case TrackOperationalState::Prepared:
		return {QColor("#2ECC71"), 8, Qt::SolidLine};
	case TrackOperationalState::Occupied:
		return {QColor("#EF5350"), 10, Qt::SolidLine};
	case TrackOperationalState::Blocked:
		return {QColor("#F2A516"), 8, Qt::DashLine};
	case TrackOperationalState::Free:
	default:
		return {Qt::transparent, 0, Qt::NoPen};
	}
}

TrainBadgeShape classifyTrainBadgeShape(TrainVisualKind kind) {
	switch (kind) {
	case TrainVisualKind::Intercity:
		return TrainBadgeShape::Capsule;
	case TrainVisualKind::Freight:
		return TrainBadgeShape::Square;
	case TrainVisualKind::Passenger:
	case TrainVisualKind::Sprinter:
	case TrainVisualKind::HighSpeed:
	default:
		return TrainBadgeShape::Rounded;
	}
}

int trainBadgeCornerRadius(TrainBadgeShape shape) {
	switch (shape) {
	case TrainBadgeShape::Capsule:
		return 8;
	case TrainBadgeShape::Rounded:
		return 3;
	case TrainBadgeShape::Square:
	default:
		return 0;
	}
}

int trackStatePriority(TrackOperationalState state) {
	switch (state) {
	case TrackOperationalState::Prepared:
		return 1;
	case TrackOperationalState::Occupied:
		return 2;
	case TrackOperationalState::Blocked:
		return 3;
	case TrackOperationalState::Free:
	default:
		return 0;
	}
}

TrainVisual classifyTrainType(const std::string& type, const std::string& description) {
	std::string text = lower(type + " " + description);
	if (containsAny(text, {"freight", "cargo", "goederen"}))
		return {TrainVisualKind::Freight, QColor(120, 95, 70), QColor(70, 55, 40), classifyTrainBadgeShape(TrainVisualKind::Freight)};
	if (containsAny(text, {"ice", "hst", "highspeed", "high speed"}))
		return {TrainVisualKind::HighSpeed, QColor(40, 130, 210), QColor(15, 70, 120), classifyTrainBadgeShape(TrainVisualKind::HighSpeed)};
	if (containsAny(text, {"sprinter", "spr"}))
		return {TrainVisualKind::Sprinter, QColor(40, 170, 110), QColor(20, 90, 60), classifyTrainBadgeShape(TrainVisualKind::Sprinter)};
	if (containsAny(text, {"intercity", " ic", "ic "}))
		return {TrainVisualKind::Intercity, QColor(235, 190, 45), QColor(120, 90, 20), classifyTrainBadgeShape(TrainVisualKind::Intercity)};
	return {TrainVisualKind::Passenger, QColor(235, 210, 55), QColor(110, 90, 20), classifyTrainBadgeShape(TrainVisualKind::Passenger)};
}

SignalCueKind classifySignalCue(int code) {
	if (code == 0)
		return SignalCueKind::Stop;
	if (code == 75)
		return SignalCueKind::Caution;
	if (code == 180 || code == 270)
		return SignalCueKind::Proceed;
	return SignalCueKind::Neutral;
}

SignalVisual classifySignalAspect(int code) {
	if (code == 0)
		return {QColor(Qt::red), classifySignalCue(code)};
	if (code == 75)
		return {QColor(Qt::yellow), classifySignalCue(code)};
	if (code == 180 || code == 270)
		return {QColor(Qt::green), classifySignalCue(code)};
	return {QColor(128, 128, 128), classifySignalCue(code)};
}

StationVisual classifyStation(bool hasPlatformId, int connectionCount) {
	if (connectionCount >= 3)
		return {StationVisualKind::Interchange, QColor(80, 120, 210), QColor(30, 60, 130)};
	if (hasPlatformId)
		return {StationVisualKind::Platform, QColor(70, 70, 70), QColor(30, 30, 30)};
	return {StationVisualKind::StopMarker, QColor(120, 120, 120), QColor(70, 70, 70)};
}

QString simulationSpeedLabel(int delayMs) {
	if (delayMs == 0)
		return "Speed: fastest";
	return QString("Speed: +%1 ms").arg(delayMs);
}

QString simulationSpeedMode(int delayMs) {
	if (delayMs == 0)
		return "Compressed";
	return "Slowed";
}
