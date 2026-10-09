#include "scene/SceneModel.h"
#include "scene/SectionInventory.h"
#include "scene/SceneValidator.h"
#include "scene/SignallingLevel.h"
#include "simulation/RuntimeLimits.h"

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static bool hasCode(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code) {
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.code == code)
			return true;
	}
	return false;
}

static bool hasCodeAndSeverity(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code,
	SceneSeverity severity) {
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.code == code && diagnostic.severity == severity)
			return true;
	}
	return false;
}

static bool hasCodeAndPath(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code,
	const std::string& path) {
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.severity == SceneSeverity::Error
			&& diagnostic.code == code && diagnostic.path == path)
			return true;
	}
	return false;
}

static const SceneDiagnostic* findCode(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code) {
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.code == code)
			return &diagnostic;
	}
	return nullptr;
}

static std::vector<SceneDiagnostic> findAll(const std::vector<SceneDiagnostic>& diagnostics,
	const std::string& code) {
	std::vector<SceneDiagnostic> found;
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.code == code)
			found.push_back(diagnostic);
	}
	return found;
}

static bool contains(const std::string& text, const std::string& part) {
	return text.find(part) != std::string::npos;
}

static SceneModel completeScene() {
	SceneModel scene;
	scene.schemaVersion = 1;
	scene.name = "Validator scene";
	scene.baseTime = "08:00:00";
	scene.settings.hasDuration = true;
	scene.settings.durationSeconds = 3600.0;
	scene.tracks.push_back({"track-1"});
	scene.nodes.push_back({"node-1", "track-1", 0.0, 0.0});
	scene.nodes.push_back({"node-2", "track-1", 1.0, 0.0});
	scene.nodes.push_back({"node-3", "track-1", 2.0, 0.0});
	scene.arcs.push_back({"arc-1", "track-1", "node-1", "node-2", 0.0, 0.0, 40.0});
	scene.arcs.push_back({"arc-2", "track-1", "node-2", "node-3", 1000.0, -1.0, 35.0});
	scene.blocks.push_back({"block-1", "track-1", 1.0});
	scene.blocks.push_back({"block-2", "track-1", 1.0});
	scene.connections.push_back({"connection-1", "node-1", "node-2", false, 0.0});

	SceneStation origin;
	origin.id = "station-1";
	origin.name = "Origin";
	origin.platforms.push_back({"platform-1", {"node-1"}});
	scene.stations.push_back(origin);
	SceneStation destination;
	destination.id = "station-2";
	destination.name = "Destination";
	destination.platforms.push_back({"platform-2", {"node-3"}});
	scene.stations.push_back(destination);

	scene.signals.push_back({"signal-1", "block-1"});
	scene.routes.push_back({"route-1", {"block-1", "block-2"}, false, "", false});
	scene.signallingAreas.push_back({"area-1", 0.0, 2.0, 0, {}});

	SceneTrainUnit unit;
	unit.id = "unit-1";
	unit.hasPhysical = true;
	unit.physical.max_speed_ms = 40.0;
	unit.tractionCurve.push_back({{0.0, 40.0, 100000.0, 0.0, 0.0}});
	scene.trainUnits.push_back(unit);
	scene.compositions.push_back({"composition-1", {"unit-1"}});

	SceneService service;
	service.id = "service-1";
	service.composition = "composition-1";
	service.route = "route-1";
	SceneStop first;
	first.stationId = "station-1";
	first.platformId = "platform-1";
	first.hasPlannedDeparture = true;
	first.plannedDepartureSeconds = 100.0;
	SceneStop last;
	last.stationId = "station-2";
	last.platformId = "platform-2";
	last.hasPlannedArrival = true;
	last.plannedArrivalSeconds = 200.0;
	service.stops = {first, last};
	scene.services.push_back(service);

	SceneScenario scenario;
	scenario.id = "baseline";
	scenario.name = "Baseline";
	scenario.incidents.push_back({"incident-1", "signal_failure", "signal-1", 300.0, 600.0});
	scenario.entranceDelays.push_back({"service-1", 1, "station-1", 10.0});
	scene.scenarios.push_back(scenario);
	scene.defaultScenarioId = "baseline";

	ScenePassenger passenger;
	passenger.id = "passenger-1";
	ScenePassengerJourney journey;
	journey.id = "journey-1";
	journey.originStationId = "station-1";
	journey.destinationStationId = "station-2";
	journey.plannedDepartureStartSeconds = 0.0;
	journey.plannedDepartureEndSeconds = 120.0;
	journey.plannedArrivalStartSeconds = 180.0;
	journey.plannedArrivalEndSeconds = 300.0;
	journey.legs.push_back({"leg-1", "station-1", "station-2", "service-1", 1});
	passenger.journeys.push_back(journey);
	scene.passengers.push_back(passenger);
	return scene;
}

// Rolling stock of the Lebanon scene (lebanon-teaching-unit). The limits are
// 1.09 * 0.75 / 9.81 = 0.08333 for braking and 209000 / (9.81 * 151000) = 0.1411
// for starting.
static SceneTrainUnit lebanonUnit() {
	SceneTrainUnit unit;
	unit.id = "unit-1";
	unit.hasPhysical = true;
	unit.physical.mass_of_traction_unit_kg = 151000.0;
	unit.physical.max_speed_ms = 36.111111111111;
	unit.physical.max_deceleration_ms2 = 0.75;
	unit.tractionCurve.push_back({{0.0, 8.611111111111, 209000.0, 0.0, 0.0}});
	unit.tractionCurve.push_back({{8.611111111111, 36.111111111111, 324607.2915, -17671.4923, 292.9005}});
	return unit;
}

// Rolling stock of the Paimpol scene (Draisy). Its larger deceleration gives
// a braking limit of 0.1643.
static SceneTrainUnit paimpolUnit() {
	SceneTrainUnit unit;
	unit.id = "unit-2";
	unit.hasPhysical = true;
	unit.physical.mass_of_traction_unit_kg = 19000.0;
	unit.physical.mass_of_a_wagon_kg = 19000.0;
	unit.physical.number_of_wagons = 1.0;
	unit.physical.max_speed_ms = 27.7778;
	unit.physical.max_deceleration_ms2 = 1.5;
	unit.tractionCurve.push_back({{0.0, 6.111, 50000.0, 0.0, 0.0}});
	unit.tractionCurve.push_back({{6.111, 16.6667, 100439.0, -10241.0, 318.41}});
	return unit;
}

// The first block holds arc-1 and the second block arc-2.
static SceneModel steepGradientScene(double firstGradient, double secondGradient) {
	SceneModel scene = completeScene();
	scene.trainUnits = {lebanonUnit()};
	scene.arcs[0].gradientPercent = firstGradient;
	scene.arcs[1].gradientPercent = secondGradient;
	return scene;
}

