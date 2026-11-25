#include "scene/RailmlImporter.h"
#include "scene/SceneModel.h"
#include "scene/SceneValidator.h"

#include <QElapsedTimer>
#include <QTest>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
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

// The text of a fixture.
std::string fixtureText(const char* name) {
	std::ifstream input(fixture(name), std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

// `text` with the first `from` replaced by `to`; fails the test when the text has no `from`.
std::string replaced(std::string text, const std::string& from, const std::string& to) {
	const std::size_t at = text.find(from);
	if (at == std::string::npos) {
		QTest::qFail(("the document has no " + from).c_str(), __FILE__, __LINE__);
		return text;
	}
	return text.replace(at, from.size(), to);
}

// Writes `text` to a railML file in the temporary directory and imports it into the folder "scene" there.
SceneImportResult importText(const TempDir& temp, const std::string& text, const RailmlImportOptions& options = {}) {
	const fs::path file = temp.path / "test.railml";
	std::ofstream(file, std::ios::binary) << text;
	return importRailmlScene(file.string(), (temp.path / "scene").string(), "", options);
}

// Loads the scene that an import wrote into the temporary directory and checks that it is runnable.
SceneModel loadRunnable(const TempDir& temp, const SceneImportResult& result) {
	if (!result.success()) {
		QTest::qFail(errorText(result.diagnostics).c_str(), __FILE__, __LINE__);
		return {};
	}
	const SceneLoadResult loaded = loadScene((temp.path / "scene").string());
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

// The text with a trains element that holds `trains` at the end of its timetable.
std::string withTrains(const std::string& text, const std::string& trains) {
	return replaced(text, "  </timetable>", "    <trains>\n" + trains + "    </trains>\n  </timetable>");
}

// A second train part of the macroscopic fixture, from Westdorf to Mittelstadt an hour after the first.
constexpr const char* kSecondTrainPart = R"(      <trainPart id="tp_rb2" trainNumber="RB 2">
        <operatingPeriodRef ref="op_workdays"/>
        <ocpsTT>
          <ocpTT ocpRef="ocp_w" sequence="1" ocpType="stop">
            <times scope="scheduled" departure="07:00:00"/>
            <sectionTT id="tp_rb2_s1">
              <trackRef ref="tw" dir="up"/>
            </sectionTT>
          </ocpTT>
          <ocpTT ocpRef="ocp_m" sequence="2" ocpType="stop">
            <times scope="scheduled" arrival="07:05:00"/>
          </ocpTT>
        </ocpsTT>
      </trainPart>
)";

// A line of one track with `stops` OCPs 1 km apart and one train part that stops at each of them, a minute apart.
std::string longLine(int stops) {
	std::string sections;
	std::string ocps;
	std::string visits;
	for (int index = 1; index <= stops; ++index) {
		const std::string id = "ocp_" + std::to_string(index);
		sections += "            <crossSection id=\"cs_" + std::to_string(index) + "\" pos=\"" + std::to_string(index * 1000) + "\" ocpRef=\"" + id + "\"/>\n";
		ocps += "      <ocp id=\"" + id + "\" name=\"Stop " + std::to_string(index) + "\"/>\n";
		char arrival[16];
		char departure[16];
		std::snprintf(arrival, sizeof arrival, "%02d:%02d:00", 6 + index / 60, index % 60);
		std::snprintf(departure, sizeof departure, "%02d:%02d:30", 6 + index / 60, index % 60);
		visits += "          <ocpTT ocpRef=\"" + id + "\" sequence=\"" + std::to_string(index) + "\" ocpType=\"stop\">\n";
		visits += std::string("            <times scope=\"scheduled\"") + (index > 1 ? " arrival=\"" + std::string(arrival) + "\"" : "")
			+ (index < stops ? " departure=\"" + std::string(departure) + "\"" : "") + "/>\n";
		if (index < stops)
			visits += "            <sectionTT><trackRef ref=\"t\" dir=\"up\"/></sectionTT>\n";
		visits += "          </ocpTT>\n";
	}
	return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<railml xmlns=\"https://www.railml.org/schemas/2018\" version=\"2.4\">\n"
		   "  <infrastructure id=\"inf\">\n    <tracks>\n      <track id=\"t\">\n        <trackTopology>\n"
		   "          <trackBegin id=\"t_begin\" pos=\"0\"><openEnd id=\"t_open1\"/></trackBegin>\n"
		   "          <trackEnd id=\"t_end\" pos=\""
		+ std::to_string((stops + 1) * 1000)
		+ "\"><openEnd id=\"t_open2\"/></trackEnd>\n"
		  "          <crossSections>\n"
		+ sections + "          </crossSections>\n        </trackTopology>\n      </track>\n    </tracks>\n"
		+ "    <operationControlPoints>\n" + ocps + "    </operationControlPoints>\n  </infrastructure>\n"
		+ "  <timetable id=\"tt\">\n    <trainParts>\n      <trainPart id=\"tp\" trainNumber=\"1\">\n        <ocpsTT>\n" + visits
		+ "        </ocpsTT>\n      </trainPart>\n    </trainParts>\n  </timetable>\n</railml>\n";
}

// A line of `stops` OCPs that the train part runs down the chainage, from ocp_1 at the high end to the last OCP at the low
// end. Main signals for that direction stand between the first `signalled` + 1 OCPs, so the OCPs after them share the
// last block of the route.
std::string downLine(int stops, int signalled) {
	std::string sections;
	std::string signalText;
	std::string ocps;
	std::string visits;
	for (int index = 1; index <= stops; ++index) {
		const std::string id = "ocp_" + std::to_string(index);
		const int pos = (stops + 1 - index) * 1000;
		sections += "            <crossSection id=\"cs_" + std::to_string(index) + "\" pos=\"" + std::to_string(pos) + "\" ocpRef=\"" + id + "\"/>\n";
		if (index <= signalled)
			signalText += "            <signal id=\"sig_" + std::to_string(index) + "\" pos=\"" + std::to_string(pos - 500)
				+ "\" dir=\"down\" type=\"main\"/>\n";
		ocps += "      <ocp id=\"" + id + "\" name=\"Stop " + std::to_string(index) + "\"/>\n";
		char arrival[16];
		char departure[16];
		std::snprintf(arrival, sizeof arrival, "%02d:%02d:00", 6 + index / 60, index % 60);
		std::snprintf(departure, sizeof departure, "%02d:%02d:30", 6 + index / 60, index % 60);
		visits += "          <ocpTT ocpRef=\"" + id + "\" sequence=\"" + std::to_string(index) + "\" ocpType=\"stop\">\n";
		visits += std::string("            <times scope=\"scheduled\"") + (index > 1 ? " arrival=\"" + std::string(arrival) + "\"" : "")
			+ (index < stops ? " departure=\"" + std::string(departure) + "\"" : "") + "/>\n";
		if (index < stops)
			visits += "            <sectionTT><trackRef ref=\"t\" dir=\"down\"/></sectionTT>\n";
		visits += "          </ocpTT>\n";
	}
	return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<railml xmlns=\"https://www.railml.org/schemas/2018\" version=\"2.4\">\n"
		   "  <infrastructure id=\"inf\">\n    <tracks>\n      <track id=\"t\">\n        <trackTopology>\n"
		   "          <trackBegin id=\"t_begin\" pos=\"0\"><openEnd id=\"t_open1\"/></trackBegin>\n"
		   "          <trackEnd id=\"t_end\" pos=\""
		+ std::to_string((stops + 1) * 1000)
		+ "\"><openEnd id=\"t_open2\"/></trackEnd>\n"
		  "          <crossSections>\n"
		+ sections + "          </crossSections>\n        </trackTopology>\n        <ocsElements><signals>\n" + signalText
		+ "        </signals></ocsElements>\n      </track>\n    </tracks>\n"
		+ "    <operationControlPoints>\n" + ocps + "    </operationControlPoints>\n  </infrastructure>\n"
		+ "  <timetable id=\"tt\">\n    <trainParts>\n      <trainPart id=\"tp\" trainNumber=\"1\">\n        <ocpsTT>\n" + visits
		+ "        </ocpsTT>\n      </trainPart>\n    </trainParts>\n  </timetable>\n</railml>\n";
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
	void refusesOutOfRangeTimes();
	void readsTimeZoneOffsets();
	void warnsAboutUnreadableTimes();
	void ignoresTimesOfOtherPeriods();
	void leavesOutSpeedsOfZero();
	void refusesDuplicateIds();
	void renamesClashingSceneIds();
	void walksDeepNesting();
	void refusesOutOfRangeCounts();
	void refusesSceneFolderHoldingFile();
	void refusesSceneThatFailsValidation();
	void endsSpeedRestrictions();
	void splitsTrainsWithManyStops();
	void extendsOneBlockPartRoutes();
	void listsOperatingPeriods();
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

// A day offset or an hour far out of range is refused at once, instead of rolling the times over day by day.
void TestRailmlImporter::refusesOutOfRangeTimes() {
	const std::string base = fixtureText("macroscopic.railml");
	const std::string later = replaced(base, R"(arrival="06:05:00" departure="06:06:00")", R"(arrival="08:10:00" departure="08:11:00")");
	for (const auto& [from, to, detail] : std::vector<std::array<std::string, 3>>{
			 {R"(departure="06:00:00"/>)", R"(departure="08:00:00" departureDay="100000000000000000000"/>)", "departureDay"},
			 {R"(departure="06:00:00"/>)", R"(departure="08:00:00" departureDay="-1"/>)", "departureDay"},
			 {R"(departure="06:00:00"/>)", R"(departure="1000000000000:00:00"/>)", "departure"},
			 {R"(<times scope="scheduled" arrival="06:14:00"/>)", R"(<times scope="scheduled" arrival="06:14:00" arrivalDay="0.5"/>)", "arrivalDay"},
		 }) {
		TempDir temp;
		QElapsedTimer timer;
		timer.start();
		const SceneImportResult result = importText(temp, replaced(later, from, to));
		QVERIFY2(timer.elapsed() < 10000, to.c_str());
		QVERIFY2(!result.success(), to.c_str());
		const SceneDiagnostic* range = findDiagnostic(result.diagnostics, "scene.import.railml.out_of_range", "tp_rb1");
		QVERIFY2(range != nullptr, errorText(result.diagnostics).c_str());
		QVERIFY2(range->message.find(detail) != std::string::npos, range->message.c_str());
		QCOMPARE(range->severity, SceneSeverity::Error);
		QVERIFY(!fs::exists(temp.path / "scene"));
	}
}

// An xs:time may end in a UTC offset. The clock time is kept as written, since timetable times are local times; offsets
// that differ from each other are reported once.
void TestRailmlImporter::readsTimeZoneOffsets() {
	const std::string base = fixtureText("macroscopic.railml");
	TempDir plainTemp;
	const SceneModel plain = loadRunnable(plainTemp, importText(plainTemp, base));
	if (QTest::currentTestFailed())
		return;
	const std::string sameOffset = replaced(replaced(base, R"(departure="06:00:00"/>)", R"(departure="06:00:00+01:00"/>)"),
		R"(arrival="06:14:00"/>)", R"(arrival="06:14:00.0000000+01:00"/>)");
	const std::string otherOffsets =
		replaced(sameOffset, R"(arrival="06:05:00" departure="06:06:00")", R"(arrival="06:05:00Z" departure="06:06:00-02:30")");
	for (const auto& [text, mixed] : std::vector<std::pair<std::string, bool>>{{sameOffset, false}, {otherOffsets, true}}) {
		TempDir temp;
		const SceneImportResult result = importText(temp, text);
		const SceneModel scene = loadRunnable(temp, result);
		if (QTest::currentTestFailed())
			return;
		QCOMPARE(scene.baseTime, plain.baseTime);
		QCOMPARE(scene.services.size(), std::size_t(1));
		const std::vector<SceneStop>& stops = scene.services.front().stops;
		const std::vector<SceneStop>& plainStops = plain.services.front().stops;
		QCOMPARE(stops.size(), plainStops.size());
		for (std::size_t index = 0; index < stops.size(); ++index) {
			QCOMPARE(stops[index].hasPlannedArrival, plainStops[index].hasPlannedArrival);
			QCOMPARE(stops[index].plannedArrivalSeconds, plainStops[index].plannedArrivalSeconds);
			QCOMPARE(stops[index].hasPlannedDeparture, plainStops[index].hasPlannedDeparture);
			QCOMPARE(stops[index].plannedDepartureSeconds, plainStops[index].plannedDepartureSeconds);
		}
		const SceneDiagnostic* offsets = findDiagnostic(result.diagnostics, "scene.import.railml.time_offset");
		QCOMPARE(offsets != nullptr, mixed);
		if (offsets != nullptr) {
			QCOMPARE(offsets->severity, SceneSeverity::Warning);
			for (const char* offset : {"-02:30", "+00:00", "+01:00"})
				QVERIFY2(offsets->message.find(offset) != std::string::npos, offsets->message.c_str());
		}
	}
}

// A time that is not an xs:time, such as one without seconds, is left out with a warning, and the stop has no such time.
void TestRailmlImporter::warnsAboutUnreadableTimes() {
	TempDir temp;
	const SceneImportResult result = importText(temp,
		replaced(fixtureText("macroscopic.railml"), R"(arrival="06:05:00" departure="06:06:00")", R"(arrival="06:05" departure="06:06:00")"));
	const SceneModel scene = loadRunnable(temp, result);
	if (QTest::currentTestFailed())
		return;
	const SceneDiagnostic* unreadable =
		findDiagnostic(result.diagnostics, "scene.import.railml.time", R"(train part tp_rb1 ocpTT 3 (ocp_m) arrival="06:05")");
	QVERIFY2(unreadable != nullptr, errorText(result.diagnostics).c_str());
	QCOMPARE(unreadable->severity, SceneSeverity::Warning);
	const std::vector<SceneStop>& stops = scene.services.front().stops;
	const auto stop = std::find_if(stops.begin(), stops.end(), [](const SceneStop& candidate) { return candidate.stationId == "ocp_m"; });
	QVERIFY(stop != stops.end());
	QVERIFY(!stop->hasPlannedArrival);
	QVERIFY(stop->hasPlannedDeparture);
}

// The times of train parts that the import leaves out, those of other operating periods, do not refuse the file.
void TestRailmlImporter::ignoresTimesOfOtherPeriods() {
	const std::string text = replaced(fixtureText("microscopic.railml"), R"(departure="10:00:00"/>)",
		R"(departure="10:00:00" departureDay="9"/>)");
	TempDir temp;
	const SceneImportResult result = importText(temp, text);
	const SceneModel scene = loadRunnable(temp, result);
	if (QTest::currentTestFailed())
		return;
	QCOMPARE(scene.services.size(), std::size_t(2));
	QVERIFY(findDiagnostic(result.diagnostics, "scene.import.railml.out_of_range") == nullptr);
	// Imported, the train part refuses the file.
	TempDir sundayTemp;
	RailmlImportOptions options;
	options.operatingPeriod = "op_sunday";
	const SceneImportResult sunday = importText(sundayTemp, text, options);
	QVERIFY(!sunday.success());
	QVERIFY2(findDiagnostic(sunday.diagnostics, "scene.import.railml.out_of_range", R"(train part tp_3 ocpTT 1 (ocp_a) departureDay="9")")
			!= nullptr,
		errorText(sunday.diagnostics).c_str());
	QVERIFY(!fs::exists(sundayTemp.path / "scene"));
}

// A speed limit of 0 km/h or less is no limit: the change is left out and reported.
void TestRailmlImporter::leavesOutSpeedsOfZero() {
	TempDir temp;
	const std::string text = replaced(fixtureText("macroscopic.railml"), R"(dir="up" vMax="80")", R"(dir="up" vMax="0")");
	const SceneImportResult result = importText(temp, replaced(text, R"(dir="down" vMax="120")", R"(dir="down" vMax="-5")"));
	const SceneModel scene = loadRunnable(temp, result);
	if (QTest::currentTestFailed())
		return;
	for (const SceneArc& arc : scene.arcs)
		QVERIFY2(arc.speedLimitMs > 0.0, arc.id.c_str());
	// Track te keeps its limit in the other direction; tw gets its up limit both ways.
	QCOMPARE(arcAt(scene, "te", 8.0)->speedLimitMs * 3.6, 80.0);
	QCOMPARE(arcAt(scene, "tw", 2.0)->speedLimitMs * 3.6, 120.0);
	const SceneDiagnostic* zero = findDiagnostic(result.diagnostics, "scene.import.railml.speed", "te_v1");
	QVERIFY2(zero != nullptr, errorText(result.diagnostics).c_str());
	QVERIFY(zero->message.find("tw_v2") != std::string::npos);
	QCOMPARE(findRow(scene, "speedChange")->skippedCount, 2);
}

// railML ids are unique: a file that uses one id for two elements of one kind is refused before anything is written.
void TestRailmlImporter::refusesDuplicateIds() {
	const std::string base = fixtureText("macroscopic.railml");
	const std::string signalText = replaced(replaced(base, "        </trackElements>\n      </track>\n      <track id=\"te\"",
												"        </trackElements>\n        <ocsElements><signals><signal id=\"sig\" pos=\"4000\" dir=\"up\" "
												"type=\"main\"/></signals></ocsElements>\n      </track>\n      <track id=\"te\""),
		"        </trackElements>\n      </track>\n    </tracks>",
		"        </trackElements>\n        <ocsElements><signals><signal id=\"sig\" pos=\"6000\" dir=\"up\" "
		"type=\"main\"/></signals></ocsElements>\n      </track>\n    </tracks>");
	const std::string train = "      <train id=\"T\" type=\"operational\"><trainPartSequence sequence=\"1\"><trainPartRef ref=\"tp_rb1\" "
							  "position=\"1\"/></trainPartSequence></train>\n";
	const std::string trainText = withTrains(base, train + train);
	const std::string trackText = replaced(base, R"(<track id="te")", R"(<track id="tw")");
	for (const auto& [text, kind, id] : std::vector<std::array<std::string, 3>>{
			 {signalText, "signal", "sig"}, {trainText, "train", "T"}, {trackText, "track", "tw"}}) {
		TempDir temp;
		const SceneImportResult result = importText(temp, text);
		QVERIFY2(!result.success(), kind.c_str());
		const SceneDiagnostic* duplicate = findDiagnostic(result.diagnostics, "scene.import.railml.duplicate_id");
		QVERIFY2(duplicate != nullptr, errorText(result.diagnostics).c_str());
		QCOMPARE(duplicate->severity, SceneSeverity::Error);
		QCOMPARE(duplicate->itemType, kind);
		QCOMPARE(duplicate->itemId, id);
		QVERIFY(!fs::exists(temp.path / "scene"));
	}
}

// Ids that railML keeps apart but the scene does not, and sequences without a number, get a suffix that is reported.
void TestRailmlImporter::renamesClashingSceneIds() {
	TempDir temp;
	std::string text = replaced(fixtureText("macroscopic.railml"), "  </infrastructure>\n",
		"  </infrastructure>\n  <rollingstock id=\"rs\">\n    <vehicles>\n"
		"      <vehicle id=\"F\" length=\"20\" speed=\"100\" bruttoWeight=\"40\" numberDrivenAxles=\"2\"/>\n"
		"      <vehicle id=\"V\" length=\"25\" speed=\"120\" bruttoWeight=\"50\" numberDrivenAxles=\"2\"/>\n"
		"    </vehicles>\n    <formations>\n"
		"      <formation id=\"F\"><trainOrder><vehicleRef orderNumber=\"1\" vehicleRef=\"V\"/></trainOrder></formation>\n"
		"      <formation id=\"railml_default\"><trainOrder><vehicleRef orderNumber=\"1\" vehicleRef=\"V\"/></trainOrder></formation>\n"
		"    </formations>\n  </rollingstock>\n");
	text = replaced(text, "    </trainParts>", std::string(kSecondTrainPart) + "    </trainParts>");
	text = withTrains(text,
		"      <train id=\"T\" type=\"operational\">\n"
		"        <trainPartSequence><trainPartRef ref=\"tp_rb1\" position=\"1\"/></trainPartSequence>\n"
		"        <trainPartSequence><trainPartRef ref=\"tp_rb2\" position=\"1\"/></trainPartSequence>\n"
		"      </train>\n");
	const SceneImportResult result = importText(temp, text);
	const SceneModel scene = loadRunnable(temp, result);
	if (QTest::currentTestFailed())
		return;
	std::vector<std::string> units;
	for (const SceneTrainUnit& unit : scene.trainUnits)
		units.push_back(unit.id);
	QCOMPARE(units, (std::vector<std::string>{"F", "railml_default", "F.2", "railml_default.2"}));
	std::vector<std::string> services;
	for (const SceneService& service : scene.services)
		services.push_back(service.id);
	QCOMPARE(services, (std::vector<std::string>{"T.0", "T.0.2"}));
	QCOMPARE(scene.services.front().composition, std::string("railml_default.2"));
	QVERIFY(findDiagnostic(result.diagnostics, "scene.import.railml.renamed", "F -> F.2") != nullptr);
	QVERIFY(findDiagnostic(result.diagnostics, "scene.import.railml.renamed", "railml_default -> railml_default.2") != nullptr);
	QVERIFY(findDiagnostic(result.diagnostics, "scene.import.railml.renamed", "T.0 -> T.0.2") != nullptr);
}

// Deeply nested elements are walked without recursion.
void TestRailmlImporter::walksDeepNesting() {
	TempDir temp;
	constexpr int depth = 100000;
	std::string nested;
	nested.reserve(depth * 7);
	for (int level = 0; level < depth; ++level)
		nested += "<n>";
	for (int level = 0; level < depth; ++level)
		nested += "</n>";
	const std::string text = replaced(fixtureText("macroscopic.railml"), R"(<ocp id="ocp_w" name="Westdorf">)",
		R"(<ocp id="ocp_w" name="Westdorf">)" + nested);
	const SceneImportResult result = importText(temp, text);
	const SceneModel scene = loadRunnable(temp, result);
	if (QTest::currentTestFailed())
		return;
	QCOMPARE(scene.stationViews.size(), std::size_t(4));
}

// A count far out of range is refused before it is converted to a whole number.
void TestRailmlImporter::refusesOutOfRangeCounts() {
	const std::string stock = "  </infrastructure>\n  <rollingstock id=\"rs\">\n    <vehicles>\n"
							  "      <vehicle id=\"V\" length=\"25\"><passenger><places category=\"class2\" count=\"PLACES\"/></passenger></vehicle>\n"
							  "    </vehicles>\n    <formations>\n"
							  "      <formation id=\"F\"><trainOrder><vehicleRef orderNumber=\"1\" vehicleRef=\"V\" vehicleCount=\"COUNT\"/></trainOrder></formation>\n"
							  "    </formations>\n  </rollingstock>\n";
	const auto document = [&stock](const std::string& places, const std::string& count) {
		return replaced(fixtureText("macroscopic.railml"), "  </infrastructure>\n",
			replaced(replaced(stock, "PLACES", places), "COUNT", count));
	};
	for (const auto& [text, detail] : std::vector<std::array<std::string, 2>>{
			 {document("80", "1e10"), "formation F vehicleCount"},
			 {document("80", "2.5"), "formation F vehicleCount"},
			 {document("1e10", "2"), "vehicle V places count"}}) {
		TempDir temp;
		const SceneImportResult result = importText(temp, text);
		QVERIFY2(!result.success(), detail.c_str());
		const SceneDiagnostic* range = findDiagnostic(result.diagnostics, "scene.import.railml.out_of_range", detail);
		QVERIFY2(range != nullptr, errorText(result.diagnostics).c_str());
		QVERIFY(!fs::exists(temp.path / "scene"));
	}
	// Counts within range are converted.
	TempDir temp;
	const SceneModel scene = loadRunnable(temp, importText(temp, document("80", "4")));
	if (QTest::currentTestFailed())
		return;
	QVERIFY(findId(scene.trainUnits, "F") != nullptr);
	QCOMPARE(findId(scene.trainUnits, "F")->passengerCapacity, 320);
}

// The import replaces the scene folder, so a folder that holds the railML file is refused.
void TestRailmlImporter::refusesSceneFolderHoldingFile() {
	TempDir temp;
	const fs::path data = temp.path / "data";
	fs::create_directories(data);
	const fs::path file = data / "net.railml";
	std::ofstream(file, std::ios::binary) << fixtureText("macroscopic.railml");
	for (const fs::path& sceneDir : {data, temp.path, data / "."}) {
		const SceneImportResult result = importRailmlScene(file.string(), sceneDir.string(), "");
		QVERIFY2(!result.success(), sceneDir.string().c_str());
		const SceneDiagnostic* refused = findDiagnostic(result.diagnostics, "scene.import.railml.destination");
		QVERIFY2(refused != nullptr, errorText(result.diagnostics).c_str());
		QVERIFY(refused->message.find("which the import would replace") != std::string::npos);
		QVERIFY(fs::exists(file));
	}
	// A folder beside the file is fine.
	QVERIFY(importRailmlScene(file.string(), (temp.path / "scene").string(), "").success());
	QVERIFY(fs::exists(file));
}

// A scene that does not pass validation is never written; the validation errors are listed.
void TestRailmlImporter::refusesSceneThatFailsValidation() {
	TempDir temp;
	const SceneImportResult result = importText(temp, replaced(fixtureText("macroscopic.railml"), R"(<track id="te")", "<track id=\"te&#9;x\""));
	QVERIFY(!result.success());
	QVERIFY2(findDiagnostic(result.diagnostics, "scene.import.railml.invalid") != nullptr, errorText(result.diagnostics).c_str());
	QVERIFY(findDiagnostic(result.diagnostics, "scene.id.separator") != nullptr);
	QVERIFY(!fs::exists(temp.path / "scene"));
}

// A speed change with vMax "end" ends the restriction that the change before it began.
void TestRailmlImporter::endsSpeedRestrictions() {
	TempDir temp;
	const std::string text = replaced(fixtureText("macroscopic.railml"), R"(<speedChange id="te_v2")",
		R"(<speedChange id="te_v3" pos="7000" dir="up" vMax="40"/>
            <speedChange id="te_v4" pos="9000" dir="up" vMax="end"/>
            <speedChange id="te_v5" pos="11000" dir="down" vMax="60"/>
            <speedChange id="te_v6" pos="10000" dir="down" vMax="end"/>
            <speedChange id="te_v2")");
	const SceneModel scene = loadRunnable(temp, importText(temp, text));
	if (QTest::currentTestFailed())
		return;
	QCOMPARE(arcAt(scene, "te", 6.0)->speedLimitMs * 3.6, 80.0);
	QCOMPARE(arcAt(scene, "te", 8.0)->speedLimitMs * 3.6, 40.0);
	QCOMPARE(arcAt(scene, "te", 9.5)->speedLimitMs * 3.6, 80.0);
	QCOMPARE(arcAt(scene, "te", 10.5)->speedLimitMs * 3.6, 60.0);
	QCOMPARE(arcAt(scene, "te", 11.5)->speedLimitMs * 3.6, 80.0);
}

// A train with more stops than a service holds runs as consecutive services that meet at a shared stop.
void TestRailmlImporter::splitsTrainsWithManyStops() {
	TempDir temp;
	const SceneImportResult result = importText(temp, longLine(42));
	const SceneModel scene = loadRunnable(temp, result);
	if (QTest::currentTestFailed())
		return;
	QCOMPARE(scene.services.size(), std::size_t(2));
	const SceneService& first = scene.services[0];
	const SceneService& second = scene.services[1];
	QCOMPARE(first.id, std::string("tp_1"));
	QCOMPARE(second.id, std::string("tp_2"));
	QCOMPARE(first.stops.size(), std::size_t(40));
	QCOMPARE(second.stops.size(), std::size_t(3));
	QCOMPARE(first.stops.back().stationId, std::string("ocp_40"));
	QCOMPARE(second.stops.front().stationId, std::string("ocp_40"));
	QVERIFY(!first.stops.back().hasPlannedDeparture);
	QVERIFY(!second.stops.front().hasPlannedArrival);
	QVERIFY(!second.hasEntryTime);
	QCOMPARE(second.stops.back().plannedArrivalSeconds - first.stops.front().plannedDepartureSeconds, 41.0 * 60.0 - 30.0);
	// The second route starts at the block of the shared stop.
	const SceneRoute* firstRoute = findId(scene.routes, first.route);
	const SceneRoute* secondRoute = findId(scene.routes, second.route);
	QVERIFY(firstRoute != nullptr && secondRoute != nullptr);
	QCOMPARE(secondRoute->blocks.front(), firstRoute->blocks.back());
	QCOMPARE(scene.routes.size(), std::size_t(2));
	QVERIFY(findDiagnostic(result.diagnostics, "scene.import.railml.service_split", "tp") != nullptr);
}

// The part of a split train that runs down the chainage within one block gets the block before it too, since a route of
// one block has to run up the chainage.
void TestRailmlImporter::extendsOneBlockPartRoutes() {
	TempDir temp;
	const SceneImportResult result = importText(temp, downLine(42, 38));
	const SceneModel scene = loadRunnable(temp, result);
	if (QTest::currentTestFailed())
		return;
	QCOMPARE(scene.services.size(), std::size_t(2));
	const SceneService& second = scene.services[1];
	QCOMPARE(second.id, std::string("tp_2"));
	QCOMPARE(second.stops.size(), std::size_t(3));
	QCOMPARE(second.stops.front().stationId, std::string("ocp_40"));
	const SceneRoute* firstRoute = findId(scene.routes, scene.services[0].route);
	const SceneRoute* secondRoute = findId(scene.routes, second.route);
	QVERIFY(firstRoute != nullptr && secondRoute != nullptr);
	QVERIFY(secondRoute->reversed);
	QCOMPARE(secondRoute->blocks.size(), std::size_t(2));
	QCOMPARE(secondRoute->blocks.back(), firstRoute->blocks.back());
}

// The periods with their train parts, for a choice before the import, and the one that an import without a choice takes.
void TestRailmlImporter::listsOperatingPeriods() {
	const RailmlOperatingPeriods periods = listRailmlOperatingPeriods(fixture("microscopic.railml"));
	QCOMPARE(periods.periods.size(), std::size_t(2));
	QCOMPARE(periods.periods[0].id, std::string("op_daily"));
	QCOMPARE(periods.periods[0].name, std::string("Daily"));
	QCOMPARE(periods.periods[0].trainParts, 2);
	QCOMPARE(periods.periods[1].id, std::string("op_sunday"));
	QCOMPARE(periods.periods[1].name, std::string("Sundays"));
	QCOMPARE(periods.periods[1].trainParts, 1);
	QCOMPARE(periods.defaultPeriod, std::string("op_daily"));

	// A train part without a period runs in each of them; a period without train parts is left out.
	TempDir temp;
	std::string text = replaced(fixtureText("microscopic.railml"), R"(<operatingPeriod id="op_sunday" name="Sundays"/>)",
		R"(<operatingPeriod id="op_sunday" name="Sundays"/><operatingPeriod id="op_never" name="Never"/>)");
	text = replaced(text, R"(<operatingPeriodRef ref="op_sunday"/>)", "");
	const fs::path file = temp.path / "periods.railml";
	std::ofstream(file, std::ios::binary) << text;
	const RailmlOperatingPeriods withoutPeriod = listRailmlOperatingPeriods(file.string());
	QCOMPARE(withoutPeriod.periods.size(), std::size_t(1));
	QCOMPARE(withoutPeriod.periods[0].trainParts, 3);
	QCOMPARE(withoutPeriod.defaultPeriod, std::string("op_daily"));

	QVERIFY(listRailmlOperatingPeriods(fixture("macroscopic.railml")).periods.size() == 1);
	QVERIFY(listRailmlOperatingPeriods(fixture("railml3.railml")).periods.empty());
	QVERIFY(listRailmlOperatingPeriods((temp.path / "missing.railml").string()).periods.empty());
}

QTEST_APPLESS_MAIN(TestRailmlImporter)
#include "test_railmlimporter.moc"
