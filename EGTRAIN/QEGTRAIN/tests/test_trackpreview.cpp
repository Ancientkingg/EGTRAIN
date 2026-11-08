#include "scene/SceneModel.h"
#include "scene/TrackPreview.h"
#include "graphics/AnnotationPlacement.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {
// Two tracks of three nodes, B0 forward and B1 back, joined by a switch at Ut, with a station at each node and a
// geographic view of both tracks.
SceneModel twoTrackScene() {
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
	return scene;
}
}

class TestTrackPreview : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void keepsConnectionIds();
	void signalNormals();
	void bendUsesOutgoingInterval_data();
	void bendUsesOutgoingInterval();
	void shiftsStationDecorations();
	void normalizesGeographicPreview();
	void projectsTracks();
	void resolvesConnection();
	void scalesSchematicLevels();
	void schematicPresentationUnits();
	void geographicPresentationUnits();
	void keepsAuthoredCoordinates();
	void fallsBackToWholeScenePresentationUnits_data();
	void fallsBackToWholeScenePresentationUnits();
	void keepsPresentationFactorOne();
	void ignoresUnmappedHiddenLines();
	void looksUpSchematicStationAnchor();
	void projectsNetherlandsCorridor();
	void leavesHiddenTracksOut();
	void anchorsStations();
	void namesPreviewSignals();
	void namesAuthoredSignalsFirst();
	void skipsConnectionSections();
	void leavesDoubleSwitchSignalFree();
	void keepsRawXWithoutStationMetadata();

private:
	SceneModel m_scene;
	TrackPreviewResult m_result;
	TrackPreviewResult m_normalized;
	// The scene with every station at one latitude and every node at y 0.
	SceneModel m_schematic;
	TrackPreviewResult m_schematicResult;
	TrackPreviewResult m_normalizedSchematic;
};

void TestTrackPreview::initTestCase() {
	m_scene = twoTrackScene();
	m_result = loadTrackPreview(m_scene);
	m_normalized = normalizeTrackPreview(m_result);
	m_schematic = m_scene;
	for (auto& station : m_schematic.stationViews)
		station.latitude = 1.0;
	for (auto& node : m_schematic.nodes)
		node.yKm = 0.0;
	m_schematicResult = loadTrackPreview(m_schematic);
	m_normalizedSchematic = normalizeTrackPreview(m_schematicResult);
}

void TestTrackPreview::keepsConnectionIds() {
	// The preview retains the canonical source connection ID.
	QCOMPARE(m_result.connections.size(), std::size_t(1));
	QCOMPARE(m_result.connections.front().id, "switch.7");
	SceneModel duplicateConnections = m_scene;
	duplicateConnections.connections.push_back({"switch.duplicate", "B0.Ut", "B1.Ut", false, 0.0});
	const auto duplicates = normalizeTrackPreview(loadTrackPreview(duplicateConnections));
	// Duplicate endpoint connections retain distinct canonical identities through normalization.
	QCOMPARE(duplicates.connections.size(), std::size_t(2));
	QCOMPARE(duplicates.connections[0].id, "switch.7");
	QCOMPARE(duplicates.connections[1].id, "switch.duplicate");
	QCOMPARE(duplicates.connections[0].firstNodeId, duplicates.connections[1].firstNodeId);
}

void TestTrackPreview::signalNormals() {
	TrackPreviewLine signalLine{"signal", {{0.0, 0.0, "a", 0.0}, {10.0, 0.0, "b", 10.0}}, 0.0};
	std::pair<double, double> normal;
	// Increasing chainage horizontal signal heads use the historical upper normal.
	QVERIFY(annotationSignalNormal(signalLine, 5.0, normal));
	QCOMPARE_LT(std::fabs(normal.first), 1e-9);
	QCOMPARE_LT(std::fabs(normal.second + 1.0), 1e-9);
	signalLine.points[1].y = 10.0;
	// Diagonal signal heads use the chainage-directed normal.
	QVERIFY(annotationSignalNormal(signalLine, 5.0, normal));
	QCOMPARE_LT(std::fabs(normal.first - std::sqrt(0.5)), 1e-9);
	QCOMPARE_LT(std::fabs(normal.second + std::sqrt(0.5)), 1e-9);
	std::reverse(signalLine.points.begin(), signalLine.points.end());
	// Reversed storage does not reverse signal heads.
	QVERIFY(annotationSignalNormal(signalLine, 5.0, normal));
	QCOMPARE_LT(std::fabs(normal.first - std::sqrt(0.5)), 1e-9);
	QCOMPARE_LT(std::fabs(normal.second + std::sqrt(0.5)), 1e-9);
	signalLine.points[0].rawX = signalLine.points[1].rawX = 5.0;
	QVERIFY2(!annotationSignalNormal(signalLine, 5.0, normal),
		"degenerate chainage cannot determine signal handedness");
}

