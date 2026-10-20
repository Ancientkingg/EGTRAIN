#include "scene/SceneModel.h"
#include "scene/TrackPreview.h"
#include "graphics/AnnotationPlacement.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
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
			 std::pair<const char*, const char*>{"Gvc", "B0.Gvc"},
			 std::pair<const char*, const char*>{"Gdg", "B0.Gdg"},
			 std::pair<const char*, const char*>{"Ut", "B0.Ut"}}) {
		SceneStation station;
		station.id = stationData.first;
		station.name = stationData.first;
		station.platforms.push_back({std::string(stationData.first) + ".platform", {stationData.second}});
		scene.stations.push_back(std::move(station));
	}

	const TrackPreviewResult result = loadTrackPreview(scene);
	bool ok = true;
	ok &= expect(result.connections.size() == 1 && result.connections.front().id == "switch.7",
		"preview retains canonical source connection ID");
	SceneModel duplicateConnections = scene;
	duplicateConnections.connections.push_back({"switch.duplicate", "B0.Ut", "B1.Ut", false, 0.0});
	const auto duplicates = normalizeTrackPreview(loadTrackPreview(duplicateConnections));
	ok &= expect(duplicates.connections.size() == 2
			&& duplicates.connections[0].id == "switch.7"
			&& duplicates.connections[1].id == "switch.duplicate"
			&& duplicates.connections[0].firstNodeId == duplicates.connections[1].firstNodeId,
		"duplicate endpoint connections retain distinct canonical identities through normalization");

	TrackPreviewLine signalLine{"signal", {{0.0, 0.0, "a", 0.0}, {10.0, 0.0, "b", 10.0}}, 0.0};
	std::pair<double, double> normal;
	ok &= expect(annotationSignalNormal(signalLine, 5.0, normal)
			&& std::fabs(normal.first) < 1e-9 && std::fabs(normal.second + 1.0) < 1e-9,
		"increasing chainage horizontal signal heads use the historical upper normal");
	signalLine.points[1].y = 10.0;
	ok &= expect(annotationSignalNormal(signalLine, 5.0, normal)
			&& std::fabs(normal.first - std::sqrt(0.5)) < 1e-9
			&& std::fabs(normal.second + std::sqrt(0.5)) < 1e-9,
		"diagonal signal heads use the chainage-directed normal");
	std::reverse(signalLine.points.begin(), signalLine.points.end());
	ok &= expect(annotationSignalNormal(signalLine, 5.0, normal)
			&& std::fabs(normal.first - std::sqrt(0.5)) < 1e-9
			&& std::fabs(normal.second + std::sqrt(0.5)) < 1e-9,
		"reversed storage does not reverse signal heads");
	TrackPreviewLine bentSignalLine{"bend", {{0.0, 0.0, "a", 0.0}, {10.0, 0.0, "b", 10.0}, {10.0, 10.0, "c", 20.0}}, 0.0};
	for (int order = 0; order < 2; ++order) {
		for (double chainage : {0.0, 9.0, 10.0, 11.0, 20.0}) {
			const double expectedX = chainage < 10.0 ? 0.0 : 1.0;
			const double expectedY = chainage < 10.0 ? -1.0 : 0.0;
			ok &= expect(annotationSignalNormal(bentSignalLine, chainage, normal)
					&& std::fabs(normal.first - expectedX) < 1e-9
					&& std::fabs(normal.second - expectedY) < 1e-9,
				"bend boundaries use the outgoing chainage interval in either storage order");
		}
		std::reverse(bentSignalLine.points.begin(), bentSignalLine.points.end());
	}
	signalLine.points[0].rawX = signalLine.points[1].rawX = 5.0;
	ok &= expect(!annotationSignalNormal(signalLine, 5.0, normal),
		"degenerate chainage cannot determine signal handedness");
	const std::vector<SceneStationView> placementViews{
		{"A", 0.0, 0.0, {{0, 0.0}}, {}},
		{"B", 0.0, 1.0, {{0, 1.0}, {1, 1.0}}, {}},
		{"C", 0.0, 2.0, {{0, 2.0}}, {}},
		{"D", -1.0, 2.0, {{1, 2.0}}, {}},
		{"E", -2.0, 2.0, {{1, 3.0}}, {}}};
	ok &= expect(stationNamedYOffset("Koge") == 900.0
			&& stationNamedYOffset("Dybbolsbro") == -920.0
			&& stationNamedYOffset("unknown") == 0.0,
		"historical named adjustments apply once on top of regional shift");
	const auto firstShift = annotationStationShift(placementViews, "A");
	const auto lastShift = annotationStationShift(placementViews, "C");
	ok &= expect(std::fabs(firstShift.first) < 1e-9
			&& std::fabs(firstShift.second - 1200.0) < 1e-9
			&& std::fabs(lastShift.second - 1200.0) < 1e-9,
		"first and last horizontal station decorations shift down 1200");
	const auto bendShift = annotationStationShift(placementViews, "D");
	const auto multiShift = annotationStationShift(placementViews, "B");
	ok &= expect(std::isfinite(bendShift.first) && std::isfinite(bendShift.second)
			&& std::fabs(bendShift.first) > 1.0 && std::fabs(bendShift.second) > 1.0,
		"station bend uses the regional bisector");
	ok &= expect(std::isfinite(multiShift.first) && std::isfinite(multiShift.second)
			&& std::fabs(multiShift.first) > 1.0 && std::fabs(multiShift.second) < 1200.0,
		"station with multiple regions averages independent shifts");
	ok &= expect(annotationStationShift({}, "A") == std::make_pair(0.0, 0.0)
			&& annotationStationShift(placementViews, "missing") == std::make_pair(0.0, 0.0),
		"absent station geometry leaves semantic anchors unchanged");
	const std::vector<SceneStationView> collapsed{
		{"A", 0.0, 0.0, {{0, 0.0}}, {}},
		{"B", 0.0, 0.0, {{0, 1.0}}, {}}};
	ok &= expect(annotationStationShift(collapsed, "A") == std::make_pair(0.0, 0.0),
		"coincident geographic stations cannot introduce nonfinite decoration offsets");
	const TrackPreviewResult normalized = normalizeTrackPreview(result);
	ok &= expect(normalized.lines.size() == result.lines.size(),
		"normalization retains every projected line");
	if (normalized.lines.size() == result.lines.size()) {
		const auto& sourcePoint = result.lines[0].points[1];
		const auto& normalizedPoint = normalized.lines[0].points[1];
		ok &= expect(sourcePoint.rawX == normalizedPoint.rawX,
			"normalization preserves raw chainage");
		const double scaleX = (normalized.lines[0].points[2].x - normalized.lines[0].points[0].x)
			/ (result.lines[0].points[2].x - result.lines[0].points[0].x);
		const double scaleY = (normalized.lines[0].points[2].y - normalized.lines[0].points[0].y)
			/ (result.lines[0].points[2].y - result.lines[0].points[0].y);
		ok &= expect(std::fabs(scaleX - scaleY) < 1e-9 && scaleX > 1000.0,
			"normalization applies one stable uniform projected scale");
		TrackPreviewPoint nodePoint;
		ok &= expect(trackPreviewPointAtNode(normalized.lines[0], "B0.Gdg", nodePoint)
				&& std::fabs(nodePoint.x - normalized.lines[0].points[1].x) < 1e-9
				&& std::fabs(nodePoint.y - normalized.lines[0].points[1].y) < 1e-9,
			"point lookup resolves normalized node anchors");
		TrackPreviewLine duplicateChainage = normalized.lines[0];
		duplicateChainage.points[2].rawX = duplicateChainage.points[1].rawX;
		ok &= expect(trackPreviewPointAtNode(duplicateChainage, "B0.Ut", nodePoint)
				&& nodePoint.nodeId == "B0.Ut",
			"node identity disambiguates equal-chainage runtime anchors");
		TrackPreviewPoint interpolated;
		ok &= expect(
			trackPreviewPointAtX(normalized.lines[0], 32.0, interpolated) && interpolated.rawX == 32.0
				&& std::fabs(
					   interpolated.x
					   - (normalized.lines[0].points[1].x
						   + (normalized.lines[0].points[2].x - normalized.lines[0].points[1].x) * 4.0 / 36.0))
					< 1e-9,
			"point lookup interpolates by raw chainage");
	}
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
									   first.points[0].y - second.points[2].y)
							 - 0.0006)
						< 1e-9
					&& std::fabs(std::hypot(first.points[1].x - second.points[1].x,
									 first.points[1].y - second.points[1].y)
						   - 0.0006)
						< 1e-9
					&& std::fabs(std::hypot(first.points[2].x - second.points[0].x,
									 first.points[2].y - second.points[0].y)
						   - 0.0006)
						< 1e-9,
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
	SceneModel schematic = scene;
	for (auto& station : schematic.stationViews)
		station.latitude = 1.0;
	for (auto& node : schematic.nodes)
		node.yKm = 0.0;
	const TrackPreviewResult schematicResult = loadTrackPreview(schematic);
	const TrackPreviewResult normalizedSchematic = normalizeTrackPreview(schematicResult);
	ok &= expect(schematicResult.lines.size() == 2
			&& std::fabs(schematicResult.lines[0].points[1].x - 0.28) < 1e-9
			&& schematicResult.lines[0].points[0].y == 0.0
			&& schematicResult.lines[1].points[0].y == 0.0
			&& schematicResult.lines[0].displayOffset == 0.0
			&& std::fabs(schematicResult.lines[1].displayOffset - 0.0012) < 1e-9,
		"constant-latitude display anchors scale schematic track levels with mapped x");
	ok &= expect(normalizedSchematic.lines.size() == 2
			&& std::fabs(normalizedSchematic.lines[0].points[1].x - 28000.0) < 1e-9
			&& std::fabs(normalizedSchematic.lines[1].displayOffset - 120.0) < 1e-9,
		"schematic normalization keeps two-level spacing in runtime units");

	// Constant-latitude Paimpol equivalent: one degree is 100 km.
	ok &= expect(std::fabs(normalizedSchematic.normalizationScale - 100000.0) < 1e-6
			&& std::fabs(normalizedSchematic.presentationScale - 0.45) < 1e-12
			&& normalizedSchematic.normalized
			&& std::all_of(schematicResult.lines.begin(), schematicResult.lines.end(),
				[](const auto& line) { return line.authoredStationProjection; }),
		"schematic presentation uses measured historical construction units");
	const auto repeated = normalizeTrackPreview(normalizedSchematic);
	ok &= expect(repeated.normalizationScale == normalizedSchematic.normalizationScale
			&& repeated.presentationScale == normalizedSchematic.presentationScale
			&& repeated.lines[1].displayOffset == normalizedSchematic.lines[1].displayOffset
			&& repeated.lines[0].points[1].x == normalizedSchematic.lines[0].points[1].x,
		"repeat normalization preserves geometry and unit metadata without double scaling");
	const double geographicScale = (normalized.lines[0].points[2].x - normalized.lines[0].points[0].x)
		/ (result.lines[0].points[2].x - result.lines[0].points[0].x);
	ok &= expect(std::fabs(normalized.normalizationScale - geographicScale) < 1e-6
			&& std::fabs(normalized.presentationScale - geographicScale / (800000.0 * 100.0 / 360.0)) < 1e-12
			&& result.lines[0].authoredStationProjection && result.lines[1].authoredStationProjection,
		"geographic projection uses actual normalization, not a schematic case multiplier");
	ok &= expect(schematic.nodes[1].xKm == 28.0 && schematic.nodes[1].yKm == 0.0
			&& schematic.trackViews[1].level == 1 && schematic.stationViews[1].longitude == 0.28
			&& normalizedSchematic.lines[0].points[1].rawX == 28.0,
		"presentation metadata does not mutate authored coordinates, levels or chainage");
	for (int fallback = 0; fallback < 7; ++fallback) {
		SceneModel candidate = schematic;
		if (fallback == 0) candidate.trackViews.clear();
		if (fallback == 1) candidate.stationViews.clear();
		if (fallback == 2) candidate.trackViews.pop_back(); // Mixed projected/raw scene.
		if (fallback == 3) candidate.stationViews[1].latitude = 90.0;
		if (fallback == 4) candidate.stationViews[1].regions[0].second = std::numeric_limits<double>::quiet_NaN();
		if (fallback == 5) candidate.nodes[1].xKm = std::numeric_limits<double>::infinity();
		if (fallback == 6) candidate.nodes[1].yKm = std::numeric_limits<double>::infinity();
		ok &= expect(normalizeTrackPreview(loadTrackPreview(candidate)).presentationScale == 1.0,
			"raw, absent, mixed, invalid or nonfinite projection retains whole-scene presentation units");
	}
	ok &= expect(normalizeTrackPreview({}).presentationScale == 1.0,
		"empty normalization retains presentation factor one");
	TrackPreviewResult degenerate = schematicResult;
	for (auto& line : degenerate.lines) {
		line.displayOffset = 0;
		for (auto& point : line.points) point.x = point.y = 0;
	}
	ok &= expect(normalizeTrackPreview(degenerate).presentationScale == 1.0,
		"degenerate normalization retains presentation factor one");
	TrackPreviewResult overflow = schematicResult;
	for (auto& line : overflow.lines) {
		line.displayOffset = 0;
		for (std::size_t i = 0; i < line.points.size(); ++i) {
			line.points[i].x = 1e308;
			line.points[i].y = static_cast<double>(i);
		}
	}
	ok &= expect(normalizeTrackPreview(overflow).presentationScale == 1.0,
		"nonfinite normalized coordinates retain presentation factor one");
	SceneModel onlyVisible = schematic;
	onlyVisible.trackViews[1].visible = false;
	onlyVisible.trackViews[1].region = 99;
	ok &= expect(std::fabs(normalizeTrackPreview(loadTrackPreview(onlyVisible)).presentationScale - 0.45) < 1e-12,
		"unmapped hidden lines do not disqualify successful visible projection");

	TrackPreviewPoint schematicStationPoint;
	ok &= expect(trackPreviewPointAtNode(normalizedSchematic.lines[1], "B1.Ut", schematicStationPoint)
			&& std::fabs(schematicStationPoint.x - normalizedSchematic.lines[1].points[0].x) < 1e-9,
		"schematic point lookup resolves the second-level station anchor");
	// Netherlands geographic corridor, with the committed endpoint latitudes/longitudes.
	SceneModel netherlands;
	netherlands.tracks = {{"corridor"}};
	netherlands.nodes = {{"south", "corridor", 0.0, 0.0}, {"north", "corridor", 48.362, 0.0}};
	netherlands.arcs = {{"corridor-arc", "corridor", "south", "north", 0.0, 0.0, 20.0}};
	netherlands.trackViews = {{"corridor", 0, 0}};
	netherlands.stationViews = {
		{"south", 52.089722, 4.872837, {{0, 0.0}}, {}},
		{"north", 52.378383, 5.310205, {{0, 48.362}}, {}}};
	const auto dutchProjection = loadTrackPreview(netherlands);
	const auto dutchNormalized = normalizeTrackPreview(dutchProjection);
	const double dutchSpan = std::max(5.310205 - 4.872837,
		std::fabs(dutchProjection.lines[0].points[1].y - dutchProjection.lines[0].points[0].y));
	const double dutchScale = 48362.0 / dutchSpan;
	ok &= expect(dutchProjection.lines[0].authoredStationProjection
			&& std::fabs(dutchNormalized.normalizationScale - dutchScale) < 1e-6
			&& std::fabs(dutchNormalized.presentationScale - dutchScale / (800000.0 * 100.0 / 360.0)) < 1e-12
			&& dutchNormalized.lines[0].points[1].rawX == 48.362
			&& dutchNormalized.lines[0].points[0].x == 4.872837 * dutchScale,
		"Netherlands geographic corridor retains physical anchors and derives its own presentation conversion");
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
				&& stationAnchor.x == 0.0 && stationAnchor.hasPlatform
				&& stationAnchor.id == "Gvc",
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
