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
	void readsDemandTableFormatsAndSumsRows();
	void readsDemandTableWithPeriodAndSkipsUnknowns();
	void reportsDemandTableErrors();
	void writesDemandTableThatReadsBack();
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

void TestPassengerDemand::readsDemandTableFormatsAndSumsRows() {
	SceneModel scene = paimpol();
	// Add a station whose name has a comma to exercise quoted CSV fields and name matching.
	SceneStation commaStation = scene.stations.front();
	commaStation.id = "Port,North";
	commaStation.name = "Port, North";
	scene.stations.push_back(commaStation);

	// BOM, Windows line endings, empty lines, reordered case-insensitive headers, all three time forms,
	// duplicate rows summed, zero-count rows skipped, and quoted fields with commas.
	const std::string text = "\xEF\xBB\xBF"
							 "COUNT,Start,End,Destination,Origin\r\n"
							 "40,08:00,09:00,Paimpol,Guingamp\r\n"
							 "\r\n"
							 "50,07:00:00,08:00:00,Paimpol,Guingamp\r\n"
							 "25,25200,28800,Paimpol,Guingamp\r\n"
							 "12,07:00:00,08:00:00,\"Port, North\",Paimpol\r\n"
							 "0,07:00:00,08:00:00,Gourland,Guingamp\r\n"
							 "0,10:00:00,11:00:00,Paimpol,Guingamp\r\n";
	const PassengerDemandTable table = readPassengerDemandTable(text, scene.stations);
	QVERIFY2(table.ok(), table.errors.empty() ? "" : table.errors[0].c_str());
	QVERIFY(table.unknownStations.empty());
	QCOMPARE(table.totalPassengers, 127LL);
	// Slices are sorted in time order; the zero-only 10:00-11:00 slice is skipped.
	QCOMPARE(table.slices.size(), std::size_t{2});
	QCOMPARE(table.slices[0].id, std::string("slice-1"));
	QCOMPARE(table.slices[0].startSeconds, 7 * 3600.0);
	QCOMPARE(table.slices[0].endSeconds, 8 * 3600.0);
	QCOMPARE(table.slices[0].cells.size(), std::size_t{2});
	QCOMPARE(table.slices[0].cells[0].originStationId, std::string("Guingamp"));
	QCOMPARE(table.slices[0].cells[0].destinationStationId, std::string("Paimpol"));
	QCOMPARE(table.slices[0].cells[0].count, 75);
	QCOMPARE(table.slices[0].cells[1].originStationId, std::string("Paimpol"));
	QCOMPARE(table.slices[0].cells[1].destinationStationId, std::string("Port,North"));
	QCOMPARE(table.slices[0].cells[1].count, 12);
	QCOMPARE(table.slices[1].id, std::string("slice-2"));
	QCOMPARE(table.slices[1].startSeconds, 8 * 3600.0);
	QCOMPARE(table.slices[1].endSeconds, 9 * 3600.0);
	QCOMPARE(table.slices[1].cells.size(), std::size_t{1});
	QCOMPARE(table.slices[1].cells[0].count, 40);
}

void TestPassengerDemand::readsDemandTableWithPeriodAndSkipsUnknowns() {
	const std::vector<SceneStation> stations = paimpol().stations;
	const std::string text = "origin,destination,start,period,count\n"
							 "Guingamp,Paimpol,07:00:00,3600,30\n"
							 "Paimpol,Guingamp,08:00:00,00:30:00,15\n"
							 "Atlantis,Paimpol,07:00:00,3600,10\n"
							 "Guingamp,Atlantis,08:00:00,00:30:00,5\n"
							 "Mars,Venus,09:00:00,1800,4\n";
	const PassengerDemandTable table = readPassengerDemandTable(text, stations);
	QVERIFY2(table.ok(), table.errors.empty() ? "" : table.errors[0].c_str());
	QCOMPARE(table.slices.size(), std::size_t{2});
	QCOMPARE(table.slices[0].startSeconds, 7 * 3600.0);
	QCOMPARE(table.slices[0].endSeconds, 8 * 3600.0);
	QCOMPARE(table.slices[1].startSeconds, 8 * 3600.0);
	QCOMPARE(table.slices[1].endSeconds, 8.5 * 3600.0);
	QCOMPARE(table.totalPassengers, 45LL);
	QCOMPARE(table.unknownStations.size(), std::size_t{3});
	QCOMPARE(table.unknownStations[0].name, std::string("Atlantis"));
	QCOMPARE(table.unknownStations[0].rows, 2);
	QCOMPARE(table.unknownStations[0].passengers, 15LL);
	QCOMPARE(table.unknownStations[1].name, std::string("Mars"));
	QCOMPARE(table.unknownStations[1].rows, 1);
	QCOMPARE(table.unknownStations[1].passengers, 4LL);
	QCOMPARE(table.unknownStations[2].name, std::string("Venus"));
	QCOMPARE(table.unknownStations[2].rows, 1);
	QCOMPARE(table.unknownStations[2].passengers, 4LL);
}

