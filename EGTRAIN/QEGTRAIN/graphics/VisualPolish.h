#ifndef VISUALPOLISH_H
#define VISUALPOLISH_H

#include <QColor>
#include <QPen>
#include <QString>
#include <string>

enum class TrackVisualKind { Local, Mainline, HighSpeed };
enum class TrainVisualKind { Passenger, Sprinter, Intercity, HighSpeed, Freight };
enum class TrainBadgeShape { Rounded, Capsule, Square };
enum class StationVisualKind { StopMarker, Platform, Interchange };
enum class TrackOperationalState { Free, Prepared, Occupied, Blocked };
enum class SignalCueKind { Neutral, Stop, Caution, Proceed };

struct TrackVisual {
	TrackVisualKind kind;
	QColor color;
	int width;
};

struct TrainVisual {
	TrainVisualKind kind;
	QColor fill;
	QColor outline;
	TrainBadgeShape shape;
	QString iconResource;
};

struct StationVisual {
	StationVisualKind kind;
	QColor fill;
	QColor outline;
	QString iconResource;
};

struct TrackStateVisual {
	QColor color;
	int width;
	Qt::PenStyle style;
};

struct SignalVisual {
	QColor lamp;
	SignalCueKind cue;
	QString iconResource;
};

TrackVisual classifyTrackSpeed(double speedLimitMetersPerSecond);
TrackStateVisual classifyTrackState(TrackOperationalState state);
int trackStatePriority(TrackOperationalState state);
TrainVisual classifyTrainType(const std::string& type, const std::string& description);
TrainBadgeShape classifyTrainBadgeShape(TrainVisualKind kind);
int trainBadgeCornerRadius(TrainBadgeShape shape);
SignalVisual classifySignalAspect(int code);
SignalCueKind classifySignalCue(int code);
StationVisual classifyStation(bool hasPlatformId, int connectionCount);
QString simulationSpeedLabel(int delayMs);
QString simulationSpeedMode(int delayMs);

#endif // VISUALPOLISH_H
