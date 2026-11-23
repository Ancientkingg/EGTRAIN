// Passenger demand slices of a scene: passengers.json, validation, legacy export and the CSV matrix of one slice.
//
// The tests start from the committed Paimpol scene, whose stations include Guingamp, Gourland and Paimpol.

#include "scene/PassengerDemandCsv.h"
#include "scene/SceneExporter.h"
#include "scene/SceneModel.h"
#include "scene/SceneReferences.h"
#include "scene/SceneValidator.h"
#include "scene/SceneWriter.h"

#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace {

SceneModel paimpol() {
	SceneLoadResult loaded = loadScene(std::string(EGTRAIN_SCENES_DIR) + "/Paimpol");
	return loaded.scene;
}

ScenePassengerDemandSlice morning() {
	return {"morning", 8 * 3600.0, 9 * 3600.0, {{"Guingamp", "Paimpol", 75}, {"Paimpol", "Gourland", 37}}};
}

std::vector<std::string> codes(const std::vector<SceneDiagnostic>& diagnostics) {
	std::vector<std::string> result;
	for (const SceneDiagnostic& diagnostic : diagnostics)
		if (diagnostic.code.rfind("scene.demand.", 0) == 0 || diagnostic.itemType == "demand_slice")
			result.push_back(diagnostic.code);
	return result;
}

bool has(const std::vector<std::string>& values, const std::string& value) {
	return std::find(values.begin(), values.end(), value) != values.end();
}

// The diagnostics with `code`, whatever their severity.
std::vector<SceneDiagnostic> withCode(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code) {
	std::vector<SceneDiagnostic> result;
	std::copy_if(diagnostics.begin(), diagnostics.end(), std::back_inserter(result),
		[&code](const SceneDiagnostic& diagnostic) { return diagnostic.code == code; });
	return result;
}

// A copy of the first station of the scene under another id, added to the scene.
void addStation(SceneModel& scene, const std::string& id) {
	SceneStation station = scene.stations.front();
	station.id = id;
	station.name = id;
	scene.stations.push_back(station);
}

} // namespace

class TestPassengerDemand : public QObject {
	Q_OBJECT

private slots:
	void savesAndLoadsDemand();
	void leavesOutAnEmptyDemand();
	void acceptsValidDemand();
	void rejectsInvalidSlices();
	void rejectsOverlappingSlices();
	void warnsAboutSlicesOutsideTheRun();
	void reportsGeneratedIdConflicts();
	void legacyExportWarnsAboutDemand();
	void readsTheSupervisorMatrix();
	void reportsMatrixErrors();
	void writesAMatrixThatReadsBack();
	void followsStationRenamesAndBlocksDeletes();
};

void TestPassengerDemand::savesAndLoadsDemand() {
	SceneModel scene = paimpol();
	QVERIFY(!scene.stations.empty());
	scene.passengerDemand = {morning(), {"evening", 17 * 3600.0, 18 * 3600.0, {{"Gourland", "Guingamp", 3}}}};
	QTemporaryDir directory;
	QVERIFY(saveScene(scene, directory.path().toStdString()).success());
	const SceneLoadResult loaded = loadScene(directory.path().toStdString());
	QVERIFY(!hasErrors(loaded.diagnostics));
	QCOMPARE(loaded.scene.passengerDemand.size(), std::size_t{2});
	const ScenePassengerDemandSlice& slice = loaded.scene.passengerDemand[0];
	QCOMPARE(slice.id, std::string("morning"));
	QCOMPARE(slice.startSeconds, 28800.0);
	QCOMPARE(slice.endSeconds, 32400.0);
	QCOMPARE(slice.cells.size(), std::size_t{2});
	QCOMPARE(slice.cells[1].originStationId, std::string("Paimpol"));
	QCOMPARE(slice.cells[1].destinationStationId, std::string("Gourland"));
	QCOMPARE(slice.cells[1].count, 37);
	QCOMPARE(loaded.scene.passengerDemand[1].cells[0].count, 3);
	// The passengers stay as they were.
	QCOMPARE(loaded.scene.passengers.size(), scene.passengers.size());
}

void TestPassengerDemand::leavesOutAnEmptyDemand() {
	QVERIFY(!encodePassengers({}).contains("demand"));
	const nlohmann::json encoded = encodePassengers({}, {morning()});
	QCOMPARE(encoded["demand"].size(), std::size_t{1});
	QCOMPARE(encoded["demand"][0]["cells"][0]["count"].get<int>(), 75);
	// A scene with demand and no passengers still writes passengers.json.
	SceneModel scene = paimpol();
	scene.passengers.clear();
	scene.passengerDemand = {morning()};
	QTemporaryDir directory;
	QVERIFY(saveScene(scene, directory.path().toStdString()).success());
	QVERIFY(fs::exists(fs::path(directory.path().toStdString()) / "passengers.json"));
	QCOMPARE(loadScene(directory.path().toStdString()).scene.passengerDemand.size(), std::size_t{1});
}

