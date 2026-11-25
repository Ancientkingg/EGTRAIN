#include "scene/RailmlImporter.h"
#include "scene/SceneModel.h"
#include "scene/SceneValidator.h"

#include <QTest>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct TempDir {
	fs::path path;

	TempDir() {
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		path = fs::temp_directory_path() / ("railml_import_test_" + std::to_string(stamp));
		fs::create_directories(path);
	}

	~TempDir() {
		std::error_code error;
		fs::remove_all(path, error);
	}
};

std::string fixture(const char* name) {
	return std::string(EGTRAIN_RAILML_FIXTURES) + "/" + name;
}

// The first diagnostic with the code whose message contains the text; null when there is none.
const SceneDiagnostic* findDiagnostic(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code,
	const std::string& text = "") {
	for (const SceneDiagnostic& diagnostic : diagnostics)
		if (diagnostic.code == code && diagnostic.message.find(text) != std::string::npos)
			return &diagnostic;
	return nullptr;
}

const SceneImportReportRow* findRow(const SceneModel& scene, const std::string& kind) {
	for (const SceneImportReportRow& row : scene.importReport)
		if (row.category == "railml:" + kind)
			return &row;
	return nullptr;
}

template <typename T>
const T* findId(const std::vector<T>& items, const std::string& id) {
	const auto found = std::find_if(items.begin(), items.end(), [&id](const T& item) { return item.id == id; });
	return found == items.end() ? nullptr : &*found;
}

const SceneArc* arcAt(const SceneModel& scene, const std::string& track, double km) {
	for (const SceneArc& arc : scene.arcs) {
		if (arc.trackId != track)
			continue;
		const SceneNode* from = findId(scene.nodes, arc.fromNodeId);
		const SceneNode* to = findId(scene.nodes, arc.toNodeId);
		if (from != nullptr && to != nullptr && km > std::min(from->xKm, to->xKm) && km < std::max(from->xKm, to->xKm))
			return &arc;
	}
	return nullptr;
}

std::string errorText(const std::vector<SceneDiagnostic>& diagnostics) {
	std::string text;
	for (const SceneDiagnostic& diagnostic : diagnostics)
		if (diagnostic.severity == SceneSeverity::Error)
			text += toDisplayText(diagnostic) + "\n";
	return text;
}

// Imports a fixture into the temporary directory, loads the scene back and checks that it is runnable.
SceneModel importFixture(const char* name, const TempDir& temp, std::vector<SceneDiagnostic>& diagnostics,
	const RailmlImportOptions& options = {}) {
	const std::string sceneDir = (temp.path / "scene").string();
	const SceneImportResult result = importRailmlScene(fixture(name), sceneDir, "", options);
	diagnostics = result.diagnostics;
	if (!result.success()) {
		QTest::qFail(errorText(result.diagnostics).c_str(), __FILE__, __LINE__);
		return {};
	}
	const SceneLoadResult loaded = loadScene(sceneDir);
	if (hasErrors(loaded.diagnostics)) {
		QTest::qFail(errorText(loaded.diagnostics).c_str(), __FILE__, __LINE__);
		return {};
	}
	const std::vector<SceneDiagnostic> runnable = validateRunnableScene(loaded.scene);
	if (hasErrors(runnable)) {
		QTest::qFail(errorText(runnable).c_str(), __FILE__, __LINE__);
		return {};
	}
	return loaded.scene;
}

} // namespace

class TestRailmlImporter : public QObject {
	Q_OBJECT

private slots:
	void importsMicroscopicInfrastructure();
	void importsMicroscopicTimetable();
	void reportsMicroscopicElements();
	void importsMacroscopicNetwork();
	void importsChosenOperatingPeriod();
	void refusesUnknownOperatingPeriod();
	void refusesRailml3();
	void refusesOtherXml();
	void refusesMissingFile();
};

