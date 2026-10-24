// Tests of the completed-run data of diagrams/CompletedRunData.h.
//
// Committed scenes run in this process, one after the other. The data captured from the run of the simulation must
// agree with it, the exports and diagram inputs must follow from it, and none of it may change when the next scene
// replaces the run. A result window opened for a scene must keep exporting that scene. On Milano_Brescia the capacity
// of a route is analysed with trains that run over parts of it, and in one of the scenes trains in opposite directions
// must call at one station of a Delays chart. Hand-made data then pins the adapters on cases that the scenes do not
// hold: missing times, repeated stops, the order of the capacity trains and shared resources.
//
//   test_completedrun SCENE_DIR...

#include "app/DispatchController.h"
#include "diagrams/AreaCapacityWindow.h"
#include "diagrams/CompletedRunData.h"
#include "diagrams/DiagramWindow.h"
#include "diagrams/RouteDiagramCoordinates.h"
#include "diagrams/RunExports.h"
#include "simulation/InitialParameters.h"
#include "simulation/Infrastructure.h"
#include "simulation/RollingStock.h"
#include "simulation/Signalling.h"
#include "simulation/Simulation.h"
#include "simulation/SimulationRun.h"
#include "util/CsvWriter.h"
#include "util/TrajectoryUtil.h"
#include "widgets/ChoiceComboBox.h"
#ifdef signals
#undef signals
#endif

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QHeaderView>
#include <QImage>
#include <QKeyEvent>
#include <QProxyStyle>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtCharts/QChart>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
	if (ok)
		return;
	++failures;
	std::cerr << "failed: " << what << "\n";
}

std::vector<std::string> lines(const std::string& text) {
	std::vector<std::string> result;
	std::istringstream stream(text);
	std::string line;
	while (std::getline(stream, line))
		result.push_back(line);
	return result;
}

// Compares two texts line by line and reports the first line that differs.
void checkSame(const std::string& what, const std::string& expected, const std::string& actual) {
	if (expected == actual)
		return;
	++failures;
	const auto expectedLines = lines(expected);
	const auto actualLines = lines(actual);
	std::size_t index = 0;
	while (index < expectedLines.size() && index < actualLines.size() && expectedLines[index] == actualLines[index])
		++index;
	const auto shown = [index](const std::vector<std::string>& all) {
		return index < all.size() ? all[index].substr(0, 300) : std::string("<end>");
	};
	std::cerr << "failed: " << what << " differs at line " << index + 1 << " (" << expectedLines.size() << " and "
			  << actualLines.size() << " lines)\n  expected: " << shown(expectedLines) << "\n  actual:   " << shown(actualLines) << "\n";
}

std::vector<std::string> dataLines(const std::string& csvText) {
	std::vector<std::string> result;
	std::size_t start = 0;
	bool header = true;
	while (start < csvText.size()) {
		const std::size_t end = csvText.find("\r\n", start);
		const std::string line = csvText.substr(start, end == std::string::npos ? std::string::npos : end - start);
		if (!header)
			result.push_back(line);
		header = false;
		if (end == std::string::npos)
			break;
		start = end + 2;
	}
	return result;
}

class CoutSilencer {
public:
	CoutSilencer() : m_saved(std::cout.rdbuf(m_sink.rdbuf())) {}
	~CoutSilencer() { std::cout.rdbuf(m_saved); }

private:
	std::ostringstream m_sink;
	std::streambuf* m_saved;
};

// --- Text of values, exact to the last digit ---

std::string num(double value) {
	if (std::isnan(value))
		return "nan";
	char buffer[40];
	std::snprintf(buffer, sizeof buffer, "%.17g", value);
	return buffer;
}

std::string describe(const BlockingTimeDiagramInput& o) {
	std::ostringstream text;
	text << o.blockId << '|' << num(o.startOccTime) << '|' << num(o.endOccTime) << '|' << num(o.posStart) << '|'
		 << num(o.posEnd) << '|' << o.switchName << '|' << o.stationName << '|' << o.isComplete << '|' << o.capacityCritical;
	for (double t : {o.startApproachTime, o.startRunTime, o.endRunTime, o.endClearTime, o.setupTime, o.sightReactionTime, o.releaseTime, o.runTimeMargin})
		text << '|' << num(t);
	return text.str();
}

std::string describe(const BlockingTimeDiagramSegment& s) {
	std::ostringstream text;
	text << s.trainName << '|' << s.blockId << '|' << num(s.startTime) << '|' << num(s.endTime) << '|' << num(s.midPositionKm)
		 << '|' << num(s.startPositionKm) << '|' << num(s.endPositionKm) << '|' << num(s.originalStartTime) << '|'
		 << num(s.originalEndTime);
	for (double t : {s.startApproachTime, s.startRunTime, s.endRunTime, s.endClearTime, s.setupTime, s.sightReactionTime, s.releaseTime, s.runTimeMargin})
		text << '|' << num(t);
	text << '|' << static_cast<int>(s.style) << '|' << s.capacityCritical << '|' << s.referenceSection << '|' << s.referenceNote;
	return text.str();
}

std::string describe(const RouteDiagramPath& path) {
	std::ostringstream text;
	text << "path " << path.id << '\n';
	for (const auto& node : path.nodes)
		text << "  " << node.nodeId << '|' << node.stationId << '|' << node.stationName << '|' << num(node.positionKm) << '\n';
	return text.str();
}

std::string describe(const BlockingTimeDiagramData& d, const QStringList& visible) {
	std::ostringstream text;
	text << describe(d.referencePath) << "omitted " << d.omitted << '\n';
	for (const auto& s : d.segments)
		text << "segment " << describe(s) << '\n';
	for (const auto& s : d.unmapped)
		text << "unmapped " << describe(s) << '\n';
	for (const auto& e : d.extents)
		text << "extent " << e.order << '|' << e.id << '|' << num(e.startKm) << '|' << num(e.endKm) << '|' << e.geometryValid << '|' << e.issue << '\n';
	for (const auto& group : d.plannedLines) {
		text << "planned line\n";
		for (const auto& r : group)
			text << "  " << r.trainName << '|' << r.stationName << '|' << r.eventType << '|' << num(r.time) << '|' << num(r.positionKm) << '\n';
	}
	for (const auto& line : d.trajectories) {
		text << "trajectory " << line.trainId << '\n';
		for (const auto& point : line.points)
			text << "  " << num(point.first) << '|' << num(point.second) << '\n';
	}
	text << buildBlockingTimeCsv(visible, d.segments, d.plannedReferences, d.trajectoryRows, d.extents, d.unmapped);
	return text.str();
}

std::string describe(const std::optional<BlockingTimeDiagramData>& d, const QStringList& visible) {
	return d ? describe(*d, visible) : std::string("no diagram\n");
}

std::string describe(const RouteDiagramData& d) {
	std::ostringstream text;
	text << describe(d.referencePath);
	for (const auto& series : d.series) {
		text << "series " << static_cast<int>(series.kind) << '|' << series.trainId << '|' << series.trainIndex << '|' << series.event
			 << '|' << series.planned << '|' << series.routeId << '\n';
		for (const auto& point : series.points)
			text << "  " << num(point.positionKm) << '|' << num(point.seconds) << '|' << point.station << '|' << point.journey << '|' << point.call << '\n';
	}
	for (const auto& row : d.csvRows)
		text << csv::makeRow(row) << '\n';
	return text.str();
}

std::string describe(const std::optional<RouteDiagramData>& d) {
	return d ? describe(*d) : std::string("no diagram\n");
}

// --- Running a scene ---

struct CapturedRun {
	std::string scene;
	SceneLoadResult loaded;
	CompletedRunData data;
};

bool runScene(const std::string& sceneDir, const std::string& outputDir, CapturedRun& run) {
	run.scene = sceneDir.substr(sceneDir.find_last_of("/\\") + 1);
	run.loaded = loadScene(sceneDir);
	if (hasErrors(run.loaded.diagnostics)) {
		std::cerr << "cannot load " << sceneDir << "\n";
		return false;
	}
	initial_variables.GUI = 0;
	initial_variables.PAX_GUI = false;
	initial_variables.exportDetailedTrajectories = false;
	initial_variables.OutputMainFolder = outputDir;
	initial_variables.InputMainFolder.clear();

	CoutSilencer silencer;
	const std::vector<SceneDiagnostic> diagnostics = simulation.prepareScene(run.loaded.scene);
	if (hasErrors(diagnostics)) {
		std::cerr << "cannot prepare " << sceneDir << "\n";
		return false;
	}
	simulation.setSnapshotsEnabled(false);
	simulation.runSimulation();
	if (simulation.run().numTrains <= 0) {
		std::cerr << sceneDir << " ran no train\n";
		return false;
	}
	// Assignment_Gvc_Gdg_Ut has no reversed route. Paimpol has 8 of its 15, those that run against the geo X coordinate.
	// Marking every other route instead shows the capture both ways.
	std::vector<Route>& routes = simulation.mutableRun().infra.routes;
	for (std::size_t r = 0; r < routes.size(); ++r)
		routes[r].reversed_direction = r % 2 == 0;
	RunProvenance provenance;
	provenance.caseName = run.loaded.scene.name;
	run.data = captureCompletedRun(simulation.run(), &run.loaded.scene, initial_variables.startingSimulationTime, provenance);
	return true;
}

// --- The data against the simulation, and what follows from it ---

void compareCapture(const CapturedRun& run) {
	const std::string scene = run.scene;
	const CompletedRunData& data = run.data;
	check(data.settings.timestepSeconds == timestep, scene + ": timestep");
	check(data.settings.horizonSteps == initial_variables.times, scene + ": horizon");
	check(data.settings.horizonSeconds() == initial_variables.times * timestep, scene + ": horizon in seconds");
	check(data.settings.startOffsetSeconds == initial_variables.startingSimulationTime, scene + ": start offset");
	check(data.settings.caseName == run.loaded.scene.name && data.provenance.caseName == run.loaded.scene.name, scene + ": case name");
	check(static_cast<int>(data.trains.size()) == simulation.run().numTrains, scene + ": train count");
	check(static_cast<int>(data.results.trains.size()) == simulation.run().numTrains, scene + ": result rows");
	// The capture reads the run that it is given, whatever the run of the process holds.
	const auto emptyRun = std::make_unique<SimulationRun>();
	const CompletedRunData fromEmptyRun = captureCompletedRun(*emptyRun, &run.loaded.scene, 0, RunProvenance());
	check(fromEmptyRun.trains.empty() && fromEmptyRun.routes.empty() && fromEmptyRun.results.trains.empty(),
		scene + ": capture of an empty run");
	check(!data.timetable.empty(), scene + ": timetable rows");

	check(data.routes.size() == simulation.run().infra.routes.size(), scene + ": route count");
	for (std::size_t r = 0; r < data.routes.size() && r < simulation.run().infra.routes.size(); ++r) {
		const CompletedRoute& route = data.routes[r];
		check(route.id == simulation.run().infra.routes[r].ID
				&& route.corridor == simulation.run().infra.routes[r].corridor,
			scene + ": route identity " + std::to_string(r));
		check(route.reversedDirection == (r % 2 == 0), scene + ": route direction " + std::to_string(r));
		const auto inputs = blockSectionInputsForRoute(simulation.run().infra.routes[r]);
		bool same = inputs.size() == route.blocks.size();
		for (std::size_t b = 0; same && b < inputs.size(); ++b)
			same = inputs[b].id == route.blocks[b].id && inputs[b].startKm == route.blocks[b].startKm && inputs[b].endKm == route.blocks[b].endKm;
		check(same, scene + ": route blocks " + std::to_string(r));
		checkSame(scene + ": route path " + std::to_string(r), describe(routeDiagramPath(simulation.run().infra.routes[r], &run.loaded.scene)),
			describe(route.path));
	}

	for (int i = 0; i < simulation.run().numTrains && i < static_cast<int>(data.trains.size()); ++i) {
		const Train& train = simulation.run().trains[i];
		const CompletedTrain& captured = data.trains[i];
		const std::string who = scene + ": " + train.trainDescription;
		check(captured.trainId == train.trainDescription && captured.operatingCode == train.operatingCode
				&& captured.serviceId == train.serviceId && captured.serviceOccurrence == train.serviceOccurrence
				&& captured.routeIndex == train.indexOfRoute && captured.routeId == train.TrainRouteID
				&& captured.scheduledDepartureTime == train.scheduled_departure_time && captured.runStartTime == train.RunStartTime,
			who + ": identity");

		check(captured.blockTimes.size() == static_cast<std::size_t>(train.N_BlockSections), who + ": block time count");
		for (int b = 0; b < train.N_BlockSections && b < static_cast<int>(captured.blockTimes.size()); ++b) {
			const BlockingTimes& source = train.BlockTime[b];
			BlockingTimeDiagramInput expected;
			expected.blockId = source.BlockID;
			expected.startOccTime = source.StartOccTime;
			expected.endOccTime = source.EndOccTime;
			expected.posStart = source.PosStart;
			expected.posEnd = source.PosEnd;
			expected.switchName = source.SwitchName;
			expected.stationName = source.stationName;
			expected.isComplete = source.IsComplete;
			expected.startApproachTime = source.StartApproachTime;
			expected.startRunTime = source.StartRunTime;
			expected.endRunTime = source.EndRunTime;
			expected.endClearTime = source.EndClearTime;
			expected.setupTime = source.setupTime;
			expected.sightReactionTime = source.sightReacTime;
			expected.releaseTime = source.ReleaseTime;
			expected.runTimeMargin = source.RunTimeMargin;
			checkSame(who + ": block time " + std::to_string(b), describe(expected), describe(captured.blockTimes[b]));
		}

		const int stopCount = std::min(train.numStations, static_cast<int>(Train::kMaxTimetableStations));
		check(static_cast<int>(captured.stops.size()) == stopCount, who + ": stop count");
		std::vector<std::vector<std::string>> allowedSets = {{}, {"no such block"}};
		if (captured.routeIndex >= 0 && captured.routeIndex < static_cast<int>(simulation.run().infra.routes.size())) {
			const Route& route = simulation.run().infra.routes[captured.routeIndex];
			for (int b = 0; b < route.N_Block_Sections; ++b)
				allowedSets.push_back({route.sequence_of_block_sections[b].ID});
		}
		for (int s = 0; s < stopCount && s < static_cast<int>(captured.stops.size()); ++s) {
			const CompletedStop& stop = captured.stops[s];
			const std::string at = who + " stop " + std::to_string(s);
			check(stop.station == train.stationNameForArrivalStats(s) && stop.nodeId == train.Stations[s].sceneNodeId
					&& stop.routeX == train.Stations[s].X && stop.scheduledArrival == train.ScheduledArrivals[s]
					&& stop.scheduledDeparture == train.ScheduledDepartures[s],
				at + ": fields");
			check(stop.routePositionMeters == train.stationRoutePositionMeters(s), at + ": route position");
			for (const auto& allowed : allowedSets)
				if (stop.onRoute(allowed) != train.stationIsOnRoute(s, allowed)) {
					check(false, at + ": on route, " + std::to_string(allowed.size()) + " blocks allowed");
					break;
				}
		}

		const CompletedTrajectory& trajectory = captured.trajectory;
		const auto segments = train.earliestActiveTrajectoryIndex < 0
			? std::vector<TrajectorySegment>()
			: validTrajectorySegments(train.instant_spatial_position, train.earliestActiveTrajectoryIndex, train.End_Time);
		bool sameSegments = segments.size() == trajectory.segments.size();
		for (std::size_t k = 0; sameSegments && k < segments.size(); ++k)
			sameSegments = segments[k].first == trajectory.segments[k].first && segments[k].last == trajectory.segments[k].last;
		check(sameSegments, who + ": trajectory segments");
		const auto sameSample = [](const std::vector<double>& series, const std::optional<double>& stored, int sample) {
			const bool present = sample >= 0 && sample < static_cast<int>(series.size());
			return present ? stored && *stored == series[static_cast<std::size_t>(sample)] : !stored;
		};
		bool sameSamples = true;
		for (const TrajectorySegment& segment : segments)
			for (int t = segment.first; t <= segment.last; ++t) {
				sameSamples = sameSamples && sameSample(train.instant_spatial_position, trajectory.at(trajectory.position, t), t)
					&& sameSample(train.instant_train_speed, trajectory.at(trajectory.speed, t), t)
					&& sameSample(train.instant_train_power_consumption, trajectory.at(trajectory.power, t), t)
					&& sameSample(train.instant_train_energy_consumption, trajectory.at(trajectory.energy, t), t)
					&& sameSample(train.instant_train_tractive_effort, trajectory.at(trajectory.effort, t), t);
				const std::uint32_t block = trajectory.blockAt(t);
				const bool blockPresent = t < static_cast<int>(train.instant_block_section_occupied.size());
				sameSamples = sameSamples
					&& (blockPresent ? block != CompletedTrajectory::kNoBlock && data.blockNames[block] == train.instant_block_section_occupied[t]
									 : block == CompletedTrajectory::kNoBlock);
			}
		check(sameSamples, who + ": trajectory samples");
	}
}