void TestPassengerDemand::acceptsValidDemand() {
	SceneModel scene = paimpol();
	// Slices that touch do not overlap.
	scene.passengerDemand = {morning(), {"later", 9 * 3600.0, 9.5 * 3600.0, {{"Guingamp", "Gourland", 1}}}};
	QVERIFY(codes(validateScene(scene)).empty());
}

void TestPassengerDemand::rejectsInvalidSlices() {
	SceneModel scene = paimpol();
	ScenePassengerDemandSlice slice = morning();
	slice.endSeconds = slice.startSeconds;
	slice.cells = {{"Guingamp", "Nowhere", 1}, {"Guingamp", "Guingamp", 2}, {"Paimpol", "Gourland", 0},
		{"Paimpol", "Gourland", 4}};
	ScenePassengerDemandSlice twin = morning();
	twin.startSeconds = 20 * 3600.0;
	twin.endSeconds = 21 * 3600.0;
	scene.passengerDemand = {slice, twin};
	const std::vector<std::string> found = codes(validateScene(scene));
	QVERIFY(has(found, "scene.demand.period"));
	QVERIFY(has(found, "scene.ref.unresolved"));
	QVERIFY(has(found, "scene.demand.same_station"));
	QVERIFY(has(found, "scene.demand.count"));
	QVERIFY(has(found, "scene.demand.duplicate_cell"));
	QVERIFY(has(found, "scene.id.duplicate"));

	// The total may not pass the limit that staging can hold.
	scene.passengerDemand = {morning()};
	scene.passengerDemand[0].cells[0].count = static_cast<int>(kMaxScenePassengerDemand);
	QVERIFY(has(codes(validateScene(scene)), "scene.demand.total"));
}

void TestPassengerDemand::rejectsOverlappingSlices() {
	SceneModel scene = paimpol();
	// Out of order in the file; the second starts before the first ends.
	scene.passengerDemand = {{"b", 8.5 * 3600.0, 10 * 3600.0, {{"Guingamp", "Gourland", 1}}}, morning()};
	const std::vector<SceneDiagnostic> diagnostics = validateScene(scene);
	const auto overlap = std::find_if(diagnostics.begin(), diagnostics.end(),
		[](const SceneDiagnostic& diagnostic) { return diagnostic.code == "scene.demand.overlap"; });
	QVERIFY(overlap != diagnostics.end());
	QCOMPARE(overlap->itemId, std::string("b"));
	QCOMPARE(overlap->relatedId, std::string("morning"));
}

void TestPassengerDemand::warnsAboutSlicesOutsideTheRun() {
	SceneModel scene = paimpol();
	// The run lasts from 07:10:40 for 9000 s, so until 09:40:40.
	QCOMPARE(scene.baseTime, std::string("07:10:40"));
	QCOMPARE(scene.settings.durationSeconds, 9000.0);
	const auto warningsFor = [&scene](const ScenePassengerDemandSlice& slice) {
		SceneModel copy = scene;
		copy.passengerDemand = {slice};
		return withCode(validateScene(copy), "scene.demand.period.outside_run");
	};
	QCOMPARE(warningsFor(morning()).size(), std::size_t{0});
	// Before the base time, also when only the start is before it.
	const std::vector<SceneDiagnostic> before = warningsFor({"early", 7 * 3600.0, 7.1 * 3600.0, {{"Guingamp", "Paimpol", 1}}});
	QCOMPARE(before.size(), std::size_t{1});
	QCOMPARE(before[0].severity, SceneSeverity::Warning);
	QCOMPARE(before[0].itemId, std::string("early"));
	QCOMPARE(warningsFor({"early", 7 * 3600.0, 8 * 3600.0, {{"Guingamp", "Paimpol", 1}}}).size(), std::size_t{1});
	// After the end of the run, also when only the end is after it.
	QCOMPARE(warningsFor({"late", 10 * 3600.0, 11 * 3600.0, {{"Guingamp", "Paimpol", 1}}}).size(), std::size_t{1});
	QCOMPARE(warningsFor({"late", 9 * 3600.0, 10 * 3600.0, {{"Guingamp", "Paimpol", 1}}}).size(), std::size_t{1});
	// A slice that fills the run exactly is inside it.
	QCOMPARE(warningsFor({"whole", 7 * 3600.0 + 640.0, 7 * 3600.0 + 640.0 + 9000.0, {{"Guingamp", "Paimpol", 1}}}).size(),
		std::size_t{0});
	// A slice with an invalid period gets the period error and no warning.
	QCOMPARE(warningsFor({"backwards", 7 * 3600.0, 6 * 3600.0, {{"Guingamp", "Paimpol", 1}}}).size(), std::size_t{0});

	// Without a valid base time or a duration there is no run to compare with.
	const ScenePassengerDemandSlice early = {"early", 1 * 3600.0, 2 * 3600.0, {{"Guingamp", "Paimpol", 1}}};
	scene.passengerDemand = {early};
	QCOMPARE(withCode(validateScene(scene), "scene.demand.period.outside_run").size(), std::size_t{1});
	for (const char* baseTime : {"", "soon", "24:00:00"}) {
		SceneModel copy = scene;
		copy.baseTime = baseTime;
		QVERIFY2(withCode(validateScene(copy), "scene.demand.period.outside_run").empty(), baseTime);
	}
	SceneModel noDuration = scene;
	noDuration.settings.hasDuration = false;
	QVERIFY(withCode(validateScene(noDuration), "scene.demand.period.outside_run").empty());
}