void TestRailmlImporter::importsMicroscopicInfrastructure() {
	TempDir temp;
	std::vector<SceneDiagnostic> diagnostics;
	const SceneModel scene = importFixture("microscopic.railml", temp, diagnostics);
	if (QTest::currentTestFailed())
		return;

	// The main line is split at its two switches; the loop joins it there.
	std::vector<std::string> tracks;
	for (const SceneTrack& track : scene.tracks)
		tracks.push_back(track.id);
	QCOMPARE(tracks, (std::vector<std::string>{"t1.s1", "t1.s2", "t1.s3", "t2"}));
	const SceneConnection* sw1 = findId(scene.connections, "sw1_c");
	const SceneConnection* sw2 = findId(scene.connections, "sw2_c");
	QVERIFY(sw1 != nullptr && sw2 != nullptr);
	QCOMPARE(sw1->fromNodeId, std::string("t1.s1.n14"));
	QCOMPARE(sw1->toNodeId, std::string("t2.n1"));
	QCOMPARE(findId(scene.nodes, sw2->toNodeId)->trackId, std::string("t1.s3"));
	QCOMPARE(findId(scene.nodes, sw2->fromNodeId)->trackId, std::string("t2"));

	// Speed changes per direction, a gradient in per mille and a radius.
	QCOMPARE(arcAt(scene, "t1.s1", 0.1)->speedLimitMs * 3.6, 100.0);
	QCOMPARE(arcAt(scene, "t1.s1", 2.7)->speedLimitMs * 3.6, 60.0);
	QCOMPARE(arcAt(scene, "t2", 3.2)->speedLimitMs * 3.6, 40.0);
	QCOMPARE(arcAt(scene, "t1.s1", 1.2)->gradientPercent, 0.005);
	QCOMPARE(arcAt(scene, "t1.s1", 2.45)->gradientPercent, 0.0);
	QCOMPARE(arcAt(scene, "t1.s1", 2.3)->curvatureRadiusM, 800.0);

	// Main signals start blocks and protect the block on their side; the distant signal is left out.
	QCOMPARE(sceneSignals(scene).size(), std::size_t(10));
	QCOMPARE(findId(sceneSignals(scene), "sig_a_up")->protectedSection, std::string("t1.s1.b3"));
	QCOMPARE(findId(sceneSignals(scene), "sig_a_down")->protectedSection, std::string("t1.s1.b1"));
	QCOMPARE(findId(sceneSignals(scene), "sig_b2_up")->protectedSection, std::string("t2.b3"));
	QVERIFY(findId(sceneSignals(scene), "sig_dist") == nullptr);
	QCOMPARE(scene.signallingAreas.size(), std::size_t(1));
	QCOMPARE(scene.signallingAreas.front().level, 0);

	// OCPs with their platform edges.
	QCOMPARE(scene.stations.size(), std::size_t(3));
	const SceneStation* bedal = findId(scene.stations, "ocp_b");
	QVERIFY(bedal != nullptr);
	QCOMPARE(bedal->name, std::string("Bedal"));
	QCOMPARE(bedal->platforms.size(), std::size_t(2));
	QCOMPARE(bedal->platforms[1].id, std::string("pe_b2"));
	QCOMPARE(bedal->platforms[1].lengthM, 150.0);
	QCOMPARE(findId(scene.nodes, bedal->platforms[1].nodeIds.front())->trackId, std::string("t2"));
	QVERIFY(findDiagnostic(diagnostics, "scene.import.railml.layout", "Schematic layout") != nullptr);
}