// Whether each row of a CSV text starts with a train of the list. Rows without a train are the sections of a route.
bool rowsOnlyOf(const std::string& text, const QStringList& trainIds) {
	for (const std::string& line : dataLines(text)) {
		if (line.empty() || line.front() == ',')
			continue;
		const std::string train = line.substr(0, line.find(','));
		if (!trainIds.contains(QString::fromStdString(train)))
			return false;
	}
	return true;
}

// The samples that the trajectory export lists for the trains of the list: those in the valid ranges of the simulation.
std::size_t trajectorySamples(const QStringList& trainIds) {
	std::size_t count = 0;
	for (int i = 0; i < simulation.run().numTrains; ++i) {
		const Train& train = simulation.run().trains[i];
		if (!trainIds.contains(QString::fromStdString(train.trainDescription)) || train.earliestActiveTrajectoryIndex < 0)
			continue;
		for (const auto& segment : validTrajectorySegments(train.instant_spatial_position, train.earliestActiveTrajectoryIndex, train.End_Time))
			count += static_cast<std::size_t>(segment.last - segment.first + 1);
	}
	return count;
}

void checkExports(const CapturedRun& run) {
	const std::string scene = run.scene;
	const CompletedRunData& data = run.data;
	const QStringList ids = trainIdsFrom(data);
	bool sameIds = static_cast<int>(ids.size()) == simulation.run().numTrains;
	for (int i = 0; sameIds && i < simulation.run().numTrains; ++i)
		sameIds = ids[i].toStdString() == simulation.run().trains[i].trainDescription;
	check(sameIds, scene + ": train ids");

	std::vector<QStringList> selections = {ids, QStringList(), QStringList{"no such train"}};
	if (!ids.isEmpty()) {
		selections.push_back(QStringList{ids.first()});
		selections.push_back(QStringList{ids.last()});
		selections.push_back(QStringList{ids.first(), ids.last()});
	}
	for (std::size_t k = 0; k < selections.size(); ++k) {
		const std::string label = scene + ", selection " + std::to_string(k);
		const std::string trajectory = trajectoryCsvFrom(data, selections[k]);
		const std::string timetable = timetableCsvFrom(data, selections[k]);
		check(rowsOnlyOf(trajectory, selections[k]) && rowsOnlyOf(timetable, selections[k])
				&& rowsOnlyOf(blockingTimeCsvFrom(data, selections[k]), selections[k]),
			label + ": the exports list the selected trains only");
		check(dataLines(trajectory).size() == trajectorySamples(selections[k]), label + ": trajectory.csv has the valid samples");
		std::size_t timetableRows = 0;
		for (const TimetableResultRow& timetableRow : data.timetable)
			timetableRows += selections[k].contains(QString::fromStdString(timetableRow.trainId)) ? 1 : 0;
		check(dataLines(timetable).size() == timetableRows, label + ": timetable.csv has the rows of the run");
	}
	check(!trajectoryCsvFrom(data, ids).empty(), scene + ": trajectory.csv has rows");
	check(!timetableCsvFrom(data, ids).empty(), scene + ": timetable.csv has rows");
	check(!blockingTimeCsvFrom(data, ids).empty(), scene + ": blocking_time.csv has rows");
	check(!blockTimesFrom(data).empty(), scene + ": blocking-time segments exist");
	const BlockingTimeScope whole = defaultBlockingTimeScope(initial_variables.times * timestep);
	const BlockingTimeScope scope = defaultBlockingTimeScopeFrom(data);
	check(whole.startTime == scope.startTime && whole.endTime == scope.endTime && whole.routeIndex == scope.routeIndex
			&& whole.trainIds == scope.trainIds && whole.blockIds == scope.blockIds && whole.sectionIndices == scope.sectionIndices,
		scene + ": default blocking-time scope");
}

// What the scope dialog offers must be what the simulation holds.
void compareScopeChoices(const CapturedRun& run) {
	const std::string scene = run.scene;
	const BlockingTimeScopeChoices choices = blockingTimeScopeChoicesFrom(run.data);
	check(choices.caseEndSeconds == initial_variables.times * timestep && choices.stepSeconds == timestep, scene + ": scope choices extent");
	check(choices.routes.size() == simulation.run().infra.routes.size(), scene + ": scope choice routes");
	for (std::size_t r = 0; r < choices.routes.size() && r < simulation.run().infra.routes.size(); ++r) {
		const Route& route = simulation.run().infra.routes[r];
		std::vector<std::string> blockIds;
		for (int b = 0; b < route.N_Block_Sections; ++b)
			blockIds.push_back(route.sequence_of_block_sections[b].ID);
		check(choices.routes[r].id == route.ID && choices.routes[r].corridor == route.corridor && choices.routes[r].blockIds == blockIds,
			scene + ": scope choice route " + std::to_string(r));
	}
}

std::vector<BlockingTimeScope> blockingScopes(const CompletedRunData& data) {
	std::vector<BlockingTimeScope> scopes;
	const BlockingTimeScope whole = defaultBlockingTimeScope(initial_variables.times * timestep);
	scopes.push_back(whole);
	BlockingTimeScope window = whole;
	window.trainIds = {simulation.run().trains[0].trainDescription};
	window.startTime = whole.endTime * 0.25;
	window.endTime = whole.endTime * 0.75;
	scopes.push_back(window);
	BlockingTimeScope late = whole;
	late.startTime = whole.endTime * 0.5;
	scopes.push_back(late);
	for (const auto& used : usedRouteReferencesFrom(data)) {
		const Route& route = simulation.run().infra.routes[used.first];
		BlockingTimeScope scope = whole;
		scope.routeIndex = used.first;
		for (int i = 0; i < simulation.run().numTrains; ++i)
			if (simulation.run().trains[i].indexOfRoute == used.first || simulation.run().trains[i].TrainRouteID == route.ID)
				scope.trainIds.push_back(simulation.run().trains[i].trainDescription);
		const int count = route.N_Block_Sections;
		for (const auto& range : std::vector<std::pair<int, int>>{{0, count - 1}, {0, std::min(2, count - 1)}, {count / 2, count - 1}, {count - 1, 0}}) {
			scope.sectionIndices = blockSectionRangeIndices(range.first, range.second, count);
			scope.blockIds.clear();
			for (int index : scope.sectionIndices)
				scope.blockIds.push_back(route.sequence_of_block_sections[index].ID);
			scopes.push_back(scope);
		}
		BlockingTimeScope noTrains = scope;
		noTrains.trainIds.clear();
		scopes.push_back(noTrains);
		BlockingTimeScope routeOnly = scope;
		routeOnly.sectionIndices.clear();
		routeOnly.blockIds.clear();
		routeOnly.startTime = whole.endTime * 0.1;
		routeOnly.endTime = whole.endTime * 0.9;
		scopes.push_back(routeOnly);
	}
	return scopes;
}

void checkBlockingDiagrams(const CapturedRun& run) {
	const auto scopes = blockingScopes(run.data);
	std::size_t withSegments = 0, withPlanned = 0, withTrajectory = 0, withSections = 0;
	for (std::size_t k = 0; k < scopes.size(); ++k) {
		const std::string label = run.scene + ": blocking-time diagram of scope " + std::to_string(k);
		const auto diagram = blockingTimeDiagramData(run.data, scopes[k]);
		if (!diagram)
			continue;
		check(diagram->timeRange.first == scopes[k].startTime && diagram->timeRange.second == scopes[k].endTime,
			label + ": the time axis spans the interval of the scope");
		std::size_t points = 0;
		bool inScope = true;
		for (const BlockingTimeTrajectory& line : diagram->trajectories)
			for (const auto& point : line.points) {
				++points;
				inScope = inScope && point.second >= scopes[k].startTime && point.second <= scopes[k].endTime;
			}
		check(points == diagram->trajectoryRows.size() && inScope, label + ": the recorded samples are those of the scope");
		bool ofScope = true;
		for (const BlockingTimeDiagramSegment& segment : diagram->segments)
			ofScope = ofScope && (scopes[k].trainIds.empty() || std::find(scopes[k].trainIds.begin(), scopes[k].trainIds.end(), segment.trainName) != scopes[k].trainIds.end());
		check(ofScope, label + ": the envelopes are those of the trains of the scope");
		withSegments += diagram->segments.empty() ? 0 : 1;
		withPlanned += diagram->plannedReferences.empty() ? 0 : 1;
		withTrajectory += diagram->trajectories.empty() ? 0 : 1;
		withSections += diagram->extents.empty() ? 0 : 1;
	}
	check(withSegments > 1 && withPlanned > 0 && withTrajectory > 0 && withSections > 0,
		run.scene + ": the scopes cover envelopes, planned references, trajectories and sections");
}

CapacityAnalysisScope capacityScopeOf(const CompletedRunData& data, int routeIndex, const std::vector<int>& sections) {
	CapacityAnalysisScope scope;
	scope.routeIndex = routeIndex;
	for (int index : sections) {
		scope.sectionIndices.push_back(index);
		scope.blockIds.push_back(data.routes[static_cast<std::size_t>(routeIndex)].blocks[static_cast<std::size_t>(index)].id);
	}
	return scope;
}

void checkCapacity(const CapturedRun& run) {
	const std::string scene = run.scene;
	const CompletedRunData& data = run.data;
	std::size_t pairs = 0;
	for (int r = 0; r < static_cast<int>(data.routes.size()); ++r) {
		const int count = static_cast<int>(data.routes[static_cast<std::size_t>(r)].blocks.size());
		std::vector<CapacityAnalysisScope> scopes;
		for (int b = 0; b < count; ++b)
			scopes.push_back(capacityScopeOf(data, r, {b}));
		if (count > 1)
			scopes.push_back(capacityScopeOf(data, r, blockSectionRangeIndices(0, count - 1, count)));
		for (std::size_t k = 0; k < scopes.size(); ++k) {
			const std::string label = scene + ": capacity scope " + std::to_string(r) + "/" + std::to_string(k);
			const auto inputs = capacityInputsFrom(data, scopes[k]);
			// Each train of the run with an occupation of a block of the scope is listed once, in the order of
			// capacityTrainOrder, and holds only occupations of blocks of the scope.
			std::vector<CapacityAnalysisTrain> inRunOrder;
			for (const CompletedTrain& train : data.trains)
				if (CapacityAnalysisTrain input = capacityInputFrom(train, scopes[k]); !input.occupations.empty())
					inRunOrder.push_back(std::move(input));
			const auto idsOf = [](const std::vector<CapacityAnalysisTrain>& trains) {
				std::vector<std::string> ids;
				for (const CapacityAnalysisTrain& train : trains)
					ids.push_back(train.runtimeId);
				return ids;
			};
			bool ordered = idsOf(inputs) == idsOf(capacityTrainOrder(inRunOrder)), occupied = true;
			for (const CapacityAnalysisTrain& input : inputs) {
				occupied = occupied && !input.occupations.empty();
				for (const BlockingTimeDiagramInput& occupation : input.occupations)
					occupied = occupied && std::any_of(scopes[k].blockIds.begin(), scopes[k].blockIds.end(), [&occupation](const std::string& block) {
						return shareBlockingTimeResource(occupation.blockId, block);
					});
			}
			check(ordered && occupied, label + " inputs");
			pairs += inputs.size() > 1 ? 1 : 0;
			const QString routeLabel = capacityScopeLabelFrom(data, scopes[k]);
			check(routeLabel.startsWith(QString::fromStdString(data.routes[static_cast<std::size_t>(r)].id)), label + " label");
		}
	}
	check(capacityScopeLabelFrom(data, CapacityAnalysisScope()).toStdString() == "no selected route", scene + ": label without a route");

	const CapacityAnalysisScope first = firstCapacityScopeWithPairFrom(data);
	if (first.routeIndex < 0) {
		std::cout << scene << ": no scope with two trains, so no capacity analysis\n";
		return;
	}
	check(pairs > 0, scene + ": scopes with a pair");
	std::vector<std::string> trainsOfFirst;
	for (const CapacityAnalysisTrain& input : capacityInputsFrom(data, first))
		trainsOfFirst.push_back(input.runtimeId);
	check(trainsOfFirst.size() > 1 && trainsOfFirst == first.occurrenceIds, scene + ": the first scope with a pair holds its trains");
	check(first.repeatFirstTrain && !first.blockIds.empty()
			&& first.blockIds.size() == data.routes[static_cast<std::size_t>(first.routeIndex)].blocks.size(),
		scene + ": the first scope with a pair is a whole route, closed by the first train one period later");
	CapacityAnalysisScope scope = first;
	std::vector<CapacityAnalysisScope> analysed = {scope};
	scope.repeatFirstTrain = false;
	scope.cycleEndOccurrenceId = scope.occurrenceIds.back();
	analysed.push_back(scope);
	scope.occurrenceIds = {scope.occurrenceIds.back(), "no such train", scope.occurrenceIds.front()};
	scope.cycleEndOccurrenceId = scope.occurrenceIds.front();
	analysed.push_back(scope);
	bool anyCsv = false;
	for (const CapacityAnalysisScope& analysis : analysed)
		anyCsv = anyCsv || !capacityCsvFrom(data, analysis).empty();
	check(anyCsv, scene + ": a capacity_analysis.csv exists");
	const CapacityAnalysisResult whole = capacityAnalysisFrom(data, first);
	check(whole.analyzable && whole.cycleTime > 0.0 && whole.cycleEndIdentity == capacityNextPeriodIdentity(whole.firstIdentity),
		scene + ": the whole first route is analysed, closed by the first train one period later");
}

// On Milano_Brescia, route8 has 50 blocks, and 30 trains run over parts of it. All of them are analysed over the whole
// route, each from where it enters the route, and the compressed diagram shows every block.
void checkPartialTrains(const CapturedRun& run) {
	if (run.scene != "Milano_Brescia")
		return;
	const CompletedRunData& data = run.data;
	const auto route8 = std::find_if(data.routes.begin(), data.routes.end(), [](const CompletedRoute& route) { return route.id == "route8"; });
	check(route8 != data.routes.end() && route8->blocks.size() == 50, "Milano_Brescia: route8 has 50 blocks");
	if (route8 == data.routes.end())
		return;
	const int routeIndex = static_cast<int>(route8 - data.routes.begin());
	const CapacityAnalysisScope scope = capacityRangeScopeFrom(data, routeIndex, 0, 49);
	check(scope.blockIds.size() == 50 && scope.occurrenceIds.size() == 30 && scope.repeatFirstTrain,
		"Milano_Brescia: the whole of route8 holds 30 trains (" + std::to_string(scope.occurrenceIds.size()) + ")");
	check(capacityScopeIssueFrom(data, scope).empty(), "Milano_Brescia: route8 can be analysed as a whole");
	const CapacityAnalysisResult result = capacityAnalysisFrom(data, scope);
	check(result.analyzable && result.compression.size() == 31 && result.cycleTime > 0.0
			&& result.cycleEndIdentity == capacityNextPeriodIdentity(scope.occurrenceIds.front()),
		"Milano_Brescia: route8 with its 30 trains and the first one a period later is analysed");
	const auto diagram = compressedBlockingTimeDiagramData(data, scope, result);
	std::set<std::string> drawn;
	if (diagram)
		for (const auto& segment : diagram->segments)
			drawn.insert(segment.trainName);
	std::string missing;
	for (const std::string& identity : result.trainIdentities)
		if (!drawn.count(identity))
			missing += " " + identity;
	check(diagram && diagram->extents.size() == 50 && drawn.size() == 31 && drawn.count(result.cycleEndIdentity) == 1
			&& diagram->unmapped.empty(),
		"Milano_Brescia: the compressed diagram has the 50 blocks and the occupations of 30 trains and the copy ("
			+ std::to_string(drawn.size()) + " trains, missing" + missing + ")");
}

// The routes that the trains of the simulation run on, in order of first use.
std::vector<std::pair<int, std::string>> routesOfTrains() {
	std::vector<std::pair<int, std::string>> used;
	for (int i = 0; i < simulation.run().numTrains; ++i) {
		const int index = simulation.run().trains[i].indexOfRoute;
		if (index >= 0 && index < static_cast<int>(simulation.run().infra.routes.size())
			&& std::none_of(used.begin(), used.end(), [index](const auto& route) { return route.first == index; }))
			used.emplace_back(index, simulation.run().infra.routes[index].ID);
	}
	return used;
}