void TestPassengerDemand::reportsGeneratedIdConflicts() {
	const auto conflicts = [](const SceneModel& scene) { return withCode(validateScene(scene), "scene.demand.id.conflict"); };
	SceneModel scene = paimpol();
	scene.passengerDemand = {morning()};
	QVERIFY(conflicts(scene).empty());

	// Two cells of different slices that make the same ID prefix "a:b:c>Gourland#".
	addStation(scene, "b:c");
	addStation(scene, "c");
	scene.passengerDemand = {{"a", 8 * 3600.0, 8.5 * 3600.0, {{"b:c", "Gourland", 1}}},
		{"a:b", 8.5 * 3600.0, 9 * 3600.0, {{"c", "Gourland", 1}}}};
	const std::vector<SceneDiagnostic> twin = conflicts(scene);
	QCOMPARE(twin.size(), std::size_t{1});
	QCOMPARE(twin[0].severity, SceneSeverity::Error);
	QCOMPARE(twin[0].itemId, std::string("a:b"));
	QCOMPARE(twin[0].relatedId, std::string("a"));
	QCOMPARE(twin[0].file, std::string("passengers.json"));
	// A repeated slice ID or cell has its own error and is not reported again.
	scene.passengerDemand = {morning(), morning()};
	QVERIFY(conflicts(scene).empty());

	// A passenger of the scene with the ID that a cell generates.
	scene = paimpol();
	scene.passengerDemand = {morning()};
	QVERIFY(!scene.passengers.empty());
	scene.passengers[0].id = "morning:Guingamp>Paimpol#74";
	const std::vector<SceneDiagnostic> same = conflicts(scene);
	QCOMPARE(same.size(), std::size_t{1});
	QCOMPARE(same[0].severity, SceneSeverity::Error);
	QCOMPARE(same[0].itemId, std::string("morning:Guingamp>Paimpol#74"));
	QCOMPARE(same[0].path, std::string("passengers[0].id"));
	// An ID that the cell does not generate, and ordinary IDs with a '#', are fine.
	for (const char* id : {"morning:Guingamp>Paimpol#75", "morning:Guingamp>Paimpol#074", "morning:Guingamp>Paimpol#7x",
			 "morning:Guingamp>Paimpol#", "morning:Guingamp>Paimpol", "morning:Guingamp>Gourland#0", "night:Guingamp>Paimpol#0",
			 "commuter#1", "#", "a#b#2"}) {
		scene.passengers[0].id = id;
		QVERIFY2(conflicts(scene).empty(), id);
	}
	// A cell that is not valid generates no passenger.
	scene.passengers[0].id = "morning:Guingamp>Paimpol#0";
	scene.passengerDemand[0].cells[0].count = 0;
	QVERIFY(conflicts(scene).empty());
}

void TestPassengerDemand::legacyExportWarnsAboutDemand() {
	SceneModel scene = paimpol();
	scene.passengerDemand = {morning()};
	QTemporaryDir source;
	QTemporaryDir target;
	QVERIFY(saveScene(scene, source.path().toStdString()).success());
	const SceneExportResult exported = exportLegacyScene(source.path().toStdString(), target.path().toStdString());
	QVERIFY(!hasErrors(exported.diagnostics));
	QVERIFY(std::any_of(exported.diagnostics.begin(), exported.diagnostics.end(), [](const SceneDiagnostic& diagnostic) {
		return diagnostic.severity == SceneSeverity::Warning && diagnostic.code == "scene.export.compatibility"
			&& diagnostic.file == "passengers.json";
	}));
}