void TestTrackPreview::bendUsesOutgoingInterval_data() {
	QTest::addColumn<bool>("reversed");
	QTest::addColumn<double>("chainage");
	for (bool reversed : {false, true})
		for (double chainage : {0.0, 9.0, 10.0, 11.0, 20.0})
			QTest::addRow("%s %g", reversed ? "reversed" : "forward", chainage) << reversed << chainage;
}

// Bend boundaries use the outgoing chainage interval in either storage order.
void TestTrackPreview::bendUsesOutgoingInterval() {
	QFETCH(bool, reversed);
	QFETCH(double, chainage);
	TrackPreviewLine bentSignalLine{"bend", {{0.0, 0.0, "a", 0.0}, {10.0, 0.0, "b", 10.0}, {10.0, 10.0, "c", 20.0}}, 0.0};
	if (reversed)
		std::reverse(bentSignalLine.points.begin(), bentSignalLine.points.end());
	const double expectedX = chainage < 10.0 ? 0.0 : 1.0;
	const double expectedY = chainage < 10.0 ? -1.0 : 0.0;
	std::pair<double, double> normal;
	QVERIFY(annotationSignalNormal(bentSignalLine, chainage, normal));
	QCOMPARE_LT(std::fabs(normal.first - expectedX), 1e-9);
	QCOMPARE_LT(std::fabs(normal.second - expectedY), 1e-9);
}

void TestTrackPreview::shiftsStationDecorations() {
	const std::vector<SceneStationView> placementViews{
		{"A", 0.0, 0.0, {{0, 0.0}}, {}},
		{"B", 0.0, 1.0, {{0, 1.0}, {1, 1.0}}, {}},
		{"C", 0.0, 2.0, {{0, 2.0}}, {}},
		{"D", -1.0, 2.0, {{1, 2.0}}, {}},
		{"E", -2.0, 2.0, {{1, 3.0}}, {}}};
	// Historical named adjustments apply once on top of regional shift.
	QCOMPARE_EQ(stationNamedYOffset("Koge"), 900.0);
	QCOMPARE_EQ(stationNamedYOffset("Dybbolsbro"), -920.0);
	QCOMPARE_EQ(stationNamedYOffset("unknown"), 0.0);
	const auto firstShift = annotationStationShift(placementViews, "A");
	const auto lastShift = annotationStationShift(placementViews, "C");
	// First and last horizontal station decorations shift down 1200.
	QCOMPARE_LT(std::fabs(firstShift.first), 1e-9);
	QCOMPARE_LT(std::fabs(firstShift.second - 1200.0), 1e-9);
	QCOMPARE_LT(std::fabs(lastShift.second - 1200.0), 1e-9);
	const auto bendShift = annotationStationShift(placementViews, "D");
	const auto multiShift = annotationStationShift(placementViews, "B");
	// A station bend uses the regional bisector.
	QVERIFY(std::isfinite(bendShift.first));
	QVERIFY(std::isfinite(bendShift.second));
	QCOMPARE_GT(std::fabs(bendShift.first), 1.0);
	QCOMPARE_GT(std::fabs(bendShift.second), 1.0);
	// A station with multiple regions averages independent shifts.
	QVERIFY(std::isfinite(multiShift.first));
	QVERIFY(std::isfinite(multiShift.second));
	QCOMPARE_GT(std::fabs(multiShift.first), 1.0);
	QCOMPARE_LT(std::fabs(multiShift.second), 1200.0);
	// Absent station geometry leaves semantic anchors unchanged.
	QVERIFY(annotationStationShift({}, "A") == std::make_pair(0.0, 0.0));
	QVERIFY(annotationStationShift(placementViews, "missing") == std::make_pair(0.0, 0.0));
	const std::vector<SceneStationView> collapsed{
		{"A", 0.0, 0.0, {{0, 0.0}}, {}},
		{"B", 0.0, 0.0, {{0, 1.0}}, {}}};
	QVERIFY2(annotationStationShift(collapsed, "A") == std::make_pair(0.0, 0.0),
		"coincident geographic stations cannot introduce nonfinite decoration offsets");
}