void checkRouteDiagrams(const CapturedRun& run) {
	const std::string scene = run.scene;
	const auto expectedUsed = routesOfTrains();
	check(expectedUsed == usedRouteReferencesFrom(run.data), scene + ": used routes");
	check(!expectedUsed.empty(), scene + ": routes are used");
	// Every sample and every timetable event of a train on a known route is a row of the export, whether it projects or not.
	std::size_t samples = 0, timetableRows = 0;
	for (int i = 0; i < simulation.run().numTrains; ++i) {
		const Train& train = simulation.run().trains[i];
		if (train.indexOfRoute < 0 || train.indexOfRoute >= static_cast<int>(simulation.run().infra.routes.size()))
			continue;
		if (train.earliestActiveTrajectoryIndex >= 0)
			for (const auto& segment : validTrajectorySegments(train.instant_spatial_position, train.earliestActiveTrajectoryIndex, train.End_Time))
				samples += static_cast<std::size_t>(segment.last - segment.first + 1);
		for (const TimetableResultRow& timetableRow : run.data.timetable)
			timetableRows += timetableRow.trainId == train.trainDescription ? 1 : 0;
	}
	std::size_t withSeries = 0;
	for (const auto& used : expectedUsed)
		for (bool timetable : {false, true}) {
			const std::string label = scene + ": " + (timetable ? "timetable graph" : "train paths") + " on " + used.second;
			const auto diagram = routeDiagramData(run.data, timetable, used.first);
			check(diagram.has_value(), label);
			if (!diagram)
				continue;
			check(diagram->referencePath.id == used.second, label + ": reference route");
			check(diagram->csvRows.size() == (timetable ? 4 * timetableRows : samples), label + ": export rows");
			withSeries += diagram->series.empty() ? 0 : 1;
		}
	check(withSeries >= 2, scene + ": route diagrams have lines");
	check(!routeDiagramData(run.data, false, -1) && !routeDiagramData(run.data, true, static_cast<int>(run.data.routes.size())),
		scene + ": no diagram without a reference route");
}

// The Delays chart on each route that a train runs on: a point for each arrival delay of the timetable at a station of
// the route, at a node of that station, in journey order, and every train drawn or listed without a call there. Gives
// the number of stations at which trains that run in opposite directions have a point, each at one position for all.
std::size_t checkDelayDiagrams(const CapturedRun& run) {
	std::size_t shared = 0;
	for (const auto& used : usedRouteReferencesFrom(run.data)) {
		const std::string label = run.scene + ": delays on " + used.second;
		const auto diagram = delayDiagramData(run.data, used.first);
		check(diagram && diagram->referencePath.id == used.second, label);
		if (!diagram)
			continue;
		check(diagram->series.size() + diagram->withoutCalls.size() == run.data.trains.size(), label + ": every train is drawn or listed");
		std::map<std::string, std::set<double>> stations;
		for (const RouteDiagramNode& node : diagram->referencePath.nodes)
			if (!node.stationName.empty())
				stations[node.stationName].insert(node.positionKm);
		std::size_t delays = 0, points = 0;
		for (const TimetableResultRow& timetableRow : run.data.timetable)
			delays += timetableRow.arrivalDelaySeconds.available && stations.count(timetableRow.stationId) ? 1 : 0;
		// The position of each point at each station, with the direction of its train: 1 up the route, -1 down.
		std::map<std::string, std::vector<std::pair<double, double>>> calls;
		for (const DelayDiagramSeries& series : diagram->series) {
			int journey = 0;
			for (const DelayDiagramPoint& point : series.points) {
				const std::string where = label + ": " + series.trainId + " stop " + std::to_string(point.journey);
				const auto station = stations.find(point.station);
				check(station != stations.end() && station->second.count(point.positionKm), where + " lies at a node of " + point.station);
				check(point.journey > journey, where + " is in journey order");
				journey = point.journey;
				const auto timetableRow = std::find_if(run.data.timetable.begin(), run.data.timetable.end(), [&](const TimetableResultRow& candidate) {
					return candidate.trainId == series.trainId && candidate.journeyIndex == point.journey;
				});
				check(timetableRow != run.data.timetable.end() && timetableRow->arrivalDelaySeconds.value == point.delaySeconds
						&& timetableRow->callIndex == point.call,
					where + " has the arrival delay of the timetable");
				++points;
			}
			const auto& line = series.points;
			if (line.size() >= 2 && line.front().positionKm != line.back().positionKm)
				for (const DelayDiagramPoint& point : line)
					calls[point.station].emplace_back(point.positionKm, line.back().positionKm > line.front().positionKm ? 1.0 : -1.0);
		}
		check(points == delays, label + ": a point for each arrival delay at a station of the route");
		for (const auto& station : calls) {
			const auto& drawn = station.second;
			if (std::none_of(drawn.begin(), drawn.end(), [](const auto& call) { return call.second > 0; })
				|| std::none_of(drawn.begin(), drawn.end(), [](const auto& call) { return call.second < 0; }))
				continue;
			++shared;
			check(std::all_of(drawn.begin(), drawn.end(), [&drawn](const auto& call) { return call.first == drawn.front().first; }),
				label + ": trains in both directions are drawn at one position of " + station.first);
		}
	}
	check(!delayDiagramData(run.data, -1) && !delayDiagramData(run.data, static_cast<int>(run.data.routes.size())),
		run.scene + ": no Delays chart without a reference route");
	return shared;
}

// The first run has to give the same text after the second run has replaced the simulation state.
std::string exportsOf(const CompletedRunData& data) {
	const QStringList ids = trainIdsFrom(data);
	std::string text = trajectoryCsvFrom(data, ids) + timetableCsvFrom(data, ids) + blockingTimeCsvFrom(data, ids)
		+ capacityCsvFrom(data, firstCapacityScopeWithPairFrom(data));
	for (const auto& used : usedRouteReferencesFrom(data))
		for (bool timetable : {false, true})
			text += describe(routeDiagramData(data, timetable, used.first));
	text += describe(blockingTimeDiagramData(data, defaultBlockingTimeScopeFrom(data)), ids);
	return text;
}

// A window with the export of a run, as the application opens one.
std::unique_ptr<DiagramWindow> openWindow(const CompletedRunData& data, CompletedRunCsv csv, const QString& fileName) {
	const auto run = std::make_shared<const CompletedRunData>(data);
	auto window = std::make_unique<DiagramWindow>(fileName);
	window->setChart(new QChart());
	window->setCsvProvider(csvProviderFrom(run, csv), fileName, trainIdsFrom(*run));
	return window;
}

// The windows that were opened for one scene, with what each exported at that time.
struct OpenWindows {
	std::unique_ptr<DiagramWindow> timetable;
	std::unique_ptr<DiagramWindow> trajectory;
	std::string timetableCsv;
	std::string trajectoryCsv;
};

OpenWindows openWindows(const CapturedRun& run) {
	OpenWindows windows;
	windows.timetable = openWindow(run.data, &timetableCsvFrom, "timetable.csv");
	windows.trajectory = openWindow(run.data, &trajectoryCsvFrom, "trajectory.csv");
	windows.timetableCsv = windows.timetable->csvContent();
	windows.trajectoryCsv = windows.trajectory->csvContent();
	const QStringList ids = trainIdsFrom(run.data);
	check(!windows.timetableCsv.empty() && !windows.trajectoryCsv.empty(), run.scene + ": the windows export rows");
	checkSame(run.scene + ": timetable window and timetable.csv", timetableCsvFrom(run.data, ids), windows.timetableCsv);
	checkSame(run.scene + ": trajectory window and trajectory.csv", trajectoryCsvFrom(run.data, ids), windows.trajectoryCsv);
	return windows;
}

// --- Hand-made data ---

BlockingTimeDiagramInput occupation(const std::string& block, double start, double end, double posStart, double posEnd,
	double startRun = -1.0) {
	BlockingTimeDiagramInput input;
	input.blockId = block;
	input.startOccTime = start;
	input.endOccTime = end;
	input.posStart = posStart;
	input.posEnd = posEnd;
	input.isComplete = true;
	input.startRunTime = startRun;
	input.endClearTime = end;
	return input;
}

CompletedStop stopAt(const std::string& station, const std::string& node, double routeX, double arrival, double departure,
	std::vector<std::string> routeBlockIds = {}) {
	CompletedStop stop;
	stop.station = station;
	stop.nodeId = node;
	stop.routeX = routeX;
	stop.routePositionMeters = routeX * 1000.0;
	stop.scheduledArrival = arrival;
	stop.scheduledDeparture = departure;
	stop.routeBlockIds = std::move(routeBlockIds);
	return stop;
}

CompletedTrain trainOn(const std::string& id, int routeIndex) {
	CompletedTrain train;
	train.trainId = id;
	train.operatingCode = "OC-" + id;
	train.routeIndex = routeIndex;
	return train;
}

// A train of the first route that occupies the blocks one after the other, each for 60 s: the first from `entry` on and
// each next one from 30 s after the one before. The record of each block has the entry of that block as its startRunTime.
CompletedTrain trainOver(const std::string& id, double entry, const std::vector<std::string>& blocks) {
	CompletedTrain train = trainOn(id, 0);
	for (const std::string& block : blocks) {
		train.blockTimes.push_back(occupation(block, entry, entry + 60.0, 0.0, 1000.0, entry));
		entry += 30.0;
	}
	return train;
}

CompletedRoute routeOf(const std::string& id, const std::vector<std::pair<std::string, double>>& nodes,
	const std::vector<BlockSectionInput>& blocks = {}) {
	CompletedRoute route;
	route.id = id;
	route.blocks = blocks;
	route.path.id = id;
	for (const auto& node : nodes)
		route.path.nodes.push_back({node.first, "", "", node.second});
	return route;
}

TimetableResultRow rowOf(const std::string& train, const std::string& station, int journey, int call,
	RunResultValue plannedArrival, RunResultValue plannedDeparture, RunResultValue simulatedArrival, RunResultValue simulatedDeparture) {
	TimetableResultRow row;
	row.trainId = train;
	row.stationId = station;
	row.journeyIndex = journey;
	row.callIndex = call;
	row.plannedArrivalSeconds = plannedArrival;
	row.plannedDepartureSeconds = plannedDeparture;
	row.simulatedArrivalSeconds = simulatedArrival;
	row.simulatedDepartureSeconds = simulatedDeparture;
	return row;
}

const RunResultValue kAbsent{false, 0.0};

RunResultValue at(double seconds) {
	return {true, seconds};
}

std::string row(const std::vector<std::string>& cells) {
	return csv::makeRow(cells);
}

std::string f(double value) {
	return csv::formatDouble(value);
}

void testMissingValues() {
	const double nan = std::numeric_limits<double>::quiet_NaN();
	{
		// A trajectory sample that the train did not record is an empty cell, and so is a value that is not finite.
		CompletedRunData data;
		data.settings.timestepSeconds = 2.0;
		data.blockNames = {"B1", "B2"};
		CompletedTrain train = trainOn("T1", 0);
		train.serviceId = "S";
		train.serviceOccurrence = 2;
		CompletedTrajectory& t = train.trajectory;
		t.firstSample = 3;
		t.position = {100.0, 200.0, 300.0};
		t.speed = {1.0, nan};
		t.power = {5000.0, 6000.0, 7000.0};
		t.energy = {1.0, 2.0, 3.0};
		t.effort = {2000.0};
		t.block = {0, CompletedTrajectory::kNoBlock, 1};
		t.segments = {{3, 5}};
		data.trains = {train};
		const auto rows = dataLines(trajectoryCsvFrom(data, QStringList{"T1"}));
		check(rows.size() == 3, "trajectory rows of the hand-made train");
		if (rows.size() == 3) {
			check(rows[0] == row({"T1", "OC-T1", "S", "2", f(6.0), f(100.0), f(1.0), f(5.0), f(2.0), f(1.0 * kEnergyMJToKWh), "B1"}), "first trajectory row");
			check(rows[1] == row({"T1", "OC-T1", "S", "2", f(8.0), f(200.0), "", f(6.0), "", f(2.0 * kEnergyMJToKWh), ""}), "second trajectory row has empty cells");
			check(rows[2] == row({"T1", "OC-T1", "S", "2", f(10.0), f(300.0), "", f(7.0), "", f(3.0 * kEnergyMJToKWh), "B2"}), "third trajectory row");
		}
		check(trajectoryCsvFrom(data, QStringList()).empty(), "no visible train gives no trajectory text");
		data.trains[0].trajectory.segments.clear();
		check(trajectoryCsvFrom(data, QStringList{"T1"}).empty(), "a train without valid samples gives no trajectory text");
	}
	{
		// A time that is absent, negative or not finite gives no planned reference; a stop that cannot be placed leaves a gap.
		CompletedRunData data;
		data.routes = {routeOf("R", {{"n1", 0.0}, {"n2", 1.0}, {"n3", 2.0}})};
		CompletedTrain train = trainOn("T", 0);
		train.stops = {stopAt("A", "n1", 0.0, -1.0, nan, {"b"}), stopAt("B", "n2", 1.0, 100.0, -1.0, {"b"}),
			stopAt("C", "n3", 2.0, 200.0, 210.0, {"b"}), stopAt("D", "missing", 3.0, 300.0, 310.0, {"b"})};
		data.trains = {train};
		const RouteDiagramPath& path = data.routes[0].path;
		const std::map<int, RouteDiagramPath> paths = {{0, path}};
		const std::map<int, RouteDiagramProjection> projections = {{0, buildRouteDiagramProjection(path, path)}};
		int omitted = 0;
		const auto references = plannedReferencesFrom(data, defaultBlockingTimeScopeFrom(data), paths, projections, omitted);
		check(omitted == 1, "the stop outside the path is counted as omitted");
		check(references.size() == 4, "planned references of the stops with times");
		if (references.size() == 4) {
			check(references[0].eventType == "arrival" && references[0].time == 100.0 && references[0].positionKm == 1.0 && references[0].stationName == "B", "arrival of the second stop");
			check(references[1].time == 200.0 && references[2].time == 210.0 && references[2].positionKm == 2.0, "both times of the third stop");
			check(std::isnan(references[3].time) && references[3].stationName.empty(), "the stop that cannot be placed leaves a gap");
		}
	}
	{
		// A timetable value that is not available is an empty cell.
		CompletedRunData data;
		data.timetable = {rowOf("T", "A", 1, 1, kAbsent, at(60.0), kAbsent, at(65.0))};
		data.timetable[0].departureDelaySeconds = at(5.0);
		const auto rows = dataLines(timetableCsvFrom(data, QStringList{"T"}));
		check(rows.size() == 1 && rows[0] == row({"T", "A", "1", "", "", f(60.0), "", f(65.0), "", f(5.0)}), "timetable row with absent times");
		check(timetableCsvFrom(data, QStringList{"U"}).empty(), "a train that is not visible has no timetable text");
	}
	{
		// A block without a complete, ordered, finite record makes no segment.
		CompletedRunData data;
		CompletedTrain train = trainOn("T", 0);
		BlockingTimeDiagramInput incomplete = occupation("B1", 0.0, 10.0, 0.0, 100.0);
		incomplete.isComplete = false;
		train.blockTimes = {incomplete, occupation("B2", 10.0, nan, 100.0, 200.0), occupation("B3", 20.0, 30.0, 200.0, 300.0), occupation("B4", 30.0, 40.0, -1.0, 400.0)};
		train.blockTimes.push_back(occupation("B5", 40.0, 50.0, 400.0, 500.0));
		train.blockTimes.back().endClearTime = -1.0;
		data.trains = {train};
		const auto segments = blockTimesFrom(data);
		check(segments.size() == 1 && segments[0].blockId == "B3" && segments[0].trainName == "T", "only the complete, finite and cleared block makes a segment");
	}
	{
		// The reference time of a capacity train: absent or non-finite entry times drop the train; boundary stops win over
		// the canonical entry; an absent scheduled entry falls back to the profile.
		CapacityAnalysisScope scope;
		scope.blockIds = {"B"};
		CompletedTrain train = trainOn("T", 0);
		train.blockTimes = {occupation("B", 10.0, 20.0, 1000.0, 1100.0, 12.0)};
		train.scheduledDepartureTime = 30.0;
		train.runStartTime = 5;
		CapacityAnalysisTrain result = capacityInputFrom(train, scope);
		check(result.occupations.size() == 1 && result.profileReferenceTime == 12.0, "capacity train with a profile reference");
		check(result.scheduledReferenceTime == 30.0 + (12.0 - 5.0) && result.referenceSource == "canonical scheduled entry + elapsed from RunStartTime", "canonical scheduled entry");
		check(result.referenceLabel == "block B entry", "label of the block entry");

		train.scheduledDepartureTime = -1.0;
		result = capacityInputFrom(train, scope);
		check(result.scheduledReferenceTime == 12.0 && result.referenceSource == "profile reference fallback (scheduled entry unavailable)", "fallback to the profile");

		train.scheduledDepartureTime = 30.0;
		train.stops = {stopAt("Far", "x", 1.006, 55.0, 56.0), stopAt("Near", "y", 1.003, 40.0, nan)};
		result = capacityInputFrom(train, scope);
		check(result.scheduledReferenceTime == 40.0 && result.referenceLabel == "Near boundary"
				&& result.referenceSource == "authored ScheduledArrival at boundary station (departure absent)",
			"a boundary stop within 5 m gives the authored arrival when the departure is absent");
		train.stops[1].scheduledDeparture = 50.0;
		result = capacityInputFrom(train, scope);
		check(result.scheduledReferenceTime == 50.0 && result.referenceSource == "authored ScheduledDeparture at boundary station", "the departure wins over the arrival");
		// The windows name the reference time in plain words; the export keeps the source as it is.
		check(capacityReferenceText(result.referenceSource, result.referenceLabel) == "planned departure at Near"
				&& capacityReferenceText("authored ScheduledArrival at boundary station (departure absent)", "Near boundary") == "planned arrival at Near"
				&& capacityReferenceText("canonical scheduled entry + elapsed from RunStartTime", "block B entry") == "planned entry time"
				&& capacityReferenceText("profile reference fallback (scheduled entry unavailable)", "block B entry") == "simulated entry time"
				&& capacityReferenceText("", "").isEmpty(),
			"the windows name each reference source in plain words");
		train.stops[1].scheduledDeparture = -1.0;
		train.stops[1].scheduledArrival = nan;
		result = capacityInputFrom(train, scope);
		check(result.scheduledReferenceTime == 37.0, "a boundary stop without a usable time leaves the canonical entry");

		train.blockTimes[0].startRunTime = -1.0;
		check(capacityInputFrom(train, scope).occupations.empty(), "a negative entry time drops the occupations");
		train.blockTimes[0].startRunTime = nan;
		check(capacityInputFrom(train, scope).occupations.empty(), "a non-finite entry time drops the occupations");
		check(capacityInputFrom(train, CapacityAnalysisScope()).occupations.empty(), "a scope without blocks has no occupations");
	}
	{
		CompletedRunData data;
		data.settings.timestepSeconds = 2.0;
		data.settings.horizonSteps = 10.0;
		check(defaultBlockingTimeScopeFrom(data).endTime == 20.0 && defaultBlockingTimeScopeFrom(data).startTime == 0.0, "the default scope ends at the horizon");
		data.settings.horizonSteps = -4.0;
		check(defaultBlockingTimeScopeFrom(data).endTime == 0.0, "the default scope has no negative end");
		check(capacityAnalysisCsv(CapacityAnalysisResult(), QString("label")).empty(), "a result that cannot be analysed has no text");
	}
}

