#ifndef VISUALPOLISH_H
#define VISUALPOLISH_H

#include <QColor>
#include <QIcon>
#include <QPen>
#include <QRectF>
#include <QString>
#include <string>

class QPainter;

enum class TrainVisualKind { Passenger,
	Sprinter,
	Intercity,
	HighSpeed,
	Freight };
enum class TrainBadgeShape { Rounded,
	Capsule,
	Square };
enum class TrackOperationalState { Free,
	Prepared,
	Occupied,
	Blocked };
enum class SignalCueKind { Neutral,
	Stop,
	Caution,
	Proceed };
enum class StationClass { Halt,
	Station,
	Major };

struct TrackVisual {
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
	QColor fill;
	QColor outline;
	StationClass cls;
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

TrackVisual freeTrackVisual();
TrackVisual classifyTrackSpeed(double speedLimitMetersPerSecond);
TrackStateVisual classifyTrackState(TrackOperationalState state);
int trackStatePriority(TrackOperationalState state);
QColor defaultTrainFill();
QColor defaultTrainOutline();
TrainVisual classifyTrainType(const std::string& type, const std::string& description);
TrainVisual resolveTrainVisual(const std::string& type, const std::string& description,
	const QColor& serviceColor = QColor());
TrainBadgeShape classifyTrainBadgeShape(TrainVisualKind kind);
int trainBadgeCornerRadius(TrainBadgeShape shape);
SignalVisual classifySignalAspect(int code);
SignalCueKind classifySignalCue(int code);
StationVisual classifyStation(bool hasPlatformId = false, int connectionCount = 0);
// Paints the mark of a station class centred in frame: a bar for a major station, a disc for a
// station and a ring for a halt. A halo of haloWidth, in the units of the painter, surrounds it.
void paintStationMark(QPainter* painter, const QRectF& frame, StationClass cls, const QColor& fill,
	const QColor& halo, qreal haloWidth);
// The mark in its dark variant for light surfaces, at 16 and 32 pixels.
QIcon stationMarkIcon(StationClass cls);
QString simulationSpeedLabel(int delayMs);
QString simulationSpeedMode(int delayMs);

#endif // VISUALPOLISH_H