void TestTrackPreview::normalizesGeographicPreview() {
	// Normalization retains every projected line.
	QCOMPARE(m_normalized.lines.size(), m_result.lines.size());
	const auto& sourcePoint = m_result.lines[0].points[1];
	const auto& normalizedPoint = m_normalized.lines[0].points[1];
	// Normalization preserves raw chainage.
	QCOMPARE_EQ(sourcePoint.rawX, normalizedPoint.rawX);
	const double scaleX = (m_normalized.lines[0].points[2].x - m_normalized.lines[0].points[0].x)
		/ (m_result.lines[0].points[2].x - m_result.lines[0].points[0].x);
	const double scaleY = (m_normalized.lines[0].points[2].y - m_normalized.lines[0].points[0].y)
		/ (m_result.lines[0].points[2].y - m_result.lines[0].points[0].y);
	// Normalization applies one stable uniform projected scale.
	QCOMPARE_LT(std::fabs(scaleX - scaleY), 1e-9);
	QCOMPARE_GT(scaleX, 1000.0);
	TrackPreviewPoint nodePoint;
	// Point lookup resolves normalized node anchors.
	QVERIFY(trackPreviewPointAtNode(m_normalized.lines[0], "B0.Gdg", nodePoint));
	QCOMPARE_LT(std::fabs(nodePoint.x - m_normalized.lines[0].points[1].x), 1e-9);
	QCOMPARE_LT(std::fabs(nodePoint.y - m_normalized.lines[0].points[1].y), 1e-9);
	TrackPreviewLine duplicateChainage = m_normalized.lines[0];
	duplicateChainage.points[2].rawX = duplicateChainage.points[1].rawX;
	// Node identity disambiguates equal-chainage runtime anchors.
	QVERIFY(trackPreviewPointAtNode(duplicateChainage, "B0.Ut", nodePoint));
	QCOMPARE(nodePoint.nodeId, "B0.Ut");
	TrackPreviewPoint interpolated;
	// Point lookup interpolates by raw chainage.
	QVERIFY(trackPreviewPointAtX(m_normalized.lines[0], 32.0, interpolated));
	QCOMPARE_EQ(interpolated.rawX, 32.0);
	QCOMPARE_LT(std::fabs(
					interpolated.x
					- (m_normalized.lines[0].points[1].x
						+ (m_normalized.lines[0].points[2].x - m_normalized.lines[0].points[1].x) * 4.0 / 36.0)),
		1e-9);
}