void TestRailmlImporter::importsMicroscopicTimetable() {
	TempDir temp;
	std::vector<SceneDiagnostic> diagnostics;
	const SceneModel scene = importFixture("microscopic.railml", temp, diagnostics);
	if (QTest::currentTestFailed())
		return;

	// The daily period, from 23:50 to 00:04 the next day.
	QCOMPARE(scene.baseTime, std::string("23:50:00"));
	QCOMPARE(scene.settings.durationSeconds, 1440.0);
	QCOMPARE(scene.services.size(), std::size_t(2));
	const SceneService* up = findId(scene.services, "tr_101");
	QVERIFY(up != nullptr);
	QCOMPARE(up->composition, std::string("f_ic"));
	QCOMPARE(up->stops.size(), std::size_t(3));
	QCOMPARE(up->stops[0].stationId, std::string("ocp_a"));
	QCOMPARE(up->stops[0].plannedDepartureSeconds, 0.0);
	QCOMPARE(up->stops[1].plannedArrivalSeconds, 120.0);
	QCOMPARE(up->stops[1].plannedDepartureSeconds, 180.0);
	QCOMPARE(up->stops[1].dwellSeconds, 30.0);
	QCOMPARE(up->stops[2].platformId, std::string("pe_b2"));
	QCOMPARE(up->stops[2].plannedArrivalSeconds, 360.0);
	const SceneRoute* upRoute = findId(scene.routes, up->route);
	QVERIFY(upRoute != nullptr && !upRoute->reversed);
	QCOMPARE(upRoute->blocks.back(), std::string("t2.b2"));

	// The return runs down the main line past midnight and passes Hollum.
	const SceneService* down = findId(scene.services, "tr_102");
	QVERIFY(down != nullptr);
	QCOMPARE(down->stops.size(), std::size_t(2));
	QCOMPARE(down->stops[0].platformId, std::string("pe_b1"));
	QCOMPARE(down->stops[0].plannedDepartureSeconds, 480.0);
	QCOMPARE(down->stops[1].stationId, std::string("ocp_a"));
	QCOMPARE(down->stops[1].plannedArrivalSeconds, 840.0);
	QVERIFY(findId(scene.routes, down->route)->reversed);
	QVERIFY(findDiagnostic(diagnostics, "scene.import.railml.midnight") != nullptr);

	// One unit for the formation: a locomotive and three coaches.
	QCOMPARE(scene.trainUnits.size(), std::size_t(1));
	const SceneTrainUnit& unit = scene.trainUnits.front();
	QCOMPARE(unit.id, std::string("f_ic"));
	QVERIFY(std::fabs(unit.physical.length_m - 98.2) < 1e-9);
	QCOMPARE(unit.physical.mass_of_traction_unit_kg, 84000.0);
	QCOMPARE(unit.physical.mass_of_a_wagon_kg, 45000.0);
	QCOMPARE(unit.physical.number_of_wagons, 3.0);
	QVERIFY(std::fabs(unit.physical.max_speed_ms - 140.0 / 3.6) < 1e-9);
	QCOMPARE(unit.passengerCapacity, 240);
	QVERIFY(!unit.tractionCurve.empty());
}

void TestRailmlImporter::reportsMicroscopicElements() {
	TempDir temp;
	std::vector<SceneDiagnostic> diagnostics;
	const SceneModel scene = importFixture("microscopic.railml", temp, diagnostics);
	if (QTest::currentTestFailed())
		return;

	const SceneImportReportRow* signalRow = findRow(scene, "signal");
	QVERIFY(signalRow != nullptr);
	QCOMPARE(signalRow->sourceCount, 11);
	QCOMPARE(signalRow->convertedCount, 10);
	QCOMPARE(signalRow->skippedCount, 1);
	QCOMPARE(signalRow->sourceFile, fixture("microscopic.railml"));
	QCOMPARE(findRow(scene, "switch")->convertedCount, 2);
	QCOMPARE(findRow(scene, "platformEdge")->convertedCount, 4);
	QCOMPARE(findRow(scene, "trainDetector")->skippedCount, 1);
	const SceneImportReportRow* parts = findRow(scene, "trainPart");
	QCOMPARE(parts->sourceCount, 3);
	QCOMPARE(parts->convertedCount, 2);
	QCOMPARE(parts->skippedCount, 1);
	// Five stops are converted; the passing time and the stops of the Sunday train part are not.
	const SceneImportReportRow* times = findRow(scene, "ocpTT");
	QCOMPARE(times->sourceCount, 8);
	QCOMPARE(times->convertedCount, 5);
	QCOMPARE(times->skippedCount, 3);

	// One diagnostic per unsupported kind and per default.
	QVERIFY(findDiagnostic(diagnostics, "scene.import.railml.unsupported", "1 trainDetector elements") != nullptr);
	QVERIFY(findDiagnostic(diagnostics, "scene.import.railml.unsupported", "of type distant") != nullptr);
	QVERIFY(findDiagnostic(diagnostics, "scene.import.railml.default", "Default traction curve for 1 train units") != nullptr);
	QVERIFY(findDiagnostic(diagnostics, "scene.import.railml.default", "Default deceleration") != nullptr);
	QVERIFY(findDiagnostic(diagnostics, "scene.import.railml.default", "signalling level 0") != nullptr);
	const SceneDiagnostic* period = findDiagnostic(diagnostics, "scene.import.railml.operating_period");
	QVERIFY(period != nullptr);
	QCOMPARE(period->itemId, std::string("op_daily"));
	QVERIFY(period->message.find("1 train parts of other operating periods are left out") != std::string::npos);
	QVERIFY(findDiagnostic(diagnostics, "scene.import.railml.signals_generated") == nullptr);
	QVERIFY(!hasErrors(diagnostics));
}

