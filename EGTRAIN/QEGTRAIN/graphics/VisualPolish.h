#ifndef VISUALPOLISH_H
#define VISUALPOLISH_H

#include <QColor>
#include <QString>
#include <string>

enum class TrackVisualKind { Local, Mainline, HighSpeed };
enum class TrainVisualKind { Passenger, Sprinter, Intercity, HighSpeed, Freight };
enum class StationVisualKind { StopMarker, Platform, Interchange };

struct TrackVisual {
	TrackVisualKind kind;
	QColor color;
	int width;
};

struct TrainVisual {
	TrainVisualKind kind;
	QColor fill;
	QColor outline;
};

struct StationVisual {
	StationVisualKind kind;
	QColor fill;
	QColor outline;
};

TrackVisual classifyTrackSpeed(double speedLimitMetersPerSecond);
TrainVisual classifyTrainType(const std::string& type, const std::string& description);
StationVisual classifyStation(bool hasPlatformId, int connectionCount);
QString simulationSpeedLabel(int delayMs);
QString simulationSpeedMode(int delayMs);

#endif // VISUALPOLISH_H