void TestTrackPreview::projectsTracks() {
	// The preview renders both in-memory tracks without legacy files.
	QCOMPARE(m_result.lines.size(), std::size_t(2));
	const auto& first = m_result.lines[0];
	const auto& second = m_result.lines[1];
	// The preview preserves canonical track IDs.
	QCOMPARE(first.id, "B0");
	QCOMPARE(second.id, "B1");
	// The preview retains every ordered node.
	QCOMPARE(first.points.size(), std::size_t(3));
	QCOMPARE(second.points.size(), std::size_t(3));
	// Preview points retain canonical node IDs.
	QCOMPARE(first.points[0].nodeId, "B0.Gvc");
	QCOMPARE(first.points[2].nodeId, "B0.Ut");
	QCOMPARE(second.points[0].nodeId, "B1.Ut");
	QCOMPARE(second.points[2].nodeId, "B1.Gvc");
	// Station display anchors map track distance to longitude.
	QCOMPARE_LT(std::fabs(first.points[0].x - 0.0), 1e-9);
	QCOMPARE_LT(std::fabs(first.points[1].x - 0.28), 1e-9);
	QCOMPARE_LT(std::fabs(first.points[2].x - 0.64), 1e-9);
	// The preview maps station latitude to Mercator display y.
	QCOMPARE_EQ(first.points[0].rawX, 0.0);
	QCOMPARE_EQ(first.points[2].rawX, 64.0);
	QCOMPARE_EQ(second.points[0].rawX, 100.0);
	QCOMPARE_EQ(second.points[2].rawX, 164.0);
	QCOMPARE_LT(std::fabs(first.points[0].y - (-61.08656629615305)), 1e-9);
	QCOMPARE_LT(std::fabs(first.points[1].y - (-61.49377304672194)), 1e-9);
	QCOMPARE_LT(std::fabs(first.points[2].y - (-61.90328104077144)), 1e-9);
	// Authored track levels separate geographic tracks perpendicular to the route.
	QCOMPARE_LT(std::fabs(std::hypot(first.points[0].x - second.points[2].x, first.points[0].y - second.points[2].y) - 0.0006), 1e-9);
	QCOMPARE_LT(std::fabs(std::hypot(first.points[1].x - second.points[1].x, first.points[1].y - second.points[1].y) - 0.0006), 1e-9);
	QCOMPARE_LT(std::fabs(std::hypot(first.points[2].x - second.points[0].x, first.points[2].y - second.points[0].y) - 0.0006), 1e-9);
	// Geographic track separation is carried by mapped points.
	QCOMPARE_EQ(first.displayOffset, 0.0);
	QCOMPARE_EQ(second.displayOffset, 0.0);
	QCOMPARE_EQ(first.levelSpacing, 0.0);
	QCOMPARE_EQ(second.levelSpacing, 0.0);
	const double alongX = second.points[2].x - second.points[1].x;
	const double alongY = second.points[2].y - second.points[1].y;
	const double acrossX = second.points[2].x - first.points[0].x;
	const double acrossY = second.points[2].y - first.points[0].y;
	// A higher geographic level stays on the left of increasing chainage.
	QCOMPARE_LT(alongX * acrossY - alongY * acrossX, 0.0);
}

void TestTrackPreview::resolvesConnection() {
	// The preview resolves one canonical connection.
	QCOMPARE(m_result.connections.size(), std::size_t(1));
	const auto& connection = m_result.connections.front();
	// Connection endpoints resolve through canonical node references.
	QCOMPARE(connection.firstTrackId, "B0");
	QCOMPARE(connection.firstNodeId, "B0.Ut");
	QCOMPARE(connection.secondTrackId, "B1");
	QCOMPARE(connection.secondNodeId, "B1.Ut");
}

void TestTrackPreview::scalesSchematicLevels() {
	// Constant-latitude display anchors scale schematic track levels with mapped x.
	QCOMPARE(m_schematicResult.lines.size(), std::size_t(2));
	QCOMPARE_LT(std::fabs(m_schematicResult.lines[0].points[1].x - 0.28), 1e-9);
	QCOMPARE_EQ(m_schematicResult.lines[0].points[0].y, 0.0);
	QCOMPARE_EQ(m_schematicResult.lines[1].points[0].y, 0.0);
	QCOMPARE_EQ(m_schematicResult.lines[0].displayOffset, 0.0);
	QCOMPARE_LT(std::fabs(m_schematicResult.lines[1].displayOffset + 0.0012), 1e-9);
	QCOMPARE_LT(std::fabs(m_schematicResult.lines[1].levelSpacing - 0.0012), 1e-9);
	// Schematic normalization keeps two-level spacing in runtime units.
	QCOMPARE(m_normalizedSchematic.lines.size(), std::size_t(2));
	QCOMPARE_LT(std::fabs(m_normalizedSchematic.lines[0].points[1].x - 28000.0), 1e-9);
	QCOMPARE_LT(std::fabs(m_normalizedSchematic.lines[1].displayOffset + 120.0), 1e-9);
	QCOMPARE_LT(std::fabs(m_normalizedSchematic.lines[0].levelSpacing - 120.0), 1e-9);
	// A higher schematic level is drawn above the level before it.
	QCOMPARE(m_normalizedSchematic.lines.size(), std::size_t(2));
	QCOMPARE_LT(m_normalizedSchematic.lines[1].points[0].y + m_normalizedSchematic.lines[1].displayOffset,
		m_normalizedSchematic.lines[0].points[0].y + m_normalizedSchematic.lines[0].displayOffset);
}