void testRepeatedStops() {
	// One station is served twice, at the same node of the route, once at each end.
	CompletedRunData data;
	data.settings.timestepSeconds = 1.0;
	data.routes = {routeOf("loop", {{"a", 0.0}, {"b", 3.0}, {"a", 6.0}}, {{"b0", 0.0, 3.0}, {"b1", 3.0, 6.0}, {"b2", 6.0, 9.0}})};
	CompletedTrain train = trainOn("L", 0);
	train.stops = {stopAt("A", "a", 0.0, -1.0, 60.0, {"b0"}), stopAt("B", "b", 3.0, 100.0, 110.0, {"b1"}),
		stopAt("A", "a", 6.0, 200.0, -1.0, {"b2"})};
	data.trains = {train};
	data.timetable = {rowOf("L", "A", 1, 1, kAbsent, at(60.0), at(1.0), at(61.0)), rowOf("L", "B", 2, 1, at(100.0), at(110.0), at(101.0), at(111.0)),
		rowOf("L", "A", 3, 2, at(200.0), kAbsent, at(201.0), kAbsent)};

	const RouteDiagramPath& path = data.routes[0].path;
	const std::map<int, RouteDiagramPath> paths = {{0, path}};
	const std::map<int, RouteDiagramProjection> projections = {{0, buildRouteDiagramProjection(path, path)}};
	int omitted = 0;
	const auto references = plannedReferencesFrom(data, defaultBlockingTimeScopeFrom(data), paths, projections, omitted);
	check(omitted == 0 && references.size() == 4, "planned references of the three stops");
	if (references.size() == 4) {
		check(references[0].eventType == "departure" && references[0].time == 60.0 && references[0].positionKm == 0.0 && references[0].stationName == "A", "first visit of A");
		check(references[1].eventType == "arrival" && references[1].positionKm == 3.0 && references[2].eventType == "departure", "stop B in journey order");
		check(references[3].eventType == "arrival" && references[3].time == 200.0 && references[3].positionKm == 6.0 && references[3].stationName == "A", "second visit of A");
	}

	BlockingTimeScope scope = defaultBlockingTimeScopeFrom(data);
	scope.blockIds = {"b2"};
	omitted = 0;
	const auto second = plannedReferencesFrom(data, scope, paths, projections, omitted);
	check(second.size() == 1 && second[0].time == 200.0, "a block scope keeps the visit on that block only");
	check(data.trains[0].stops[0].onRoute() && data.trains[0].stops[0].onRoute({"b0"}) && !data.trains[0].stops[0].onRoute({"b2"}) && !data.trains[0].stops[0].onRoute({"zz"}),
		"a stop is on the route within its own block only");
	check(!stopAt("X", "n", 0.0, 0.0, 0.0).onRoute() && !stopAt("X", "n", 0.0, 0.0, 0.0).onRoute({"b0"}), "a station off the route is never on it");

	const auto diagram = routeDiagramData(data, true, 0);
	check(diagram.has_value(), "timetable graph of the loop route");
	if (!diagram)
		return;
	std::vector<std::string> expectedRows;
	const auto record = [&](const char* event, const char* station, int journey, int call, RunResultValue time, std::optional<double> x) {
		expectedRows.push_back(row({"L", "loop", "loop", event, station, std::to_string(journey), std::to_string(call), time.available ? f(time.value) : "", x ? f(*x) : ""}));
	};
	const double stopX[3] = {0.0, 3.0, 6.0};
	for (const char* event : {"planned arrival", "planned departure", "simulated arrival", "simulated departure"})
		for (int k = 0; k < 3; ++k) {
			const std::string name = event;
			const TimetableResultRow& source = data.timetable[k];
			const RunResultValue time = name == "planned arrival" ? source.plannedArrivalSeconds : name == "planned departure" ? source.plannedDepartureSeconds
				: name == "simulated arrival"																				   ? source.simulatedArrivalSeconds
																															   : source.simulatedDepartureSeconds;
			record(event, source.stationId.c_str(), source.journeyIndex, source.callIndex, time, stopX[k]);
		}
	std::string expectedText;
	for (const auto& line : expectedRows)
		expectedText += line + "\n";
	std::string actualText;
	for (const auto& line : diagram->csvRows)
		actualText += row(line) + "\n";
	checkSame("export rows of the repeated stops", expectedText, actualText);

	// The planned arrival of the first visit is absent, so its line starts with the second stop and runs to the second visit.
	const RouteDiagramSeries* plannedArrival = nullptr;
	for (const auto& series : diagram->series)
		if (series.event == "planned arrival")
			plannedArrival = &series;
	check(plannedArrival && plannedArrival->planned && plannedArrival->kind == RouteDiagramSeries::Kind::Event, "a line of planned arrivals");
	if (plannedArrival) {
		check(plannedArrival->points.size() == 2, "the planned arrival line has two points");
		if (plannedArrival->points.size() == 2) {
			check(plannedArrival->points[0].positionKm == 3.0 && plannedArrival->points[0].call == 1 && plannedArrival->points[0].journey == 2, "second stop");
			check(plannedArrival->points[1].positionKm == 6.0 && plannedArrival->points[1].call == 2 && plannedArrival->points[1].station == "A", "the second visit of A is call 2");
		}
	}
	std::size_t dwells = 0;
	for (const auto& series : diagram->series)
		dwells += series.kind == RouteDiagramSeries::Kind::Dwell ? 1 : 0;
	check(dwells == 3, "a dwell where arrival and departure both exist");
}

void testReversedRoutes() {
	// The west route runs through the nodes of the east route in the other direction, so its abscissa runs the other way.
	CompletedRunData data;
	data.settings.timestepSeconds = 1.0;
	data.routes = {routeOf("east", {{"n0", 0.0}, {"n1", 2.0}, {"n2", 4.0}, {"n3", 6.0}}, {{"e0", 0.0, 2.0}, {"e1", 2.0, 4.0}, {"e2", 4.0, 6.0}}),
		routeOf("west", {{"n3", 0.0}, {"n2", 2.0}, {"n1", 4.0}, {"n0", 6.0}}, {{"w0", 0.0, 2.0}, {"w1", 2.0, 4.0}, {"w2", 4.0, 6.0}})};
	CompletedTrain train = trainOn("W", 1);
	CompletedTrajectory& t = train.trajectory;
	t.firstSample = 0;
	t.position = {1000.0, 2000.0, 3000.0, 9000.0, 5000.0};
	t.segments = {{0, 4}};
	train.stops = {stopAt("S2", "n2", 2.0, 10.0, 20.0, {"w1"}), stopAt("S1", "n1", 4.0, 30.0, 40.0, {"w2"})};
	data.trains = {train};
	data.timetable = {rowOf("W", "S2", 1, 1, at(10.0), at(20.0), at(12.0), at(22.0)), rowOf("W", "S1", 2, 1, at(30.0), at(40.0), at(32.0), at(42.0))};

	const auto paths = routeDiagramData(data, false, 0);
	check(paths.has_value(), "train paths on the east route");
	if (paths) {
		check(paths->series.size() == 2, "the sample outside the route splits the line");
		if (paths->series.size() == 2) {
			check(paths->series[0].kind == RouteDiagramSeries::Kind::Trajectory && paths->series[0].routeId == "west", "trajectory of the west train");
			check(paths->series[0].points.size() == 3, "three projected samples before the gap");
			if (paths->series[0].points.size() == 3) {
				check(paths->series[0].points[0].positionKm == 5.0 && paths->series[0].points[1].positionKm == 4.0 && paths->series[0].points[2].positionKm == 3.0, "the projection runs the other way");
				check(paths->series[0].points[2].seconds == 2.0, "time of the third sample");
			}
			check(paths->series[1].points.size() == 1 && paths->series[1].points[0].positionKm == 1.0 && paths->series[1].points[0].seconds == 4.0, "the line resumes after the gap");
		}
		check(paths->csvRows.size() == 5 && paths->csvRows[3] == std::vector<std::string>({"W", "east", "west", "trajectory", "", "", "", f(3.0), ""}),
			"the unprojectable sample is an export row without a position");
	}

	const auto timetable = routeDiagramData(data, true, 0);
	check(timetable.has_value(), "timetable graph on the east route");
	if (timetable) {
		const RouteDiagramSeries* planned = nullptr;
		for (const auto& series : timetable->series)
			if (series.event == "planned arrival")
				planned = &series;
		check(planned && planned->points.size() == 2, "planned arrivals of the west train");
		if (planned && planned->points.size() == 2)
			check(planned->points[0].positionKm == 4.0 && planned->points[1].positionKm == 2.0 && planned->points[0].seconds == 10.0, "the stops are mirrored onto the east route");
	}

	const auto onItself = routeDiagramData(data, false, 1);
	check(onItself && onItself->series.size() == 1 && onItself->series[0].points.size() == 5, "the reference route needs no projection");
	if (onItself && !onItself->series.empty() && onItself->series[0].points.size() == 5)
		check(onItself->series[0].points[3].positionKm == 9.0, "positions are kept on the reference route");

	// The blocking-time projection mirrors the envelope of the west train in the same way.
	data.trains[0].blockTimes = {occupation("w1", 10.0, 20.0, 2000.0, 4000.0)};
	BlockingTimeScope scope = defaultBlockingTimeScopeFrom(data);
	scope.endTime = 100.0;
	scope.routeIndex = 0;
	scope.trainIds = {"W"};
	const auto blocking = blockingTimeDiagramData(data, scope);
	check(blocking && blocking->segments.size() == 1, "an envelope of the west train on the east route");
	if (blocking && blocking->segments.size() == 1)
		check(blocking->segments[0].startPositionKm == 4.0 && blocking->segments[0].endPositionKm == 2.0 && blocking->segments[0].midPositionKm == 3.0,
			"the ends of the envelope are mirrored");
}

void testDelayDiagram() {
	// The route passes Alpha, Bravo, Mike, Bravo again and Charlie; two sections share the first node of Bravo. The station
	// Bravo has the id st-b, and a timetable row names a station by its name.
	CompletedRunData data;
	CompletedRoute line = routeOf("line", {});
	line.path.nodes = {{"a", "A", "Alpha", 0.0}, {"b1", "st-b", "Bravo", 3.0}, {"b1", "st-b", "Bravo", 3.0}, {"m", "M", "Mike", 4.5},
		{"x", "", "", 5.0}, {"b2", "st-b", "Bravo", 6.0}, {"c", "C", "Charlie", 9.0}};
	data.routes = {line, routeOf("empty", {})};
	data.trains = {trainOn("Up", 0), trainOn("Down", 0), trainOn("Shuttle", 0), trainOn("Unplanned", 0), trainOn("Away", 0), trainOn("Idle", 0)};
	const auto call = [&data](const std::string& train, const std::string& station, int journey, int callIndex, std::optional<double> delay) {
		TimetableResultRow entry = rowOf(train, station, journey, callIndex, kAbsent, kAbsent, kAbsent, kAbsent);
		entry.arrivalDelaySeconds = delay ? at(*delay) : kAbsent;
		data.timetable.push_back(entry);
	};
	// Up has no arrival delay at Alpha, calls at Zulu off the route and comes back to Bravo after Mike.
	call("Up", "Alpha", 1, 1, std::nullopt);
	call("Up", "Bravo", 2, 1, 60.0);
	call("Up", "Mike", 3, 1, 30.0);
	call("Up", "Zulu", 4, 1, 10.0);
	call("Up", "Bravo", 5, 2, 90.0);
	call("Up", "Charlie", 6, 1, -30.0);
	// Down runs the other way, so it reaches the second node of Bravo first.
	call("Down", "Charlie", 1, 1, 120.0);
	call("Down", "Bravo", 2, 1, 45.0);
	call("Down", "Mike", 3, 1, 15.0);
	call("Down", "Bravo", 4, 2, 20.0);
	call("Down", "Alpha", 5, 1, 0.0);
	// Shuttle calls only at Bravo, so nothing gives its direction.
	call("Shuttle", "Bravo", 1, 1, 5.0);
	call("Shuttle", "Bravo", 2, 2, 6.0);
	call("Unplanned", "Mike", 1, 1, std::nullopt);
	// The id of Bravo is not the name of a station of the route.
	call("Away", "Zulu", 1, 1, 50.0);
	call("Away", "st-b", 2, 1, 40.0);

	const auto pointsOf = [](const DelayDiagramSeries& series) {
		std::string text = series.trainId + ":";
		for (const DelayDiagramPoint& point : series.points)
			text += " " + num(point.positionKm) + "|" + num(point.delaySeconds) + "|" + point.station + "|" + std::to_string(point.journey) + "|" + std::to_string(point.call);
		return text + "\n";
	};
	const auto diagram = delayDiagramData(data, 0);
	check(diagram && diagram->referencePath.id == "line", "the Delays chart of the line");
	if (!diagram)
		return;
	std::string actual;
	for (const DelayDiagramSeries& series : diagram->series)
		actual += pointsOf(series);
	checkSame("the delays at the stations of the line",
		"Up: 3|60|Bravo|2|1 4.5|30|Mike|3|1 6|90|Bravo|5|2 9|-30|Charlie|6|1\n"
		"Down: 9|120|Charlie|1|1 6|45|Bravo|2|1 4.5|15|Mike|3|1 3|20|Bravo|4|2 0|0|Alpha|5|1\n"
		"Shuttle: 3|5|Bravo|1|1 3|6|Bravo|2|2\n"
		"Unplanned:\n",
		actual);
	check(diagram->withoutCalls == std::vector<std::string>({"Away", "Idle"}), "the trains without a call at a station of the line");
	check(!delayDiagramData(data, 1) && !delayDiagramData(data, -1) && !delayDiagramData(data, 2), "no Delays chart without a route with nodes");
}