void TestPassengerDemand::reportsDemandTableErrors() {
	const std::vector<SceneStation> stations = paimpol().stations;
	const auto mentions = [](const PassengerDemandTable& table, const std::string& part) {
		return std::any_of(table.errors.begin(), table.errors.end(),
			[&part](const std::string& error) { return error.find(part) != std::string::npos; });
	};

	// Empty file, both end and period, neither end nor period.
	QVERIFY(!readPassengerDemandTable("", stations).ok());
	const PassengerDemandTable both = readPassengerDemandTable(
		"origin,destination,start,end,period,count\nGuingamp,Paimpol,07:00,08:00,3600,5\n", stations);
	QVERIFY(!both.ok());
	QVERIFY(mentions(both, "both end and period"));
	const PassengerDemandTable neither = readPassengerDemandTable(
		"origin,destination,start,count\nGuingamp,Paimpol,07:00,5\n", stations);
	QVERIFY(!neither.ok());
	QVERIFY(mentions(neither, "either an end or a period"));

	// Row-level errors: bad times, non-integer/negative counts, same station, overlap.
	const PassengerDemandTable bad = readPassengerDemandTable(
		"origin,destination,start,end,count\n"
		"Guingamp,Paimpol,soon,08:00:00,5\n"
		"Guingamp,Paimpol,08:00:00,07:00:00,5\n"
		"Guingamp,Paimpol,07:00:00,08:00:00,-3\n"
		"Guingamp,Paimpol,07:00:00,08:00:00,2.5\n"
		"Guingamp,Guingamp,07:00:00,08:00:00,4\n"
		"Guingamp,Paimpol,07:00:00,08:00:00,10\n"
		"Paimpol,Guingamp,07:30:00,08:30:00,10\n",
		stations);
	QVERIFY(!bad.ok());
	QVERIFY(bad.slices.empty());
	QVERIFY(mentions(bad, "Line 2: Start time"));
	QVERIFY(mentions(bad, "Line 3: End time must be after start time"));
	QVERIFY(mentions(bad, "Line 4: Count '-3'"));
	QVERIFY(mentions(bad, "Line 5: Count '2.5'"));
	QVERIFY(mentions(bad, "Line 6: Origin and destination station Guingamp must differ"));
	QVERIFY(mentions(bad, "Line 8: Period 07:30:00-08:30:00 overlaps 07:00:00-08:00:00"));

	// Total above kMaxScenePassengerDemand.
	const PassengerDemandTable overLimit = readPassengerDemandTable(
		"origin,destination,start,end,count\n"
		"Guingamp,Paimpol,07:00:00,08:00:00,600000\n"
		"Paimpol,Guingamp,08:00:00,09:00:00,500000\n",
		stations);
	QVERIFY(!overLimit.ok());
	QVERIFY(overLimit.slices.empty());
	QVERIFY(mentions(overLimit, std::to_string(kMaxScenePassengerDemand)));
}

void TestPassengerDemand::writesDemandTableThatReadsBack() {
	SceneModel scene = paimpol();
	// Deliberately out of time order and out of station order; writePassengerDemandTable normalizes both.
	const ScenePassengerDemandSlice evening{"evening", 17 * 3600.0, 18 * 3600.0, {{"Paimpol", "Guingamp", 22}}};
	const ScenePassengerDemandSlice early{"early", 8 * 3600.0, 9 * 3600.0,
		{{"Paimpol", "Gourland", 37}, {"Guingamp", "Paimpol", 75}}};
	const std::string text = writePassengerDemandTable({evening, early}, scene.stations);
	QVERIFY(text.rfind("origin,destination,start,end,count\n", 0) == 0);
	const PassengerDemandTable readBack = readPassengerDemandTable(text, scene.stations);
	QVERIFY2(readBack.ok(), readBack.errors.empty() ? "" : readBack.errors[0].c_str());
	QCOMPARE(readBack.slices.size(), std::size_t{2});
	QCOMPARE(readBack.slices[0].startSeconds, 8 * 3600.0);
	QCOMPARE(readBack.slices[0].endSeconds, 9 * 3600.0);
	QCOMPARE(readBack.slices[0].cells.size(), std::size_t{2});
	QCOMPARE(readBack.slices[0].cells[0].originStationId, std::string("Guingamp"));
	QCOMPARE(readBack.slices[0].cells[0].destinationStationId, std::string("Paimpol"));
	QCOMPARE(readBack.slices[0].cells[0].count, 75);
	QCOMPARE(readBack.slices[0].cells[1].originStationId, std::string("Paimpol"));
	QCOMPARE(readBack.slices[0].cells[1].destinationStationId, std::string("Gourland"));
	QCOMPARE(readBack.slices[0].cells[1].count, 37);
	QCOMPARE(readBack.slices[1].startSeconds, 17 * 3600.0);
	QCOMPARE(readBack.slices[1].endSeconds, 18 * 3600.0);
	QCOMPARE(readBack.slices[1].cells.size(), std::size_t{1});
	QCOMPARE(readBack.slices[1].cells[0].count, 22);
	QCOMPARE(writePassengerDemandTable(readBack.slices, scene.stations), text);
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