// Constant-latitude Paimpol equivalent: one degree is 100 km.
void TestTrackPreview::schematicPresentationUnits() {
	// Schematic presentation uses measured historical construction units.
	QCOMPARE_LT(std::fabs(m_normalizedSchematic.normalizationScale - 100000.0), 1e-6);
	QCOMPARE_LT(std::fabs(m_normalizedSchematic.presentationScale - 0.45), 1e-12);
	QVERIFY(m_normalizedSchematic.normalized);
	QVERIFY(std::all_of(m_schematicResult.lines.begin(), m_schematicResult.lines.end(),
		[](const auto& line) { return line.authoredStationProjection; }));
	const auto repeated = normalizeTrackPreview(m_normalizedSchematic);
	// Repeat normalization preserves geometry and unit metadata without double scaling.
	QCOMPARE_EQ(repeated.normalizationScale, m_normalizedSchematic.normalizationScale);
	QCOMPARE_EQ(repeated.presentationScale, m_normalizedSchematic.presentationScale);
	QCOMPARE_EQ(repeated.lines[1].displayOffset, m_normalizedSchematic.lines[1].displayOffset);
	QCOMPARE_EQ(repeated.lines[0].points[1].x, m_normalizedSchematic.lines[0].points[1].x);
}

void TestTrackPreview::geographicPresentationUnits() {
	const double geographicScale = (m_normalized.lines[0].points[2].x - m_normalized.lines[0].points[0].x)
		/ (m_result.lines[0].points[2].x - m_result.lines[0].points[0].x);
	// Geographic projection uses actual normalization, not a schematic case multiplier.
	QCOMPARE_LT(std::fabs(m_normalized.normalizationScale - geographicScale), 1e-6);
	QCOMPARE_LT(std::fabs(m_normalized.presentationScale - geographicScale / (800000.0 * 100.0 / 360.0)), 1e-12);
	QVERIFY(m_result.lines[0].authoredStationProjection);
	QVERIFY(m_result.lines[1].authoredStationProjection);
}

void TestTrackPreview::keepsAuthoredCoordinates() {
	// Presentation metadata does not mutate authored coordinates, levels or chainage.
	QCOMPARE_EQ(m_schematic.nodes[1].xKm, 28.0);
	QCOMPARE_EQ(m_schematic.nodes[1].yKm, 0.0);
	QCOMPARE(m_schematic.trackViews[1].level, 1);
	QCOMPARE_EQ(m_schematic.stationViews[1].longitude, 0.28);
	QCOMPARE_EQ(m_normalizedSchematic.lines[0].points[1].rawX, 28.0);
}

void TestTrackPreview::fallsBackToWholeScenePresentationUnits_data() {
	QTest::addColumn<int>("fallback");
	QTest::newRow("no track views") << 0;
	QTest::newRow("no station views") << 1;
	QTest::newRow("mixed projected and raw") << 2;
	QTest::newRow("latitude 90") << 3;
	QTest::newRow("region chainage not a number") << 4;
	QTest::newRow("infinite x") << 5;
	QTest::newRow("infinite y") << 6;
}

// Raw, absent, mixed, invalid or nonfinite projection retains whole-scene presentation units.
void TestTrackPreview::fallsBackToWholeScenePresentationUnits() {
	QFETCH(int, fallback);
	SceneModel candidate = m_schematic;
	if (fallback == 0) candidate.trackViews.clear();
	if (fallback == 1) candidate.stationViews.clear();
	if (fallback == 2) candidate.trackViews.pop_back(); // Mixed projected/raw scene.
	if (fallback == 3) candidate.stationViews[1].latitude = 90.0;
	if (fallback == 4) candidate.stationViews[1].regions[0].second = std::numeric_limits<double>::quiet_NaN();
	if (fallback == 5) candidate.nodes[1].xKm = std::numeric_limits<double>::infinity();
	if (fallback == 6) candidate.nodes[1].yKm = std::numeric_limits<double>::infinity();
	QCOMPARE_EQ(normalizeTrackPreview(loadTrackPreview(candidate)).presentationScale, 1.0);
}