void testCapacityOrder() {
	CompletedRunData data;
	const auto trainWithEntry = [](const std::string& id, double entry) {
		CompletedTrain train = trainOn(id, 0);
		train.blockTimes = {occupation("B", entry, entry + 10.0, 0.0, 100.0, entry)};
		return train;
	};
	data.trains = {trainWithEntry("a", 0.0), trainWithEntry("b", 100.0), trainWithEntry("c", 200.0), trainOn("d", 0)};
	data.routes = {routeOf("r", {{"n", 0.0}}, {{"B", 0.0, 1.0}})};
	CapacityAnalysisScope scope;
	scope.routeIndex = 0;
	scope.blockIds = {"B"};
	scope.sectionIndices = {0};
	const auto ids = [](const std::vector<CapacityAnalysisTrain>& trains) {
		std::string text;
		for (const auto& train : trains)
			text += train.runtimeId;
		return text;
	};
	check(ids(capacityInputsFrom(data, scope)) == "abc", "without a selection the trains come in their order on the blocks, and trains without occupations are left out");
	CompletedRunData reversed = data;
	std::reverse(reversed.trains.begin(), reversed.trains.end());
	check(ids(capacityInputsFrom(reversed, scope)) == "abc", "the order on the blocks does not follow the order of the run");
	scope.occurrenceIds = {"c", "a"};
	check(ids(capacityInputsFrom(data, scope)) == "ca", "the order follows the occurrence ids");
	scope.occurrenceIds = {"b", "missing", "d", "a"};
	check(ids(capacityInputsFrom(data, scope)) == "ba", "ids that are missing or have no occupation are dropped");
	scope.occurrenceIds = {"missing"};
	check(capacityInputsFrom(data, scope).empty(), "a selection of unknown ids gives no train");
	scope.occurrenceIds = {"a", "b"};
	check(capacityScopeIssueFrom(data, scope).empty(), "a unique entry has no issue");
	check(capacityScopeLabelFrom(data, scope).toStdString() == "r / B to B", "label of the scope");
	check(capacityScopeTextFrom(data, scope) == "r, block 1", "a scope of one block names its number");
	CapacityAnalysisScope unnumbered = scope;
	unnumbered.sectionIndices.clear();
	check(capacityScopeTextFrom(data, unnumbered) == "r, 1 block", "a scope without block numbers names its block count");
	data.routes[0].corridor = "north";
	check(capacityScopeLabelFrom(data, scope).toStdString() == "r / north / B to B", "label with a corridor");
	scope.sectionIndices = {0};
	check(capacityScopeTextFrom(data, scope) == "r (north), block 1" && capacityScopeBlocksText(data, scope) == "Block 1: B",
		"the window names a scope by its route, corridor and block number");
	scope.sectionIndices.clear();
	// The capacity windows name a period in hours or minutes when it has whole ones.
	check(capacityPeriodText(3600.0) == "1 h" && capacityPeriodText(7200.0) == "2 h" && capacityPeriodText(1800.0) == "30 min"
			&& capacityPeriodText(5400.0) == "90 min" && capacityPeriodText(90.5) == "90.5 s",
		"the period in hours, minutes or seconds");
	scope.routeIndex = 7;
	check(capacityScopeLabelFrom(data, scope).toStdString() == "no selected route" && capacityScopeIssueFrom(data, scope).empty(), "a route that does not exist");

	// A train that enters the block twice takes its reference where it enters first.
	data.trains[1].blockTimes.push_back(occupation("B", 150.0, 160.0, 0.0, 100.0, 150.0));
	scope.routeIndex = 0;
	scope.sectionIndices = {0};
	const CapacityAnalysisTrain twice = capacityInputFrom(data.trains[1], scope);
	check(capacityScopeIssueFrom(data, scope).empty() && twice.occupations.size() == 2 && twice.profileReferenceTime == 100.0
			&& twice.referenceLabel == "block B entry",
		"a train that enters the block twice can be analysed from its first entry");

	const CapacityAnalysisScope pair = firstCapacityScopeWithPairFrom(data);
	check(pair.routeIndex == 0 && pair.blockIds == std::vector<std::string>({"B"}) && pair.sectionIndices == std::vector<int>({0})
			&& pair.occurrenceIds == std::vector<std::string>({"a", "b", "c"}) && pair.repeatFirstTrain && pair.cycleEndOccurrenceId.empty(),
		"the first scope with two trains is closed by the first train one period later");
	data.trains.resize(1);
	check(firstCapacityScopeWithPairFrom(data).routeIndex == -1, "no scope with two trains");
}

// The analysed blocks are a range of the route, and every train with a usable occupation there is analysed, from where
// it enters the range.
void testCapacityRange() {
	CompletedRunData data;
	data.routes = {routeOf("r", {{"n", 0.0}}, {{"A", 0.0, 1.0}, {"B", 1.0, 2.0}, {"C", 2.0, 3.0}, {"D", 3.0, 4.0}, {"E", 4.0, 5.0}})};
	// a leaves out D and b leaves out A. c runs over A only, and the record of d on C has no usable StartRunTime. w runs
	// from E back to D.
	data.trains = {trainOver("a", 0.0, {"A", "B", "C", "E"}), trainOver("b", 100.0, {"B", "C", "D", "E"}), trainOver("c", 300.0, {"A"}),
		trainOver("d", 400.0, {"B", "C"}), trainOver("w", 500.0, {"E", "D"})};
	data.trains[3].blockTimes.back().startRunTime = -1.0;
	const auto idsOf = [](const std::vector<CapacityAnalysisTrain>& trains) {
		std::vector<std::string> ids;
		for (const CapacityAnalysisTrain& train : trains)
			ids.push_back(train.runtimeId);
		return ids;
	};

	const CapacityAnalysisScope whole = firstCapacityScopeWithPairFrom(data);
	check(whole.routeIndex == 0 && whole.blockIds == std::vector<std::string>({"A", "B", "C", "D", "E"})
			&& whole.sectionIndices == std::vector<int>({0, 1, 2, 3, 4}) && whole.occurrenceIds == std::vector<std::string>({"a", "b", "c", "d", "w"})
			&& whole.repeatFirstTrain,
		"the default scope is the whole route with every train that runs over a part of it");
	const CapacityAnalysisScope middle = capacityRangeScopeFrom(data, 0, 3, 1);
	check(middle.blockIds == std::vector<std::string>({"B", "C", "D"}) && middle.sectionIndices == std::vector<int>({1, 2, 3})
			&& middle.occurrenceIds == std::vector<std::string>({"a", "b", "d", "w"}),
		"a range given from its last block runs from B to D and leaves out c");
	check(capacityRangeScopeFrom(data, 0, 3, 9).sectionIndices == std::vector<int>({3, 4}), "a range is cut to the route");
	const CapacityAnalysisScope missing = capacityRangeScopeFrom(data, 5, 0, 4);
	check(missing.routeIndex == 5 && missing.blockIds.empty() && missing.occurrenceIds.empty(), "no blocks or trains on a route that does not exist");

	// Over D and E, a enters at E, b at D and w at E, each where it runs first into the range.
	const CapacityAnalysisScope end = capacityRangeScopeFrom(data, 0, 3, 4);
	const std::vector<CapacityAnalysisTrain> inputs = capacityInputsFrom(data, end);
	check(idsOf(inputs) == std::vector<std::string>({"a", "b", "w"}), "a, b and w run over D or E");
	if (inputs.size() == 3)
		check(inputs[0].profileReferenceTime == 90.0 && inputs[0].referenceLabel == "block E entry" && inputs[0].occupations.size() == 1
				&& inputs[1].profileReferenceTime == 160.0 && inputs[1].referenceLabel == "block D entry" && inputs[1].occupations.size() == 2
				&& inputs[2].profileReferenceTime == 500.0 && inputs[2].referenceLabel == "block E entry",
			"each train takes its reference where it enters the range");
	const CapacityAnalysisResult partial = capacityAnalysisFrom(data, end);
	check(partial.analyzable && partial.cycleEndIdentity == "a (next period)" && partial.cycleTime > 0.0,
		"trains that run over parts of the range are analysed");
	// On C the record of d cannot be a reference, and it has no other record there.
	check(capacityRangeScopeFrom(data, 0, 2, 2).occurrenceIds == std::vector<std::string>({"a", "b"}), "a record without a usable StartRunTime gives no reference");
	const CapacityAnalysisTrain d = capacityInputFrom(data.trains[3], middle);
	check(d.occupations.size() == 2 && d.profileReferenceTime == 400.0, "another record of the train in the range gives the reference");

	// o runs on another route, but its records carry the ids of B and C of the first route.
	data.routes.push_back(routeOf("s", {{"m", 0.0}}, {{"B", 0.0, 1.0}, {"C", 1.0, 2.0}}));
	CompletedTrain other = trainOver("o", 600.0, {"B", "C"});
	other.routeIndex = 1;
	data.trains.push_back(other);
	const std::vector<std::string> withOther = capacityRangeScopeFrom(data, 0, 1, 2).occurrenceIds;
	check(std::find(withOther.begin(), withOther.end(), "o") != withOther.end(),
		"an occurrence of another route runs over the blocks whose ids its records carry");

	// X lies twice on the route. A range that holds one of them cannot be analysed, a range that holds both can.
	CompletedRunData looped;
	looped.routes = {routeOf("r", {{"n", 0.0}}, {{"X", 0.0, 1.0}, {"Y", 1.0, 2.0}, {"Z", 2.0, 3.0}, {"X", 3.0, 4.0}})};
	looped.trains = {trainOver("a", 0.0, {"X", "Y"}), trainOver("b", 100.0, {"X", "Y"})};
	check(capacityScopeIssueFrom(looped, capacityRangeScopeFrom(looped, 0, 0, 1)).find("also used outside the chosen blocks") != std::string::npos,
		"a range with a resource that occurs again outside it cannot be analysed");
	check(capacityScopeIssueFrom(looped, firstCapacityScopeWithPairFrom(looped)).empty(), "the whole route holds both");
}

// Two trains over B and C. The route runs against the geo X coordinate, from 36.5 km to 34.5 km. b has recorded samples
// at 0.5 km, 1.5 km and 2.5 km from 600 s on.
CompletedRunData compressedPairRun() {
	CompletedRunData data;
	data.settings.timestepSeconds = 1.0;
	data.routes = {routeOf("r", {{"n0", 0.0}, {"n1", 1.0}, {"n2", 2.0}}, {{"B", 0.0, 1.0, 36.5, 35.5}, {"C", 1.0, 2.0, 35.5, 34.5}})};
	CompletedTrain a = trainOn("a", 0);
	a.blockTimes = {occupation("B", 500.0, 600.0, 0.0, 1000.0, 505.0), occupation("C", 550.0, 700.0, 1000.0, 2000.0, 555.0)};
	CompletedTrain b = trainOn("b", 0);
	b.blockTimes = {occupation("B", 520.0, 650.0, 0.0, 1000.0, 525.0), occupation("C", 600.0, 800.0, 1000.0, 2000.0, 605.0)};
	b.trajectory.firstSample = 600;
	b.trajectory.position = {500.0, 1500.0, 2500.0};
	b.trajectory.segments = {{600, 602}};
	data.trains = {a, b};
	return data;
}

// b follows a at the minimum headway, which C governs, so the compressed diagram moves its envelopes and its recorded
// samples 100 s later.
void testCompressedDiagram() {
	const CompletedRunData data = compressedPairRun();
	const CapacityAnalysisScope scope = firstCapacityScopeWithPairFrom(data);
	check(scope.blockIds == std::vector<std::string>({"B", "C"}), "the two trains share B and C");
	const CapacityAnalysisResult result = analyzeCapacity(capacityInputsFrom(data, scope));
	const auto diagram = compressedBlockingTimeDiagramData(data, scope, result);
	check(diagram && diagram->referencePath.id == "r" && diagram->extents.size() == 2 && diagram->segments.size() == 4
			&& diagram->plannedReferences.empty() && diagram->plannedLines.empty(),
		"the compressed envelopes on the sections of the scope, without planned references");
	check(diagram && diagram->routeSectionCount == 2, "the compressed strip counts the sections of the whole route");
	if (!diagram)
		return;
	const auto followerOnC = std::find_if(diagram->segments.begin(), diagram->segments.end(),
		[](const BlockingTimeDiagramSegment& segment) { return segment.trainName == "b" && segment.blockId == "C"; });
	check(followerOnC != diagram->segments.end() && followerOnC->startTime == 700.0 && followerOnC->originalStartTime == 600.0
			&& followerOnC->capacityCritical,
		"b moves by 100 s and touches a on C");
	check(diagram->trajectories.size() == 1 && diagram->trajectories[0].trainId == "b"
			&& diagram->trajectories[0].points == std::vector<std::pair<double, double>>({{0.5, 700.0}, {1.5, 701.0}}),
		"the samples of b move with its envelopes, and the one beyond them is left out");
	check(diagram->trajectoryRows.size() == 2 && diagram->trajectoryRows[1][8] == f(701.0), "the export rows of the samples have the moved times");
	check(diagram->trajectoryRows.size() == 2 && diagram->trajectoryRows[0].size() == 24 && diagram->trajectoryRows[0][23] == f(36.0)
			&& diagram->trajectoryRows[1][23] == f(35.0),
		"the export rows of the samples have the geo X of their position");
	check(std::abs(diagram->timeRange.first - 480.0) < 1e-9 && std::abs(diagram->timeRange.second - 920.0) < 1e-9,
		"the time axis spans the compressed envelopes, 500 s to 900 s, with a margin of 5 %");
	check(!compressedBlockingTimeDiagramData(data, CapacityAnalysisScope(), result), "no diagram without a route");
	const auto empty = compressedBlockingTimeDiagramData(data, scope, CapacityAnalysisResult());
	check(empty && empty->segments.empty() && empty->timeRange == std::make_pair(0.0, 1.0), "without envelopes the time axis spans 0 s to 1 s");
	check(diagram->repeatedTrains.empty(), "a result without the first train one period later repeats no train");
}

// With a period of 1000 s, a one period later enters at 1505 s and follows b at their minimum headway of 230 s, which C
// governs: at 855 s, a cycle of 350 s. The compressed diagram draws it on the route of a, with the samples of a moved
// 350 s.
void testCompressedCopy() {
	CompletedRunData data = compressedPairRun();
	data.trains[0].trajectory.firstSample = 560;
	data.trains[0].trajectory.position = {500.0, 1500.0};
	data.trains[0].trajectory.segments = {{560, 561}};
	CapacityAnalysisScope scope = firstCapacityScopeWithPairFrom(data);
	scope.periodSeconds = 1000.0;
	check(scope.repeatFirstTrain, "the default scope is closed by the first train one period later");
	const CapacityAnalysisResult result = capacityAnalysisFrom(data, scope);
	check(result.analyzable && result.trainIdentities == std::vector<std::string>({"a", "b", "a (next period)"})
			&& result.cycleEndIdentity == "a (next period)" && result.compression.size() == 3
			&& result.compression[2].originalReference == 1505.0 && result.compression[2].compressedReference == 855.0
			&& result.cycleTime == 350.0 && std::abs(result.cyclePercentage - 35.0) < 1e-9,
		"a one period later closes the cycle after b");
	const auto diagram = compressedBlockingTimeDiagramData(data, scope, result);
	check(diagram && diagram->segments.size() == 6 && diagram->unmapped.empty()
			&& diagram->repeatedTrains == std::map<std::string, std::string>({{"a (next period)", "a"}}),
		"the copy is drawn on the route of a and repeats a");
	if (!diagram)
		return;
	const auto copyOn = [&diagram](const std::string& block) {
		return std::find_if(diagram->segments.begin(), diagram->segments.end(),
			[&block](const BlockingTimeDiagramSegment& segment) { return segment.trainName == "a (next period)" && segment.blockId == block; });
	};
	check(copyOn("B") != diagram->segments.end() && copyOn("B")->startTime == 850.0 && copyOn("B")->originalStartTime == 1500.0
			&& copyOn("C") != diagram->segments.end() && copyOn("C")->startTime == 900.0 && copyOn("C")->endTime == 1050.0
			&& copyOn("C")->capacityCritical,
		"the copy occupies B from 850 s and C from 900 s, where it touches b");
	const auto samplesOf = [&diagram](const std::string& train) {
		std::vector<std::pair<double, double>> points;
		for (const auto& trajectory : diagram->trajectories)
			if (trajectory.trainId == train)
				points.insert(points.end(), trajectory.points.begin(), trajectory.points.end());
		return points;
	};
	check(samplesOf("a") == std::vector<std::pair<double, double>>({{0.5, 560.0}, {1.5, 561.0}})
			&& samplesOf("a (next period)") == std::vector<std::pair<double, double>>({{0.5, 910.0}, {1.5, 911.0}}),
		"the copy has the samples of a, 350 s later");
	check(std::abs(diagram->timeRange.first - 472.5) < 1e-9 && std::abs(diagram->timeRange.second - 1077.5) < 1e-9,
		"the time axis spans the envelopes of the copy, to 1050 s");
	check(capacityCsvFrom(data, scope).find("a (next period)") != std::string::npos, "the export names the copy");
	CapacityAnalysisScope single = scope;
	single.occurrenceIds = {"a"};
	const CapacityAnalysisResult alone = capacityAnalysisFrom(data, single);
	check(!alone.analyzable && alone.trainIdentities == std::vector<std::string>({"a"}) && capacityCsvFrom(data, single).empty(),
		"one train gets no copy one period later, so it is not analysed and has no export");
}

