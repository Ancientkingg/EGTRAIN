#ifndef VISUALPOLISH_H
#define VISUALPOLISH_H

#include <QColor>
#include <QIcon>
#include <QPen>
#include <QRectF>
#include <QString>
#include <string>

class QPainter;

/// The kind of a train, from substrings of its type and description, tested in the order Freight, HighSpeed,
/// Sprinter, Intercity; the first match wins. It chooses the icon and the badge shape.
enum class TrainVisualKind { Passenger, ///< None of the substrings below.
	Sprinter,							///< "sprinter" or "spr".
	Intercity,							///< "intercity", " ic" or "ic ".
	HighSpeed,							///< "ice", "hst", "highspeed" or "high speed".
	Freight /**< "freight", "cargo" or "goederen". */ };
/// The shape of a train badge.
enum class TrainBadgeShape { Rounded, ///< Corners with a radius of 3 pixels.
	Capsule,						  ///< Corners with a radius of 8 pixels.
	Square /**< Square corners. */ };
/// The state of a track line during a run. The inspector shows it; the canvas paints only Occupied, in
/// occupiedTrackColor().
enum class TrackOperationalState { Free, ///< None of the states below.
	Prepared,							 ///< A route through the section has a signal code other than 0.
	Occupied,							 ///< A train occupies the arc.
	Blocked /**< A signal failure in the section is active. */ };
/// The state that a signal head shows besides its colour.
enum class SignalCueKind { Neutral, ///< Any other aspect code: the head is unavailable.
	Stop,							///< Aspect codes 0 and 751.
	Caution,						///< Aspect code 75.
	Proceed /**< Aspect codes 180 and 270. */ };
/// The class of a station, which chooses its mark.
enum class StationClass { Halt, ///< No platform.
	Station,					///< At least one platform.
	Major /**< Three or more connections at its node. */ };

/// The look of a track line.
struct TrackVisual {
	QColor color; ///< The colour of the line.
	int width;	  ///< The width of the cosmetic pen in pixels.
};

/// The look of a train.
struct TrainVisual {
	TrainVisualKind kind;  ///< The kind of the train.
	QColor fill;		   ///< The fill of the bodies.
	QColor outline;		   ///< The outline of the bodies.
	TrainBadgeShape shape; ///< The shape of the badge.
	QString iconResource;  ///< The Qt resource path of the icon of the kind.
};

/// The look of the square of a station node.
struct StationVisual {
	QColor fill;	  ///< The fill of the square.
	QColor outline;	  ///< The outline of the square.
	StationClass cls; ///< The class of the station.
};

/// The look of a track state.
struct TrackStateVisual {
	QColor color;		///< The colour of the line.
	int width;			///< The width of the pen in pixels.
	Qt::PenStyle style; ///< The style of the pen.
};

/// The look of a signal head.
struct SignalVisual {
	QColor lamp;		  ///< The colour of the lamp.
	SignalCueKind cue;	  ///< The state besides the colour.
	QString iconResource; ///< The Qt resource path of the icon of the state.
};

/// The look of a free track: that of a track below 120 km/h.
TrackVisual freeTrackVisual();
/// The look of a track by its speed limit in m/s: blue and 4 pixels wide from 200 km/h, light grey and 3 pixels
/// wide from 120 km/h, grey and 2 pixels wide below.
TrackVisual classifyTrackSpeed(double speedLimitMetersPerSecond);
/// The colour of a track line that a train occupies: red, RGB (255, 0, 0), the colour that the original renderer gave
/// an occupied block. The line keeps the width of its speed class.
QColor occupiedTrackColor();
/// The look of a track state. Free has a transparent colour, a width of 0 and no pen.
TrackStateVisual classifyTrackState(TrackOperationalState state);
/// The rank of a state when a track line has more than one; the highest wins: Blocked 3, Occupied 2,
/// Prepared 1, Free 0.
int trackStatePriority(TrackOperationalState state);
/// The fill of a train without a service colour: RGB (235, 210, 55).
QColor defaultTrainFill();
/// The outline of a train without a service colour: RGB (110, 90, 20).
QColor defaultTrainOutline();
/// The kind, badge shape and icon of a train from substrings of its type and description, ignoring case and
/// tested in the order that the TrainVisualKind comment gives, with the default colours.
TrainVisual classifyTrainType(const std::string& type, const std::string& description);
/// classifyTrainType(), with the fill set to serviceColor and the outline to a shade of it half as bright
/// when serviceColor is valid.
TrainVisual resolveTrainVisual(const std::string& type, const std::string& description,
	const QColor& serviceColor = QColor());
/// The badge shape of a kind: a capsule for an intercity, square for freight, rounded for the others.
TrainBadgeShape classifyTrainBadgeShape(TrainVisualKind kind);
/// The corner radius of a badge shape in pixels.
int trainBadgeCornerRadius(TrainBadgeShape shape);
/// The lamp colour, state and icon of a signal head for an aspect code: red, yellow or green by SignalCueKind,
/// grey RGB (128, 128, 128) otherwise.
SignalVisual classifySignalAspect(int code);
/// The state of a signal head for an aspect code, as SignalCueKind lists it.
SignalCueKind classifySignalCue(int code);
/// The look of a station node: Major with three or more connections, Station with a platform id, Halt otherwise.
StationVisual classifyStation(bool hasPlatformId = false, int connectionCount = 0);
/// Paints the mark of a station class centred in frame: a bar for a major station, a disc for a
/// station and a ring for a halt. A halo of haloWidth, in the units of the painter, surrounds it.
void paintStationMark(QPainter* painter, const QRectF& frame, StationClass cls, const QColor& fill,
	const QColor& halo, qreal haloWidth);
/// The mark in its dark variant for light surfaces, at 16 and 32 pixels.
QIcon stationMarkIcon(StationClass cls);
/// The playback speed for a delay of delayMs milliseconds per simulated second, as a factor of real time:
/// 1000 / delayMs with an "x", whole from 10x and with one decimal below. "Fastest" for a delay of 0 or less.
QString simulationSpeedLabel(int delayMs);
/// The label of simulationSpeedLabel() followed by " real time", or "Fastest".
QString simulationSpeedMode(int delayMs);

#endif // VISUALPOLISH_H