static int steepGradientWarningCount(const SceneModel& scene) {
	const auto diagnostics = validateScene(scene);
	return static_cast<int>(std::count_if(diagnostics.begin(), diagnostics.end(),
		[](const SceneDiagnostic& diagnostic) { return diagnostic.code == "scene.route.gradient.steep"; }));
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Usage: test_scenevalidator <fixture_dir>\n";
		return 1;
	}

	bool ok = true;
	const SceneModel clean = completeScene();
	ok &= expect(!hasCode(validateScene(clean), "scene.topology.tracks.none"),
		"semantic validation does not reject complete topology");
	ok &= expect(validateScene(clean).empty(), "complete scene passes semantic validation");
	ok &= expect(validateRunnableScene(clean).empty(), "complete scene passes runnable validation");
	const std::string colorCode = "scene.service.color.invalid";
	for (const char* value : {"", "#112233", "#abcdef", "#ABCDEF", "#aBc012"}) {
		SceneModel colored = clean;
		colored.services[0].visualizationColor = value;
		ok &= expect(!hasCode(validateScene(colored), colorCode) && validateScene(colored).empty(),
			"unset and valid service colours give no diagnostic");
	}
	for (const char* value : {"#fff", "#12345g", "red", "#1234567", " #112233", "#112233 "}) {
		SceneModel colored = clean;
		colored.services[0].visualizationColor = value;
		const auto diagnostics = validateScene(colored);
		const SceneDiagnostic* invalidColor = findCode(diagnostics, colorCode);
		ok &= expect(diagnostics.size() == 1 && invalidColor != nullptr
				&& invalidColor->severity == SceneSeverity::Warning && invalidColor->file == "services.json"
				&& invalidColor->itemType == "service" && invalidColor->itemId == "service-1"
				&& invalidColor->path == "services[service-1].visualization_color"
				&& invalidColor->relatedId == value && contains(invalidColor->message, "default train colour")
				&& contains(invalidColor->suggestedFix, "#3C8DD2"),
			"invalid service colour gives one warning naming the service and field");
		ok &= expect(!hasErrors(diagnostics) && validateRunnableScene(colored).size() == 1,
			"invalid service colour does not block a run");
	}
	int red = -1, green = -1, blue = -1;
	ok &= expect(sceneParseVisualizationColor("#3C8DD2", &red, &green, &blue)
			&& red == 0x3C && green == 0x8D && blue == 0xD2,
		"upper-case colour parses to its channels");
	red = green = blue = -1;
	ok &= expect(sceneParseVisualizationColor("#3c8dd2", &red, &green, &blue)
			&& red == 0x3C && green == 0x8D && blue == 0xD2,
		"lower-case colour parses to its channels");
	ok &= expect(sceneParseVisualizationColor("#000000", &red, &green, &blue) && red == 0 && green == 0 && blue == 0
			&& sceneParseVisualizationColor("#FFffFF", &red, &green, &blue) && red == 255 && green == 255
			&& blue == 255 && sceneParseVisualizationColor("#112233"),
		"colour bounds parse without outputs too");
	red = green = blue = -1;
	for (const char* value : {"", "#", "#fff", "#ffff", "#fffff", "#fffffff", "#AARRGGBB", "#80112233", "112233",
			 "#11223g", "#11 233", " #112233", "#112233 ", "#112233\n", "red", "#+12233", "#-12233", "#0x1233",
			 "#12345\xB2", "#1234\xC3\xA9"})
		ok &= expect(!sceneParseVisualizationColor(value, &red, &green, &blue) && red == -1 && green == -1
				&& blue == -1,
			"malformed colour text is rejected and leaves outputs unchanged");
	SceneModel lookup = clean;
	lookup.services[0].visualizationColor = "#3c8dd2";
	ok &= expect(sceneServiceVisualizationColor(lookup, "service-1") == "#3c8dd2"
			&& sceneServiceVisualizationColor(clean, "service-1").empty()
			&& sceneServiceVisualizationColor(lookup, "service-9").empty()
			&& sceneServiceVisualizationColor(lookup, "").empty(),
		"service colour lookup returns the stored text, or empty for none or an unknown id");
	SceneModel timetable = clean;
	timetable.services[0].stops[0].hasPlannedArrival = true;
	timetable.services[0].stops[0].plannedArrivalSeconds = 90.0;
	ok &= expect(!hasErrors(validateScene(timetable)), "implicit departure entry allows origin pre-departure dwell");
	timetable.services[0].hasEntryTime = true;
	timetable.services[0].entryTimeSeconds = 100.0;
	ok &= expect(hasCodeAndSeverity(validateScene(timetable), "scene.time.order", SceneSeverity::Error),
		"first arrival before explicit entry blocks Run");
	timetable.services[0].stops[0].plannedDepartureSeconds = 95.0;
	const auto beforeEntry = validateScene(timetable);
	ok &= expect(std::count_if(beforeEntry.begin(), beforeEntry.end(), [](const SceneDiagnostic& d) {
		return d.code == "scene.time.order";
	}) == 2,
		"each event before entry is diagnosed without regressing the ordering cursor");
	timetable.services[0].entryTimeSeconds = 0.0;
	timetable.services[0].stops[0].hasPlannedDeparture = false;
	timetable.services[0].stops[1].plannedArrivalSeconds = 80.0;
	ok &= expect(hasCodeAndSeverity(validateScene(timetable), "scene.time.order", SceneSeverity::Error),
		"arrival-only row constrains the next event");
	timetable = clean;
	timetable.services[0].stops[0].hasPlannedArrival = true;
	timetable.services[0].stops[0].plannedArrivalSeconds = 90.0;
	timetable.services[0].stops[0].dwellSeconds = 20.0;
	ok &= expect(hasCodeAndSeverity(validateScene(timetable), "scene.dwell.exceeds_window", SceneSeverity::Warning)
			&& !hasErrors(validateScene(timetable)),
		"short dwell window remains advisory for historical schedules");
	for (const double invalid : {-1.0, std::numeric_limits<double>::infinity(),
			 std::numeric_limits<double>::quiet_NaN()}) {
		timetable = clean;
		timetable.services[0].hasEntryTime = true;
		timetable.services[0].entryTimeSeconds = invalid;
		timetable.services[0].stops[0].plannedDepartureSeconds = invalid;
		timetable.services[0].stops[0].dwellSeconds = invalid;
		const auto errors = validateScene(timetable);
		ok &= expect(hasCode(errors, "scene.time.entry.invalid") && hasCode(errors, "scene.time.invalid")
				&& hasCode(errors, "scene.dwell.invalid"),
			"entry, planned times and dwell reject invalid numbers");
	}
	timetable = clean;
	timetable.stations.push_back({"context", "Outside", true, 10.0, {}});
	timetable.services[0].stops.insert(timetable.services[0].stops.begin(),
		{"context", "", true, true, -20.0, -10.0, 0.0});
	timetable.services[0].hasEntryTime = true;
	timetable.services[0].entryTimeSeconds = 100.0;
	ok &= expect(!hasErrors(validateScene(timetable)), "negative off-route context does not constrain runnable chronology");
	timetable = clean;
	timetable.services[0].through = true;
	ok &= expect(validateScene(timetable).empty(), "nonempty stops override historical through flag");
	timetable.services[0].through = false;
	timetable.services[0].stops.clear();
	timetable.passengers.clear();
	timetable.scenarios[0].entranceDelays.clear();
	ok &= expect(validateRunnableScene(timetable).empty(), "empty stops need no through flag");
	const std::filesystem::path outputRoot = "scene-output-root";
	const std::vector<std::pair<std::string, std::string>> outputNameCases = {
		{"Readable Scene", "Readable Scene"},
		{"M\xC3\xBCnchen", "M\xC3\xBCnchen"},
		{".temp", ".temp"},
		{"COM10", "COM10"},
		{"LPT0", "LPT0"},
		{"auxiliary", "auxiliary"},
		{"", "scene"},
		{"../outside", "scene"},
		{"..\\outside", "scene"},
		{".", "scene"},
		{"..", "scene"},
		{"/absolute", "scene"},
		{"\\absolute", "scene"},
		{"//server/share", "scene"},
		{"\\\\server\\share", "scene"},
		{"C:", "scene"},
		{"C:\\outside", "scene"},
		{"CON", "scene"},
		{"cOn.txt", "scene"},
		{"PRN.log", "scene"},
		{"aUx.cfg", "scene"},
		{"NUL.json", "scene"},
		{"COM1", "scene"},
		{"com9.txt", "scene"},
		{"LPT1", "scene"},
		{"lpt9.csv", "scene"},
		{std::string("COM") + "\xC2\xB9.txt", "scene"},
		{std::string("lpt") + "\xC2\xB2", "scene"},
		{std::string("COM") + "\xB3", "scene"},
		{"bad<name", "scene"},
		{"bad>name", "scene"},
		{"bad:name", "scene"},
		{"bad\"name", "scene"},
		{"bad/name", "scene"},
		{"bad\\name", "scene"},
		{"bad|name", "scene"},
		{"bad?name", "scene"},
		{"bad*name", "scene"},
		{std::string("control") + '\x01', "scene"},
		{"trailing ", "scene"},
		{"trailing.", "scene"},
	};
	for (const auto& outputName : outputNameCases) {
		const std::string component = sceneOutputDirectoryComponent(outputName.first);
		const std::filesystem::path joined = outputRoot / component;
		ok &= expect(component == outputName.second && joined.lexically_normal().parent_path() == outputRoot,
			("scene output component is safe for " + outputName.first).c_str());
	}
	SceneModel unsafeSceneName = clean;
	unsafeSceneName.name = "../outside";
	ok &= expect(hasCodeAndPath(validateRunnableScene(unsafeSceneName), "scene.name.path", "name"),
		"runnable validation rejects unsafe scene output names");
	SceneModel reservedSceneName = clean;
	reservedSceneName.name = "CON.txt";
	ok &= expect(hasCodeAndPath(validateRunnableScene(reservedSceneName), "scene.name.path", "name"),
		"runnable validation rejects reserved scene output names");
	SceneModel stopLimit = clean;
	while (stopLimit.services[0].stops.size() < static_cast<std::size_t>(RuntimeLimits::kMaxTimetableStops))
		stopLimit.services[0].stops.push_back(stopLimit.services[0].stops.back());
	ok &= expect(!hasCode(validateRunnableScene(stopLimit), "scene.capacity.runtime"),
		"runnable validation accepts the exact timetable stop limit");
	SceneModel tooManyStops = stopLimit;
	tooManyStops.services[0].stops.push_back(tooManyStops.services[0].stops.back());
	ok &= expect(hasCodeAndPath(validateRunnableScene(tooManyStops), "scene.capacity.runtime",
					 "services[service-1].stops"),
		"runnable validation rejects one stop above the limit");
	SceneModel trainLimit = clean;
	trainLimit.services[0].hasRepeat = true;
	trainLimit.services[0].headwaySeconds = 1.0;
	trainLimit.services[0].hasRepeatCount = true;
	trainLimit.services[0].repeatCount = RuntimeLimits::kMaxExpandedTrains;
	ok &= expect(!hasCode(validateRunnableScene(trainLimit), "scene.capacity.runtime"),
		"runnable validation accepts the exact expanded train limit");
	SceneModel tooManyTrains = trainLimit;
	tooManyTrains.services[0].repeatCount = RuntimeLimits::kMaxExpandedTrains + 1;
	ok &= expect(hasCodeAndPath(validateRunnableScene(tooManyTrains), "scene.capacity.runtime", "services"),
		"runnable validation rejects one expanded train above the limit");
	SceneModel horizonTrainLimit = clean;
	horizonTrainLimit.services[0].hasRepeat = true;
	horizonTrainLimit.services[0].headwaySeconds = 1.0;
	ok &= expect(hasCode(validateRunnableScene(horizonTrainLimit), "scene.capacity.runtime"),
		"runnable validation rejects saved-horizon train expansion above the limit");
	ok &= expect(!hasCode(validateRunnableScene(horizonTrainLimit, {}, std::optional<double>(1.0)),
					 "scene.capacity.runtime"),
		"runnable validation uses an effective duration override for train capacity");
	const SceneRunSelection sparseSelection{{tooManyTrains.services[0].id,
		RuntimeLimits::kMaxExpandedTrains + 1}};
	ok &= expect(!hasCode(validateRunnableScene(tooManyTrains, sparseSelection), "scene.capacity.runtime"),
		"runnable validation applies train capacity to a sparse selected run");
	const SceneRunSelection invalidSelections{{tooManyTrains.services[0].id,
												  RuntimeLimits::kMaxExpandedTrains + 2},
		{"service-missing", 1}};
	ok &= expect(!hasCode(validateRunnableScene(tooManyTrains, invalidSelections), "scene.capacity.runtime"),
		"invalid selected rows do not fall back to all-scene train capacity");
	SceneModel invalidPlatformLength = clean;
	invalidPlatformLength.stations[0].platforms[0].hasLength = true;
	invalidPlatformLength.stations[0].platforms[0].lengthM = std::numeric_limits<double>::infinity();
	ok &= expect(hasCodeAndPath(validateScene(invalidPlatformLength), "scene.platform.length.invalid",
					 "stations[0].platforms[0].length_m"),
		"explicit non-finite platform length is rejected at its field");
	SceneModel invalidPlatformWidth = clean;
	invalidPlatformWidth.stations[0].platforms[0].hasWidth = true;
	invalidPlatformWidth.stations[0].platforms[0].widthM = 0.0;
	ok &= expect(hasCodeAndPath(validateScene(invalidPlatformWidth), "scene.platform.width.invalid",
					 "stations[0].platforms[0].width_m"),
		"explicit non-positive platform width is rejected at its field");
	SceneModel invalidPlatformCapacity = clean;
	invalidPlatformCapacity.stations[0].platforms[0].hasLength = true;
	invalidPlatformCapacity.stations[0].platforms[0].lengthM = 0.01;
	invalidPlatformCapacity.stations[0].platforms[0].hasWidth = true;
	invalidPlatformCapacity.stations[0].platforms[0].widthM = 0.01;
	ok &= expect(hasCodeAndPath(validateScene(invalidPlatformCapacity), "scene.platform.capacity.invalid",
					 "stations[0].platforms[0]"),
		"platform geometry must produce a usable integer capacity");
	SceneModel nonFinitePassengerWindow = clean;
	nonFinitePassengerWindow.passengers[0].journeys[0].plannedArrivalEndSeconds =
		std::numeric_limits<double>::quiet_NaN();
	ok &= expect(hasCodeAndPath(validateScene(nonFinitePassengerWindow), "scene.passenger.window",
					 "passengers[0].journeys[0].planned_arrival"),
		"non-finite passenger windows are rejected");
	SceneModel passengerMissingStop = clean;
	passengerMissingStop.services[0].stops.pop_back();
	const auto passengerStopDiagnostics = validateScene(passengerMissingStop);
	ok &= expect(hasCodeAndPath(passengerStopDiagnostics, "scene.passenger.leg.stop",
					 "passengers[0].journeys[0].legs[0].destination"),
		"passenger leg destination must be a stop of its service");
	SceneModel reversePassengerLeg = clean;
	reversePassengerLeg.passengers[0].journeys[0].originStationId = "station-2";
	reversePassengerLeg.passengers[0].journeys[0].destinationStationId = "station-1";
	reversePassengerLeg.passengers[0].journeys[0].legs[0].originStationId = "station-2";
	reversePassengerLeg.passengers[0].journeys[0].legs[0].destinationStationId = "station-1";
	ok &= expect(hasCodeAndPath(validateScene(reversePassengerLeg), "scene.passenger.leg.order",
					 "passengers[0].journeys[0].legs[0].destination"),
		"passenger leg rejects a reverse ordered service pair");
	SceneModel legacyReversePassengerLeg = reversePassengerLeg;
	legacyReversePassengerLeg.importReport.push_back({"legacy_root", ""});
	const auto legacyReverseDiagnostics = validateScene(legacyReversePassengerLeg);
	ok &= expect(hasCodeAndSeverity(legacyReverseDiagnostics, "scene.passenger.leg.order", SceneSeverity::Warning)
			&& !hasCodeAndPath(legacyReverseDiagnostics, "scene.passenger.leg.order",
				"passengers[0].journeys[0].legs[0].destination"),
		"legacy reverse passenger legs remain loadable with an actionable warning");
	SceneModel repeatedPassengerStops = clean;
	repeatedPassengerStops.services[0].stops = {
		{"station-2", "platform-2", true, true, 100.0, 110.0, 0.0},
		{"station-1", "platform-1", true, true, 120.0, 130.0, 0.0},
		{"station-2", "platform-2", true, true, 140.0, 150.0, 0.0}};
	SceneServiceStopPair repeatedPair;
	ok &= expect(resolveScenePassengerLegStops(repeatedPassengerStops.services[0],
					 repeatedPassengerStops.passengers[0].journeys[0].legs[0], repeatedPair)
			&& repeatedPair.originIndex == 1 && repeatedPair.destinationIndex == 2
			&& !hasCode(validateScene(repeatedPassengerStops), "scene.passenger.leg.order"),
		"repeated service stations resolve to an ordered stop pair");
	const SceneSectionInventory inventory = buildSceneSectionInventory(clean);
	ok &= expect(inventory.sections.size() == 4
			&& inventory.sections[0].id == "@block-1@"
			&& inventory.sections[1].id == "@block-2@"
			&& inventory.sections[2].id == "@block-1@-0.000000/@block-1@-1.000000"
			&& inventory.sections[3].id == "@block-1@-0.000000/@block-2@-1.000000",
		"section inventory derives canonical base and connection section IDs");
	ok &= expect(inventory.resolve("block-1") != nullptr
			&& inventory.resolve("block-1")->id == "@block-1@"
			&& inventory.resolve("@block-1@-0.000000/@block-2@-1.000000") != nullptr
			&& inventory.resolve("@block-1@-10/@block-2@-20") == nullptr,
		"section resolver accepts base aliases but rejects unknown compound tokens");
	SceneModel duplicateSection = clean;
	duplicateSection.blocks.push_back({"block-1", "track-1", 0.5});
	const SceneSectionInventory duplicateInventory = buildSceneSectionInventory(duplicateSection);
	ok &= expect(duplicateInventory.ambiguous("block-1")
			&& duplicateInventory.resolve("block-1") == nullptr,
		"duplicate base IDs are not resolved ambiguously");
	SceneModel disconnectedRoute = clean;
	disconnectedRoute.routes[0].blocks = {"block-1", "block-1"};
	ok &= expect(hasCode(validateScene(disconnectedRoute), "scene.route.disconnected"),
		"authored disconnected route order is rejected");
	SceneModel reverseRoute = clean;
	reverseRoute.routes[0].blocks = {"block-2", "block-1"};
	ok &= expect(hasCode(validateScene(reverseRoute), "scene.ref.stop.order"),
		"stop order follows the route direction rather than unordered membership");
	std::reverse(reverseRoute.services[0].stops.begin(), reverseRoute.services[0].stops.end());
	const auto reverseTraversal = buildSceneRouteTraversal(reverseRoute, reverseRoute.routes[0]);
	ok &= expect(reverseTraversal.resolved && reverseTraversal.direction == -1
			&& reverseTraversal.visits.front().stationId == "station-2"
			&& !hasCode(validateScene(reverseRoute), "scene.ref.stop.order"),
		"reversed stops resolve on a reversed route");
	ok &= expect(!hasCode(validateScene(reverseRoute), "scene.route.disconnected"),
		"coherent reverse route order remains valid");
	SceneModel directionChange = clean;
	directionChange.routes[0].blocks = {"block-1", "block-2", "block-1"};
	ok &= expect(hasCode(validateScene(directionChange), "scene.route.direction"),
		"a route cannot change direction between connected sections");
	SceneModel switchTopology;
	switchTopology.tracks = {{"switch-a"}, {"switch-b"}, {"switch-c"}};
	switchTopology.nodes = {{"a.0", "switch-a", 0.0, 0.0}, {"a.1", "switch-a", 1.0, 0.0},
		{"b.0", "switch-b", 2.0, 0.0}, {"b.1", "switch-b", 4.0, 0.0},
		{"c.0", "switch-c", 5.0, 0.0}, {"c.1", "switch-c", 6.0, 0.0}};
	switchTopology.arcs = {{"a.arc", "switch-a", "a.0", "a.1", 0.0, 0.0, 20.0},
		{"b.arc", "switch-b", "b.0", "b.1", 0.0, 0.0, 20.0},
		{"c.arc", "switch-c", "c.0", "c.1", 0.0, 0.0, 20.0}};
	switchTopology.blocks = {{"a.block", "switch-a", 1.0}, {"b.block", "switch-b", 2.0},
		{"c.block", "switch-c", 1.0}};
	switchTopology.connections = {{"a-to-b", "a.1", "b.0", false, 0.0},
		{"b-to-c", "b.1", "c.0", false, 0.0}, {"a-to-c", "a.1", "c.0", false, 0.0}};
	const SceneSectionInventory switchInventory = buildSceneSectionInventory(switchTopology);
	std::string aToB;
	std::string bToC;
	std::string aToC;
	for (const auto& section : switchInventory.sections) {
		if (section.sourceConnectionId == "a-to-b")
			aToB = section.id;
		else if (section.sourceConnectionId == "b-to-c")
			bToC = section.id;
		else if (section.sourceConnectionId == "a-to-c")
			aToC = section.id;
	}
	SceneModel switchChain = switchTopology;
	switchChain.routes.push_back({"switch-route", {aToB, bToC}, false, "", false});
	const auto switchChainDiagnostics = validateScene(switchChain);
	switchChain.stations = {{"A", "A", false, 0.0, {{"A.p", {"a.0"}}}},
		{"B-entry", "B entry", false, 0.0, {{"B.in", {"b.0"}}}},
		{"B-exit", "B exit", false, 0.0, {{"B.out", {"b.1"}}}},
		{"C", "C", false, 0.0, {{"C.p", {"c.1"}}}}};
	const auto switchTraversal = buildSceneRouteTraversal(switchChain, switchChain.routes[0]);
	ok &= expect(switchTraversal.resolved && switchTraversal.visits.size() == 4
			&& switchTraversal.visits[1].nodeId == "b.0" && switchTraversal.visits[1].sectionIndex == 0
			&& switchTraversal.visits[2].nodeId == "b.1" && switchTraversal.visits[2].sectionIndex == 1,
		"overlapping connection sections retain only their clipped platform anchors");
	ok &= expect(sceneSectionsOverlap("@A@same", 0.0, 2.0, "@B@same", 1.0, 3.0)
			&& !sceneSectionsOverlap("@A@", 0.0, 1.0, "@A@", 1.0, 2.0),
		"legacy section token matching is shared and touching intervals do not overlap");
	ok &= expect(!aToB.empty() && !bToC.empty()
			&& !hasCode(switchChainDiagnostics, "scene.route.disconnected")
			&& !hasCode(switchChainDiagnostics, "scene.route.direction"),
		"connection-derived sections join through the shared exit and entry block");
	SceneModel forkedSwitchChain = switchTopology;
	forkedSwitchChain.routes.push_back({"forked-route", {aToB, aToC}, false, "", false});
	ok &= expect(!aToC.empty() && hasCode(validateScene(forkedSwitchChain), "scene.route.disconnected"),
		"overlapping switch sections on the wrong branch are rejected");
	SceneModel reversedFork = switchTopology;
	reversedFork.nodes[0].xKm = 100.0;
	reversedFork.nodes[1].xKm = 101.0;
	reversedFork.nodes[2].xKm = 0.0;
	reversedFork.nodes[3].xKm = 1.0;
	reversedFork.nodes[4].xKm = 2.0;
	reversedFork.nodes[5].xKm = 3.0;
	const SceneSectionInventory reversedInventory = buildSceneSectionInventory(reversedFork);
	std::string bToA;
	std::string cToA;
	for (const auto& section : reversedInventory.sections) {
		if (section.sourceConnectionId == "a-to-b")
			bToA = section.id;
		else if (section.sourceConnectionId == "a-to-c")
			cToA = section.id;
	}
	reversedFork.importReport.push_back({"legacy_root", ""});
	reversedFork.routes.push_back({"reversed-fork", {bToA, cToA}, false, "", false});
	ok &= expect(!bToA.empty() && !cToA.empty()
			&& hasCode(validateScene(reversedFork), "scene.route.disconnected"),
		"legacy compatibility cannot turn a wrong-branch switch fork into a regional jump");
	SceneModel switchDirectionChange = switchTopology;
	switchDirectionChange.importReport.push_back({"legacy_root", ""});
	switchDirectionChange.routes.push_back(
		{"switch-u-turn", {aToB, bToC, aToB}, false, "", false});
	ok &= expect(hasCode(validateScene(switchDirectionChange), "scene.route.direction"),
		"legacy provenance cannot hide a connection-derived route reversal");
	SceneModel regionalSwitchDirectionChange = switchTopology;
	regionalSwitchDirectionChange.blocks = {{"a.block", "switch-a", 1.0},
		{"b.left", "switch-b", 1.0}, {"b.right", "switch-b", 1.0},
		{"c.block", "switch-c", 1.0}};
	std::string regionalAToB;
	std::string regionalBToC;
	for (const auto& section : buildSceneSectionInventory(regionalSwitchDirectionChange).sections) {
		if (section.sourceConnectionId == "a-to-b")
			regionalAToB = section.id;
		else if (section.sourceConnectionId == "b-to-c")
			regionalBToC = section.id;
	}
	regionalSwitchDirectionChange.importReport.push_back({"legacy_root", ""});
	regionalSwitchDirectionChange.routes.push_back(
		{"regional-switch-u-turn", {regionalAToB, regionalBToC, regionalAToB}, false, "", false});
	ok &= expect(!regionalAToB.empty() && !regionalBToC.empty()
			&& hasCode(validateScene(regionalSwitchDirectionChange), "scene.route.direction"),
		"legacy regional bridges cannot suppress both directions of a derived U-turn");
	SceneModel mixedRegionalDirectionChange = regionalSwitchDirectionChange;
	mixedRegionalDirectionChange.routes = {{"mixed-regional-switch-u-turn",
		{"b.left", regionalBToC, regionalAToB}, false, "", false}};
	ok &= expect(hasCode(validateScene(mixedRegionalDirectionChange), "scene.route.direction"),
		"an ordinary transition cannot hide an opposing legacy derived-section direction");
	SceneModel regionJump = clean;
	regionJump.tracks.push_back({"region-track"});
	regionJump.nodes.push_back({"region-node-1", "region-track", 100.0, 0.0});
	regionJump.nodes.push_back({"region-node-2", "region-track", 101.0, 0.0});
	regionJump.arcs.push_back({"region-arc", "region-track", "region-node-1", "region-node-2",
		0.0, 0.0, 20.0});
	regionJump.blocks.push_back({"region-block", "region-track", 1.0});
	regionJump.routes.push_back({"region-route", {"block-2", "region-block"}, false, "", false});
	regionJump.importReport.push_back({"legacy_root", ""});
	const auto regionJumpDiagnostics = validateScene(regionJump);
	ok &= expect(hasCode(regionJumpDiagnostics, "scene.route.region_jump")
			&& !hasCode(regionJumpDiagnostics, "scene.route.disconnected"),
		"legacy cross-region coordinate discontinuities remain visible and compatible");
	SceneModel segmentedRegionJump = regionJump;
	segmentedRegionJump.blocks.back().lengthKm = 0.5;
	segmentedRegionJump.blocks.push_back({"region-block-2", "region-track", 0.5});
	segmentedRegionJump.routes = {{"segmented-region-route",
		{"block-1", "block-2", "region-block-2", "region-block"}, false, "", false}};
	const auto segmentedRegionDiagnostics = validateScene(segmentedRegionJump);
	ok &= expect(hasCode(segmentedRegionDiagnostics, "scene.route.region_jump")
			&& !hasCode(segmentedRegionDiagnostics, "scene.route.direction"),
		"legacy route direction is checked independently on each connected regional segment");
	regionJump.importReport.clear();
	ok &= expect(hasCode(validateScene(regionJump), "scene.route.disconnected"),
		"new canonical scenes reject undeclared cross-region route jumps");
	SceneModel unboundSignal = clean;
	unboundSignal.signals[0].protectedSection.clear();
	ok &= expect(hasCode(validateScene(unboundSignal), "scene.signal.binding.missing"),
		"unbound signals produce an actionable binding diagnostic");
	SceneModel invalidSignalBinding = clean;
	invalidSignalBinding.signals[0].protectedSection = "@block-1@-10/@block-2@-20";
	ok &= expect(hasCode(validateScene(invalidSignalBinding), "scene.signal.binding.unresolved"),
		"malformed signal section bindings are rejected");
	SceneModel directBlockIncident = clean;
	directBlockIncident.scenarios[0].incidents[0].target = "block-2";
	ok &= expect(validateScene(directBlockIncident).empty(),
		"direct base-block signal-failure targets remain compatible");
	SceneModel ambiguousSignalTarget = clean;
	ambiguousSignalTarget.signals[0].id = "block-1";
	ambiguousSignalTarget.scenarios[0].incidents[0].target = "block-1";
	ok &= expect(hasCode(validateScene(ambiguousSignalTarget), "scene.ref.ambiguous"),
		"signal failures reject an ID that identifies both a signal and section");
	SceneModel validAreas = clean;
	validAreas.signallingAreas = {
		{"network-area", 0.0, 2.0, 2, {}},
		{"track-area", 0.25, 1.75, 4, "track-1"},
	};
	ok &= expect(validateScene(validAreas).empty(),
		"network-wide and track-scoped signalling areas validate");
	SceneModel duplicateArea = clean;
	duplicateArea.signallingAreas = {{"area", 0.0, 1.0, 2, {}}, {"area", 1.0, 2.0, 2, {}}};
	ok &= expect(hasCode(validateScene(duplicateArea), "scene.id.duplicate"),
		"signalling area IDs must be unique");
	SceneModel invalidAreaRange = clean;
	invalidAreaRange.signallingAreas = {{"area", 1.0, 1.0, 2, {}}};
	ok &= expect(hasCode(validateScene(invalidAreaRange), "scene.signalling_area.range"),
		"signalling area ranges must increase");
	auto rangeErrors = findAll(validateScene(invalidAreaRange), "scene.signalling_area.range");
	const std::string rangeMessage = "Signalling area area has start_km 1.000000 and end_km 1.000000; "
									 "start_km must be finite and below end_km (blocks of the network cover 0.000000 to 2.000000 km)";
	ok &= expect(rangeErrors.size() == 1 && rangeErrors[0].message == rangeMessage,
		"the range error names the area, both values and the extent of the network");
	SceneModel trackWithoutBlocks = clean;
	trackWithoutBlocks.tracks.push_back({"track-2"});
	trackWithoutBlocks.signallingAreas = {{"other", 1.0, 0.5, 2, "track-2"}};
	rangeErrors = findAll(validateScene(trackWithoutBlocks), "scene.signalling_area.range");
	ok &= expect(rangeErrors.size() == 1 && contains(rangeErrors[0].message, "(track track-2 has no blocks)"),
		"the range error of an area on a track without blocks says so");
	SceneModel invalidTrackRange = clean;
	invalidTrackRange.signallingAreas = {{"scoped", 2.5, 1.0, 2, "track-1"}};
	rangeErrors = findAll(validateScene(invalidTrackRange), "scene.signalling_area.range");
	ok &= expect(rangeErrors.size() == 1 && contains(rangeErrors[0].message, "start_km 2.500000 and end_km 1.000000")
			&& contains(rangeErrors[0].message, "(blocks of track track-1 cover 0.000000 to 2.000000 km)"),
		"the range error of a track-scoped area gives the extent of that track");
	invalidAreaRange.signallingAreas[0].startKm = std::numeric_limits<double>::quiet_NaN();
	ok &= expect(hasCode(validateScene(invalidAreaRange), "scene.signalling_area.range"),
		"signalling area coordinates must be finite");
	SceneModel invalidAreaLevel = clean;
	invalidAreaLevel.signallingAreas = {{"area", 0.0, 1.0, 6, {}}};
	ok &= expect(hasCode(validateScene(invalidAreaLevel), "scene.signalling_area.level"),
		"signalling area levels must be between zero and five");
	const auto levelErrors = findAll(validateScene(invalidAreaLevel), "scene.signalling_area.level");
	ok &= expect(levelErrors.size() == 1 && levelErrors[0].message == "Signalling area area has level 6; the level must be between 0 and 5",
		"the level error names the area and the offending level");
	SceneModel unknownAreaTrack = clean;
	unknownAreaTrack.signallingAreas = {{"area", 0.0, 1.0, 2, "missing-track"}};
	ok &= expect(hasCodeAndPath(validateScene(unknownAreaTrack), "scene.ref.unresolved",
					 "signalling_areas[0].track"),
		"signalling area track references must resolve");
	const auto unknownTrackDiagnostics = validateScene(unknownAreaTrack);
	const auto unknownTrackErrors = findAll(unknownTrackDiagnostics, "scene.ref.unresolved");
	const std::string unknownTrackMessage = "Signalling area area refers to unknown track missing-track "
											"(tracks of the network: track-1; blocks of the network cover 0.000000 to 2.000000 km)";
	ok &= expect(unknownTrackErrors.size() == 1 && unknownTrackErrors[0].message == unknownTrackMessage,
		"the unknown track error names the area, the track, the known tracks and the extent of the network");
	SceneModel overlapWithoutSharedSection = clean;
	overlapWithoutSharedSection.signallingAreas = {
		{"first", 0.0, 1.5, 2, {}}, {"second", 1.0, 2.0, 3, {}}};
	ok &= expect(!hasCode(validateRunnableScene(overlapWithoutSharedSection),
					 "scene.signalling_area.conflict"),
		"coordinate overlap is allowed when no complete section receives both levels");
	SceneModel conflictingNetworkAreas = clean;
	conflictingNetworkAreas.signallingAreas = {
		{"first", 0.0, 2.0, 2, {}}, {"second", 0.0, 2.0, 3, {}}};
	ok &= expect(hasCode(validateRunnableScene(conflictingNetworkAreas),
					 "scene.signalling_area.conflict"),
		"network-wide areas cannot assign different levels to one runtime section");
	SceneModel conflictingTrackAreas = clean;
	conflictingTrackAreas.signallingAreas = {
		{"first", 0.0, 2.0, 2, "track-1"}, {"second", 0.0, 2.0, 3, "track-1"}};
	ok &= expect(hasCode(validateRunnableScene(conflictingTrackAreas),
					 "scene.signalling_area.conflict"),
		"same-track areas cannot assign different levels to one runtime section");
	SceneModel conflictingDerivedAreas = clean;
	conflictingDerivedAreas.tracks.push_back({"track-2"});
	conflictingDerivedAreas.nodes.push_back({"node-4", "track-2", 3.0, 0.0});
	conflictingDerivedAreas.nodes.push_back({"node-5", "track-2", 4.0, 0.0});
	conflictingDerivedAreas.arcs.push_back(
		{"arc-3", "track-2", "node-4", "node-5", 0.0, 0.0, 35.0});
	conflictingDerivedAreas.blocks.push_back({"block-3", "track-2", 1.0});
	conflictingDerivedAreas.connections = {
		{"connection-1", "node-3", "node-4", false, 0.0}};
	conflictingDerivedAreas.signallingAreas = {
		{"first", 0.0, 4.0, 2, "track-1"}, {"second", 0.0, 4.0, 3, "track-2"}};
	ok &= expect(hasCode(validateRunnableScene(conflictingDerivedAreas),
					 "scene.signalling_area.conflict"),
		"different track scopes cannot conflict on one derived switch section");

	const std::string levelMissing = "scene.signalling.level.missing";
	SceneModel noAreas = clean;
	noAreas.signallingAreas.clear();
	const auto noAreaDiagnostics = validateRunnableScene(noAreas);
	const SceneDiagnostic* noAreaWarning = findCode(noAreaDiagnostics, levelMissing);
	ok &= expect(noAreaWarning != nullptr && noAreaWarning->severity == SceneSeverity::Warning
			&& noAreaWarning->file == "signalling.json" && noAreaWarning->path == "signalling_areas"
			&& noAreaWarning->message.rfind("No signalling area is defined. 2 of 2 route sections", 0) == 0
			&& contains(noAreaWarning->message, "(track track-1)")
			&& contains(noAreaWarning->suggestedFix, "0.000000 to 2.000000 km"),
		"route sections without any signalling area produce one warning");
	ok &= expect(!hasErrors(noAreaDiagnostics) && !hasCode(validateScene(noAreas), levelMissing),
		"missing signalling level is a runnable-only warning");
	ok &= expect(!hasCode(validateRunnableScene(clean), levelMissing),
		"a network-wide area covering all route sections removes the warning");

	SceneModel partialArea = clean;
	partialArea.signallingAreas = {{"partial", 0.0, 1.5, 2, {}}};
	const auto partialDiagnostics = validateRunnableScene(partialArea);
	const SceneDiagnostic* partialWarning = findCode(partialDiagnostics, levelMissing);
	ok &= expect(partialWarning != nullptr && contains(partialWarning->message, "1 of 2 route sections has no signalling level and runs")
			&& contains(partialWarning->message, "block-2") && !contains(partialWarning->message, "block-1")
			&& !contains(partialWarning->message, "No signalling area")
			&& contains(partialWarning->suggestedFix, "1.000000 to 2.000000 km"),
		"a partial area names only the route sections it does not contain");

	SceneModel scopedArea = conflictingDerivedAreas;
	const SceneSectionInventory scopedInventory = buildSceneSectionInventory(scopedArea);
	std::string derivedSectionId;
	for (const auto& section : scopedInventory.sections) {
		if (section.connectionDerived)
			derivedSectionId = section.id;
	}
	scopedArea.routes[0].blocks = {"block-1", "block-2", derivedSectionId, "block-3"};
	scopedArea.signallingAreas = {{"scoped", 0.0, 10.0, 2, "track-1"}};
	const auto scopedDiagnostics = validateRunnableScene(scopedArea);
	const SceneDiagnostic* scopedWarning = findCode(scopedDiagnostics, levelMissing);
	ok &= expect(!derivedSectionId.empty() && scopedWarning != nullptr
			&& contains(scopedWarning->message, "1 of 4 route sections")
			&& contains(scopedWarning->message, "block-3") && contains(scopedWarning->message, "(track track-2)")
			&& !contains(scopedWarning->message, derivedSectionId),
		"a track-scoped area covers its tracks and the derived section that touches them");

	SceneModel unusedSection = clean;
	unusedSection.routes[0].blocks = {"block-1"};
	unusedSection.signallingAreas.clear();
	const auto unusedDiagnostics = validateRunnableScene(unusedSection);
	const SceneDiagnostic* unusedWarning = findCode(unusedDiagnostics, levelMissing);
	ok &= expect(unusedWarning != nullptr && contains(unusedWarning->message, "1 of 1 route sections")
			&& !contains(unusedWarning->message, "block-2"),
		"sections on no route are not reported");
	SceneModel noRoutes = clean;
	noRoutes.routes.clear();
	noRoutes.signallingAreas.clear();
	ok &= expect(!hasCode(validateRunnableScene(noRoutes), levelMissing),
		"a scene without routes has no route sections to report");

	// The warning does not depend on the direction of the route and follows the builder at the area edges.
	auto levelMissingMessage = [&](const SceneModel& candidate) {
		const SceneDiagnostic* warning = nullptr;
		const auto candidateDiagnostics = validateRunnableScene(candidate);
		warning = findCode(candidateDiagnostics, levelMissing);
		return warning == nullptr ? std::string() : warning->message;
	};
	SceneModel reversedLevels = clean;
	reversedLevels.routes[0].blocks = {"block-2", "block-1"};
	ok &= expect(buildSceneRouteTraversal(reversedLevels, reversedLevels.routes[0]).direction == -1,
		"the test route runs in decreasing chainage");
	reversedLevels.signallingAreas.clear();
	ok &= expect(contains(levelMissingMessage(reversedLevels), "2 of 2 route sections have no signalling level")
			&& contains(levelMissingMessage(reversedLevels), "@block-2@, @block-1@"),
		"a reversed route without areas names its sections in route order");
	reversedLevels.signallingAreas = {{"partial", 0.0, 1.5, 2, {}}};
	const std::string reversedPartial = levelMissingMessage(reversedLevels);
	ok &= expect(contains(reversedPartial, "1 of 2 route sections has no signalling level")
			&& contains(reversedPartial, "@block-2@") && !contains(reversedPartial, "@block-1@"),
		"a reversed route gets the same warning as the forward route");
	reversedLevels.signallingAreas = {{"all", 0.0, 2.0, 2, {}}};
	ok &= expect(levelMissingMessage(reversedLevels).empty(), "a reversed route covered by an area has no warning");

	SceneModel edges = clean;
	edges.signallingAreas = {{"first", 0.0, 1.0, 2, {}}};
	std::string edgeMessage = levelMissingMessage(edges);
	ok &= expect(contains(edgeMessage, "1 of 2 route sections") && contains(edgeMessage, "@block-2@")
			&& !contains(edgeMessage, "@block-1@"),
		"a section that ends exactly at the area end is covered");
	edges.signallingAreas = {{"first", 0.0, 1.0 - 5e-9, 2, {}}};
	ok &= expect(contains(levelMissingMessage(edges), "1 of 2 route sections"),
		"an area end just inside the tolerance still covers the section");
	edges.signallingAreas = {{"first", 0.0, 1.0 - 1e-7, 2, {}}};
	ok &= expect(contains(levelMissingMessage(edges), "2 of 2 route sections"),
		"an area end outside the tolerance does not cover the section");
	edges.signallingAreas = {{"second", 1.0 + 5e-9, 2.0, 2, {}}};
	edgeMessage = levelMissingMessage(edges);
	ok &= expect(contains(edgeMessage, "1 of 2 route sections") && contains(edgeMessage, "@block-1@")
			&& !contains(edgeMessage, "@block-2@"),
		"an area start just inside the tolerance still covers the section");
	edges.signallingAreas = {{"second", 1.0 + 1e-7, 2.0, 2, {}}};
	ok &= expect(contains(levelMissingMessage(edges), "2 of 2 route sections"),
		"an area start outside the tolerance does not cover the section");
	edges.signallingAreas = {{"first", 0.0, 1.0, 2, {}}, {"second", 1.0, 2.0, 3, {}}};
	ok &= expect(levelMissingMessage(edges).empty() && !hasCode(validateRunnableScene(edges), "scene.signalling_area.conflict"),
		"adjacent areas that meet on a section edge cover both sections without a conflict");
	edges.signallingAreas = {{"first", 0.0, 1.5, 2, {}}, {"second", 1.5, 2.0, 3, {}}};
	edgeMessage = levelMissingMessage(edges);
	ok &= expect(contains(edgeMessage, "1 of 2 route sections") && contains(edgeMessage, "@block-2@"),
		"a section that crosses the edge between two areas is reported");
	edges.signallingAreas.push_back({"bridge", 0.5, 2.0, 3, {}});
	ok &= expect(levelMissingMessage(edges).empty(),
		"a third area that contains the straddling section removes the warning");

	// Conflicts are reported once for each pair of areas, with the number of sections and where they lie.
	const std::string conflictCode = "scene.signalling_area.conflict";
	const auto pairDiagnostics = findAll(validateRunnableScene(conflictingNetworkAreas), conflictCode);
	ok &= expect(pairDiagnostics.size() == 1 && pairDiagnostics[0].itemId == "second"
			&& pairDiagnostics[0].relatedId == "first" && pairDiagnostics[0].path == "signalling_areas[1]"
			&& pairDiagnostics[0].message == "Signalling area first (2 ETCS Level 2 fixed block) and signalling area "
											 "second (3 ETCS Level 3 moving block) assign different levels to 4 runtime sections, the first being "
											 "@block-1@, between 0.000000 and 2.000000 km",
		"two areas that disagree about two sections give one conflict that names both areas, levels and the sections");
	SceneModel singleConflict = clean;
	singleConflict.signallingAreas = {{"wide", 0.0, 2.0, 0, {}}, {"narrow", 1.0, 2.0, 5, {}}};
	const auto singleConflictDiagnostics = findAll(validateRunnableScene(singleConflict), conflictCode);
	const std::string singleConflictMessage = "Signalling area wide (0 ATB fixed block) and signalling area narrow "
											  "(5 BACC track circuits) assign different levels to 1 runtime section: "
											  "@block-2@, between 1.000000 and 2.000000 km";
	ok &= expect(singleConflictDiagnostics.size() == 1 && singleConflictDiagnostics[0].message == singleConflictMessage,
		"a conflict about one section names that section and its range");
	ok &= expect(findAll(validateRunnableScene(clean), conflictCode).empty()
			&& findAll(validateRunnableScene(overlapWithoutSharedSection), conflictCode).empty(),
		"areas that agree or share no complete section give no conflict");
	SceneModel sameLevelOverlap = clean;
	sameLevelOverlap.signallingAreas = {{"a", 0.0, 2.0, 2, {}}, {"b", 0.0, 2.0, 2, "track-1"}, {"c", 1.0, 2.0, 2, {}}};
	ok &= expect(findAll(validateRunnableScene(sameLevelOverlap), conflictCode).empty(),
		"overlapping areas with the same level give no conflict");

	// An area edge inside a route section leaves that section out of the area.
	const std::string splitCode = "scene.signalling_area.splits_section";
	SceneModel splitEnd = clean;
	splitEnd.signallingAreas = {{"head", 0.0, 1.5, 2, {}}};
	const auto splitEndDiagnostics = validateRunnableScene(splitEnd);
	const auto splitEndWarnings = findAll(splitEndDiagnostics, splitCode);
	ok &= expect(splitEndWarnings.size() == 1 && splitEndWarnings[0].severity == SceneSeverity::Warning
			&& splitEndWarnings[0].file == "signalling.json" && splitEndWarnings[0].itemId == "head"
			&& splitEndWarnings[0].path == "signalling_areas[0].end_km" && splitEndWarnings[0].relatedId == "@block-2@"
			&& splitEndWarnings[0].message == "The end of signalling area head (1.500000 km) lies inside route section "
											  "@block-2@ (1.000000 to 2.000000 km), so the section is not part of the area"
			&& contains(splitEndWarnings[0].suggestedFix, "1.000000 or 2.000000 km")
			&& !hasErrors(splitEndDiagnostics) && !hasCode(validateScene(splitEnd), splitCode),
		"an area end inside a route section gives one warning that names the section, the edge and the section range");
	SceneModel splitStart = clean;
	splitStart.signallingAreas = {{"tail", 0.5, 2.0, 2, {}}};
	const auto splitStartWarnings = findAll(validateRunnableScene(splitStart), splitCode);
	ok &= expect(splitStartWarnings.size() == 1 && splitStartWarnings[0].path == "signalling_areas[0].start_km"
			&& splitStartWarnings[0].message == "The start of signalling area tail (0.500000 km) lies inside route section "
												"@block-1@ (0.000000 to 1.000000 km), so the section is not part of the area",
		"an area start inside a route section gives one warning");
	SceneModel splitBoth = clean;
	splitBoth.signallingAreas = {{"inner", 0.5, 1.5, 2, {}}};
	ok &= expect(findAll(validateRunnableScene(splitBoth), splitCode).size() == 2,
		"each edge that cuts a section gives its own warning");
	SceneModel splitOffRoute = clean;
	splitOffRoute.routes[0].blocks = {"block-1"};
	splitOffRoute.signallingAreas = {{"head", 0.0, 1.5, 2, {}}};
	ok &= expect(findAll(validateRunnableScene(splitOffRoute), splitCode).empty(),
		"an edge inside a section that is on no route gives no warning");
	for (const SceneSignallingArea& area : {SceneSignallingArea{"exact", 0.0, 1.0, 2, {}},
			 SceneSignallingArea{"inside", 0.0, 1.0 - 5e-9, 2, {}}, SceneSignallingArea{"start", 1.0 + 5e-9, 2.0, 2, {}},
			 SceneSignallingArea{"all", 0.0, 2.0, 2, "track-1"}}) {
		SceneModel aligned = clean;
		aligned.signallingAreas = {area};
		ok &= expect(findAll(validateRunnableScene(aligned), splitCode).empty(),
			"an area edge on a section edge, within the tolerance, gives no warning");
	}
	SceneModel splitMany = clean;
	splitMany.tracks.push_back({"track-2"});
	splitMany.nodes.push_back({"node-4", "track-2", 0.0, 1.0});
	splitMany.nodes.push_back({"node-5", "track-2", 2.0, 1.0});
	splitMany.arcs.push_back({"arc-3", "track-2", "node-4", "node-5", 0.0, 0.0, 35.0});
	splitMany.blocks.push_back({"block-3", "track-2", 2.0});
	splitMany.routes[0].blocks = {"block-1", "block-2", "block-3"};
	splitMany.signallingAreas = {{"head", 0.0, 1.5, 2, {}}};
	const auto splitManyWarnings = findAll(validateRunnableScene(splitMany), splitCode);
	ok &= expect(splitManyWarnings.size() == 1 && contains(splitManyWarnings[0].message, "@block-2@ (1.000000 to 2.000000 km)")
			&& contains(splitManyWarnings[0].message, "; @block-3@ is cut the same way"),
		"an edge that cuts sections of several tracks gives one warning that names the others");

	// An area that contains no complete section.
	const std::string emptyCode = "scene.signalling_area.empty";
	SceneModel emptyArea = clean;
	emptyArea.signallingAreas = {{"far", 5.0, 6.0, 2, {}}, {"a", 0.0, 2.0, 2, {}}};
	const auto emptyDiagnostics = validateRunnableScene(emptyArea);
	const auto emptyWarnings = findAll(emptyDiagnostics, emptyCode);
	ok &= expect(emptyWarnings.size() == 1 && emptyWarnings[0].severity == SceneSeverity::Warning
			&& emptyWarnings[0].file == "signalling.json" && emptyWarnings[0].itemId == "far"
			&& emptyWarnings[0].path == "signalling_areas[0]"
			&& emptyWarnings[0].message == "Signalling area far (5.000000 to 6.000000 km) contains no complete section; "
										   "blocks of the network cover 0.000000 to 2.000000 km"
			&& !hasErrors(emptyDiagnostics) && !hasCode(validateScene(emptyArea), emptyCode),
		"an area outside the network gives one warning with the extent of the network");
	SceneModel insideSection = clean;
	insideSection.signallingAreas = {{"small", 0.2, 0.8, 2, {}}, {"a", 0.0, 2.0, 2, {}}};
	const auto insideWarnings = findAll(validateRunnableScene(insideSection), emptyCode);
	ok &= expect(insideWarnings.size() == 1 && insideWarnings[0].itemId == "small"
			&& findAll(validateRunnableScene(insideSection), splitCode).size() == 2,
		"an area inside one section contains no section and its edges cut that section");
	SceneModel trackExtent = clean;
	trackExtent.signallingAreas = {{"far", 5.0, 6.0, 2, "track-1"}, {"a", 0.0, 2.0, 2, {}}};
	const auto trackExtentWarnings = findAll(validateRunnableScene(trackExtent), emptyCode);
	ok &= expect(trackExtentWarnings.size() == 1
			&& contains(trackExtentWarnings[0].message, "blocks of track track-1 cover 0.000000 to 2.000000 km"),
		"a track-scoped area outside its track gives the extent of that track");
	SceneModel unusableAreas = clean;
	unusableAreas.signallingAreas = {{"inverted", 2.0, 0.0, 2, {}}, {"level", 0.0, 2.0, 7, {}},
		{"track", 0.0, 2.0, 2, "missing-track"}, {"a", 0.0, 2.0, 2, {}}};
	const auto unusableDiagnostics = validateRunnableScene(unusableAreas);
	ok &= expect(findAll(unusableDiagnostics, emptyCode).empty() && findAll(unusableDiagnostics, splitCode).empty(),
		"areas that are already invalid get no empty or split warning");
	SceneModel edgesAdjacent = clean;
	edgesAdjacent.signallingAreas = {{"first", 0.0, 1.0, 2, {}}, {"second", 1.0, 2.0, 3, {}}};
	SceneModel alignedAreas = clean;
	alignedAreas.signallingAreas = {{"network", 0.0, 2.0, 2, {}}, {"track", 0.0, 2.0, 4, "track-1"}};
	for (const SceneModel* correct : std::initializer_list<const SceneModel*>{&clean, &alignedAreas, &edgesAdjacent}) {
		const auto correctDiagnostics = validateRunnableScene(*correct);
		ok &= expect(!hasCode(correctDiagnostics, emptyCode) && !hasCode(correctDiagnostics, splitCode)
				&& !hasCode(correctDiagnostics, conflictCode),
			"a correct scene gets no area warning");
	}

	// The warning about route sections without a level names the stretches and the areas next to them.
	const auto stretchNone = findAll(validateRunnableScene(noAreas), levelMissing);
	const std::string stretchNoneFix = "In Infrastructure > Signalling area add a network-wide area covering "
									   "0.000000 to 2.000000 km, or a track-scoped area for each track listed";
	ok &= expect(stretchNone.size() == 1 && stretchNone[0].suggestedFix == stretchNoneFix,
		"without areas the fix gives the range of the network and no list of stretches");
	const auto stretchPartial = findAll(validateRunnableScene(partialArea), levelMissing);
	const std::string stretchPartialMessage =
		"1 of 2 route sections has no signalling level and runs without signalling: @block-2@ (track track-1)";
	const std::string stretchPartialFix = "or a track-scoped area for each track listed. Without a level: "
										  "track-1 1.000000 to 2.000000 km (after area partial)";
	ok &= expect(stretchPartial.size() == 1 && stretchPartial[0].message == stretchPartialMessage
			&& contains(stretchPartial[0].suggestedFix, stretchPartialFix),
		"a stretch after an area names that area");
	SceneModel gapBetween = conflictingDerivedAreas;
	gapBetween.routes[0].blocks = {"block-3"};
	gapBetween.signallingAreas = {{"left", 0.0, 3.0, 2, "track-2"}, {"right", 4.0, 5.0, 2, {}}};
	const auto stretchBetween = findAll(validateRunnableScene(gapBetween), levelMissing);
	const std::string stretchBetweenFix =
		". Without a level: track-2 3.000000 to 4.000000 km (between area left and area right)";
	ok &= expect(stretchBetween.size() == 1 && contains(stretchBetween[0].suggestedFix, stretchBetweenFix),
		"a stretch between two areas names both");
	SceneModel gapBefore = conflictingDerivedAreas;
	gapBefore.routes[0].blocks = {"block-1", "block-2", "block-3"};
	gapBefore.signallingAreas = {{"right", 4.0, 5.0, 2, {}}};
	const auto stretchBefore = findAll(validateRunnableScene(gapBefore), levelMissing);
	const std::string stretchBeforeFix = "track-1 0.000000 to 2.000000 km (before area right), "
										 "track-2 3.000000 to 4.000000 km (before area right)";
	ok &= expect(stretchBefore.size() == 1 && contains(stretchBefore[0].suggestedFix, stretchBeforeFix),
		"each track gets its own stretch, before the area that follows it");
	const auto stretchInside = findAll(validateRunnableScene(splitBoth), levelMissing);
	ok &= expect(stretchInside.size() == 1 && contains(stretchInside[0].suggestedFix, "track-1 0.000000 to 2.000000 km (area inner lies inside it)"),
		"a stretch with an area inside it says so");
	SceneModel gapRuns = clean;
	gapRuns.blocks = {{"block-1", "track-1", 1.0}, {"block-2", "track-1", 1.0}, {"block-3", "track-1", 1.0}};
	gapRuns.nodes.push_back({"node-4", "track-1", 3.0, 0.0});
	gapRuns.arcs.push_back({"arc-3", "track-1", "node-3", "node-4", 0.0, 0.0, 35.0});
	gapRuns.routes[0].blocks = {"block-1", "block-2", "block-3"};
	gapRuns.signallingAreas = {{"middle", 1.0, 2.0, 2, {}}};
	const auto stretchRuns = findAll(validateRunnableScene(gapRuns), levelMissing);
	ok &= expect(stretchRuns.size() == 1 && contains(stretchRuns[0].message, "2 of 3 route sections have no signalling level")
			&& contains(stretchRuns[0].suggestedFix, "track-1 0.000000 to 1.000000 km (before area middle), "
													 "track-1 2.000000 to 3.000000 km (after area middle)"),
		"separate uncovered stretches of one track are listed one by one");

	// The names of the signalling levels.
	const std::pair<int, const char*> levelLabels[] = {
		{kSignallingLevelUnset, "No signalling"}, {0, "0 ATB fixed block"},
		{1, "1 ETCS Level 1 fixed block"}, {2, "2 ETCS Level 2 fixed block"},
		{3, "3 ETCS Level 3 moving block"}, {4, "4 Virtual coupling"}, {5, "5 BACC track circuits"},
		{-1, "Invalid level -1"}, {6, "Invalid level 6"}, {-99999998, "Invalid level -99999998"}};
	std::vector<std::string> levelDescriptions;
	for (const auto& entry : levelLabels) {
		ok &= expect(signallingLevelName(entry.first) == entry.second, "a signalling level has its label");
		levelDescriptions.push_back(signallingLevelDescription(entry.first));
	}
	ok &= expect(std::find(levelDescriptions.begin(), levelDescriptions.end(), "") == levelDescriptions.end(),
		"every signalling level has a description");
	ok &= expect(std::set<std::string>(levelDescriptions.begin(), levelDescriptions.begin() + 7).size() == 7
			&& levelDescriptions[7] == "Valid levels are 0 to 5." && levelDescriptions[8] == levelDescriptions[7],
		"the valid levels differ in description and the invalid ones say which levels are valid");

	// The numbers, the range and the unset value of the signalling levels.
	const std::pair<SignallingLevel, int> levelNumbers[] = {{SignallingLevel::Atb, 0}, {SignallingLevel::EtcsLevel1, 1},
		{SignallingLevel::EtcsLevel2, 2}, {SignallingLevel::EtcsLevel3, 3}, {SignallingLevel::VirtualCoupling, 4},
		{SignallingLevel::Bacc, 5}};
	for (const auto& entry : levelNumbers)
		ok &= expect(levelValue(entry.first) == entry.second, "a signalling level has the number of the file format");
	for (int level = 0; level <= 5; ++level)
		ok &= expect(isValidSignallingLevel(level), "the numbers 0 to 5 are signalling levels");
	for (const int level : {-1, 6, kSignallingLevelUnset, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
		ok &= expect(!isValidSignallingLevel(level), "numbers outside 0 to 5 are not signalling levels");
	ok &= expect(kSignallingLevelUnset == -99999999, "the unset level has its stored value");
	SceneModel highestAreaLevel = clean;
	highestAreaLevel.signallingAreas = {{"area", 0.0, 1.0, 5, {}}};
	ok &= expect(findAll(validateScene(highestAreaLevel), "scene.signalling_area.level").empty(),
		"an area of level 5 has a valid level");
	SceneModel negativeAreaLevel = clean;
	negativeAreaLevel.signallingAreas = {{"area", 0.0, 1.0, -1, {}}};
	const auto negativeLevelErrors = findAll(validateScene(negativeAreaLevel), "scene.signalling_area.level");
	ok &= expect(negativeLevelErrors.size() == 1
			&& negativeLevelErrors[0].message == "Signalling area area has level -1; the level must be between 0 and 5",
		"an area of level -1 has an invalid level");
	ok &= expect(findAll(validateRunnableScene(invalidAreaLevel), emptyCode).empty(),
		"an area with an invalid level takes no part in the warning about empty areas");

	// Which area decides the level of each section.
	auto analyze = [](const SceneModel& candidate) {
		return analyzeSignallingAreas(candidate, buildSceneSectionInventory(candidate));
	};
	auto sectionOf = [](const SceneSignallingAnalysis& analysis, const char* id) {
		const SceneSectionSignalling* section = analysis.section(id);
		return section == nullptr ? SceneSectionSignalling() : *section;
	};
	const std::size_t none = SceneSectionSignalling::kNoArea;
	const SceneSignallingAnalysis cleanAnalysis = analyze(clean);
	ok &= expect(sectionOf(cleanAnalysis, "@block-1@").level == 0 && sectionOf(cleanAnalysis, "@block-1@").decidingArea == 0
			&& sectionOf(cleanAnalysis, "@block-2@").level == 0 && sectionOf(cleanAnalysis, "@block-2@").onRoute
			&& sectionOf(cleanAnalysis, "@block-1@").conflicts.empty(),
		"a network-wide area decides the level of the sections it contains");
	ok &= expect(cleanAnalysis.section("@no-such-block@") == nullptr, "an unknown section has no analysis");
	ok &= expect(cleanAnalysis.areas.size() == 1 && cleanAnalysis.areas[0].sectionCount >= 2
			&& cleanAnalysis.areas[0].routeSectionCount == 2
			&& cleanAnalysis.areas[0].sectionsSplitByStart.empty() && cleanAnalysis.areas[0].sectionsSplitByEnd.empty(),
		"an area that holds every section counts them and splits none");

	SceneModel scoped = clean;
	scoped.signallingAreas = {{"network", 0.0, 2.0, 1, {}}, {"track", 0.0, 1.0, 3, "track-1"},
		{"elsewhere", 0.0, 2.0, 5, "track-2"}};
	const SceneSignallingAnalysis scopedAnalysis = analyze(scoped);
	ok &= expect(sectionOf(scopedAnalysis, "@block-1@").level == 3 && sectionOf(scopedAnalysis, "@block-1@").decidingArea == 1
			&& sectionOf(scopedAnalysis, "@block-1@").conflicts.empty(),
		"a track-scoped area overrides the network-wide area without a conflict");
	ok &= expect(sectionOf(scopedAnalysis, "@block-2@").level == 1 && sectionOf(scopedAnalysis, "@block-2@").decidingArea == 0,
		"the network-wide area decides where the track-scoped area does not reach");
	ok &= expect(scopedAnalysis.areas[0].routeSectionCount == 2 && scopedAnalysis.areas[1].routeSectionCount == 1
			&& scopedAnalysis.areas[2].sectionCount == 0,
		"an area counts the sections it contains even when another area decides them, and none on another track");

	SceneModel conflicting = clean;
	conflicting.signallingAreas = {{"a", 0.0, 2.0, 2, {}}, {"b", 0.0, 1.0, 2, {}}, {"c", 0.0, 2.0, 3, {}},
		{"d", 0.0, 2.0, 4, {}}, {"t1", 0.0, 2.0, 1, "track-1"}, {"t2", 1.0, 2.0, 5, "track-1"}};
	const SceneSignallingAnalysis conflictAnalysis = analyze(conflicting);
	const SceneSectionSignalling conflictOne = sectionOf(conflictAnalysis, "@block-1@");
	ok &= expect(conflictOne.level == 1 && conflictOne.decidingArea == 4 && conflictOne.conflicts.size() == 2
			&& conflictOne.conflicts[0].firstArea == 0 && conflictOne.conflicts[0].area == 2
			&& !conflictOne.conflicts[0].trackScoped && conflictOne.conflicts[1].area == 3
			&& !conflictOne.conflicts[1].trackScoped,
		"areas that disagree with the first area of their scope are listed as conflicts in area order");
	const SceneSectionSignalling conflictTwo = sectionOf(conflictAnalysis, "@block-2@");
	ok &= expect(conflictTwo.level == 1 && conflictTwo.conflicts.size() == 3 && conflictTwo.conflicts[2].trackScoped
			&& conflictTwo.conflicts[2].firstArea == 4 && conflictTwo.conflicts[2].area == 5,
		"conflicts of the network-wide scope come before those of the track scope");
	const auto conflictingRun = validateRunnableScene(conflicting);
	std::vector<std::string> conflictErrors;
	for (const SceneDiagnostic& diagnostic : findAll(conflictingRun, "scene.signalling_area.conflict")) {
		ok &= expect(diagnostic.severity == SceneSeverity::Error && diagnostic.file == "signalling.json"
				&& diagnostic.itemType == "signalling_area",
			"an area conflict is an error on the signalling file");
		conflictErrors.push_back(diagnostic.itemId + " " + diagnostic.path + " " + diagnostic.relatedId);
	}
	ok &= expect(conflictErrors == std::vector<std::string>{"c signalling_areas[2] a", "d signalling_areas[3] a", "t2 signalling_areas[5] t1"},
		"the validator reports one conflict for each pair of areas that disagree about a section");
	SceneModel agreeing = clean;
	agreeing.signallingAreas = {{"a", 0.0, 2.0, 2, {}}, {"b", 0.0, 1.5, 2, {}}};
	ok &= expect(sectionOf(analyze(agreeing), "@block-1@").conflicts.empty() && sectionOf(analyze(agreeing), "@block-1@").decidingArea == 0,
		"areas that give the same level do not conflict and the first one decides");

	SceneModel splitting = clean;
	splitting.signallingAreas = {{"head", 0.0, 1.5, 2, {}}, {"tail", 0.5, 2.0, 3, {}}};
	const SceneSignallingAnalysis splitAnalysis = analyze(splitting);
	auto listed = [](const std::vector<std::string>& ids, const char* id) {
		return std::find(ids.begin(), ids.end(), id) != ids.end();
	};
	ok &= expect(listed(splitAnalysis.areas[0].sectionsSplitByEnd, "@block-2@")
			&& !listed(splitAnalysis.areas[0].sectionsSplitByEnd, "@block-1@")
			&& splitAnalysis.areas[0].sectionsSplitByStart.empty()
			&& listed(splitAnalysis.areas[1].sectionsSplitByStart, "@block-1@")
			&& !listed(splitAnalysis.areas[1].sectionsSplitByStart, "@block-2@")
			&& splitAnalysis.areas[1].sectionsSplitByEnd.empty(),
		"an area edge names the sections it cuts through");
	ok &= expect(splitAnalysis.areas[0].routeSectionCount == 1 && splitAnalysis.areas[1].routeSectionCount == 1
			&& sectionOf(splitAnalysis, "@block-1@").level == 2 && sectionOf(splitAnalysis, "@block-2@").level == 3,
		"a section that crosses the edge of an area is not counted for it");
	splitting.signallingAreas = {{"head", 0.0, 1.5, 2, {}}, {"tail", 1.5, 2.0, 3, {}}};
	ok &= expect(sectionOf(analyze(splitting), "@block-2@").level == kSignallingLevelUnset
			&& sectionOf(analyze(splitting), "@block-2@").decidingArea == none,
		"a section cut by the edge between two areas has no level");

	SceneModel tolerance = clean;
	tolerance.signallingAreas = {{"in", 0.0, 1.0 - 5e-9, 2, {}}};
	SceneSignallingAnalysis toleranceAnalysis = analyze(tolerance);
	ok &= expect(sectionOf(toleranceAnalysis, "@block-1@").level == 2
			&& !listed(toleranceAnalysis.areas[0].sectionsSplitByEnd, "@block-1@"),
		"an area end just inside the tolerance holds the section and does not cut it");
	tolerance.signallingAreas = {{"out", 0.0, 1.0 - 1e-7, 2, {}}};
	toleranceAnalysis = analyze(tolerance);
	ok &= expect(sectionOf(toleranceAnalysis, "@block-1@").level == kSignallingLevelUnset
			&& listed(toleranceAnalysis.areas[0].sectionsSplitByEnd, "@block-1@"),
		"an area end outside the tolerance cuts the section");
	tolerance.signallingAreas = {{"in", 1.0 + 5e-9, 2.0, 2, {}}};
	toleranceAnalysis = analyze(tolerance);
	ok &= expect(sectionOf(toleranceAnalysis, "@block-2@").level == 2
			&& !listed(toleranceAnalysis.areas[0].sectionsSplitByStart, "@block-2@"),
		"an area start just inside the tolerance holds the section and does not cut it");

	SceneModel unusable = clean;
	unusable.signallingAreas = {{"inverted", 2.0, 0.0, 2, {}}, {"empty", 1.0, 1.0, 2, {}},
		{"high", 0.0, 2.0, 6, {}}, {"low", 0.0, 2.0, -1, {}},
		{"infinite", 0.0, std::numeric_limits<double>::infinity(), 2, {}}};
	const SceneSignallingAnalysis unusableAnalysis = analyze(unusable);
	bool unusableCounted = false;
	for (const SceneAreaSignalling& area : unusableAnalysis.areas)
		unusableCounted |= area.sectionCount != 0 || !area.sectionsSplitByStart.empty() || !area.sectionsSplitByEnd.empty();
	ok &= expect(!unusableCounted && sectionOf(unusableAnalysis, "@block-1@").level == kSignallingLevelUnset
			&& sectionOf(unusableAnalysis, "@block-1@").conflicts.empty(),
		"areas with an empty range or a level outside 0 to 5 take no part");

	SceneModel offRoute = clean;
	offRoute.routes[0].blocks = {"block-1"};
	const SceneSignallingAnalysis offRouteAnalysis = analyze(offRoute);
	ok &= expect(sectionOf(offRouteAnalysis, "@block-1@").onRoute && !sectionOf(offRouteAnalysis, "@block-2@").onRoute
			&& offRouteAnalysis.areas[0].routeSectionCount == 1 && offRouteAnalysis.areas[0].sectionCount >= 2,
		"the route sections of an area are those of its sections that a route uses");
	offRoute.routes[0].blocks = {"block-2", "block-1"};
	ok &= expect(analyze(offRoute).areas[0].routeSectionCount == 2, "a route in decreasing chainage uses the same sections");

	const std::string noEffect = "scene.single_track.no_effect";
	SceneModel restricted = clean;
	restricted.singleTrackRestrictions = {{"block-1", "block-2", "block-1", "block-2"}};
	for (const int level : {0, 1, 2, 3, 4, 5}) {
		restricted.signallingAreas = {{"network", 0.0, 2.0, level, {}}};
		ok &= expect(!hasCode(validateRunnableScene(restricted), noEffect),
			"a single-track restriction at any signalling level acts and gets no warning");
	}
	restricted.signallingAreas.clear();
	const auto unsignalledDiagnostics = validateRunnableScene(restricted);
	const SceneDiagnostic* unsignalledWarning = findCode(unsignalledDiagnostics, noEffect);
	ok &= expect(unsignalledWarning != nullptr && unsignalledWarning->severity == SceneSeverity::Warning
			&& unsignalledWarning->file == "signalling.json" && unsignalledWarning->path == "single_track_restrictions[0]"
			&& contains(unsignalledWarning->message, "Single-track restriction 0 (start_block block-1, end_block block-2, "
													 "protected_start_block block-1, protected_end_block block-2)")
			&& contains(unsignalledWarning->message, "start_block block-1 has no signalling level")
			&& contains(unsignalledWarning->message, "protected_end_block block-2 has no signalling level")
			&& contains(unsignalledWarning->suggestedFix, "signalling level") && !hasErrors(unsignalledDiagnostics)
			&& !hasCode(validateScene(restricted), noEffect) && hasCode(unsignalledDiagnostics, levelMissing),
		"a single-track restriction without a signalling level warns that it has no effect");
	restricted.singleTrackRestrictions = {{"block-1", "block-2", "block-1", "block-2"}, {"block-2", "block-1", "block-2", "block-1"}};
	std::size_t noEffectCount = 0;
	for (const SceneDiagnostic& diagnostic : validateRunnableScene(restricted))
		noEffectCount += diagnostic.code == noEffect ? 1 : 0;
	ok &= expect(noEffectCount == 2, "each single-track restriction gets one warning");
	restricted.singleTrackRestrictions = {{"block-1", "block-2", "block-1", "block-2"}};
	restricted.signallingAreas = {{"partial", 0.0, 1.5, 2, {}}};
	const auto partialRestrictionDiagnostics = validateRunnableScene(restricted);
	const SceneDiagnostic* partialRestrictionWarning = findCode(partialRestrictionDiagnostics, noEffect);
	ok &= expect(partialRestrictionWarning != nullptr && contains(partialRestrictionWarning->message, "end_block block-2 has no signalling level")
			&& !contains(partialRestrictionWarning->message, "start_block block-1 has"),
		"the warning names only the blocks without a signalling level");
	restricted.singleTrackRestrictions.clear();
	restricted.signallingAreas.clear();
	ok &= expect(!hasCode(validateRunnableScene(restricted), noEffect), "a scene without restrictions has nothing to warn about");

	const std::string steepCode = "scene.route.gradient.steep";
	const SceneModel steepDescent = steepGradientScene(-0.09, 0.0);
	const auto steepDescentDiagnostics = validateScene(steepDescent);
	const SceneDiagnostic* steepDescentWarning = findCode(steepDescentDiagnostics, steepCode);
	ok &= expect(steepDescentWarning != nullptr && steepDescentWarning->severity == SceneSeverity::Warning
			&& steepDescentWarning->file == "infrastructure.json"
			&& contains(steepDescentWarning->message, "Route route-1 with composition composition-1: 1 of 2 arcs is")
			&& contains(steepDescentWarning->message, "arc-1 (-0.09)")
			&& contains(steepDescentWarning->message, "limit 0.08333")
			&& contains(steepDescentWarning->message, "limit 0.1411")
			&& contains(steepDescentWarning->message, "rise per length")
			&& contains(steepDescentWarning->suggestedFix, "gradient_percent")
			&& !hasErrors(steepDescentDiagnostics),
		"a descent steeper than the braking limit gives a warning that names the arc and both limits");
	ok &= expect(hasCode(validateRunnableScene(steepDescent), steepCode),
		"runnable validation reports the steep gradient too");
	ok &= expect(steepGradientWarningCount(steepGradientScene(0.0, 0.15)) == 1,
		"an ascent steeper than the starting limit gives a warning");
	ok &= expect(steepGradientWarningCount(steepGradientScene(0.0, 0.09)) == 0,
		"an ascent above the braking limit but below the starting limit gives no warning");
	ok &= expect(steepGradientWarningCount(steepGradientScene(-0.08, 0.14)) == 0,
		"arcs just below both limits give no warning");

	SceneModel reversedRoute = steepGradientScene(0.09, 0.0);
	reversedRoute.routes[0].blocks = {"block-2", "block-1"};
	ok &= expect(buildSceneRouteTraversal(reversedRoute, reversedRoute.routes[0]).direction == -1
			&& steepGradientWarningCount(reversedRoute) == 1,
		"an ascent in the stored direction is a descent on a reversed route");
	reversedRoute = steepGradientScene(-0.09, 0.0);
	reversedRoute.routes[0].blocks = {"block-2", "block-1"};
	ok &= expect(steepGradientWarningCount(reversedRoute) == 0,
		"a descent in the stored direction is an ascent below the starting limit on a reversed route");

	SceneModel twoServices = steepDescent;
	twoServices.services.push_back(twoServices.services.front());
	twoServices.services.back().id = "service-2";
	ok &= expect(steepGradientWarningCount(twoServices) == 1,
		"services with the same route and composition share one warning");

	SceneModel twoCompositions = steepGradientScene(-0.1, 0.0);
	twoCompositions.trainUnits.push_back(paimpolUnit());
	twoCompositions.compositions.push_back({"composition-2", {"unit-2"}});
	twoCompositions.services.push_back(twoCompositions.services.front());
	twoCompositions.services.back().id = "service-2";
	twoCompositions.services.back().composition = "composition-2";
	const auto twoCompositionDiagnostics = validateScene(twoCompositions);
	ok &= expect(steepGradientWarningCount(twoCompositions) == 1
			&& contains(findCode(twoCompositionDiagnostics, steepCode)->message, "composition-1")
			&& !contains(findCode(twoCompositionDiagnostics, steepCode)->message, "composition-2"),
		"a composition with a larger deceleration is judged on its own limit");
	twoCompositions.arcs[0].gradientPercent = -0.2;
	ok &= expect(steepGradientWarningCount(twoCompositions) == 2,
		"each composition on the route gets its own warning");

	SceneModel unknownComposition = steepDescent;
	unknownComposition.services[0].composition = "missing";
	ok &= expect(steepGradientWarningCount(unknownComposition) == 0,
		"a service without a resolvable composition gets no gradient warning");
	SceneModel unknownRoute = steepDescent;
	unknownRoute.routes[0].blocks = {"missing-block"};
	ok &= expect(steepGradientWarningCount(unknownRoute) == 0,
		"a route without resolvable sections gets no gradient warning");

	SceneModel longRoute = clean;
	longRoute.nodes.clear();
	longRoute.arcs.clear();
	longRoute.blocks.clear();
	longRoute.connections.clear();
	longRoute.routes[0].blocks.clear();
	longRoute.signallingAreas.clear();
	for (int index = 0; index <= 200; ++index)
		longRoute.nodes.push_back({"n" + std::to_string(index), "track-1", static_cast<double>(index), 0.0});
	for (int index = 0; index < 200; ++index) {
		longRoute.arcs.push_back({"a" + std::to_string(index), "track-1", "n" + std::to_string(index),
			"n" + std::to_string(index + 1), 0.0, 0.0, 40.0});
		longRoute.blocks.push_back({"b" + std::to_string(index), "track-1", 1.0});
		longRoute.routes[0].blocks.push_back("b" + std::to_string(index));
	}
	const auto longDiagnostics = validateRunnableScene(longRoute);
	const SceneDiagnostic* longWarning = findCode(longDiagnostics, levelMissing);
	ok &= expect(
		std::count_if(longDiagnostics.begin(), longDiagnostics.end(), [&](const SceneDiagnostic& d) { return d.code == levelMissing; }) == 1
			&& longWarning != nullptr
			&& contains(longWarning->message, "200 of 200 route sections")
			&& contains(longWarning->message, "@b4@ and 195 more")
			&& !contains(longWarning->message, "@b5@")
			&& contains(longWarning->message, "(track track-1)"),
		"a long route gets one warning that names at most five sections");

	SceneModel longTrack = clean;
	for (int index = 4; index <= 1601; ++index)
		longTrack.nodes.push_back({"node-" + std::to_string(index), "track-1", static_cast<double>(index - 1), 0.0});
	for (int index = 3; index <= 1600; ++index)
		longTrack.arcs.push_back({"arc-" + std::to_string(index), "track-1", "node-" + std::to_string(index),
			"node-" + std::to_string(index + 1), 0.0, 0.0, 40.0});
	for (int index = 3; index <= 96; ++index)
		longTrack.blocks.push_back({"block-" + std::to_string(index), "track-1", 17.0});
	ok &= expect(!hasCode(validateRunnableScene(longTrack), "scene.capacity.runtime"),
		"a track with more than 1500 nodes and arcs is not rejected for size");

	SceneModel manySections = clean;
	for (int index = 4; index <= 6002; ++index)
		manySections.nodes.push_back({"node-" + std::to_string(index), "track-1", static_cast<double>(index - 1), 0.0});
	for (int index = 3; index <= 6001; ++index) {
		manySections.arcs.push_back({"arc-" + std::to_string(index), "track-1", "node-" + std::to_string(index),
			"node-" + std::to_string(index + 1), 0.0, 0.0, 40.0});
		manySections.blocks.push_back({"block-" + std::to_string(index), "track-1", 1.0});
	}
	ok &= expect(!hasCode(validateRunnableScene(manySections), "scene.capacity.runtime"),
		"a scene with more than 6000 sections is not rejected for size");

	SceneModel negativeChainage = clean;
	negativeChainage.nodes = {{"node-1", "track-1", -2.0, 0.0}, {"node-2", "track-1", -1.0, 0.0},
		{"node-3", "track-1", 0.0, 0.0}};
	negativeChainage.connections.clear();
	negativeChainage.signallingAreas.clear();
	const auto negativeDiagnostics = validateRunnableScene(negativeChainage);
	const SceneDiagnostic* negativeWarning = findCode(negativeDiagnostics, levelMissing);
	ok &= expect(negativeWarning != nullptr
			&& contains(negativeWarning->suggestedFix, "-2.000000 to 0.000000 km"),
		"the suggested area range reports negative chainage");

	const auto conflictDiagnostics = validateRunnableScene(conflictingNetworkAreas);
	ok &= expect(hasCode(conflictDiagnostics, "scene.signalling_area.conflict")
			&& !hasCode(conflictDiagnostics, levelMissing),
		"a section with conflicting areas does not also get the missing level warning");

	struct FailureCase {
		const char* name;
		void (*mutate)(SceneModel&);
		const char* code;
		const char* path = nullptr;
	};
	const FailureCase cases[] = {
		{"empty track id", [](SceneModel& scene) { scene.tracks[0].id.clear(); }, "scene.id.empty",
			"tracks[0].id"},
		{"empty node id", [](SceneModel& scene) { scene.nodes[0].id.clear(); }, "scene.id.empty",
			"nodes[0].id"},
		{"empty arc id", [](SceneModel& scene) { scene.arcs[0].id.clear(); }, "scene.id.empty",
			"arcs[0].id"},
		{"empty block id", [](SceneModel& scene) { scene.blocks[0].id.clear(); }, "scene.id.empty",
			"blocks[0].id"},
		{"empty connection id", [](SceneModel& scene) { scene.connections[0].id.clear(); }, "scene.id.empty",
			"connections[0].id"},
		{"empty station id", [](SceneModel& scene) { scene.stations[0].id.clear(); }, "scene.id.empty",
			"stations[0].id"},
		{"empty platform id", [](SceneModel& scene) { scene.stations[0].platforms[0].id.clear(); }, "scene.id.empty",
			"stations[0].platforms[0].id"},
		{"duplicate route id", [](SceneModel& scene) { scene.routes.push_back(scene.routes[0]); },
			"scene.id.duplicate"},
		{"unknown composition unit", [](SceneModel& scene) { scene.compositions[0].units[0] = "missing-unit"; },
			"scene.ref.unresolved"},
		{"unknown stop platform", [](SceneModel& scene) { scene.services[0].stops[0].platformId = "missing-platform"; },
			"scene.ref.platform"},
		{"unknown scenario incident target", [](SceneModel& scene) {
			 scene.scenarios[0].incidents[0].target = "missing-signal";
		 },
			"scene.ref.unresolved"},
		{"missing base time", [](SceneModel& scene) { scene.baseTime.clear(); }, "scene.basetime.missing"},
		{"out-of-range base time", [](SceneModel& scene) { scene.baseTime = "24:00:00"; }, "scene.basetime.invalid"},
		{"non-positive duration", [](SceneModel& scene) { scene.settings.durationSeconds = 0.0; }, "scene.duration.invalid"},
		{"non-finite duration", [](SceneModel& scene) {
			 scene.settings.durationSeconds = std::numeric_limits<double>::quiet_NaN();
		 },
			"scene.duration.invalid"},
		{"performance below range", [](SceneModel& scene) {
			 scene.services[0].performancePercent = 0.5;
		 },
			"scene.performance.invalid", "services[service-1].performance_percent"},
		{"non-finite maximum speed", [](SceneModel& scene) {
			 scene.services[0].hasMaximumSpeed = true;
			 scene.services[0].maximumSpeedKmh = std::numeric_limits<double>::infinity();
		 },
			"scene.speed.invalid", "services[service-1].maximum_speed_kmh"},
		{"non-positive repeat count", [](SceneModel& scene) {
			 scene.services[0].hasRepeat = true;
			 scene.services[0].headwaySeconds = 30.0;
			 scene.services[0].hasRepeatCount = true;
			 scene.services[0].repeatCount = 0;
		 },
			"scene.repeat.count.invalid", "services[service-1].repeat.count"},
		{"non-decimal operating-code step", [](SceneModel& scene) {
			 scene.services[0].operatingCode = "R100";
			 scene.services[0].hasRepeat = true;
			 scene.services[0].headwaySeconds = 30.0;
			 scene.services[0].hasOperatingCodeStep = true;
			 scene.services[0].operatingCodeStep = 2;
		 },
			"scene.repeat.step.invalid", "services[service-1].repeat.operating_code_step"},
		{"overflowing operating-code step", [](SceneModel& scene) {
			 scene.services[0].operatingCode = "9223372036854775806";
			 scene.services[0].hasRepeat = true;
			 scene.services[0].headwaySeconds = 30.0;
			 scene.services[0].hasRepeatCount = true;
			 scene.services[0].repeatCount = 2;
			 scene.services[0].hasOperatingCodeStep = true;
			 scene.services[0].operatingCodeStep = 2;
		 },
			"scene.repeat.step.invalid", "services[service-1].repeat.operating_code_step"},
		{"entrance delay beyond explicit pattern", [](SceneModel& scene) {
			 scene.services[0].hasRepeat = true;
			 scene.services[0].headwaySeconds = 30.0;
			 scene.services[0].hasRepeatCount = true;
			 scene.services[0].repeatCount = 1;
			 scene.scenarios[0].entranceDelays[0].occurrence = 2;
		 },
			"scene.entrance.occurrence.out_of_horizon", "scenarios[0].entrance_delays[0].occurrence"},
		{"non-finite entrance delay", [](SceneModel& scene) {
			 scene.scenarios[0].entranceDelays[0].delaySeconds = std::numeric_limits<double>::quiet_NaN();
		 },
			"scene.delay.invalid", "scenarios[0].entrance_delays[0].delay_seconds"},
		{"entrance delay station outside service stops", [](SceneModel& scene) {
			 scene.stations.push_back({"station-3", "Unused", false, 0.0, {}});
			 scene.scenarios[0].entranceDelays[0].stationId = "station-3";
		 },
			"scene.entrance.station", "scenarios[0].entrance_delays[0].station"},
		{"entrance delay stop without departure", [](SceneModel& scene) {
			 scene.scenarios[0].entranceDelays[0].stationId = "station-2";
		 },
			"scene.entrance.timetable", "scenarios[0].entrance_delays[0].station"},
		{"conflicting entrance delays", [](SceneModel& scene) {
			 scene.scenarios[0].entranceDelays.push_back({"service-1", 1, "station-1", 20.0});
		 },
			"scene.entrance.conflict", "scenarios[0].entrance_delays[1].delay_seconds"},
		{"non-finite node coordinate", [](SceneModel& scene) {
			 scene.nodes[0].xKm = std::numeric_limits<double>::quiet_NaN();
		 },
			"scene.node.coordinate.invalid", "nodes[0].x_km"},
		{"negative arc curvature", [](SceneModel& scene) {
			 scene.arcs[0].curvatureRadiusM = -1.0;
		 },
			"scene.arc.curvature.invalid", "arcs[0].curvature_radius_m"},
		{"non-positive arc speed", [](SceneModel& scene) { scene.arcs[0].speedLimitMs = 0.0; }, "scene.arc.speed.invalid", "arcs[0].speed_limit_ms"},
		{"non-positive block length", [](SceneModel& scene) { scene.blocks[0].lengthKm = 0.0; }, "scene.block.length.invalid", "blocks[0].length_km"},
		{"invalid optional connection speed", [](SceneModel& scene) {
			 scene.connections[0].hasSpeedLimit = true;
			 scene.connections[0].speedLimitMs = 0.0;
		 },
			"scene.connection.speed.invalid", "connections[0].speed_limit_ms"},
		{"arc endpoint on another track", [](SceneModel& scene) {
			 scene.tracks.push_back({"track-2"});
			 scene.nodes[1].trackId = "track-2";
		 },
			"scene.topology.track", "arcs[0].to"},
		{"arc self-loop", [](SceneModel& scene) { scene.arcs[0].toNodeId = "node-1"; }, "scene.topology.loop", "arcs[0].to"},
		{"ambiguous outgoing arc", [](SceneModel& scene) { scene.arcs[1].fromNodeId = "node-1"; }, "scene.topology.ambiguous", "tracks[0].nodes"},
		{"disconnected node", [](SceneModel& scene) {
			 scene.nodes.push_back({"node-4", "track-1", 3.0, 0.0});
		 },
			"scene.topology.disconnected", "tracks[0].nodes"},
		{"descending chain order", [](SceneModel& scene) { scene.nodes[1].xKm = -1.0; }, "scene.topology.order", "arcs[0].to"},
		{"empty tracks", [](SceneModel& scene) { scene.tracks.clear(); }, "scene.topology.tracks.none"},
		{"empty nodes", [](SceneModel& scene) { scene.nodes.clear(); }, "scene.topology.nodes.none"},
		{"empty arcs", [](SceneModel& scene) { scene.arcs.clear(); }, "scene.topology.arcs.none"},
		{"empty blocks", [](SceneModel& scene) { scene.blocks.clear(); }, "scene.topology.blocks.none"},
		{"empty routes", [](SceneModel& scene) { scene.routes.clear(); }, "scene.routes.none"},
		{"unbound platform", [](SceneModel& scene) {
			 scene.stations[0].platforms[0].nodeIds.clear();
		 },
			"scene.platform.nodes.none", "stations[0].platforms[0].nodes"},
		{"unanchored station", [](SceneModel& scene) { scene.stations[0].platforms.clear(); }, "scene.station.anchor.missing", "stations[0]"},
		{"non-finite station position", [](SceneModel& scene) {
			 scene.stations[0].hasPosition = true;
			 scene.stations[0].positionKm = std::numeric_limits<double>::quiet_NaN();
		 },
			"scene.station.position.invalid", "stations[0].position_km"},
		{"conflicting platform node", [](SceneModel& scene) {
			 scene.stations[1].platforms[0].nodeIds = {"node-1"};
		 },
			"scene.platform.node.conflict", "stations[1].platforms[0].nodes[0]"},
		{"platform outside service route", [](SceneModel& scene) {
			 scene.tracks.push_back({"track-2"});
			 scene.nodes.push_back({"node-4", "track-2", 0.0, 1.0});
			 scene.nodes.push_back({"node-5", "track-2", 1.0, 1.0});
			 scene.arcs.push_back({"arc-3", "track-2", "node-4", "node-5", 0.0, 0.0, 35.0});
			 scene.blocks.push_back({"block-3", "track-2", 1.0});
			 scene.stations[1].platforms[0].nodeIds = {"node-5"};
		 },
			"scene.ref.platform.route", "services[service-1].stops[1].platform"},
		{"platform outside composite switch route", [](SceneModel& scene) {
			 scene.tracks = {{"track-A"}, {"track-B"}};
			 scene.nodes = {
				 {"a-0", "track-A", 0.0, 0.0}, {"a-05", "track-A", 0.5, 0.0},
				 {"a-2", "track-A", 2.0, 0.0}, {"b-0", "track-B", 0.0, 1.0},
				 {"b-15", "track-B", 1.5, 1.0}, {"b-2", "track-B", 2.0, 1.0}};
			 scene.arcs = {
				 {"a-1", "track-A", "a-0", "a-05", 0.0, 0.0, 40.0},
				 {"a-2", "track-A", "a-05", "a-2", 0.0, 0.0, 40.0},
				 {"b-1", "track-B", "b-0", "b-15", 0.0, 0.0, 40.0},
				 {"b-2", "track-B", "b-15", "b-2", 0.0, 0.0, 40.0}};
			 scene.blocks = {{"A", "track-A", 2.0}, {"B", "track-B", 2.0}};
			 scene.connections = {{"switch", "a-05", "b-15", false, 0.0}};
			 scene.stations[0].platforms[0].nodeIds = {"a-0"};
			 scene.stations[1].platforms[0].nodeIds = {"a-2"};
			 scene.routes[0].blocks = {"@A@-0.500000/@B@-1.500000"};
		 },
			"scene.ref.platform.route", "services[service-1].stops[1].platform"},
		{"unknown route block", [](SceneModel& scene) { scene.routes[0].blocks[0] = "missing-block"; }, "scene.ref.unresolved"},
		{"unknown default scenario", [](SceneModel& scene) { scene.defaultScenarioId = "missing"; }, "scene.ref.unresolved"},
		{"discontinuous passenger leg", [](SceneModel& scene) {
			 scene.passengers[0].journeys[0].legs[0].destinationStationId = "station-1";
		 },
			"scene.passenger.continuity"},
	};
	for (const auto& test : cases) {
		SceneModel broken = clean;
		test.mutate(broken);
		const auto diagnostics = validateRunnableScene(broken);
		bool passed = hasCode(diagnostics, test.code);
		if (test.path)
			passed = passed && hasCodeAndPath(diagnostics, test.code, test.path);
		ok &= expect(passed, test.name);
	}
	SceneModel reservedBlockId = clean;
	reservedBlockId.blocks[0].id = "Depot/1";
	reservedBlockId.routes[0].blocks[0] = "Depot/1";
	reservedBlockId.signals[0].protectedSection = "Depot/1";
	const auto reservedBlockIdDiagnostics = validateRunnableScene(reservedBlockId);
	ok &= expect(reservedBlockIdDiagnostics.size() == 1
			&& hasCodeAndPath(reservedBlockIdDiagnostics, "scene.id.reserved", "blocks[0].id"),
		"reserved slash in block id reports one actionable validation error");
	SceneModel reducedBreakdown = clean;
	SceneIncident& reducedIncident = reducedBreakdown.scenarios[0].incidents[0];
	reducedIncident.type = "train_breakdown";
	reducedIncident.target = "service-1";
	reducedIncident.startSeconds = 300.0;
	reducedIncident.endSeconds = 0.0;
	reducedIncident.hasEndSeconds = false;
	reducedIncident.hasReducedSpeed = true;
	reducedIncident.reducedSpeedKmh = 40.0;
	reducedIncident.hasOccurrence = true;
	reducedIncident.occurrence = 1;
	ok &= expect(validateRunnableScene(reducedBreakdown).empty(),
		"reduced-speed breakdown may omit recovery end");
	SceneModel extendedOccurrence = reducedBreakdown;
	extendedOccurrence.settings.durationSeconds = 1.0;
	extendedOccurrence.services[0].hasRepeat = true;
	extendedOccurrence.services[0].headwaySeconds = 10.0;
	extendedOccurrence.scenarios[0].incidents[0].occurrence = 2;
	extendedOccurrence.scenarios[0].entranceDelays[0].occurrence = 2;
	const auto savedHorizonDiagnostics = validateRunnableScene(extendedOccurrence);
	ok &= expect(hasCode(savedHorizonDiagnostics, "scene.occurrence.invalid")
			&& hasCode(savedHorizonDiagnostics, "scene.entrance.occurrence.out_of_horizon"),
		"saved horizon rejects occurrence-specific rows outside its repeat pattern");
	const auto extendedHorizonDiagnostics = validateRunnableScene(
		extendedOccurrence, {}, std::optional<double>(20.0));
	ok &= expect(!hasCode(extendedHorizonDiagnostics, "scene.occurrence.invalid")
			&& !hasCode(extendedHorizonDiagnostics, "scene.entrance.occurrence.out_of_horizon"),
		"duration override applies to breakdown and entrance-delay occurrences");

	SceneModel fullHoldWithoutEnd = reducedBreakdown;
	fullHoldWithoutEnd.scenarios[0].incidents[0].hasReducedSpeed = false;
	fullHoldWithoutEnd.scenarios[0].incidents[0].reducedSpeedKmh = 0.0;
	ok &= expect(hasCode(validateRunnableScene(fullHoldWithoutEnd), "scene.incident.window"),
		"legacy full-hold breakdown requires an end");

	SceneModel invalidBreakdown = reducedBreakdown;
	invalidBreakdown.scenarios[0].incidents[0].occurrence = 0;
	ok &= expect(hasCode(validateRunnableScene(invalidBreakdown), "scene.occurrence.invalid"),
		"breakdown occurrence must be positive");
	invalidBreakdown = reducedBreakdown;
	invalidBreakdown.services[0].hasRepeat = true;
	invalidBreakdown.services[0].headwaySeconds = 30.0;
	invalidBreakdown.services[0].hasRepeatCount = true;
	invalidBreakdown.services[0].repeatCount = 1;
	invalidBreakdown.scenarios[0].incidents[0].occurrence = 2;
	ok &= expect(hasCode(validateRunnableScene(invalidBreakdown), "scene.occurrence.invalid"),
		"breakdown occurrence must be inside the configured repeat range");
	invalidBreakdown = reducedBreakdown;
	invalidBreakdown.scenarios[0].incidents[0].reducedSpeedKmh = 0.0;
	ok &= expect(hasCode(validateRunnableScene(invalidBreakdown), "scene.incident.speed"),
		"reduced breakdown speed must be positive");
	invalidBreakdown = reducedBreakdown;
	invalidBreakdown.scenarios[0].incidents[0].hasEndSeconds = true;
	invalidBreakdown.scenarios[0].incidents[0].endSeconds = 300.0;
	ok &= expect(hasCode(validateRunnableScene(invalidBreakdown), "scene.incident.window"),
		"breakdown recovery end must be after start");
	invalidBreakdown = reducedBreakdown;
	invalidBreakdown.scenarios[0].incidents[0].terminateAtDestination = true;
	invalidBreakdown.scenarios[0].incidents[0].type = "signal_failure";
	invalidBreakdown.scenarios[0].incidents[0].target = "signal-1";
	ok &= expect(hasCode(validateRunnableScene(invalidBreakdown), "scene.incident.fields"),
		"signal failures reject breakdown-only fields");
	SceneService repeated = clean.services[0];
	repeated.operatingCode = "1723";
	repeated.hasRepeat = true;
	repeated.headwaySeconds = 30.0;
	repeated.hasRepeatCount = true;
	repeated.repeatCount = 3;
	repeated.hasOperatingCodeStep = true;
	repeated.operatingCodeStep = 2;
	ok &= expect(sceneServiceOccurrenceCount(repeated, 1.0) == 3,
		"explicit repeat count overrides the duration horizon");
	ok &= expect(sceneServiceOccurrenceOperatingCode(repeated, 1) == "1723"
			&& sceneServiceOccurrenceOperatingCode(repeated, 2) == "1725"
			&& sceneServiceOccurrenceOperatingCode(repeated, 3) == "1727",
		"decimal operating-code step expands occurrences predictably");
	SceneService readableRepeat = clean.services[0];
	readableRepeat.hasRepeat = true;
	readableRepeat.headwaySeconds = 30.0;
	ok &= expect(sceneServiceOccurrenceOperatingCode(readableRepeat, 2) == "service-1-2",
		"repeated services without a step expose readable occurrence codes");
	SceneModel negativeGradient = clean;
	negativeGradient.arcs[0].gradientPercent = -100.0;
	ok &= expect(validateScene(negativeGradient).empty(), "negative arc gradient is allowed");
	SceneModel coordinateTolerance = clean;
	coordinateTolerance.nodes[1].xKm = -5e-9;
	ok &= expect(validateScene(coordinateTolerance).empty(), "native coordinate tolerance is preserved");

	SceneModel overflowingBlocks = clean;
	overflowingBlocks.blocks[0].lengthKm = 2.1;
	ok &= expect(hasCode(validateRunnableScene(overflowingBlocks), "scene.capacity.runtime"),
		"block layout overflow is rejected");
	SceneModel clippedFinalBlock = clean;
	clippedFinalBlock.blocks[1].lengthKm = 1.5;
	ok &= expect(hasCode(validateRunnableScene(clippedFinalBlock), "scene.native.block.clipped"),
		"an overlong final block retains its compatibility clipping warning");

	SceneModel twentyOneArcBlock = clean;
	std::string previousNode = twentyOneArcBlock.nodes.back().id;
	for (int index = 3; index <= 22; ++index) {
		const std::string node = "long-node-" + std::to_string(index);
		twentyOneArcBlock.nodes.push_back({node, "track-1", static_cast<double>(index), 0.0});
		twentyOneArcBlock.arcs.push_back({"long-arc-" + std::to_string(index), "track-1",
			previousNode, node, 0.0, 0.0, 35.0});
		previousNode = node;
	}
	twentyOneArcBlock.blocks[0].lengthKm = 21.0;
	ok &= expect(hasCode(validateRunnableScene(twentyOneArcBlock), "scene.capacity.runtime"),
		"block section arc capacity is enforced");

	SceneModel derivedIdCollision = clean;
	derivedIdCollision.tracks.push_back({"track-2"});
	derivedIdCollision.nodes.push_back({"node-4", "track-2", 3.0, 0.0});
	derivedIdCollision.nodes.push_back({"node-5", "track-2", 4.0, 0.0});
	derivedIdCollision.arcs.push_back({"arc-3", "track-2", "node-4", "node-5", 0.0, 0.0, 35.0});
	derivedIdCollision.blocks.push_back({"block-3", "track-2", 1.0});
	derivedIdCollision.connections.push_back({"switch-1", "node-2", "node-4", false, 0.0});
	derivedIdCollision.connections.push_back({"switch-2", "node-2", "node-4", false, 0.0});
	ok &= expect(hasCode(validateRunnableScene(derivedIdCollision), "scene.capacity.runtime"),
		"derived switch section ID collisions are rejected");

	SceneModel runtimeIdCollision = clean;
	runtimeIdCollision.blocks[0].id = "block.a";
	runtimeIdCollision.blocks[1].id = "@block.a@";
	runtimeIdCollision.routes[0].blocks = {"block.a", "@block.a@"};
	ok &= expect(hasCode(validateRunnableScene(runtimeIdCollision), "scene.capacity.runtime"),
		"runtime block ID normalization rejects collisions");
	SceneModel incompleteRuntimeIdCollision = runtimeIdCollision;
	incompleteRuntimeIdCollision.trainUnits.clear();
	incompleteRuntimeIdCollision.compositions.clear();
	incompleteRuntimeIdCollision.services.clear();
	ok &= expect(hasCode(validateRunnableScene(incompleteRuntimeIdCollision), "scene.capacity.runtime"),
		"unrelated incomplete authoring does not hide infrastructure runtime errors");

	SceneModel routeCapacity = clean;
	routeCapacity.routes[0].blocks.assign(601, "block-1");
	ok &= expect(hasCode(validateRunnableScene(routeCapacity), "scene.capacity.runtime"),
		"route block-token capacity is enforced");

	SceneModel endpointFanout = clean;
	for (int index = 0; index < 6; ++index)
		endpointFanout.connections.push_back({"fanout-" + std::to_string(index), "node-1", "node-3", false, 0.0});
	ok &= expect(hasCode(validateRunnableScene(endpointFanout), "scene.capacity.runtime"),
		"node endpoint fanout capacity is enforced");

	SceneModel dependencyFanout = clean;
	dependencyFanout.blocks[0].lengthKm = 0.2;
	dependencyFanout.blocks[1].lengthKm = 0.18;
	dependencyFanout.blockDependencies.push_back({"block-1", "block-2"});
	for (int index = 0; index < 9; ++index) {
		const std::string target = "dependency-target-" + std::to_string(index);
		dependencyFanout.blocks.push_back({target, "track-1", 0.18});
		dependencyFanout.blockDependencies.push_back({"block-1", target});
	}
	ok &= expect(hasCode(validateRunnableScene(dependencyFanout), "scene.capacity.runtime"),
		"derived and explicit dependency fanout capacity is enforced");

	// Composite route entries resolve each basic block after stripping @...@ positions.
	SceneModel composite = clean;
	composite.routes[0].blocks = {"@block-1@-0.000000/@block-2@-1.000000"};
	composite.blockDependencies.push_back({"@block-1@-0.000000/@block-2@-1.000000", "block-1"});
	composite.singleTrackRestrictions.push_back({"block-1", "block-2",
		"block-1", "block-2"});
	composite.stationBoundaries.push_back({"block-1", true, "block-2", false});
	ok &= expect(validateScene(composite).empty(), "composite route block components validate");
	composite.scenarios[0].incidents[0].target = "block-2";
	ok &= expect(validateScene(composite).empty(), "signal failure accepts a basic block target");

	SceneService counted;
	counted.id = "counted";
	counted.stops.emplace_back();
	counted.stops.front().hasPlannedDeparture = true;
	counted.stops.front().plannedDepartureSeconds = 120.0;
	counted.hasRepeat = true;
	counted.headwaySeconds = 60.0;
	counted.hasRepeatCount = true;
	counted.repeatCount = 5;
	ok &= expect(sceneServiceInWindowCount(counted, 240.0) == 2,
		"implicit first departure and exclusive horizon determine in-period count");
	counted.hasEntryTime = true;
	counted.entryTimeSeconds = -60.0;
	ok &= expect(sceneServiceScheduledEntry(counted, 2) == 0.0
			&& sceneServiceInWindowCount(counted, 180.0) == 3,
		"explicit entry wins and negative entries are outside the horizon");
	ok &= expect(sceneServiceInWindowCount(counted, 180.0,
					 {{"counted", 1}, {"counted", 2}, {"counted", 5}, {"other", 2}})
			== 1,
		"selected in-period count retains stable identity and horizon limits");
	counted.repeatCount = std::numeric_limits<int>::max();
	ok &= expect(sceneServiceInWindowCount(counted, 180.0) == 3
			&& sceneServiceOccurrenceCount(counted, 180.0) == counted.repeatCount,
		"large configured totals stay intact while window counting is bounded");

	const std::string fixtureDir = argv[1];
	ok &= expect(!hasErrors(validateSceneStructure(fixtureDir)),
		"historical fixture remains structurally loadable");
	const auto runnableFixture = validateRunnableSceneDirectory(fixtureDir);
	ok &= expect(hasCode(runnableFixture, "scene.topology.tracks.none"),
		"incomplete historical fixture fails runnable validation");
	ok &= expect(hasCode(validateSceneStructure("/no/such/scene/dir"), "scene.dir.missing"),
		"missing directory reports structural error");

	if (!ok)
		return 1;
	std::cout << "all SceneValidator tests passed\n";
	return 0;
}