void TestPassengerDemand::readsTheSupervisorMatrix() {
	// The start of the matrix of the Paimpol case, with its byte order mark, Windows line ends and empty trailing cells.
	const std::string text = "\xEF\xBB\xBFTime Reference,3600,,,\r\n"
							 "O/D,Guingamp,Gourland,Paimpol\r\n"
							 "Guingamp,0,75,68\r\n"
							 "\r\n"
							 "Gourland,49,0,\r\n"
							 "Paimpol,52,37,0,\r\n";
	const PassengerDemandCsv matrix = readPassengerDemandCsv(text, paimpol().stations);
	QVERIFY2(matrix.ok(), matrix.errors.empty() ? "" : matrix.errors[0].c_str());
	QCOMPARE(matrix.periodSeconds, 3600.0);
	QCOMPARE(matrix.cells.size(), std::size_t{5});
	QCOMPARE(matrix.cells[0].originStationId, std::string("Guingamp"));
	QCOMPARE(matrix.cells[0].destinationStationId, std::string("Gourland"));
	QCOMPARE(matrix.cells[0].count, 75);
	QCOMPARE(matrix.cells[2].originStationId, std::string("Gourland"));
	QCOMPARE(matrix.cells[2].count, 49);
	QCOMPARE(matrix.cells[4].destinationStationId, std::string("Gourland"));
	QCOMPARE(matrix.cells[4].count, 37);
	// The Time Reference row may be left out.
	const PassengerDemandCsv bare = readPassengerDemandCsv("O/D,Paimpol\nGuingamp,4\n", paimpol().stations);
	QVERIFY(bare.ok());
	QCOMPARE(bare.periodSeconds, 0.0);
	QCOMPARE(bare.cells.size(), std::size_t{1});
}

void TestPassengerDemand::reportsMatrixErrors() {
	const std::vector<SceneStation> stations = paimpol().stations;
	const PassengerDemandCsv bad = readPassengerDemandCsv("Time Reference,soon\n"
														  "O/D,Paimpol,Atlantis,Paimpol\n"
														  "Guingamp,1.5,2,3,4\n"
														  "Paimpol,7,0,0\n"
														  "Mars,1\n",
		stations);
	QVERIFY(!bad.ok());
	QVERIFY(bad.cells.empty());
	const auto mentions = [&bad](const std::string& part) {
		return std::any_of(bad.errors.begin(), bad.errors.end(),
			[&part](const std::string& error) { return error.find(part) != std::string::npos; });
	};
	QVERIFY(mentions("Line 1: Time Reference"));
	QVERIFY(mentions("Line 2: Unknown destination station Atlantis"));
	QVERIFY(mentions("Line 2: Destination station Paimpol appears twice"));
	QVERIFY(mentions("Line 3: Count 1.5"));
	QVERIFY(mentions("Line 3: The row has more counts"));
	QVERIFY(mentions("Line 4: Count for Paimpol to itself must be 0"));
	QVERIFY(mentions("Line 5: Unknown origin station Mars"));
	QVERIFY(!readPassengerDemandCsv("", stations).ok());
	QVERIFY(!readPassengerDemandCsv("Time Reference,60\n", stations).ok());
}

void TestPassengerDemand::writesAMatrixThatReadsBack() {
	const std::vector<SceneStation> stations = paimpol().stations;
	const std::string text = writePassengerDemandCsv(morning(), stations);
	QVERIFY(text.rfind("Time Reference,3600\nO/D,", 0) == 0);
	const PassengerDemandCsv matrix = readPassengerDemandCsv(text, stations);
	QVERIFY(matrix.ok());
	QCOMPARE(matrix.periodSeconds, 3600.0);
	QCOMPARE(matrix.cells.size(), std::size_t{2});
	QCOMPARE(matrix.cells[0].count, 75);
	QCOMPARE(matrix.cells[1].originStationId, std::string("Paimpol"));
	QCOMPARE(matrix.cells[1].count, 37);
}

void TestPassengerDemand::followsStationRenamesAndBlocksDeletes() {
	SceneModel scene = paimpol();
	scene.passengerDemand = {morning()};
	const std::vector<std::string> users = sceneDirectDeleteConsumers(scene, "stations", "Gourland");
	QVERIFY(std::find(users.begin(), users.end(), "demand slice 'morning'") != users.end());
	sceneRewriteStationReferences(scene, "Gourland", "Gourland_2");
	QCOMPARE(scene.passengerDemand[0].cells[1].destinationStationId, std::string("Gourland_2"));
	QCOMPARE(scene.passengerDemand[0].cells[0].originStationId, std::string("Guingamp"));
}

QTEST_APPLESS_MAIN(TestPassengerDemand)
#include "test_passengerdemand.moc"
