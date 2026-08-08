#include "scene/SceneModel.h"
#include "scene/TrackPreview.h"

#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

int main() {
	SceneModel scene;
	scene.tracks = {{"line-z"}, {"line.a"}};
	scene.nodes = {
		{"z.start", "line-z", 10.0, 1.0},
		{"z.end", "line-z", 20.0, 2.0},
		{"a.start", "line.a", 10.0, -1.0},
		{"a.end", "line.a", 20.0, -2.0},
	};
	scene.arcs = {
		{"z.arc", "line-z", "z.start", "z.end", 0.0, 0.0, 12.0},
		{"a.arc", "line.a", "a.start", "a.end", 0.0, 0.0, 12.0},
	};
	scene.connections.push_back({"switch.7", "z.end", "a.end", false, 0.0});
	SceneStation station;
	station.id = "station.anchor";
	station.name = "Anchor";
	station.platforms.push_back({"platform-east", {"z.start"}});
	scene.stations.push_back(station);

	const TrackPreviewResult result = loadTrackPreview(scene);
	bool ok = true;
	ok &= expect(result.lines.size() == 2, "preview renders both in-memory tracks without legacy files");
	if (result.lines.size() == 2) {
		ok &= expect(result.lines[0].id == "line-z" && result.lines[1].id == "line.a",
				"preview preserves arbitrary string track IDs in deterministic order");
		ok &= expect(result.lines[0].points.front().nodeId == "z.start"
				&& result.lines[0].points.back().nodeId == "z.end",
				"preview points retain canonical node IDs");
	}
	ok &= expect(result.connections.size() == 1, "preview resolves one canonical connection");
	if (!result.connections.empty()) {
		const auto& connection = result.connections.front();
		ok &= expect(connection.firstTrackId == "line-z" && connection.firstNodeId == "z.end"
				&& connection.secondTrackId == "line.a" && connection.secondNodeId == "a.end",
				"connection endpoints resolve through canonical node references");
	}
	ok &= expect(result.stations.size() == 1, "preview renders one station anchor");
	if (!result.stations.empty()) {
		const auto& stationAnchor = result.stations.front();
		ok &= expect(stationAnchor.name == "Anchor" && stationAnchor.nodeId == "z.start"
				&& stationAnchor.x == 10.0,
				"station preview prefers the platform node anchor");
	}
	return ok ? 0 : 1;
}