void TestRailmlImporter::importsMacroscopicNetwork() {
	TempDir temp;
	std::vector<SceneDiagnostic> diagnostics;
	const SceneModel scene = importFixture("macroscopic.railml", temp, diagnostics);
	if (QTest::currentTestFailed())
		return;

	// The two tracks meet in the macroscopic node at Mittelstadt.
	QCOMPARE(scene.tracks.size(), std::size_t(2));
	QCOMPARE(scene.connections.size(), std::size_t(1));
	QCOMPARE(findId(scene.nodes, scene.connections.front().fromNodeId)->trackId, std::string("tw"));
	QCOMPARE(findId(scene.nodes, scene.connections.front().toNodeId)->trackId, std::string("te"));

	// A block between each pair of OCPs, with a generated signal at each end for each direction.
	QCOMPARE(scene.blocks.size(), std::size_t(3));
	QCOMPARE(sceneSignals(scene).size(), std::size_t(6));
	QCOMPARE(findId(sceneSignals(scene), "tw.b1.up")->protectedSection, std::string("tw.b1"));
	QCOMPARE(findId(sceneSignals(scene), "te.b1.down")->protectedSection, std::string("te.b1"));
	QVERIFY(findDiagnostic(diagnostics, "scene.import.railml.signals_generated", "6 signals are generated") != nullptr);

	// OCPs become stations, Mittelstadt with a platform on each track.
	QCOMPARE(scene.stations.size(), std::size_t(4));
	QCOMPARE(findId(scene.stations, "ocp_m")->platforms.size(), std::size_t(2));
	QCOMPARE(arcAt(scene, "tw", 2.0)->gradientPercent, -0.004);
	QCOMPARE(arcAt(scene, "te", 8.0)->speedLimitMs * 3.6, 80.0);

	// The coordinates place the stations.
	QVERIFY(findDiagnostic(diagnostics, "scene.import.railml.layout", "Geographic layout") != nullptr);
	QCOMPARE(scene.stationViews.size(), std::size_t(4));
	const auto mittelstadt = std::find_if(scene.stationViews.begin(), scene.stationViews.end(),
		[](const SceneStationView& view) { return view.stationId == "ocp_m"; });
	QVERIFY(mittelstadt != scene.stationViews.end());
	QCOMPARE(mittelstadt->latitude, 51.02);
	QCOMPARE(mittelstadt->longitude, 13.07);

	// The train part passes Kleinfeld and stops at Mittelstadt; without a formation it gets the default unit.
	QCOMPARE(scene.services.size(), std::size_t(1));
	const SceneService& service = scene.services.front();
	QCOMPARE(service.id, std::string("tp_rb1"));
	QCOMPARE(service.composition, std::string("railml_default"));
	QCOMPARE(service.stops.size(), std::size_t(3));
	QCOMPARE(service.stops[1].stationId, std::string("ocp_m"));
	QCOMPARE(service.stops[1].plannedArrivalSeconds, 300.0);
	QCOMPARE(service.stops[1].dwellSeconds, 45.0);
	QCOMPARE(service.stops[2].plannedArrivalSeconds, 840.0);
	QCOMPARE(scene.trainUnits.size(), std::size_t(1));
	QVERIFY(findDiagnostic(diagnostics, "scene.import.railml.default", "railml_default") != nullptr);
	QCOMPARE(findRow(scene, "macroscopicNode")->convertedCount, 4);
	QVERIFY(findRow(scene, "signal") == nullptr);
}

