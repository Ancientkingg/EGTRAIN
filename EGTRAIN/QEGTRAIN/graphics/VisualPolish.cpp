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

TrackVisual freeTrackVisual() {
	return {QColor("#A0ACB4"), 2};
}

TrackStateVisual classifyTrackState(TrackOperationalState state) {
	switch (state) {
	case TrackOperationalState::Prepared:
		return {QColor("#4C8DAE"), 6, Qt::DashDotLine};
	case TrackOperationalState::Occupied:
		return {QColor("#D05A47"), 8, Qt::SolidLine};
	case TrackOperationalState::Blocked:
		return {QColor("#D6A13A"), 8, Qt::DashLine};
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
		return {TrainVisualKind::Freight, QColor("#A99787"), QColor("#5D4C3F"), classifyTrainBadgeShape(TrainVisualKind::Freight), ":/icons/train-freight.svg"};
	if (containsAny(text, {"ice", "hst", "highspeed", "high speed"}))
		return {TrainVisualKind::HighSpeed, QColor("#86A6B9"), QColor("#3E627A"), classifyTrainBadgeShape(TrainVisualKind::HighSpeed), ":/icons/train-high-speed.svg"};
	if (containsAny(text, {"sprinter", "spr"}))
		return {TrainVisualKind::Sprinter, QColor("#86AA96"), QColor("#3F6A54"), classifyTrainBadgeShape(TrainVisualKind::Sprinter), ":/icons/train-sprinter.svg"};
	if (containsAny(text, {"intercity", " ic", "ic "}))
		return {TrainVisualKind::Intercity, QColor("#C6A86E"), QColor("#765623"), classifyTrainBadgeShape(TrainVisualKind::Intercity), ":/icons/train-intercity.svg"};
	return {TrainVisualKind::Passenger, QColor("#9BA5AA"), QColor("#4A5960"), classifyTrainBadgeShape(TrainVisualKind::Passenger), ":/icons/train-passenger.svg"};
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
		return {QColor(Qt::red), classifySignalCue(code), ":/icons/signal-stop.svg"};
	if (code == 75)
		return {QColor(Qt::yellow), classifySignalCue(code), ":/icons/signal-caution.svg"};
	if (code == 180 || code == 270)
		return {QColor(Qt::green), classifySignalCue(code), ":/icons/signal-proceed.svg"};
	return {QColor(128, 128, 128), classifySignalCue(code), ":/icons/signal-neutral.svg"};
}

StationVisual classifyStation(bool hasPlatformId, int connectionCount) {
	if (connectionCount >= 3)
		return {StationVisualKind::Interchange, QColor(80, 120, 210), QColor(30, 60, 130), ":/icons/station-interchange.svg"};
	if (hasPlatformId)
		return {StationVisualKind::Platform, QColor(70, 70, 70), QColor(30, 30, 30), ":/icons/station-platform.svg"};
	return {StationVisualKind::StopMarker, QColor(120, 120, 120), QColor(70, 70, 70), ":/icons/station-stop.svg"};
}

// One simulated second renders every delayMs, so the nominal playback factor
// against real time is 1000 / delayMs; zero delay runs unthrottled.
QString simulationSpeedLabel(int delayMs) {
	if (delayMs <= 0)
		return "Speed: fastest";
	const double factor = 1000.0 / delayMs;
	if (factor >= 10.0)
		return QString("Speed: %1x").arg(qRound(factor));
	return QString("Speed: %1x").arg(factor, 0, 'f', 1);
}

QString simulationSpeedMode(int delayMs) {
	if (delayMs <= 0)
		return "Fastest";
	const double factor = 1000.0 / delayMs;
	if (factor >= 10.0)
		return QString("%1x real time").arg(qRound(factor));
	return QString("%1x real time").arg(factor, 0, 'f', 1);
}