// o runs on a route that shares no node with r, but its record of C shares the resource of C of r. Its envelope lies on
// C of r in the compressed diagram.
void testCompressedOtherRoute() {
	CompletedRunData data = compressedPairRun();
	data.routes.push_back(routeOf("s", {{"m0", 0.0}, {"m1", 1.0}}, {{"C", 0.0, 1.0}}));
	CompletedTrain other = trainOn("o", 1);
	other.blockTimes = {occupation("C", 2000.0, 2100.0, 0.0, 1000.0, 2005.0)};
	data.trains.push_back(other);
	const CapacityAnalysisScope scope = firstCapacityScopeWithPairFrom(data);
	const CapacityAnalysisResult result = capacityAnalysisFrom(data, scope);
	check(scope.occurrenceIds == std::vector<std::string>({"a", "b", "o"}) && result.analyzable, "o is analysed with a and b");
	const auto diagram = compressedBlockingTimeDiagramData(data, scope, result);
	const auto onC = diagram ? std::find_if(diagram->segments.begin(), diagram->segments.end(),
								   [](const BlockingTimeDiagramSegment& segment) { return segment.trainName == "o"; })
							 : std::vector<BlockingTimeDiagramSegment>::const_iterator();
	check(diagram && onC != diagram->segments.end() && onC->startPositionKm == 1.0 && onC->endPositionKm == 2.0 && onC->referenceSection == 2
			&& diagram->unmapped.empty() && diagram->omitted == 0,
		"the envelope of o lies on C of r");
}

void testRepeatedResource() {
	// The block X lies twice on the route. A scope of one section keeps the envelope on that section only.
	CompletedRunData data;
	data.settings.timestepSeconds = 1.0;
	data.routes = {routeOf("r", {{"n0", 0.0}, {"n1", 1.0}, {"n2", 2.0}, {"n3", 3.0}}, {{"X", 0.0, 1.0}, {"Y", 1.0, 2.0}, {"X", 2.0, 3.0}})};
	CompletedTrain train = trainOn("T", 0);
	train.blockTimes = {occupation("X", 10.0, 20.0, 0.0, 1000.0), occupation("Y", 20.0, 30.0, 1000.0, 2000.0), occupation("X", 30.0, 40.0, 2000.0, 3000.0)};
	data.trains = {train};
	BlockingTimeScope scope;
	scope.endTime = 100.0;
	scope.routeIndex = 0;
	scope.trainIds = {"T"};
	scope.blockIds = {"X"};
	scope.sectionIndices = {0};
	auto diagram = blockingTimeDiagramData(data, scope);
	check(diagram && diagram->segments.size() == 1 && diagram->segments[0].startTime == 10.0 && diagram->segments[0].referenceSection == 1,
		"the first section keeps the first envelope of X");
	check(diagram && diagram->extents.size() == 1 && diagram->extents[0].id == "X" && diagram->extents[0].startKm == 0.0, "the extent of the first section");
	check(diagram && diagram->routeSectionCount == 3, "the strip counts the sections of the whole route");
	scope.sectionIndices = {2};
	diagram = blockingTimeDiagramData(data, scope);
	check(diagram && diagram->segments.size() == 1 && diagram->segments[0].startTime == 30.0 && diagram->segments[0].referenceSection == 3,
		"the last section keeps the second envelope of X");
	scope.sectionIndices.clear();
	diagram = blockingTimeDiagramData(data, scope);
	check(diagram && diagram->segments.size() == 2, "a scope without sections keeps both envelopes");
	scope.trainIds.clear();
	diagram = blockingTimeDiagramData(data, scope);
	check(diagram && diagram->segments.size() == 2, "a route scope without a train list keeps the envelopes of every train");
	scope.routeIndex = 4;
	check(!blockingTimeDiagramData(data, scope), "a route that does not exist has no diagram");
}

void testRouteScopeOfSharedBlocks() {
	// r0 and r1 share X and Y, and T runs on r1. A scope of r0 over X to Y shows the envelopes of T.
	CompletedRunData data;
	data.settings.timestepSeconds = 1.0;
	const std::vector<std::pair<std::string, double>> nodes = {{"n0", 0.0}, {"n1", 1.0}, {"n2", 2.0}};
	data.routes = {routeOf("r0", nodes, {{"X", 0.0, 1.0}, {"Y", 1.0, 2.0}}), routeOf("r1", nodes, {{"X", 0.0, 1.0}, {"Y", 1.0, 2.0}})};
	CompletedTrain train = trainOn("T", 1);
	train.blockTimes = {occupation("X", 10.0, 20.0, 0.0, 1000.0), occupation("Y", 20.0, 30.0, 1000.0, 2000.0)};
	train.stops = {stopAt("S", "n1", 1.0, 15.0, 18.0, {"X"})};
	data.trains = {train};
	BlockingTimeScope scope;
	scope.endTime = 100.0;
	scope.routeIndex = 0;
	scope.blockIds = {"X", "Y"};
	scope.sectionIndices = {0, 1};
	const auto diagram = blockingTimeDiagramData(data, scope);
	check(diagram && diagram->referencePath.id == "r0" && diagram->segments.size() == 2 && diagram->segments[0].trainName == "T"
			&& diagram->segments[1].trainName == "T",
		"a route scope shows a train of another route that occupies its blocks");
	check(diagram && !diagram->plannedReferences.empty(), "and the planned stops of that train on its blocks");
	// A route without blocks has nothing in its scope.
	scope.blockIds.clear();
	scope.sectionIndices.clear();
	const auto empty = blockingTimeDiagramData(data, scope);
	check(empty && empty->segments.empty() && empty->plannedReferences.empty(), "a route without blocks shows no train");
}

void testSharedResources() {
	// "X@sw@Y" holds the resources X and Y; a plain block shares a resource with it when the ids match.
	CompletedRunData data;
	CompletedTrain first = trainOn("first", 0);
	first.blockTimes = {occupation("X@sw@Y", 10.0, 30.0, 0.0, 200.0, 10.0)};
	CompletedTrain second = trainOn("second", 0);
	second.blockTimes = {occupation("Y", 20.0, 40.0, 100.0, 200.0, 20.0), occupation("Z", 40.0, 50.0, 200.0, 300.0, 40.0)};
	data.trains = {first, second};
	const auto segments = blockTimesFrom(data);
	check(segments.size() == 3, "three segments");
	if (segments.size() == 3) {
		check(segments[0].style == BlockingTimeSegmentStyle::Critical && segments[1].style == BlockingTimeSegmentStyle::Critical, "the composite and the plain block conflict");
		check(segments[2].style != BlockingTimeSegmentStyle::Critical, "the unrelated block does not");
	}
	CapacityAnalysisScope scope;
	scope.blockIds = {"Y"};
	const auto inputs = capacityInputsFrom(data, scope);
	check(inputs.size() == 2 && inputs[0].occupations.size() == 1 && inputs[0].occupations[0].blockId == "X@sw@Y", "the composite block is an occupation of the resource Y");
	check(inputs.size() == 2 && inputs[1].occupations.size() == 1 && inputs[1].occupations[0].blockId == "Y", "only the matching block of the second train");
	scope.blockIds = {"X"};
	const auto composite = capacityInputsFrom(data, scope);
	check(composite.size() == 1 && composite[0].runtimeId == "first", "the resource X is held by the composite block only");
	scope.blockIds = {"Y", "Z"};
	const auto several = capacityInputsFrom(data, scope);
	check(several.size() == 2 && several[0].occupations.size() == 1 && several[1].occupations.size() == 2, "a scope of several blocks takes the records of each");
}

void testUsedRoutes() {
	CompletedRunData data;
	data.routes = {routeOf("r0", {}), routeOf("r1", {}), routeOf("r2", {})};
	for (int index : {1, 1, 0, 7, -1, 1, 2, 0})
		data.trains.push_back(trainOn("T" + std::to_string(data.trains.size()), index));
	const auto used = usedRouteReferencesFrom(data);
	check(used == std::vector<std::pair<int, std::string>>({{1, "r1"}, {0, "r0"}, {2, "r2"}}), "routes in order of first use, each once, without unknown indices");
	check(usedRouteReferencesFrom(CompletedRunData()).empty(), "no route in an empty run");
}

// The picture of a table shows every column and row, whatever the width of the table.
void testFullTableImage() {
	QTableWidget table;
	const QStringList headers = {"Train", "Operating code", "Start time (hh:mm:ss)", "Energy consumed (kWh)",
		"Energy consumed with regenerative braking (kWh)", "Substation request (kWh)", "Substation request with regenerative braking (kWh)"};
	table.setColumnCount(headers.size());
	table.setHorizontalHeaderLabels(headers);
	table.setRowCount(30);
	for (int row = 0; row < table.rowCount(); ++row)
		for (int column = 0; column < table.columnCount(); ++column)
			table.setItem(row, column, new QTableWidgetItem(QString("r%1 c%2").arg(row).arg(column)));
	const QColor marker(250, 10, 10);
	table.item(table.rowCount() - 1, table.columnCount() - 1)->setBackground(marker);
	table.resizeColumnsToContents();
	table.resize(320, 140);
	table.show();
	QApplication::processEvents();

	int columnsWidth = 0;
	for (int column = 0; column < table.columnCount(); ++column)
		columnsWidth += table.columnWidth(column);
	int rowsHeight = 0;
	for (int row = 0; row < table.rowCount(); ++row)
		rowsHeight += table.rowHeight(row);
	check(columnsWidth > table.width() && rowsHeight > table.height(), "the table is narrower and lower than its contents");

	const QImage image = fullTableImage(table);
	const qreal ratio = image.devicePixelRatio();
	check(image.width() / ratio >= columnsWidth + table.verticalHeader()->width() && image.height() / ratio >= rowsHeight + table.horizontalHeader()->height(),
		"the picture of the table spans all columns and rows");
	// The marked cell is the last of the last row. Without scroll bars it is drawn in the bottom right corner.
	bool marked = false;
	for (int x = image.width() - 12; x < image.width() - 2; ++x)
		for (int y = image.height() - 12; y < image.height() - 2; ++y)
			marked = marked || image.pixelColor(x, y) == marker;
	check(marked, "the last cell is drawn in the picture, with no scroll bar over it");
	check(table.width() == 320 && table.height() == 140, "the picture leaves the size of the table as it was");
}

// Runs the scope dialog on hand-made choices and lets `answer` act on it once it is open.
bool askScope(const BlockingTimeScopeChoices& choices, BlockingTimeScope& scope, const std::function<void(QDialog&)>& answer) {
	QTimer::singleShot(0, [&answer]() {
		auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
		check(dialog && dialog->windowTitle() == "Blocking time", "the scope dialog opens");
		if (!dialog)
			return;
		answer(*dialog);
		// A dialog that the answer leaves open would never return.
		if (dialog->isVisible())
			dialog->reject();
	});
	return chooseBlockingTimeScope(nullptr, choices, scope);
}

void press(QDialog& dialog, QDialogButtonBox::StandardButton button) {
	auto* buttons = dialog.findChild<QDialogButtonBox*>();
	check(buttons && buttons->button(button), "the scope dialog has its buttons");
	if (buttons && buttons->button(button))
		buttons->button(button)->click();
}

void testScopeDialog() {
	BlockingTimeScopeChoices choices;
	choices.caseEndSeconds = 600.0;
	choices.stepSeconds = 5.0;
	choices.routes = {{"r0", "north", {"a", "b", "c"}}, {"r1", "", {"x", "y"}}};

	BlockingTimeScope scope = defaultBlockingTimeScope(choices.caseEndSeconds);
	check(scope.startTime == 0.0 && scope.endTime == 600.0 && scope.routeIndex == -1, "the default scope is the whole run");
	check(defaultBlockingTimeScope(-3.0).endTime == 0.0, "the default scope has no negative end");

	// All routes: no block range and no train list.
	bool accepted = askScope(choices, scope, [](QDialog& dialog) {
		const auto spins = dialog.findChildren<QDoubleSpinBox*>();
		check(spins.size() == 2 && spins[0]->maximum() == 600.0 && spins[0]->singleStep() == 5.0, "the interval is limited to the run");
		spins[0]->setValue(100.0);
		spins[1]->setValue(250.0);
		press(dialog, QDialogButtonBox::Ok);
	});
	check(accepted && scope.routeIndex == -1 && scope.trainIds.empty() && scope.blockIds.empty() && scope.sectionIndices.empty()
			&& scope.startTime == 100.0 && scope.endTime == 250.0,
		"the dialog accepts all routes with an interval");

	// A route with a range of its blocks gives no train list, so the diagram takes every train on those blocks.
	accepted = askScope(choices, scope, [](QDialog& dialog) {
		const auto combos = dialog.findChildren<QComboBox*>();
		check(combos.size() == 3 && combos[0]->count() == 3, "the dialog lists the routes and all routes");
		if (combos.size() != 3)
			return;
		for (const QComboBox* combo : combos)
			check(dynamic_cast<const ChoiceComboBox*>(combo), "the scope dialog shows its choices in choice combos");
		combos[0]->setCurrentIndex(1);
		check(combos[0]->itemText(0) == "All routes" && combos[0]->itemText(1) == "r0  (north)" && combos[0]->itemText(2) == "r1",
			"the routes show their corridor when they have one");
		check(combos[1]->count() == 3 && combos[1]->itemText(2) == "3: c" && combos[2]->currentIndex() == 2, "the blocks of the route are listed");
		combos[1]->setCurrentIndex(1);
		press(dialog, QDialogButtonBox::Ok);
	});
	check(accepted && scope.routeIndex == 0 && scope.sectionIndices == std::vector<int>({1, 2}) && scope.blockIds == std::vector<std::string>({"b", "c"})
			&& scope.trainIds.empty(),
		"the dialog accepts a route and a range of its blocks, with no train list");

	// The last block may come before the first.
	accepted = askScope(choices, scope, [](QDialog& dialog) {
		const auto combos = dialog.findChildren<QComboBox*>();
		if (combos.size() != 3)
			return;
		combos[0]->setCurrentIndex(2);
		combos[2]->setCurrentIndex(0);
		combos[1]->setCurrentIndex(1);
		press(dialog, QDialogButtonBox::Ok);
	});
	check(accepted && scope.routeIndex == 1 && scope.sectionIndices == std::vector<int>({0, 1}) && scope.trainIds.empty(),
		"the blocks are taken in route order");

	// An empty interval is widened by one step, or moved back at the end of the run.
	accepted = askScope(choices, scope, [](QDialog& dialog) {
		const auto spins = dialog.findChildren<QDoubleSpinBox*>();
		spins[0]->setValue(40.0);
		spins[1]->setValue(40.0);
		press(dialog, QDialogButtonBox::Ok);
	});
	check(accepted && scope.startTime == 40.0 && scope.endTime == 45.0, "an empty interval gets one step");
	accepted = askScope(choices, scope, [](QDialog& dialog) {
		const auto spins = dialog.findChildren<QDoubleSpinBox*>();
		spins[0]->setValue(600.0);
		spins[1]->setValue(600.0);
		press(dialog, QDialogButtonBox::Ok);
	});
	check(accepted && scope.startTime == 595.0 && scope.endTime == 600.0, "an empty interval at the end of the run moves back one step");

	const BlockingTimeScope before = scope;
	accepted = askScope(choices, scope, [](QDialog& dialog) { press(dialog, QDialogButtonBox::Cancel); });
	check(!accepted && scope.startTime == before.startTime && scope.endTime == before.endTime, "cancel keeps the scope");

	// A run without routes still offers the interval.
	BlockingTimeScopeChoices none;
	none.caseEndSeconds = 30.0;
	BlockingTimeScope plain = defaultBlockingTimeScope(30.0);
	accepted = askScope(none, plain, [](QDialog& dialog) { press(dialog, QDialogButtonBox::Ok); });
	check(accepted && plain.routeIndex == -1 && plain.startTime == 0.0 && plain.endTime == 30.0, "a run without routes gives the whole run");
}

