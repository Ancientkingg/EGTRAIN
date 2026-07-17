#ifndef TRACKPREVIEW_H
#define TRACKPREVIEW_H

#include <string>
#include <vector>

struct TrackPreviewPoint {
	double x = 0.0;
	double y = 0.0;
};

struct TrackPreviewLine {
	int id = -1;
	std::vector<TrackPreviewPoint> points;
};

struct TrackPreviewConnection {
	int firstTrackId = -1;
	double firstX = 0.0;
	int secondTrackId = -1;
	double secondX = 0.0;
};

// Station anchor along the corridor; Stations.txt stores one row per
// direction band as "<x>\t<station id>".
struct TrackPreviewStation {
	std::string name;
	double x = 0.0;
};

struct TrackPreviewResult {
	std::vector<TrackPreviewLine> lines;
	std::vector<TrackPreviewConnection> connections;
	std::vector<TrackPreviewStation> stations;
	std::vector<std::string> warnings;
};

TrackPreviewResult loadTrackPreview(const std::string& sceneDir);

#endif
