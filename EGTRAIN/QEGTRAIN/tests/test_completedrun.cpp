// Tests of the completed-run data of diagrams/CompletedRunData.h.
//
// Two committed scenes run in this process, one after the other. The data captured from the run of the simulation must
// agree with it, the exports and diagram inputs must follow from it, and none of it may change when the next scene
// replaces the run. A result window opened for a scene must keep exporting that scene. Hand-made data then pins
// the adapters on cases that the scenes do not hold: missing times, repeated stops, reversed routes, the order of the
// capacity trains and shared resources.
//
//   test_completedrun SCENE_DIR...

#include "app/DispatchController.h"
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
	// No committed scene has a reversed route. Marking every other route shows the capture both ways.
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
	check(static_cast<int>(choices.trains.size()) == simulation.run().numTrains, scene + ": scope choice trains");
	for (int i = 0; i < simulation.run().numTrains && i < static_cast<int>(choices.trains.size()); ++i)
		check(choices.trains[i].id == simulation.run().trains[i].trainDescription
				&& choices.trains[i].routeIndex == simulation.run().trains[i].indexOfRoute
				&& choices.trains[i].routeId == simulation.run().trains[i].TrainRouteID,
			scene + ": scope choice train " + std::to_string(i));
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
			// The trains keep the order of the run, and each holds an occupation of a block of the scope.
			std::size_t next = 0;
			bool ordered = true, occupied = true;
			for (const CapacityAnalysisTrain& input : inputs) {
				while (next < data.trains.size() && data.trains[next].trainId != input.runtimeId)
					++next;
				ordered = ordered && next < data.trains.size();
				++next;
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
	CapacityAnalysisScope scope = first;
	std::vector<CapacityAnalysisScope> analysed = {scope};
	scope.cycleEndOccurrenceId = scope.occurrenceIds.back();
	analysed.push_back(scope);
	scope.occurrenceIds = {scope.occurrenceIds.back(), "no such train", scope.occurrenceIds.front()};
	scope.cycleEndOccurrenceId = scope.occurrenceIds.front();
	analysed.push_back(scope);
	bool anyCsv = false;
	for (const CapacityAnalysisScope& analysis : analysed)
		anyCsv = anyCsv || !capacityCsvFrom(data, analysis).empty();
	check(anyCsv, scene + ": a capacity_analysis.csv exists");
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
	check(ids(capacityInputsFrom(data, scope)) == "abc", "without a selection the order of the run is kept and trains without occupations are left out");
	scope.occurrenceIds = {"c", "a"};
	check(ids(capacityInputsFrom(data, scope)) == "ca", "the order follows the occurrence ids");
	scope.occurrenceIds = {"b", "missing", "d", "a"};
	check(ids(capacityInputsFrom(data, scope)) == "ba", "ids that are missing or have no occupation are dropped");
	scope.occurrenceIds = {"missing"};
	check(capacityInputsFrom(data, scope).empty(), "a selection of unknown ids gives no train");
	scope.occurrenceIds = {"a", "b"};
	check(capacityScopeIssueFrom(data, scope).empty(), "a unique entry has no issue");
	check(capacityScopeLabelFrom(data, scope).toStdString() == "r / B to B", "label of the scope");
	data.routes[0].corridor = "north";
	check(capacityScopeLabelFrom(data, scope).toStdString() == "r / north / B to B", "label with a corridor");
	scope.routeIndex = 7;
	check(capacityScopeLabelFrom(data, scope).toStdString() == "no selected route" && capacityScopeIssueFrom(data, scope).empty(), "a route that does not exist");

	// Two records of the entry resource in one train cannot be told apart.
	data.trains[1].blockTimes.push_back(occupation("B", 150.0, 160.0, 0.0, 100.0, 150.0));
	scope.routeIndex = 0;
	check(capacityScopeIssueFrom(data, scope).find("Occurrence b has several records") == 0, "an ambiguous entry is named");
	scope.occurrenceIds = {"a"};
	check(capacityScopeIssueFrom(data, scope).empty(), "the ambiguity matters only for the selected occurrences");

	const CapacityAnalysisScope pair = firstCapacityScopeWithPairFrom(data);
	check(pair.routeIndex == 0 && pair.blockIds == std::vector<std::string>({"B"}) && pair.sectionIndices == std::vector<int>({0})
			&& pair.occurrenceIds == std::vector<std::string>({"a", "b", "c"}),
		"first scope with two trains");
	data.trains.resize(1);
	check(firstCapacityScopeWithPairFrom(data).routeIndex == -1, "no scope with two trains");
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
	scope.sectionIndices = {2};
	diagram = blockingTimeDiagramData(data, scope);
	check(diagram && diagram->segments.size() == 1 && diagram->segments[0].startTime == 30.0 && diagram->segments[0].referenceSection == 3,
		"the last section keeps the second envelope of X");
	scope.sectionIndices.clear();
	diagram = blockingTimeDiagramData(data, scope);
	check(diagram && diagram->segments.size() == 2, "a scope without sections keeps both envelopes");
	scope.trainIds.clear();
	diagram = blockingTimeDiagramData(data, scope);
	check(diagram && diagram->segments.empty() && diagram->plannedReferences.empty(), "a route scope without trains shows nothing");
	scope.routeIndex = 4;
	check(!blockingTimeDiagramData(data, scope), "a route that does not exist has no diagram");
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
		check(dialog && dialog->windowTitle() == "Blocking-time scope", "the scope dialog opens");
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
	choices.trains = {{"T0", 0, "r0"}, {"T1", 1, "r1"}, {"T2", 9, "r0"}, {"T3", -1, ""}};

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

	// A route with a range of its blocks takes the trains that run on it, by index or by route id.
	accepted = askScope(choices, scope, [](QDialog& dialog) {
		const auto combos = dialog.findChildren<QComboBox*>();
		check(combos.size() == 3 && combos[0]->count() == 3, "the dialog lists the routes and all routes");
		if (combos.size() != 3)
			return;
		for (const QComboBox* combo : combos)
			check(dynamic_cast<const ChoiceComboBox*>(combo), "the scope dialog shows its choices in choice combos");
		combos[0]->setCurrentIndex(1);
		check(combos[0]->itemText(1) == "r0  (north)" && combos[0]->itemText(2) == "r1  (no corridor)", "the routes show their corridor");
		check(combos[1]->count() == 3 && combos[1]->itemText(2) == "3:c" && combos[2]->currentIndex() == 2, "the blocks of the route are listed");
		combos[1]->setCurrentIndex(1);
		press(dialog, QDialogButtonBox::Ok);
	});
	check(accepted && scope.routeIndex == 0 && scope.sectionIndices == std::vector<int>({1, 2}) && scope.blockIds == std::vector<std::string>({"b", "c"})
			&& scope.trainIds == std::vector<std::string>({"T0", "T2"}),
		"the dialog accepts a route, a range of its blocks and takes its trains");

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
	check(accepted && scope.routeIndex == 1 && scope.sectionIndices == std::vector<int>({0, 1}) && scope.trainIds == std::vector<std::string>({"T1"}),
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

void testCapacityDialog() {
	CompletedRunData run;
	const auto trainWithEntry = [](const std::string& id, double entry) {
		CompletedTrain train = trainOn(id, 0);
		train.blockTimes = {occupation("B", entry, entry + 10.0, 0.0, 100.0, entry)};
		return train;
	};
	run.trains = {trainWithEntry("a", 0.0), trainWithEntry("b", 100.0), trainWithEntry("c", 200.0), trainOn("d", 0)};
	run.routes = {routeOf("r", {{"n", 0.0}}, {{"B", 0.0, 1.0}, {"C", 1.0, 2.0}}), routeOf("s", {{"n", 0.0}}, {{"X", 0.0, 1.0}})};
	run.routes[0].corridor = "north";

	CapacityAnalysisScope none;
	check(!chooseCapacityAnalysisScope(nullptr, CompletedRunData(), none), "a run without routes has nothing to choose");

	CapacityAnalysisScope scope;
	bool accepted = askCapacityScope(run, scope, [](QDialog& dialog) {
		const auto combos = dialog.findChildren<QComboBox*>();
		check(combos.size() == 4, "the dialog has the route, the two blocks and the closing occurrence");
		if (combos.size() != 4)
			return;
		check(combos[0]->count() == 2 && combos[0]->itemText(0) == "r  (north)" && combos[0]->itemText(1) == "s  (no corridor)", "the routes are listed");
		check(combos[1]->count() == 2 && combos[1]->itemText(0) == "1:B" && combos[2]->currentIndex() == 1, "the blocks of the first route are listed");
		for (const QLabel* label : dialog.findChildren<QLabel*>())
			check(!label->text().contains("Gdg"), "the capacity dialog names no station of one case");
		const auto list = dialog.findChild<QListWidget*>();
		combos[2]->setCurrentIndex(0);
		check(list && list->count() == 3, "three trains have an occupation on the first block");
		check(list && list->item(0)->text() == "a | OC-a | canonical scheduled entry + elapsed from RunStartTime" && list->item(0)->checkState() == Qt::Checked, "an occurrence is listed with its code and reference");
		check(combos[3]->count() == 4 && combos[3]->itemText(3) == "c | OC-c", "the closing occurrence can be any listed train");
		const auto spin = dialog.findChild<QDoubleSpinBox*>();
		if (spin)
			spin->setValue(7200.0);
		list->item(1)->setCheckState(Qt::Unchecked);
		combos[3]->setCurrentIndex(3);
		dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
	});
	check(accepted && scope.routeIndex == 0 && scope.blockIds == std::vector<std::string>({"B"}) && scope.sectionIndices == std::vector<int>({0})
			&& scope.occurrenceIds == std::vector<std::string>({"a", "c"}) && scope.cycleEndOccurrenceId == "c" && scope.periodSeconds == 7200.0,
		"the dialog accepts the checked occurrences and the closing one");

	// Fewer than two occurrences, or a closing occurrence that is not after the first one, keeps the dialog open and
	// says why in the dialog. An edit takes the message away, and the caller's scope stays as it was.
	CapacityAnalysisScope kept;
	accepted = askCapacityScope(run, kept, [](QDialog& dialog) {
		const auto combos = dialog.findChildren<QComboBox*>();
		const auto list = dialog.findChild<QListWidget*>();
		const auto* status = dialog.findChild<QLabel*>("capacityScopeStatus");
		auto* ok = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
		check(status, "the dialog has a label for its refusals");
		if (combos.size() != 4 || !list || !status)
			return;
		check(status->isHidden(), "the dialog opens without a message");
		bool noteIsHelp = false;
		for (const QLabel* label : dialog.findChildren<QLabel*>())
			noteIsHelp |= label->text().startsWith("Checked occurrences are analyzed") && label->property("dialogHelp").toBool();
		check(noteIsHelp, "the note of the dialog is a help note");
		combos[2]->setCurrentIndex(0);
		list->item(1)->setCheckState(Qt::Unchecked);
		list->item(2)->setCheckState(Qt::Unchecked);
		forbidMessageBox();
		ok->click();
		check(dialog.isVisible() && status->isVisible() && status->property("dialogStatus").toString() == "error"
				&& status->text() == "Select at least two ordered occurrences with a common entry block.",
			"fewer than two occurrences are refused in the dialog");
		list->item(1)->setCheckState(Qt::Checked);
		check(status->isHidden(), "an edit takes the message away");
		combos[3]->setCurrentIndex(1);
		forbidMessageBox();
		ok->click();
		check(dialog.isVisible() && status->isVisible() && status->property("dialogStatus").toString() == "error"
				&& status->text().startsWith("Choose a checked cycle-closing occurrence"),
			"a closing occurrence that is the first row is refused in the dialog");
		combos[3]->setCurrentIndex(2);
		check(status->isHidden(), "choosing another closing occurrence takes the message away");
		list->item(1)->setCheckState(Qt::Unchecked);
		list->item(2)->setCheckState(Qt::Unchecked);
		forbidMessageBox();
		ok->click();
		check(status->isVisible(), "fewer than two occurrences are refused again");
		dialog.findChild<QDoubleSpinBox*>()->setValue(1800.0);
		check(status->isHidden(), "changing the period takes the message away");
		dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();
	});
	check(!accepted && kept.routeIndex == -1 && kept.blockIds.empty() && kept.occurrenceIds.empty() && kept.cycleEndOccurrenceId.empty(),
		"cancel after a refusal gives no scope");

	// With exactly two checked occurrences the second one is the only one that can close the cycle, so it is chosen.
	CapacityAnalysisScope pair;
	accepted = askCapacityScope(run, pair, [](QDialog& dialog) {
		const auto combos = dialog.findChildren<QComboBox*>();
		const auto list = dialog.findChild<QListWidget*>();
		const auto* status = dialog.findChild<QLabel*>("capacityScopeStatus");
		if (combos.size() != 4 || !list || !status)
			return;
		combos[2]->setCurrentIndex(0);
		check(combos[3]->currentIndex() == 0, "three checked occurrences leave the closing occurrence to the user");
		list->item(1)->setCheckState(Qt::Unchecked);
		check(combos[3]->currentData().toString() == "c", "the second of two checked occurrences closes the cycle");
		list->item(1)->setCheckState(Qt::Checked);
		list->item(2)->setCheckState(Qt::Unchecked);
		check(combos[3]->currentData().toString() == "b", "the choice follows the checked occurrences");
		list->item(2)->setCheckState(Qt::Checked);
		check(combos[3]->currentData().toString() == "b", "a third checked occurrence keeps the choice");
		list->item(2)->setCheckState(Qt::Unchecked);
		forbidMessageBox();
		dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
	});
	check(accepted && pair.occurrenceIds == std::vector<std::string>({"a", "b"}) && pair.cycleEndOccurrenceId == "b",
		"two checked occurrences are accepted without choosing the closing one");
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
	choices.trains = {{"T0", 0, routeId}};

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
	for (int k = 1; k < argc; ++k) {
		CapturedRun& run = runs[static_cast<std::size_t>(k - 1)];
		if (!runScene(argv[k], output.path().toStdString(), run))
			return 1;
		compareCapture(run);
		checkExports(run);
		compareScopeChoices(run);
		checkBlockingDiagrams(run);
		checkCapacity(run);
		checkRouteDiagrams(run);
		exports.push_back(exportsOf(run.data));
		windows.push_back(openWindows(run));
	}
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
	testReversedRoutes();
	testCapacityOrder();
	testSharedResources();
	testUsedRoutes();
	testScopeDialog();
	testCapacityDialog();
	testFullTableImage();
	testScopeDialogsWithLongIds();

	if (failures)
		std::cerr << failures << " checks failed\n";
	return failures ? 1 : 0;
}