// Runs the capacity scope dialog on a hand-made run and lets `answer` act on it once it is open.
bool askCapacityScope(const CompletedRunData& run, CapacityAnalysisScope& scope, const std::function<void(QDialog&)>& answer) {
	QTimer::singleShot(0, [&answer]() {
		auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
		check(dialog && dialog->windowTitle() == "Capacity", "the capacity dialog opens");
		if (!dialog)
			return;
		answer(*dialog);
		if (dialog->isVisible())
			dialog->reject();
	});
	return chooseCapacityAnalysisScope(nullptr, run, scope);
}

// A refusal is shown in the dialog. A message box that opens over it is closed and counted as a failure.
void forbidMessageBox() {
	QTimer::singleShot(0, []() {
		auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
		if (!box)
			return;
		check(false, "a refusal opens no message box");
		box->accept();
	});
}

// The three choosers of a run of Paimpol name the blocks of route0 by their labels, with the short ids of the section strip as tooltips.
void checkLabelledChoosers(const CapturedRun& run) {
	if (run.scene != "Paimpol")
		return;
	const CompletedRunData& data = run.data;
	const auto route0 = std::find_if(data.routes.begin(), data.routes.end(), [](const CompletedRoute& route) { return route.id == "route0"; });
	check(route0 != data.routes.end() && data.sectionLabels.label("@0-B0@") == "B0 0.000 to 0.230 km",
		"Paimpol: the run keeps the labels of the sections of its scene");
	if (route0 == data.routes.end())
		return;
	const int routeIndex = static_cast<int>(route0 - data.routes.begin());

	BlockingTimeScope scope = defaultBlockingTimeScopeFrom(data);
	askScope(blockingTimeScopeChoicesFrom(data), scope, [routeIndex](QDialog& dialog) {
		const auto combos = dialog.findChildren<QComboBox*>();
		if (combos.size() != 3)
			return;
		combos[0]->setCurrentIndex(combos[0]->findData(routeIndex));
		check(combos[1]->itemText(0) == "1: B0 0.000 to 0.230 km" && combos[1]->itemData(0, Qt::ToolTipRole).toString() == "0-B0"
				&& combos[1]->itemText(71) == "72: B0 21.010 to 21.235, B1 21.252 to 21.450 km"
				&& combos[1]->itemData(71, Qt::ToolTipRole).toString() == "71-B0/0-B1",
			"Paimpol: the blocking-time chooser names the blocks of route0 by their labels");
		dialog.reject();
	});

	CapacityAnalysisScope capacity = capacityRangeScopeFrom(data, routeIndex, 35, 71);
	// The window names the blocks by their labels; the export keeps the ids.
	check(capacityScopeTextFrom(data, capacity).startsWith("route0") && capacityScopeTextFrom(data, capacity).endsWith(", blocks 36 to 72")
			&& capacityScopeBlocksText(data, capacity)
				== "First block 36: B0 10.210 to 10.510 km\nLast block 72: B0 21.010 to 21.235, B1 21.252 to 21.450 km"
			&& capacityScopeLabelFrom(data, capacity).endsWith(" / @35-B0@ to @71-B0@-21.235000/@0-B1@-21.252000"),
		"Paimpol: the capacity window names its section by block numbers with the labels in the tooltip, the export by ids");
	const std::vector<CapacityHeadwayEvidence> evidence = {{"@35-B0@", "@0-B0@", 12.0, 2.0, 10.0}};
	check(capacityEvidenceText(evidence, &data.sectionLabels)
				== "B0 10.210 to 10.510 km / B0 0.000 to 0.230 km: leader offset 12 s, follower offset 2 s, headway 10 s"
			&& capacityEvidenceText(evidence) == "@35-B0@/@0-B0@ (leader=12 s; follower=2 s; candidate=10 s)"
			&& capacityEvidenceBlocks(evidence) == "35-B0 / 0-B0",
		"Paimpol: the evidence names its blocks by labels in the tooltip, by short ids in the table and by ids with its figures in the export");
	askCapacityScope(data, capacity, [](QDialog& dialog) {
		const auto* from = dialog.findChild<QComboBox*>("capacityFromBlock");
		const auto* to = dialog.findChild<QComboBox*>("capacityToBlock");
		if (!from || !to)
			return;
		check(from->itemText(0) == "1: B0 0.000 to 0.230 km" && from->itemData(0, Qt::ToolTipRole).toString() == "0-B0"
				&& from->currentText() == "36: B0 10.210 to 10.510 km" && from->currentData(Qt::ToolTipRole).toString() == "35-B0"
				&& to->currentText() == "72: B0 21.010 to 21.235, B1 21.252 to 21.450 km"
				&& to->currentData(Qt::ToolTipRole).toString() == "71-B0/0-B1",
			"Paimpol: the capacity chooser names the blocks of route0 by their labels and opens on the given range");
		dialog.reject();
	});

	AreaCapacityScopeDialog area(data);
	auto* routes = area.findChild<QComboBox*>("areaCapacityRoute");
	if (routes)
		routes->setCurrentIndex(routes->findData(routeIndex));
	const auto* first = area.findChild<QComboBox*>("areaCapacityFirstBlock");
	check(first && first->itemText(0) == "1: B0 0.000 to 0.230 km" && first->itemData(0, Qt::ToolTipRole).toString() == "0-B0",
		"Paimpol: the capacity by area chooser names the blocks of route0 by their labels");
}

// The parts of the capacity dialog, by their names. The route is the first combo.
struct CapacityDialogParts {
	QComboBox* route = nullptr;
	QComboBox* from = nullptr;
	QComboBox* to = nullptr;
	QComboBox* cycleEnd = nullptr;
	QListWidget* list = nullptr;
	QLabel* firstTrain = nullptr;
	QLabel* status = nullptr;
	QPushButton* ok = nullptr;
	bool complete() const { return route && from && to && cycleEnd && list && firstTrain && status && ok; }
};

CapacityDialogParts capacityDialogParts(QDialog& dialog) {
	CapacityDialogParts parts;
	const auto combos = dialog.findChildren<QComboBox*>();
	check(combos.size() == 4, "the dialog has the route, the first and the last block and the cycle-closing train");
	parts.route = combos.value(0);
	parts.from = dialog.findChild<QComboBox*>("capacityFromBlock");
	parts.to = dialog.findChild<QComboBox*>("capacityToBlock");
	parts.cycleEnd = dialog.findChild<QComboBox*>("capacityCycleEnd");
	parts.list = dialog.findChild<QListWidget*>();
	parts.firstTrain = dialog.findChild<QLabel*>("capacityFirstTrain");
	parts.status = dialog.findChild<QLabel*>("capacityScopeStatus");
	parts.ok = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
	check(parts.complete(), "the capacity dialog has all its parts");
	return parts;
}

// The texts of the rows of a list or the entries of a combo.
QStringList rowsOf(const QListWidget* list) {
	QStringList rows;
	for (int row = 0; row < list->count(); ++row)
		rows << list->item(row)->text();
	return rows;
}

QStringList entriesOf(const QComboBox* combo) {
	QStringList entries;
	for (int index = 0; index < combo->count(); ++index)
		entries << combo->itemText(index);
	return entries;
}

void testCapacityDialog() {
	CompletedRunData run;
	run.settings.startOffsetSeconds = 6 * 3600;
	// a and b run over B and C, c over B only and e over C only; d has no occupation.
	run.trains = {trainOver("a", 0.0, {"B", "C"}), trainOver("b", 100.0, {"B", "C"}), trainOver("c", 200.0, {"B"}), trainOn("d", 0),
		trainOver("e", 300.0, {"C"})};
	run.routes = {routeOf("r", {{"n", 0.0}}, {{"B", 0.0, 1.0}, {"C", 1.0, 2.0}}), routeOf("s", {{"n", 0.0}}, {{"X", 0.0, 1.0}})};
	run.routes[0].corridor = "north";
	const QString rowA = "06:00:00  a (OC-a), blocks 1 to 2";
	const QString rowB = "06:01:40  b (OC-b), blocks 1 to 2";
	const QString rowC = "06:03:20  c (OC-c), block 1";
	const QString rowE = "06:05:00  e (OC-e), block 2";
	const QString nextPeriod = "First train, one period later";

	CapacityAnalysisScope none;
	check(!chooseCapacityAnalysisScope(nullptr, CompletedRunData(), none), "a run without routes has nothing to choose");

	// Without a given scope the dialog opens on the whole first route, with every train of it checked in their order on
	// the blocks, and the first train one period later closing the cycle.
	CapacityAnalysisScope scope;
	bool accepted = askCapacityScope(run, scope, [&](QDialog& dialog) {
		const CapacityDialogParts parts = capacityDialogParts(dialog);
		if (!parts.complete())
			return;
		check(entriesOf(parts.route) == QStringList({"r  (north)", "s"}), "the routes are listed");
		for (const QLabel* label : dialog.findChildren<QLabel*>())
			check(!label->text().contains("Gdg"), "the capacity dialog names no station of one case");
		check(entriesOf(parts.from) == QStringList({"1: B", "2: C"}) && entriesOf(parts.to) == entriesOf(parts.from)
				&& parts.from->itemData(1, Qt::ToolTipRole).toString() == "C" && parts.from->currentIndex() == 0 && parts.to->currentIndex() == 1,
			"the blocks run from the first to the last block of the route");
		check(rowsOf(parts.list) == QStringList({rowA, rowB, rowC, rowE}),
			"the trains of the blocks are listed in their order, with their entry time, code and blocks");
		bool allChecked = true, tipped = true;
		for (int row = 0; row < parts.list->count(); ++row) {
			allChecked = allChecked && parts.list->item(row)->checkState() == Qt::Checked;
			tipped = tipped && parts.list->item(row)->toolTip() == parts.list->item(row)->text();
		}
		check(allChecked && tipped, "every listed train is checked, with its row as the tooltip");
		check(parts.firstTrain->text() == "First train of the cycle: " + rowA, "the first checked row starts the cycle");
		check(entriesOf(parts.cycleEnd) == QStringList({nextPeriod, rowB, rowC, rowE}) && parts.cycleEnd->currentIndex() == 0,
			"the cycle is closed by the first train one period later or a checked row after the first");
		bool noteIsHelp = false;
		for (const QLabel* label : dialog.findChildren<QLabel*>())
			noteIsHelp |= label->text()
					== "Checked trains are analysed in list order. The cycle runs from the first checked train to the cycle-closing "
					   "train, the first train of the next period. Drag a row to change the order."
				&& label->property("dialogHelp").toBool();
		check(noteIsHelp, "the note of the dialog is a help note");
		check(parts.status->isHidden() && parts.ok->isEnabled(), "the dialog opens without a message");
		if (parts.list->count() != 4)
			return;
		parts.list->item(2)->setCheckState(Qt::Unchecked);
		check(entriesOf(parts.cycleEnd) == QStringList({nextPeriod, rowB, rowE}), "an unchecked row cannot close the cycle");
		dialog.findChild<QDoubleSpinBox*>()->setValue(7200.0);
		parts.ok->click();
	});
	check(accepted && scope.routeIndex == 0 && scope.blockIds == std::vector<std::string>({"B", "C"}) && scope.sectionIndices == std::vector<int>({0, 1})
			&& scope.occurrenceIds == std::vector<std::string>({"a", "b", "e"}) && scope.repeatFirstTrain && scope.cycleEndOccurrenceId.empty()
			&& scope.periodSeconds == 7200.0,
		"the dialog accepts the blocks, the checked trains and the first train one period later");

	// Fewer than two checked trains, or a cycle-closing train that shares no block with an earlier checked train, disable
	// OK and say why. A train listed before one that runs ahead of it is a warning only. The caller's scope stays as it
	// was on cancel.
	CapacityAnalysisScope kept;
	accepted = askCapacityScope(run, kept, [&](QDialog& dialog) {
		const CapacityDialogParts parts = capacityDialogParts(dialog);
		if (!parts.complete() || parts.list->count() != 4)
			return;
		for (const int row : {1, 2, 3})
			parts.list->item(row)->setCheckState(Qt::Unchecked);
		check(!parts.ok->isEnabled() && parts.status->isVisible() && parts.status->property("dialogStatus").toString() == "error"
				&& parts.status->text() == "Check at least two trains.",
			"one checked train disables OK and says why");
		check(entriesOf(parts.cycleEnd) == QStringList({nextPeriod}), "with one checked train only the first train one period later is left");
		parts.list->item(1)->setCheckState(Qt::Checked);
		check(parts.ok->isEnabled() && parts.status->isHidden(), "a second checked train enables OK and takes the message away");
		for (const int row : {2, 3})
			parts.list->item(row)->setCheckState(Qt::Checked);
		parts.cycleEnd->setCurrentIndex(parts.cycleEnd->findData("e"));
		parts.list->item(0)->setCheckState(Qt::Unchecked);
		parts.list->item(1)->setCheckState(Qt::Unchecked);
		check(parts.firstTrain->text() == "First train of the cycle: " + rowC && parts.cycleEnd->currentData().toString() == "e",
			"the first checked row follows the checks, and e still closes the cycle");
		check(!parts.ok->isEnabled() && parts.status->property("dialogStatus").toString() == "error"
				&& parts.status->text() == "e shares no block with an earlier checked train, so it closes no cycle. Choose another cycle-closing train.",
			"a cycle-closing train that shares no block with an earlier checked train disables OK");
		QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
		QApplication::processEvents();
		QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
		const auto* scroll = dialog.findChild<QScrollArea*>("dialogBodyScroll");
		const auto* body = scroll->widget();
		check(!body->isAncestorOf(parts.status) && !body->isAncestorOf(parts.firstTrain)
				&& parts.status->geometry().bottom() <= dialog.findChild<QDialogButtonBox*>()->geometry().top(),
			"the first train and the status sit above the buttons, outside the scrolling body");
		check(parts.firstTrain->mapTo(&dialog, QPoint()).x() == parts.list->mapTo(&dialog, QPoint()).x(),
			"the first train lines up with the list");
		// On a short screen the list gives up its height, not the body: with room for four rows of it, the body does not scroll.
		const int fourRows = 4 * parts.list->sizeHintForRow(0) + 2 * parts.list->frameWidth();
		dialog.resize(dialog.width(), dialog.height() - (parts.list->height() - fourRows));
		QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
		QApplication::processEvents();
		check(scroll->verticalScrollBar()->maximum() == 0, "the list, not the body, gives up height on a short screen");
		parts.cycleEnd->setCurrentIndex(0);
		check(parts.ok->isEnabled() && parts.status->isHidden(), "the first train one period later closes the cycle");
		parts.cycleEnd->setCurrentIndex(parts.cycleEnd->findData("e"));
		parts.list->item(0)->setCheckState(Qt::Checked);
		parts.list->item(1)->setCheckState(Qt::Checked);
		check(parts.ok->isEnabled() && parts.status->isHidden(), "with a and b checked e shares C with an earlier checked train");
		check(parts.list->model()->moveRow(QModelIndex(), 1, QModelIndex(), 0) && rowsOf(parts.list) == QStringList({rowB, rowA, rowC, rowE}),
			"b is moved above a");
		check(parts.firstTrain->text() == "First train of the cycle: " + rowB && entriesOf(parts.cycleEnd) == QStringList({nextPeriod, rowA, rowC, rowE})
				&& parts.cycleEnd->currentData().toString() == "e",
			"the first train and the cycle-closing trains follow the move");
		check(parts.ok->isEnabled() && parts.status->isVisible() && parts.status->property("dialogStatus").toString() == "warning"
				&& parts.status->text() == "a runs ahead of b in these blocks; the analysis keeps the order of the list.",
			"a train listed after one that it runs ahead of is a warning");
		parts.list->item(0)->setCheckState(Qt::Unchecked);
		check(parts.cycleEnd->currentData().toString() == "e", "e stays the cycle-closing train while it is a checked row after the first");
		parts.list->item(3)->setCheckState(Qt::Unchecked);
		check(parts.cycleEnd->currentIndex() == 0, "an unchecked cycle-closing train falls back to the first train one period later");

		// Over C only, c is not listed, the others are listed in their order on C again and b stays unchecked.
		parts.from->setCurrentIndex(1);
		check(rowsOf(parts.list) == QStringList({"06:00:30  a (OC-a), block 2", "06:02:10  b (OC-b), block 2", rowE})
				&& parts.list->item(1)->checkState() == Qt::Unchecked && parts.list->item(2)->checkState() == Qt::Unchecked,
			"another range lists its trains in their order, with the unchecked ones still unchecked");
		parts.to->setCurrentIndex(0);
		check(parts.from->currentIndex() == 0 && parts.to->currentIndex() == 0, "a last block before the first block moves the first block");
		parts.from->setCurrentIndex(1);
		check(parts.from->currentIndex() == 1 && parts.to->currentIndex() == 1, "a first block after the last block moves the last block");
		dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();
	});
	check(!accepted && kept.routeIndex == -1 && kept.blockIds.empty() && kept.occurrenceIds.empty() && kept.cycleEndOccurrenceId.empty(),
		"cancel gives no scope");

	// The dialog opens on a given scope: its range, its trains checked and first in their order, the other trains of the
	// range unchecked, its cycle-closing train and its period, so OK takes it at once.
	CapacityAnalysisScope given;
	given.routeIndex = 0;
	given.blockIds = {"B", "C"};
	given.sectionIndices = {0, 1};
	given.occurrenceIds = {"b", "a"};
	given.cycleEndOccurrenceId = "a";
	given.periodSeconds = 1800.0;
	accepted = askCapacityScope(run, given, [&](QDialog& dialog) {
		const CapacityDialogParts parts = capacityDialogParts(dialog);
		if (!parts.complete())
			return;
		check(rowsOf(parts.list) == QStringList({rowB, rowA, rowC, rowE}) && parts.list->item(0)->checkState() == Qt::Checked
				&& parts.list->item(1)->checkState() == Qt::Checked && parts.list->item(2)->checkState() == Qt::Unchecked
				&& parts.list->item(3)->checkState() == Qt::Unchecked,
			"the trains of the given scope come first, in their order, and the others are unchecked");
		check(parts.cycleEnd->currentData().toString() == "a" && dialog.findChild<QDoubleSpinBox*>()->value() == 1800.0,
			"the cycle-closing train and the period of the given scope are chosen");
		check(parts.status->property("dialogStatus").toString() == "warning", "the given order is a warning");
		parts.ok->click();
	});
	check(accepted && given.blockIds == std::vector<std::string>({"B", "C"}) && given.occurrenceIds == std::vector<std::string>({"b", "a"})
			&& given.cycleEndOccurrenceId == "a" && !given.repeatFirstTrain && given.periodSeconds == 1800.0,
		"OK takes the given scope at once");

	// A given range of C only opens on it.
	CapacityAnalysisScope ofC;
	ofC.routeIndex = 0;
	ofC.blockIds = {"C"};
	ofC.sectionIndices = {1};
	ofC.repeatFirstTrain = true;
	accepted = askCapacityScope(run, ofC, [&](QDialog& dialog) {
		const CapacityDialogParts parts = capacityDialogParts(dialog);
		if (!parts.complete())
			return;
		check(parts.from->currentIndex() == 1 && parts.to->currentIndex() == 1 && parts.list->count() == 3 && parts.cycleEnd->currentIndex() == 0,
			"the dialog opens on the given range with all its trains checked");
		parts.ok->click();
	});
	check(accepted && ofC.blockIds == std::vector<std::string>({"C"}) && ofC.occurrenceIds == std::vector<std::string>({"a", "b", "e"}) && ofC.repeatFirstTrain,
		"OK takes the trains of the given range");

	// The dialog opens on the route of a given scope. Another route lists its blocks and trains, and a train unchecked on
	// a route stays unchecked when that route is chosen again.
	for (const char* id : {"x", "y"}) {
		CompletedTrain train = trainOn(id, 1);
		train.blockTimes = {occupation("X", 0.0, 10.0, 0.0, 100.0, 0.0)};
		run.trains.push_back(train);
	}
	CapacityAnalysisScope chosen;
	chosen.routeIndex = 1;
	accepted = askCapacityScope(run, chosen, [&](QDialog& dialog) {
		const CapacityDialogParts parts = capacityDialogParts(dialog);
		if (!parts.complete())
			return;
		check(parts.route->currentIndex() == 1 && entriesOf(parts.from) == QStringList({"1: X"}) && parts.list->count() == 2,
			"the dialog opens on the given route");
		parts.route->setCurrentIndex(0);
		check(entriesOf(parts.from) == QStringList({"1: B", "2: C"}) && parts.from->currentIndex() == 0 && parts.to->currentIndex() == 1
				&& rowsOf(parts.list) == QStringList({rowA, rowB, rowC, rowE}),
			"the first route lists its blocks and its trains");
		if (parts.list->count() != 4)
			return;
		parts.list->item(2)->setCheckState(Qt::Unchecked);
		parts.route->setCurrentIndex(1);
		parts.route->setCurrentIndex(0);
		check(parts.list->item(2)->checkState() == Qt::Unchecked && parts.list->item(1)->checkState() == Qt::Checked,
			"an unchecked train stays unchecked when its route is chosen again");
		parts.route->setCurrentIndex(1);
		parts.ok->click();
	});
	check(accepted && chosen.routeIndex == 1 && chosen.blockIds == std::vector<std::string>({"X"}) && chosen.sectionIndices == std::vector<int>({0})
			&& chosen.occurrenceIds == std::vector<std::string>({"x", "y"}) && chosen.repeatFirstTrain,
		"the two trains of the given route are taken");
}