void TestTrackPreview::keepsPresentationFactorOne() {
	// Empty normalization retains presentation factor one.
	QCOMPARE_EQ(normalizeTrackPreview({}).presentationScale, 1.0);
	TrackPreviewResult degenerate = m_schematicResult;
	for (auto& line : degenerate.lines) {
		line.displayOffset = 0;
		for (auto& point : line.points) point.x = point.y = 0;
	}
	// Degenerate normalization retains presentation factor one.
	QCOMPARE_EQ(normalizeTrackPreview(degenerate).presentationScale, 1.0);
	TrackPreviewResult overflow = m_schematicResult;
	for (auto& line : overflow.lines) {
		line.displayOffset = 0;
		for (std::size_t i = 0; i < line.points.size(); ++i) {
			line.points[i].x = 1e308;
			line.points[i].y = static_cast<double>(i);
		}
	}
	// Nonfinite normalized coordinates retain presentation factor one.
	QCOMPARE_EQ(normalizeTrackPreview(overflow).presentationScale, 1.0);
}

void TestTrackPreview::ignoresUnmappedHiddenLines() {
	SceneModel onlyVisible = m_schematic;
	onlyVisible.trackViews[1].visible = false;
	onlyVisible.trackViews[1].region = 99;
	// Unmapped hidden lines do not disqualify successful visible projection.
	QCOMPARE_LT(std::fabs(normalizeTrackPreview(loadTrackPreview(onlyVisible)).presentationScale - 0.45), 1e-12);
}

void TestTrackPreview::looksUpSchematicStationAnchor() {
	TrackPreviewPoint schematicStationPoint;
	// Schematic point lookup resolves the second-level station anchor.
	QVERIFY(trackPreviewPointAtNode(m_normalizedSchematic.lines[1], "B1.Ut", schematicStationPoint));
	QCOMPARE_LT(std::fabs(schematicStationPoint.x - m_normalizedSchematic.lines[1].points[0].x), 1e-9);
}

// Netherlands geographic corridor, with the committed endpoint latitudes/longitudes.
void TestTrackPreview::projectsNetherlandsCorridor() {
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
	// The Netherlands geographic corridor retains physical anchors and derives its own presentation conversion.
	QVERIFY(dutchProjection.lines[0].authoredStationProjection);
	QCOMPARE_LT(std::fabs(dutchNormalized.normalizationScale - dutchScale), 1e-6);
	QCOMPARE_LT(std::fabs(dutchNormalized.presentationScale - dutchScale / (800000.0 * 100.0 / 360.0)), 1e-12);
	QCOMPARE_EQ(dutchNormalized.lines[0].points[1].rawX, 48.362);
	QCOMPARE_EQ(dutchNormalized.lines[0].points[0].x, 4.872837 * dutchScale);
}

void TestTrackPreview::leavesHiddenTracksOut() {
	SceneModel hidden = m_scene;
	hidden.trackViews[1].visible = false;
	hidden.stations.front().platforms.insert(hidden.stations.front().platforms.begin(),
		{"Gvc.hidden-platform", {"B1.Gvc"}});
	const TrackPreviewResult hiddenResult = loadTrackPreview(hidden);
	// Hidden display tracks and their connections stay out of the preview.
	QCOMPARE(hiddenResult.lines.size(), std::size_t(1));
	QCOMPARE(hiddenResult.lines.front().id, "B0");
	QVERIFY(hiddenResult.connections.empty());
	// The station preview skips hidden platform tracks when choosing its anchor.
	QVERIFY(!hiddenResult.stations.empty());
	QCOMPARE(hiddenResult.stations.front().nodeId, "B0.Gvc");
	// Hidden tracks do not leave orphaned preview signals.
	QCOMPARE(hiddenResult.previewSignals.size(), std::size_t(1));
	QCOMPARE(hiddenResult.previewSignals.front().trackId, "B0");
}

