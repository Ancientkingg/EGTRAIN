#ifndef TRACKPREVIEW_H
#define TRACKPREVIEW_H

#include <string>
#include <vector>

struct TrackPreviewPoint {
	double x = 0.0;
	double y = 0.0;
	std::string nodeId;
	double rawX = 0.0; // Source x for position-only station lookup.
};

struct TrackPreviewLine {
	std::string id;
	std::vector<TrackPreviewPoint> points;
	double displayOffset = 0.0;
};

struct TrackPreviewConnection {
	std::string firstTrackId;
	std::string firstNodeId;
	std::string secondTrackId;
	std::string secondNodeId;
};

struct TrackPreviewStation {
	std::string name;
	std::string nodeId;
	double x = 0.0;
	bool hasPlatform = false;
};

struct TrackPreviewSignal {
	std::string trackId;
	std::string sectionId;
	std::string nodeId;
	double rawX = 0.0;
};

struct TrackPreviewResult {
	std::vector<TrackPreviewLine> lines;
	std::vector<TrackPreviewConnection> connections;
	std::vector<TrackPreviewStation> stations;
	std::vector<TrackPreviewSignal> previewSignals;
	std::vector<std::string> warnings;
};

struct SceneModel;

TrackPreviewResult loadTrackPreview(const SceneModel& scene);

#endif