// --- Long identifiers ---

// What a scope dialog with long ids has to show: it is as wide as the screen allows and no wider, the body scrolls only
// vertically, the buttons stay below it and inside the dialog, and no choice reaches out of the visible body.
void checkScopeDialogFits(QDialog& dialog, const std::string& what) {
	// Nested layout requests can stay queued in a callback of the modal loop. Settle them before measuring.
	QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
	QApplication::processEvents();
	QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
	const QRect available = dialog.screen()->availableGeometry();
	check(dialog.width() <= available.width() * 9 / 10 && dialog.height() <= available.height() * 4 / 5, what + ": within the screen");
	check(dialog.width() == dialog.maximumWidth(), what + ": widened to show its choices");
	auto* scroll = dialog.findChild<QScrollArea*>("dialogBodyScroll");
	auto* buttons = dialog.findChild<QDialogButtonBox*>();
	check(scroll && buttons, what + ": body and buttons");
	if (!scroll || !buttons)
		return;
	check(scroll->horizontalScrollBar()->maximum() == 0, what + ": no horizontal scrolling");
	check(buttons->isVisible() && buttons->geometry().bottom() <= dialog.height() && scroll->geometry().bottom() <= buttons->geometry().top(),
		what + ": the buttons stay below the body");
	for (const QComboBox* combo : dialog.findChildren<QComboBox*>())
		check(combo->mapTo(scroll->viewport(), QPoint(combo->width(), 0)).x() <= scroll->viewport()->width(), what + ": a choice stays inside the body");
}

// 2000 blocks, the last with a 300-character id.
std::vector<std::string> longBlockIds() {
	std::vector<std::string> ids;
	for (int index = 0; index < 2000; ++index) {
		std::string id = "block-" + std::to_string(index) + "-";
		id.append(index == 1999 ? 300 - id.size() : 30, 'b');
		ids.push_back(std::move(id));
	}
	return ids;
}

// The route combo of a scope dialog lists only short ids, so the dialog opens narrower than the long block ids need where
// the font allows it. Choosing the route with those blocks widens it again.
void checkWidensForLongBlocks(QDialog& dialog, int longRoute, bool opensNarrow, const std::string& what) {
	const auto combos = dialog.findChildren<QComboBox*>();
	check(!combos.isEmpty(), what + ": the dialog has its route combo");
	if (combos.isEmpty())
		return;
	if (opensNarrow)
		check(dialog.width() < dialog.maximumWidth(), what + ": opens narrower than the long block ids need");
	combos[0]->setCurrentIndex(longRoute);
	checkScopeDialogFits(dialog, what + " after choosing the route with the long block ids");
}

// The scope dialogs of a run whose route has a 300-character id and 2000 blocks, at 10 pt, at 18 pt and at 1.25 times 18 pt,
// which covers a platform font that is wider than the macOS one. Each fits the screen, with the route chosen or not, and its
// body does not scroll sideways.
void testScopeDialogsWithLongIds() {
	const QFont original = QApplication::font();
	const std::string routeId(300, 'R');
	const std::vector<std::string> blockIds = longBlockIds();

	BlockingTimeScopeChoices choices;
	choices.caseEndSeconds = 600.0;
	choices.routes.push_back({routeId, std::string(300, 'C'), blockIds});

	CompletedRunData run;
	std::vector<BlockSectionInput> blocks;
	for (std::size_t index = 0; index < blockIds.size(); ++index)
		blocks.push_back({blockIds[index], static_cast<double>(index), static_cast<double>(index + 1)});
	run.routes = {routeOf(routeId, {{"n", 0.0}}, blocks)};
	for (const char* id : {"a", "b", "c"}) {
		CompletedTrain train = trainOn(id, 0);
		train.blockTimes = {occupation(blockIds.front(), 0.0, 10.0, 0.0, 100.0, 0.0)};
		run.trains.push_back(train);
	}

	// Short route ids and a second route with the long block ids.
	BlockingTimeScopeChoices shortChoices;
	shortChoices.caseEndSeconds = 600.0;
	shortChoices.routes = {{"R1", "", {"a", "b"}}, {"R2", "", blockIds}};
	CompletedRunData shortRun;
	shortRun.routes = {routeOf("R1", {{"n", 0.0}}, {{"a", 0.0, 1.0}, {"b", 1.0, 2.0}}), routeOf("R2", {{"n", 0.0}}, blocks)};
	// Two trains on R1, so the capacity dialog opens on it, and two on the last block of R2, whose 300-character id is the
	// last block that the dialog shows for R2.
	for (const char* id : {"m", "n"}) {
		CompletedTrain train = trainOn(id, 0);
		train.blockTimes = {occupation("a", 0.0, 10.0, 0.0, 100.0, 0.0)};
		shortRun.trains.push_back(train);
	}
	for (const char* id : {"p", "q"}) {
		CompletedTrain train = trainOn(id, 1);
		train.blockTimes = {occupation(blockIds.back(), 0.0, 10.0, 0.0, 100.0, 0.0)};
		shortRun.trains.push_back(train);
	}

	for (const double points : {10.0, 18.0, 22.5}) {
		QFont font = original;
		font.setPointSizeF(points);
		QApplication::setFont(font);
		const std::string what = QString::number(points).toStdString() + " pt";

		BlockingTimeScope scope = defaultBlockingTimeScope(choices.caseEndSeconds);
		bool accepted = askScope(choices, scope, [&what](QDialog& dialog) {
			checkScopeDialogFits(dialog, "blocking-time scope with long ids at " + what);
			const auto combos = dialog.findChildren<QComboBox*>();
			if (combos.size() == 3) {
				combos[0]->setCurrentIndex(1);
				QApplication::processEvents();
				checkScopeDialogFits(dialog, "blocking-time scope with 2000 blocks at " + what);
			}
			press(dialog, QDialogButtonBox::Cancel);
		});
		check(!accepted, "the scope dialog with long ids is cancelled");

		CapacityAnalysisScope capacity;
		accepted = askCapacityScope(run, capacity, [&what](QDialog& dialog) {
			checkScopeDialogFits(dialog, "capacity options with 2000 blocks at " + what);
			dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();
		});
		check(!accepted, "the capacity dialog with long ids is cancelled");

		// At 18 pt and 22.5 pt the dialog is as wide as the screen allows from the start.
		const bool opensNarrow = points == 10.0;
		accepted = askScope(shortChoices, scope, [&what, opensNarrow](QDialog& dialog) {
			checkWidensForLongBlocks(dialog, 2, opensNarrow, "blocking-time scope at " + what);
			press(dialog, QDialogButtonBox::Cancel);
		});
		check(!accepted, "the scope dialog with short route ids is cancelled");
		accepted = askCapacityScope(shortRun, capacity, [&what, opensNarrow](QDialog& dialog) {
			checkWidensForLongBlocks(dialog, 1, opensNarrow, "capacity options at " + what);
			dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();
		});
		check(!accepted, "the capacity dialog with short route ids is cancelled");
	}
	QApplication::setFont(original);
}

// The order of the footer and the focus policy of its buttons on macOS, where Cancel comes first.
class MacFooterStyle : public QProxyStyle {
public:
	MacFooterStyle() : QProxyStyle(QStringLiteral("Fusion")) {}
	int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const override {
		if (hint == SH_DialogButtonLayout)
			return QDialogButtonBox::MacLayout;
		if (hint == SH_Button_FocusPolicy)
			return Qt::TabFocus;
		return QProxyStyle::styleHint(hint, option, widget, returnData);
	}
};

// With the footer of macOS the scope dialog opens with OK as the default and the focus on the route. Cancel with the
// focus leaves the default to OK, so Return accepts. Runs last: it changes the application style.
void testScopeDialogMacFooter() {
	QApplication::setStyle(new MacFooterStyle);
	BlockingTimeScopeChoices choices;
	choices.caseEndSeconds = 600.0;
	choices.routes = {{"r0", "north", {"a", "b"}}};
	BlockingTimeScope scope = defaultBlockingTimeScope(choices.caseEndSeconds);
	const bool accepted = askScope(choices, scope, [](QDialog& dialog) {
		QApplication::processEvents();
		auto* buttons = dialog.findChild<QDialogButtonBox*>();
		QPushButton* okButton = buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
		QPushButton* cancelButton = buttons ? buttons->button(QDialogButtonBox::Cancel) : nullptr;
		check(okButton && cancelButton, "the scope dialog has OK and Cancel");
		if (!okButton || !cancelButton)
			return;
		check(dialog.focusWidget() == dialog.findChildren<QComboBox*>().value(0), "the route has the focus when the scope dialog opens");
		check(okButton->isDefault() && !cancelButton->isDefault(), "OK is the default button of the scope dialog and Cancel is not");
		cancelButton->setFocus(Qt::TabFocusReason);
		QApplication::processEvents();
		check(okButton->isDefault() && !cancelButton->isDefault(), "Cancel with the focus leaves the default to OK");
		QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
		QApplication::sendEvent(cancelButton, &enter);
	});
	check(accepted, "Return accepts the scope dialog while Cancel has the focus");
}

} // namespace

int main(int argc, char** argv) {
	QApplication application(argc, argv);
	if (argc < 3) {
		std::cerr << "usage: test_completedrun SCENE_DIR SCENE_DIR...\n";
		return 2;
	}
	QTemporaryDir output;
	if (!output.isValid()) {
		std::cerr << "cannot create a temporary output directory\n";
		return 2;
	}

	std::vector<CapturedRun> runs(static_cast<std::size_t>(argc - 1));
	std::vector<std::string> exports;
	std::vector<OpenWindows> windows;
	std::size_t sharedStations = 0;
	for (int k = 1; k < argc; ++k) {
		CapturedRun& run = runs[static_cast<std::size_t>(k - 1)];
		if (!runScene(argv[k], output.path().toStdString(), run))
			return 1;
		compareCapture(run);
		checkExports(run);
		compareScopeChoices(run);
		checkBlockingDiagrams(run);
		checkCapacity(run);
		checkPartialTrains(run);
		checkRouteDiagrams(run);
		sharedStations += checkDelayDiagrams(run);
		exports.push_back(exportsOf(run.data));
		windows.push_back(openWindows(run));
		checkLabelledChoosers(run);
	}
	check(sharedStations > 0, "trains in opposite directions call at one station of a Delays chart");
	// Every run has been replaced by the next one by now. The data of each must still give what it gave.
	for (std::size_t k = 0; k < runs.size(); ++k) {
		checkSame(runs[k].scene + ": exports of the run after later runs", exports[k], exportsOf(runs[k].data));
		checkSame(runs[k].scene + ": timetable window after later runs", windows[k].timetableCsv, windows[k].timetable->csvContent());
		checkSame(runs[k].scene + ": trajectory window after later runs", windows[k].trajectoryCsv, windows[k].trajectory->csvContent());
		for (std::size_t other = k + 1; other < runs.size(); ++other) {
			check(exports[k] != exports[other], runs[k].scene + " and " + runs[other].scene + " give different exports");
			check(windows[k].timetableCsv != windows[other].timetableCsv && windows[k].trajectoryCsv != windows[other].trajectoryCsv,
				runs[k].scene + " and " + runs[other].scene + " give different window exports");
		}
	}

	testMissingValues();
	testRepeatedStops();
	testRepeatedResource();
	testRouteScopeOfSharedBlocks();
	testReversedRoutes();
	testDelayDiagram();
	testCapacityOrder();
	testCapacityRange();
	testCompressedDiagram();
	testCompressedCopy();
	testCompressedOtherRoute();
	testSharedResources();
	testUsedRoutes();
	testScopeDialog();
	testCapacityDialog();
	testFullTableImage();
	testScopeDialogsWithLongIds();
	testScopeDialogMacFooter();

	if (failures)
		std::cerr << failures << " checks failed\n";
	return failures ? 1 : 0;
}