void TestRailmlImporter::importsChosenOperatingPeriod() {
	TempDir temp;
	std::vector<SceneDiagnostic> diagnostics;
	RailmlImportOptions options;
	options.operatingPeriod = "op_sunday";
	const SceneModel scene = importFixture("microscopic.railml", temp, diagnostics, options);
	if (QTest::currentTestFailed())
		return;
	QCOMPARE(scene.services.size(), std::size_t(1));
	QCOMPARE(scene.services.front().id, std::string("tp_3"));
	QCOMPARE(scene.services.front().composition, std::string("railml_default"));
	QCOMPARE(scene.baseTime, std::string("10:00:00"));
	QVERIFY(scene.description.find("operating period op_sunday") != std::string::npos);
	QVERIFY(findDiagnostic(diagnostics, "scene.import.railml.operating_period", "2 train parts of other") != nullptr);
}

void TestRailmlImporter::refusesUnknownOperatingPeriod() {
	TempDir temp;
	RailmlImportOptions options;
	options.operatingPeriod = "op_never";
	const fs::path sceneDir = temp.path / "scene";
	const SceneImportResult result = importRailmlScene(fixture("microscopic.railml"), sceneDir.string(), "", options);
	QVERIFY(!result.success());
	QVERIFY(findDiagnostic(result.diagnostics, "scene.import.railml.operating_period", "op_never") != nullptr);
	QCOMPARE(findDiagnostic(result.diagnostics, "scene.import.railml.operating_period")->severity, SceneSeverity::Error);
	QVERIFY(!fs::exists(sceneDir));
}

void TestRailmlImporter::refusesRailml3() {
	TempDir temp;
	const fs::path sceneDir = temp.path / "scene";
	const SceneImportResult result = importRailmlScene(fixture("railml3.railml"), sceneDir.string(), "");
	QVERIFY(!result.success());
	QCOMPARE(result.diagnostics.size(), std::size_t(1));
	QCOMPARE(result.diagnostics.front().code, std::string("scene.import.railml.version"));
	QVERIFY(result.diagnostics.front().message.find("railML 3") != std::string::npos);
	QVERIFY(!fs::exists(sceneDir));
}

void TestRailmlImporter::refusesOtherXml() {
	TempDir temp;
	const fs::path sceneDir = temp.path / "scene";
	const SceneImportResult result = importRailmlScene(fixture("other.xml"), sceneDir.string(), "");
	QVERIFY(!result.success());
	QCOMPARE(result.diagnostics.size(), std::size_t(1));
	QCOMPARE(result.diagnostics.front().code, std::string("scene.import.railml.format"));
	QVERIFY(!fs::exists(sceneDir));
}

void TestRailmlImporter::refusesMissingFile() {
	TempDir temp;
	const fs::path sceneDir = temp.path / "scene";
	const SceneImportResult result = importRailmlScene((temp.path / "missing.railml").string(), sceneDir.string(), "");
	QVERIFY(!result.success());
	QCOMPARE(result.diagnostics.size(), std::size_t(1));
	QCOMPARE(result.diagnostics.front().code, std::string("scene.import.railml.read"));
	QVERIFY(!fs::exists(sceneDir));
}

QTEST_APPLESS_MAIN(TestRailmlImporter)
#include "test_railmlimporter.moc"
