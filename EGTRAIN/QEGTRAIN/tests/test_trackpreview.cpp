#include "scene/SceneModel.h"
#include "scene/TrackPreview.h"

#include <cmath>
#include <iostream>
#include <utility>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

int main() {
	SceneModel scene;
	scene.tracks = {{"B0"}, {"B1"}};
	scene.nodes = {
		{"B0.Gvc", "B0", 0.0, 1.0},
		{"B0.Gdg", "B0", 28.0, 2.0},
		{"B0.Ut", "B0", 64.0, 3.0},
		{"B1.Ut", "B1", 100.0, -1.0},
		{"B1.Gdg", "B1", 136.0, -2.0},
		{"B1.Gvc", "B1", 164.0, -3.0},
	};
	scene.arcs = {
		{"B0.arc.1", "B0", "B0.Gvc", "B0.Gdg", 0.0, 0.0, 12.0},
		{"B0.arc.2", "B0", "B0.Gdg", "B0.Ut", 0.0, 0.0, 12.0},
		{"B1.arc.1", "B1", "B1.Ut", "B1.Gdg", 0.0, 0.0, 12.0},
		{"B1.arc.2", "B1", "B1.Gdg", "B1.Gvc", 0.0, 0.0, 12.0},
	};
	scene.blocks = {
		{"B0.block.1", "B0", 28.0},
		{"B0.block.2", "B0", 36.0},
		{"B1.block.1", "B1", 36.0},
		{"B1.block.2", "B1", 64.0},
	};
	scene.connections.push_back({"switch.7", "B0.Ut", "B1.Ut", false, 0.0});
	scene.trackViews = {{"B0", 0, 0}, {"B1", 1, 1}};
	scene.stationViews = {
		{"Gvc", 52.0, 0.0, {{0, 0.0}, {1, 164.0}}, {}},
		{"Gdg", 52.25, 0.28, {{0, 28.0}, {1, 136.0}}, {}},
		{"Ut", 52.5, 0.64, {{0, 64.0}, {1, 100.0}}, {}},
	};
	for (const auto& stationData : {
			std::pair<const char*, const char*> {"Gvc", "B0.Gvc"},
			std::pair<const char*, const char*> {"Gdg", "B0.Gdg"},
			std::pair<const char*, const char*> {"Ut", "B0.Ut"}}) {
		SceneStation station;
		station.id = stationData.first;
		station.name = stationData.first;
		station.platforms.push_back({std::string(stationData.first) + ".platform", {stationData.second}});
		scene.stations.push_back(std::move(station));
	}

	const TrackPreviewResult result = loadTrackPreview(scene);
	bool ok = true;
	ok &= expect(result.lines.size() == 2, "preview renders both in-memory tracks without legacy files");
	if (result.lines.size() == 2) {
		const auto& first = result.lines[0];
		const auto& second = result.lines[1];
		ok &= expect(first.id == "B0" && second.id == "B1",
				"preview preserves canonical track IDs");
		ok &= expect(first.points.size() == 3 && second.points.size() == 3,
				"preview retains every ordered node");
		if (first.points.size() == 3 && second.points.size() == 3) {
			ok &= expect(first.points[0].nodeId == "B0.Gvc"
					&& first.points[2].nodeId == "B0.Ut"
					&& second.points[0].nodeId == "B1.Ut"
					&& second.points[2].nodeId == "B1.Gvc",
					"preview points retain canonical node IDs");
			ok &= expect(std::fabs(first.points[0].x - 0.0) < 1e-9
					&& std::fabs(first.points[1].x - 0.28) < 1e-9
					&& std::fabs(first.points[2].x - 0.64) < 1e-9,
					"station display anchors map track distance to longitude");
			ok &= expect(first.points[0].rawX == 0.0 && first.points[2].rawX == 64.0
					&& second.points[0].rawX == 100.0 && second.points[2].rawX == 164.0
					&& std::fabs(first.points[0].y - (-61.08656629615305)) < 1e-9
					&& std::fabs(first.points[1].y - (-61.49377304672194)) < 1e-9
					&& std::fabs(first.points[2].y - (-61.90328104077144)) < 1e-9,
					"preview maps station latitude to Mercator display y");
			ok &= expect(std::fabs(std::hypot(first.points[0].x - second.points[2].x,
					first.points[0].y - second.points[2].y) - 0.0006) < 1e-9
					&& std::fabs(std::hypot(first.points[1].x - second.points[1].x,
						first.points[1].y - second.points[1].y) - 0.0006) < 1e-9
					&& std::fabs(std::hypot(first.points[2].x - second.points[0].x,
						first.points[2].y - second.points[0].y) - 0.0006) < 1e-9,
					"authored track levels separate geographic tracks perpendicular to the route");
		}
		ok &= expect(first.displayOffset == 0.0 && second.displayOffset == 0.0,
				"geographic track separation is carried by mapped points");
	}
	ok &= expect(result.connections.size() == 1, "preview resolves one canonical connection");
	if (!result.connections.empty()) {
		const auto& connection = result.connections.front();
		ok &= expect(connection.firstTrackId == "B0" && connection.firstNodeId == "B0.Ut"
				&& connection.secondTrackId == "B1" && connection.secondNodeId == "B1.Ut",
				"connection endpoints resolve through canonical node references");
	}
	SceneModel hidden = scene;
	hidden.trackViews[1].visible = false;
	hidden.stations.front().platforms.insert(hidden.stations.front().platforms.begin(),
		{"Gvc.hidden-platform", {"B1.Gvc"}});
	const TrackPreviewResult hiddenResult = loadTrackPreview(hidden);
	ok &= expect(hiddenResult.lines.size() == 1 && hiddenResult.lines.front().id == "B0"
			&& hiddenResult.connections.empty(),
			"hidden display tracks and their connections stay out of the preview");
	ok &= expect(!hiddenResult.stations.empty()
			&& hiddenResult.stations.front().nodeId == "B0.Gvc",
			"station preview skips hidden platform tracks when choosing its anchor");
	ok &= expect(hiddenResult.previewSignals.size() == 1
			&& hiddenResult.previewSignals.front().trackId == "B0",
			"hidden tracks do not leave orphaned preview signals");
	ok &= expect(result.stations.size() == 3, "preview renders one station anchor per station");
	if (!result.stations.empty()) {
		const auto& stationAnchor = result.stations.front();
		ok &= expect(stationAnchor.name == "Gvc" && stationAnchor.nodeId == "B0.Gvc"
				&& stationAnchor.x == 0.0 && stationAnchor.hasPlatform,
				"station preview prefers the platform node anchor");
	}
	ok &= expect(result.previewSignals.size() == 2,
			"preview emits later base boundaries but skips each track's first section");
	if (result.previewSignals.size() == 2) {
		ok &= expect(result.previewSignals[0].trackId == "B0"
				&& result.previewSignals[0].sectionId == "@B0.block.2@"
				&& result.previewSignals[0].nodeId == "B0.Gdg"
				&& result.previewSignals[0].rawX == 28.0
				&& result.previewSignals[1].trackId == "B1"
				&& result.previewSignals[1].sectionId == "@B1.block.2@"
				&& result.previewSignals[1].nodeId == "B1.Gdg"
				&& result.previewSignals[1].rawX == 136.0,
				"preview signal identity uses the base section boundary and source coordinate");
	}
	for (const auto& signal : result.previewSignals)
		ok &= expect(signal.sectionId.find('/') == std::string::npos,
				"connection-derived sections do not produce preview signals");

	SceneModel doubleSwitch;
	doubleSwitch.tracks = {{"double-switch"}};
	doubleSwitch.nodes = {
		{"double-switch.start", "double-switch", 10.0, 0.0},
		{"double-switch.mid.1", "double-switch", 10.01, 0.0},
		{"double-switch.mid.2", "double-switch", 10.02, 0.0},
		{"double-switch.end", "double-switch", 10.03, 0.0},
	};
	doubleSwitch.arcs = {
		{"double-switch.arc.1", "double-switch", "double-switch.start", "double-switch.mid.1", 0.0, 0.0, 12.0},
		{"double-switch.arc.2", "double-switch", "double-switch.mid.1", "double-switch.mid.2", 0.0, 0.0, 12.0},
		{"double-switch.arc.3", "double-switch", "double-switch.mid.2", "double-switch.end", 0.0, 0.0, 12.0},
	};
	doubleSwitch.blocks = {
		{"double-switch.block.1", "double-switch", 0.015},
		{"double-switch.block.2", "double-switch", 0.015},
	};
	const TrackPreviewResult switchResult = loadTrackPreview(doubleSwitch);
	ok &= expect(switchResult.previewSignals.empty(),
			"legacy three-segment double-switch midpoint remains signal-free");

	SceneModel insufficient;
	insufficient.tracks = {{"raw"}};
	insufficient.nodes = {{"raw.start", "raw", 10.0, 0.0}, {"raw.end", "raw", 20.0, 0.0}};
	insufficient.arcs = {{"raw.arc", "raw", "raw.start", "raw.end", 0.0, 0.0, 12.0}};
	insufficient.trackViews = {{"raw", 0, 0}};
	insufficient.stationViews = {{"only", 1.0, 0.5, {{0, 10.0}}, {}}};
	const TrackPreviewResult unchanged = loadTrackPreview(insufficient);
	ok &= expect(unchanged.lines.size() == 1 && unchanged.lines[0].points.size() == 2
			&& unchanged.lines[0].points[0].x == 10.0 && unchanged.lines[0].points[1].x == 20.0,
			"insufficient station metadata leaves raw preview x unchanged");
	return ok ? 0 : 1;
}