void TestTrackPreview::anchorsStations() {
	// The preview renders one station anchor per station.
	QCOMPARE(m_result.stations.size(), std::size_t(3));
	const auto& stationAnchor = m_result.stations.front();
	// The station preview prefers the platform node anchor.
	QCOMPARE(stationAnchor.name, "Gvc");
	QCOMPARE(stationAnchor.nodeId, "B0.Gvc");
	QCOMPARE_EQ(stationAnchor.x, 0.0);
	QVERIFY(stationAnchor.hasPlatform);
	QCOMPARE(stationAnchor.id, "Gvc");
}

void TestTrackPreview::namesPreviewSignals() {
	// The preview emits later base boundaries but skips each track's first section.
	QCOMPARE(m_result.previewSignals.size(), std::size_t(2));
	// The preview signal identity uses the base section boundary and source coordinate.
	QCOMPARE(m_result.previewSignals[0].trackId, "B0");
	QCOMPARE(m_result.previewSignals[0].sectionId, "@B0.block.2@");
	QCOMPARE(m_result.previewSignals[0].nodeId, "B0.Gdg");
	QCOMPARE_EQ(m_result.previewSignals[0].rawX, 28.0);
	QCOMPARE(m_result.previewSignals[1].trackId, "B1");
	QCOMPARE(m_result.previewSignals[1].sectionId, "@B1.block.2@");
	QCOMPARE(m_result.previewSignals[1].nodeId, "B1.Gdg");
	QCOMPARE_EQ(m_result.previewSignals[1].rawX, 136.0);
	// A preview signal without an authored signal is named by the label of its section.
	QCOMPARE(m_result.previewSignals[0].sectionLabel, "B0 28.000 to 64.000 km");
	QCOMPARE(m_result.previewSignals[0].label, "B0 28.000 to 64.000 km");
	QCOMPARE(m_result.previewSignals[1].sectionLabel, "B1 136.000 to 164.000 km");
}

// An authored signal that protects the section, by its runtime id or by its block id, is named first, with the
// label in parentheses.
void TestTrackPreview::namesAuthoredSignalsFirst() {
	SceneModel authored = m_scene;
	sceneSignals(authored) = {{"sig1", "@B0.block.2@"}, {"other", "@B1.block.1@"}, {"sig2", "B0.block.2"}};
	const TrackPreviewResult authoredResult = loadTrackPreview(authored);
	// A preview signal names the authored signals of its section before the label.
	QVERIFY(!authoredResult.previewSignals.empty());
	QCOMPARE(authoredResult.previewSignals[0].sectionLabel, "B0 28.000 to 64.000 km");
	QCOMPARE(authoredResult.previewSignals[0].label, "sig1, sig2 (B0 28.000 to 64.000 km)");
}

void TestTrackPreview::skipsConnectionSections() {
	for (const auto& signal : m_result.previewSignals)
		QVERIFY2(signal.sectionId.find('/') == std::string::npos,
			"connection-derived sections do not produce preview signals");
}

void TestTrackPreview::leavesDoubleSwitchSignalFree() {
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
	QVERIFY2(switchResult.previewSignals.empty(),
		"legacy three-segment double-switch midpoint remains signal-free");
}

void TestTrackPreview::keepsRawXWithoutStationMetadata() {
	SceneModel insufficient;
	insufficient.tracks = {{"raw"}};
	insufficient.nodes = {{"raw.start", "raw", 10.0, 0.0}, {"raw.end", "raw", 20.0, 0.0}};
	insufficient.arcs = {{"raw.arc", "raw", "raw.start", "raw.end", 0.0, 0.0, 12.0}};
	insufficient.trackViews = {{"raw", 0, 0}};
	insufficient.stationViews = {{"only", 1.0, 0.5, {{0, 10.0}}, {}}};
	const TrackPreviewResult unchanged = loadTrackPreview(insufficient);
	// Insufficient station metadata leaves raw preview x unchanged.
	QCOMPARE(unchanged.lines.size(), std::size_t(1));
	QCOMPARE(unchanged.lines[0].points.size(), std::size_t(2));
	QCOMPARE_EQ(unchanged.lines[0].points[0].x, 10.0);
	QCOMPARE_EQ(unchanged.lines[0].points[1].x, 20.0);
}

QTEST_APPLESS_MAIN(TestTrackPreview)
#include "test_trackpreview.moc"
