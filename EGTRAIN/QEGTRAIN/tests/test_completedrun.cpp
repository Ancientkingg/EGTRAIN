// Tests of the completed-run data of diagrams/CompletedRunData.h.
//
// Committed scenes run in this process, one after the other. The data captured from the run of the simulation must
// agree with it, the exports and diagram inputs must follow from it, and none of it may change when the next scene
// replaces the run. A result window opened for a scene must keep exporting that scene. On Milano_Brescia the capacity
// of a route is analysed with trains that run over parts of it, and in one of the scenes trains in opposite directions
// must call at one station of a Delays chart. Hand-made data then pins the adapters on cases that the scenes do not
// hold: missing times, repeated stops, the order of the capacity trains and shared resources.
//
// The scenes are read from EGTRAIN_SCENES_DIR. The first slot that needs the run of a scene runs it and keeps it, with
// its exports and the result windows opened at once, for the slots after it. agreesWithSimulation compares the data
// with the state that the run leaves in the simulation, so it runs its scene each time.

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
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QHeaderView>
#include <QImage>
#include <QKeyEvent>
#include <QProxyStyle>
#include <QScopeGuard>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
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

std::vector<std::string> lines(const std::string& text) {
	std::vector<std::string> result;
	std::istringstream stream(text);
	std::string line;
	while (std::getline(stream, line))
		result.push_back(line);
	return result;
}

// Compares two texts line by line and fails with the first line that differs.
void checkSame(const std::string& what, const std::string& expected, const std::string& actual) {
	if (expected == actual)
		return;
	const auto expectedLines = lines(expected);
	const auto actualLines = lines(actual);
	std::size_t index = 0;
	while (index < expectedLines.size() && index < actualLines.size() && expectedLines[index] == actualLines[index])
		++index;
	const auto shown = [index](const std::vector<std::string>& all) {
		return index < all.size() ? all[index].substr(0, 300) : std::string("<end>");
	};
	std::ostringstream text;
	text << what << " differs at line " << index + 1 << " (" << expectedLines.size() << " and " << actualLines.size()
		 << " lines)\n  expected: " << shown(expectedLines) << "\n  actual:   " << shown(actualLines);
	QFAIL(text.str().c_str());
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

// The blocking-time CSV of the whole run for the visible trains, as the window that opens without its scope dialog
// exports it.
std::string blockingTimeCsvOf(const CompletedRunData& data, const QStringList& visible) {
	const std::optional<BlockingTimeDiagramData> d = blockingTimeDiagramData(data, defaultBlockingTimeScopeFrom(data));
	return d ? buildBlockingTimeCsv(visible, d->segments, d->plannedReferences, d->trajectoryRows, d->extents, d->unmapped)
			 : std::string();
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

// The scenes of the test, in the order in which they run.
const char* const kScenes[] = {"Assignment_Gvc_Gdg_Ut", "Paimpol", "Milano_Brescia"};

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

// One row for each scene.
void addSceneRows() {
	QTest::addColumn<QString>("scene");
	for (const char* scene : kScenes)
		QTest::newRow(scene) << QString(scene);
}

// --- The data against the simulation, and what follows from it ---

void compareCapture(const CapturedRun& run) {
	const std::string scene = run.scene;
	const CompletedRunData& data = run.data;
	QVERIFY2(data.settings.timestepSeconds == timestep, (scene + ": timestep").c_str());
	QVERIFY2(data.settings.horizonSteps == initial_variables.times, (scene + ": horizon").c_str());
	QVERIFY2(data.settings.horizonSeconds() == initial_variables.times * timestep, (scene + ": horizon in seconds").c_str());
	QVERIFY2(data.settings.startOffsetSeconds == initial_variables.startingSimulationTime, (scene + ": start offset").c_str());
	const std::string caseName = scene + ": case name";
	QVERIFY2(data.settings.caseName == run.loaded.scene.name, caseName.c_str());
	QVERIFY2(data.provenance.caseName == run.loaded.scene.name, caseName.c_str());
	QVERIFY2(static_cast<int>(data.trains.size()) == simulation.run().numTrains, (scene + ": train count").c_str());
	QVERIFY2(static_cast<int>(data.results.trains.size()) == simulation.run().numTrains, (scene + ": result rows").c_str());
	// The capture reads the run that it is given, whatever the run of the process holds.
	const auto emptyRun = std::make_unique<SimulationRun>();
	const CompletedRunData fromEmptyRun = captureCompletedRun(*emptyRun, &run.loaded.scene, 0, RunProvenance());
	const std::string emptyCapture = scene + ": capture of an empty run";
	QVERIFY2(fromEmptyRun.trains.empty(), emptyCapture.c_str());
	QVERIFY2(fromEmptyRun.routes.empty(), emptyCapture.c_str());
	QVERIFY2(fromEmptyRun.results.trains.empty(), emptyCapture.c_str());
	QVERIFY2(!data.timetable.empty(), (scene + ": timetable rows").c_str());

	QVERIFY2(data.routes.size() == simulation.run().infra.routes.size(), (scene + ": route count").c_str());
	for (std::size_t r = 0; r < data.routes.size() && r < simulation.run().infra.routes.size(); ++r) {
		const CompletedRoute& route = data.routes[r];
		const std::string identity = scene + ": route identity " + std::to_string(r);
		QVERIFY2(route.id == simulation.run().infra.routes[r].ID, identity.c_str());
		QVERIFY2(route.corridor == simulation.run().infra.routes[r].corridor, identity.c_str());
		QVERIFY2(route.reversedDirection == (r % 2 == 0), (scene + ": route direction " + std::to_string(r)).c_str());
		const auto inputs = blockSectionInputsForRoute(simulation.run().infra.routes[r]);
		bool same = inputs.size() == route.blocks.size();
		for (std::size_t b = 0; same && b < inputs.size(); ++b)
			same = inputs[b].id == route.blocks[b].id && inputs[b].startKm == route.blocks[b].startKm && inputs[b].endKm == route.blocks[b].endKm;
		QVERIFY2(same, (scene + ": route blocks " + std::to_string(r)).c_str());
		checkSame(scene + ": route path " + std::to_string(r), describe(routeDiagramPath(simulation.run().infra.routes[r], &run.loaded.scene)),
			describe(route.path));
		if (QTest::currentTestFailed())
			return;
	}

	for (int i = 0; i < simulation.run().numTrains && i < static_cast<int>(data.trains.size()); ++i) {
		const Train& train = simulation.run().trains[i];
		const CompletedTrain& captured = data.trains[i];
		const std::string who = scene + ": " + train.trainDescription;
		const std::string identity = who + ": identity";
		QVERIFY2(captured.trainId == train.trainDescription, identity.c_str());
		QVERIFY2(captured.operatingCode == train.operatingCode, identity.c_str());
		QVERIFY2(captured.serviceId == train.serviceId, identity.c_str());
		QVERIFY2(captured.serviceOccurrence == train.serviceOccurrence, identity.c_str());
		QVERIFY2(captured.routeIndex == train.indexOfRoute, identity.c_str());
		QVERIFY2(captured.routeId == train.TrainRouteID, identity.c_str());
		QVERIFY2(captured.scheduledDepartureTime == train.scheduled_departure_time, identity.c_str());
		QVERIFY2(captured.runStartTime == train.RunStartTime, identity.c_str());

		QVERIFY2(captured.blockTimes.size() == static_cast<std::size_t>(train.N_BlockSections), (who + ": block time count").c_str());
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
			if (QTest::currentTestFailed())
				return;
		}

		const int stopCount = std::min(train.numStations, static_cast<int>(Train::kMaxTimetableStations));
		QVERIFY2(static_cast<int>(captured.stops.size()) == stopCount, (who + ": stop count").c_str());
		std::vector<std::vector<std::string>> allowedSets = {{}, {"no such block"}};
		if (captured.routeIndex >= 0 && captured.routeIndex < static_cast<int>(simulation.run().infra.routes.size())) {
			const Route& route = simulation.run().infra.routes[captured.routeIndex];
			for (int b = 0; b < route.N_Block_Sections; ++b)
				allowedSets.push_back({route.sequence_of_block_sections[b].ID});
		}
		for (int s = 0; s < stopCount && s < static_cast<int>(captured.stops.size()); ++s) {
			const CompletedStop& stop = captured.stops[s];
			const std::string at = who + " stop " + std::to_string(s);
			const std::string fields = at + ": fields";
			QVERIFY2(stop.station == train.stationNameForArrivalStats(s), fields.c_str());
			QVERIFY2(stop.nodeId == train.Stations[s].sceneNodeId, fields.c_str());
			QVERIFY2(stop.routeX == train.Stations[s].X, fields.c_str());
			QVERIFY2(stop.scheduledArrival == train.ScheduledArrivals[s], fields.c_str());
			QVERIFY2(stop.scheduledDeparture == train.ScheduledDepartures[s], fields.c_str());
			QVERIFY2(stop.routePositionMeters == train.stationRoutePositionMeters(s), (at + ": route position").c_str());
			for (const auto& allowed : allowedSets)
				if (stop.onRoute(allowed) != train.stationIsOnRoute(s, allowed))
					QFAIL((at + ": on route, " + std::to_string(allowed.size()) + " blocks allowed").c_str());
		}

		const CompletedTrajectory& trajectory = captured.trajectory;
		const auto segments = train.earliestActiveTrajectoryIndex < 0
			? std::vector<TrajectorySegment>()
			: validTrajectorySegments(train.instant_spatial_position, train.earliestActiveTrajectoryIndex, train.End_Time);
		bool sameSegments = segments.size() == trajectory.segments.size();
		for (std::size_t k = 0; sameSegments && k < segments.size(); ++k)
			sameSegments = segments[k].first == trajectory.segments[k].first && segments[k].last == trajectory.segments[k].last;
		QVERIFY2(sameSegments, (who + ": trajectory segments").c_str());
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
		QVERIFY2(sameSamples, (who + ": trajectory samples").c_str());
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
	QVERIFY2(sameIds, (scene + ": train ids").c_str());

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
		const std::string selectedOnly = label + ": the exports list the selected trains only";
		QVERIFY2(rowsOnlyOf(trajectory, selections[k]), selectedOnly.c_str());
		QVERIFY2(rowsOnlyOf(timetable, selections[k]), selectedOnly.c_str());
		QVERIFY2(rowsOnlyOf(blockingTimeCsvOf(data, selections[k]), selections[k]), selectedOnly.c_str());
		QVERIFY2(dataLines(trajectory).size() == trajectorySamples(selections[k]), (label + ": trajectory.csv has the valid samples").c_str());
		std::size_t timetableRows = 0;
		for (const TimetableResultRow& timetableRow : data.timetable)
			timetableRows += selections[k].contains(QString::fromStdString(timetableRow.trainId)) ? 1 : 0;
		QVERIFY2(dataLines(timetable).size() == timetableRows, (label + ": timetable.csv has the rows of the run").c_str());
	}
	QVERIFY2(!trajectoryCsvFrom(data, ids).empty(), (scene + ": trajectory.csv has rows").c_str());
	QVERIFY2(!timetableCsvFrom(data, ids).empty(), (scene + ": timetable.csv has rows").c_str());
	QVERIFY2(!blockingTimeCsvOf(data, ids).empty(), (scene + ": blocking_time.csv has rows").c_str());
	QVERIFY2(!blockTimesFrom(data).empty(), (scene + ": blocking-time segments exist").c_str());
	const BlockingTimeScope whole = defaultBlockingTimeScope(initial_variables.times * timestep);
	const BlockingTimeScope scope = defaultBlockingTimeScopeFrom(data);
	const std::string defaultScope = scene + ": default blocking-time scope";
	QVERIFY2(whole.startTime == scope.startTime, defaultScope.c_str());
	QVERIFY2(whole.endTime == scope.endTime, defaultScope.c_str());
	QVERIFY2(whole.routeIndex == scope.routeIndex, defaultScope.c_str());
	QVERIFY2(whole.trainIds == scope.trainIds, defaultScope.c_str());
	QVERIFY2(whole.blockIds == scope.blockIds, defaultScope.c_str());
	QVERIFY2(whole.sectionIndices == scope.sectionIndices, defaultScope.c_str());
}

// What the scope dialog offers must be what the simulation holds.
void compareScopeChoices(const CapturedRun& run) {
	const std::string scene = run.scene;
	const BlockingTimeScopeChoices choices = blockingTimeScopeChoicesFrom(run.data);
	const std::string extent = scene + ": scope choices extent";
	QVERIFY2(choices.caseEndSeconds == initial_variables.times * timestep, extent.c_str());
	QVERIFY2(choices.stepSeconds == timestep, extent.c_str());
	QVERIFY2(choices.routes.size() == simulation.run().infra.routes.size(), (scene + ": scope choice routes").c_str());
	for (std::size_t r = 0; r < choices.routes.size() && r < simulation.run().infra.routes.size(); ++r) {
		const Route& route = simulation.run().infra.routes[r];
		std::vector<std::string> blockIds;
		for (int b = 0; b < route.N_Block_Sections; ++b)
			blockIds.push_back(route.sequence_of_block_sections[b].ID);
		const std::string choice = scene + ": scope choice route " + std::to_string(r);
		QVERIFY2(choices.routes[r].id == route.ID, choice.c_str());
		QVERIFY2(choices.routes[r].corridor == route.corridor, choice.c_str());
		QVERIFY2(choices.routes[r].blockIds == blockIds, choice.c_str());
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
		const std::string axis = label + ": the time axis spans the interval of the scope";
		QVERIFY2(diagram->timeRange.first == scopes[k].startTime, axis.c_str());
		QVERIFY2(diagram->timeRange.second == scopes[k].endTime, axis.c_str());
		std::size_t points = 0;
		bool inScope = true;
		for (const BlockingTimeTrajectory& line : diagram->trajectories)
			for (const auto& point : line.points) {
				++points;
				inScope = inScope && point.second >= scopes[k].startTime && point.second <= scopes[k].endTime;
			}
		const std::string samples = label + ": the recorded samples are those of the scope";
		QVERIFY2(points == diagram->trajectoryRows.size(), samples.c_str());
		QVERIFY2(inScope, samples.c_str());
		bool ofScope = true;
		for (const BlockingTimeDiagramSegment& segment : diagram->segments)
			ofScope = ofScope && (scopes[k].trainIds.empty() || std::find(scopes[k].trainIds.begin(), scopes[k].trainIds.end(), segment.trainName) != scopes[k].trainIds.end());
		QVERIFY2(ofScope, (label + ": the envelopes are those of the trains of the scope").c_str());
		withSegments += diagram->segments.empty() ? 0 : 1;
		withPlanned += diagram->plannedReferences.empty() ? 0 : 1;
		withTrajectory += diagram->trajectories.empty() ? 0 : 1;
		withSections += diagram->extents.empty() ? 0 : 1;
	}
	const std::string covered = run.scene + ": the scopes cover envelopes, planned references, trajectories and sections";
	QVERIFY2(withSegments > 1, covered.c_str());
	QVERIFY2(withPlanned > 0, covered.c_str());
	QVERIFY2(withTrajectory > 0, covered.c_str());
	QVERIFY2(withSections > 0, covered.c_str());
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
			const std::string inputsLabel = label + " inputs";
			QVERIFY2(ordered, inputsLabel.c_str());
			QVERIFY2(occupied, inputsLabel.c_str());
			pairs += inputs.size() > 1 ? 1 : 0;
			const QString routeLabel = capacityScopeLabelFrom(data, scopes[k]);
			QVERIFY2(routeLabel.startsWith(QString::fromStdString(data.routes[static_cast<std::size_t>(r)].id)), (label + " label").c_str());
		}
	}
	QVERIFY2(capacityScopeLabelFrom(data, CapacityAnalysisScope()).toStdString() == "no selected route", (scene + ": label without a route").c_str());

	const CapacityAnalysisScope first = firstCapacityScopeWithPairFrom(data);
	if (first.routeIndex < 0) {
		std::cout << scene << ": no scope with two trains, so no capacity analysis\n";
		return;
	}
	QVERIFY2(pairs > 0, (scene + ": scopes with a pair").c_str());
	std::vector<std::string> trainsOfFirst;
	for (const CapacityAnalysisTrain& input : capacityInputsFrom(data, first))
		trainsOfFirst.push_back(input.runtimeId);
	const std::string holdsTrains = scene + ": the first scope with a pair holds its trains";
	QVERIFY2(trainsOfFirst.size() > 1, holdsTrains.c_str());
	QVERIFY2(trainsOfFirst == first.occurrenceIds, holdsTrains.c_str());
	const std::string wholeRoute = scene + ": the first scope with a pair is a whole route, closed by the first train one period later";
	QVERIFY2(first.repeatFirstTrain, wholeRoute.c_str());
	QVERIFY2(!first.blockIds.empty(), wholeRoute.c_str());
	QVERIFY2(first.blockIds.size() == data.routes[static_cast<std::size_t>(first.routeIndex)].blocks.size(), wholeRoute.c_str());
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
	QVERIFY2(anyCsv, (scene + ": a capacity_analysis.csv exists").c_str());
	const CapacityAnalysisResult whole = capacityAnalysisFrom(data, first);
	const std::string wholeAnalysed = scene + ": the whole first route is analysed, closed by the first train one period later";
	QVERIFY2(whole.analyzable, wholeAnalysed.c_str());
	QVERIFY2(whole.cycleTime > 0.0, wholeAnalysed.c_str());
	QVERIFY2(whole.cycleEndIdentity == capacityNextPeriodIdentity(whole.firstIdentity), wholeAnalysed.c_str());
}

// On Milano_Brescia, route8 has 50 blocks, and 30 trains run over parts of it. All of them are analysed over the whole
// route, each from where it enters the route, and the compressed diagram shows every block.
void checkPartialTrains(const CapturedRun& run) {
	const CompletedRunData& data = run.data;
	const auto route8 = std::find_if(data.routes.begin(), data.routes.end(), [](const CompletedRoute& route) { return route.id == "route8"; });
	// Milano_Brescia: route8 has 50 blocks
	QVERIFY(route8 != data.routes.end());
	QCOMPARE(route8->blocks.size(), std::size_t(50));
	const int routeIndex = static_cast<int>(route8 - data.routes.begin());
	const CapacityAnalysisScope scope = capacityRangeScopeFrom(data, routeIndex, 0, 49);
	const std::string holds = "Milano_Brescia: the whole of route8 holds 30 trains (" + std::to_string(scope.occurrenceIds.size()) + ")";
	QVERIFY2(scope.blockIds.size() == 50, holds.c_str());
	QVERIFY2(scope.occurrenceIds.size() == 30, holds.c_str());
	QVERIFY2(scope.repeatFirstTrain, holds.c_str());
	QVERIFY2(capacityScopeIssueFrom(data, scope).empty(), "Milano_Brescia: route8 can be analysed as a whole");
	const CapacityAnalysisResult result = capacityAnalysisFrom(data, scope);
	// Milano_Brescia: route8 with its 30 trains and the first one a period later is analysed
	QVERIFY(result.analyzable);
	QCOMPARE(result.compression.size(), std::size_t(31));
	QCOMPARE_GT(result.cycleTime, 0.0);
	QCOMPARE(result.cycleEndIdentity, capacityNextPeriodIdentity(scope.occurrenceIds.front()));
	const auto diagram = compressedBlockingTimeDiagramData(data, scope, result);
	std::set<std::string> drawn;
	if (diagram)
		for (const auto& segment : diagram->segments)
			drawn.insert(segment.trainName);
	std::string missing;
	for (const std::string& identity : result.trainIdentities)
		if (!drawn.count(identity))
			missing += " " + identity;
	const std::string compressed = "Milano_Brescia: the compressed diagram has the 50 blocks and the occupations of 30 trains and the copy ("
		+ std::to_string(drawn.size()) + " trains, missing" + missing + ")";
	QVERIFY2(diagram, compressed.c_str());
	QVERIFY2(diagram->extents.size() == 50, compressed.c_str());
	QVERIFY2(drawn.size() == 31, compressed.c_str());
	QVERIFY2(drawn.count(result.cycleEndIdentity) == 1, compressed.c_str());
	QVERIFY2(diagram->unmapped.empty(), compressed.c_str());
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
	QVERIFY2(expectedUsed == usedRouteReferencesFrom(run.data), (scene + ": used routes").c_str());
	QVERIFY2(!expectedUsed.empty(), (scene + ": routes are used").c_str());
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
			QVERIFY2(diagram.has_value(), label.c_str());
			QVERIFY2(diagram->referencePath.id == used.second, (label + ": reference route").c_str());
			QVERIFY2(diagram->csvRows.size() == (timetable ? 4 * timetableRows : samples), (label + ": export rows").c_str());
			withSeries += diagram->series.empty() ? 0 : 1;
		}
	QVERIFY2(withSeries >= 2, (scene + ": route diagrams have lines").c_str());
	const std::string withoutReference = scene + ": no diagram without a reference route";
	QVERIFY2(!routeDiagramData(run.data, false, -1), withoutReference.c_str());
	QVERIFY2(!routeDiagramData(run.data, true, static_cast<int>(run.data.routes.size())), withoutReference.c_str());
}

// The Delays chart on each route that a train runs on: a point for each arrival delay of the timetable at a station of
// the route, at a node of that station, in journey order, and every train drawn or listed without a call there. Adds to
// `shared` the number of stations at which trains that run in opposite directions have a point, each at one position for
// all.
void checkDelayDiagrams(const CapturedRun& run, std::size_t& shared) {
	for (const auto& used : usedRouteReferencesFrom(run.data)) {
		const std::string label = run.scene + ": delays on " + used.second;
		const auto diagram = delayDiagramData(run.data, used.first);
		QVERIFY2(diagram, label.c_str());
		QVERIFY2(diagram->referencePath.id == used.second, label.c_str());
		QVERIFY2(diagram->series.size() + diagram->withoutCalls.size() == run.data.trains.size(), (label + ": every train is drawn or listed").c_str());
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
				const std::string atNode = where + " lies at a node of " + point.station;
				QVERIFY2(station != stations.end(), atNode.c_str());
				QVERIFY2(station->second.count(point.positionKm), atNode.c_str());
				QVERIFY2(point.journey > journey, (where + " is in journey order").c_str());
				journey = point.journey;
				const auto timetableRow = std::find_if(run.data.timetable.begin(), run.data.timetable.end(), [&](const TimetableResultRow& candidate) {
					return candidate.trainId == series.trainId && candidate.journeyIndex == point.journey;
				});
				const std::string delay = where + " has the arrival delay of the timetable";
				QVERIFY2(timetableRow != run.data.timetable.end(), delay.c_str());
				QVERIFY2(timetableRow->arrivalDelaySeconds.value == point.delaySeconds, delay.c_str());
				QVERIFY2(timetableRow->callIndex == point.call, delay.c_str());
				++points;
			}
			const auto& line = series.points;
			if (line.size() >= 2 && line.front().positionKm != line.back().positionKm)
				for (const DelayDiagramPoint& point : line)
					calls[point.station].emplace_back(point.positionKm, line.back().positionKm > line.front().positionKm ? 1.0 : -1.0);
		}
		QVERIFY2(points == delays, (label + ": a point for each arrival delay at a station of the route").c_str());
		for (const auto& station : calls) {
			const auto& drawn = station.second;
			if (std::none_of(drawn.begin(), drawn.end(), [](const auto& call) { return call.second > 0; })
				|| std::none_of(drawn.begin(), drawn.end(), [](const auto& call) { return call.second < 0; }))
				continue;
			++shared;
			QVERIFY2(std::all_of(drawn.begin(), drawn.end(), [&drawn](const auto& call) { return call.first == drawn.front().first; }),
				(label + ": trains in both directions are drawn at one position of " + station.first).c_str());
		}
	}
	const std::string withoutReference = run.scene + ": no Delays chart without a reference route";
	QVERIFY2(!delayDiagramData(run.data, -1), withoutReference.c_str());
	QVERIFY2(!delayDiagramData(run.data, static_cast<int>(run.data.routes.size())), withoutReference.c_str());
}

// The first run has to give the same text after the second run has replaced the simulation state.
std::string exportsOf(const CompletedRunData& data) {
	const QStringList ids = trainIdsFrom(data);
	std::string text = trajectoryCsvFrom(data, ids) + timetableCsvFrom(data, ids) + blockingTimeCsvOf(data, ids)
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
	return windows;
}

// The first run of a scene, with what its data exported and what its windows showed right after the run.
struct SceneRun {
	CapturedRun run;
	std::string exports;
	OpenWindows windows;
};

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

// Runs the scope dialog on hand-made choices and lets `answer` act on it once it is open.
bool askScope(const BlockingTimeScopeChoices& choices, BlockingTimeScope& scope, const std::function<void(QDialog&)>& answer) {
	QTimer::singleShot(0, [&answer]() {
		auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
		if (dialog == nullptr)
			QFAIL("the scope dialog opens");
		if (dialog->windowTitle() != "Blocking time") {
			dialog->reject();
			QFAIL("the scope dialog opens");
		}
		answer(*dialog);
		// A dialog that the answer leaves open would never return.
		if (dialog->isVisible())
			dialog->reject();
	});
	return chooseBlockingTimeScope(nullptr, choices, scope);
}

void press(QDialog& dialog, QDialogButtonBox::StandardButton button) {
	auto* buttons = dialog.findChild<QDialogButtonBox*>();
	if (buttons == nullptr || buttons->button(button) == nullptr)
		QFAIL("the scope dialog has its buttons");
	buttons->button(button)->click();
}

// Runs the capacity scope dialog on a hand-made run and lets `answer` act on it once it is open.
bool askCapacityScope(const CompletedRunData& run, CapacityAnalysisScope& scope, const std::function<void(QDialog&)>& answer) {
	QTimer::singleShot(0, [&answer]() {
		auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
		if (dialog == nullptr)
			QFAIL("the capacity dialog opens");
		if (dialog->windowTitle() != "Capacity") {
			dialog->reject();
			QFAIL("the capacity dialog opens");
		}
		answer(*dialog);
		if (dialog->isVisible())
			dialog->reject();
	});
	return chooseCapacityAnalysisScope(nullptr, run, scope);
}

// The three choosers of a run of Paimpol name the blocks of route0 by their labels, with the short ids of the section strip as tooltips.
void checkLabelledChoosers(const CapturedRun& run) {
	const CompletedRunData& data = run.data;
	const auto route0 = std::find_if(data.routes.begin(), data.routes.end(), [](const CompletedRoute& route) { return route.id == "route0"; });
	// Paimpol: the run keeps the labels of the sections of its scene
	QVERIFY(route0 != data.routes.end());
	QCOMPARE(data.sectionLabels.label("@0-B0@"), "B0 0.000 to 0.230 km");
	const int routeIndex = static_cast<int>(route0 - data.routes.begin());

	BlockingTimeScope scope = defaultBlockingTimeScopeFrom(data);
	askScope(blockingTimeScopeChoicesFrom(data), scope, [routeIndex](QDialog& dialog) {
		const auto combos = dialog.findChildren<QComboBox*>();
		if (combos.size() != 3)
			return;
		combos[0]->setCurrentIndex(combos[0]->findData(routeIndex));
		// Paimpol: the blocking-time chooser names the blocks of route0 by their labels
		QCOMPARE(combos[1]->itemText(0), "1: B0 0.000 to 0.230 km");
		QCOMPARE(combos[1]->itemData(0, Qt::ToolTipRole).toString(), "0-B0");
		QCOMPARE(combos[1]->itemText(71), "72: B0 21.010 to 21.235, B1 21.252 to 21.450 km");
		QCOMPARE(combos[1]->itemData(71, Qt::ToolTipRole).toString(), "71-B0/0-B1");
		dialog.reject();
	});
	if (QTest::currentTestFailed())
		return;

	CapacityAnalysisScope capacity = capacityRangeScopeFrom(data, routeIndex, 35, 71);
	// The window names the blocks by their labels; the export keeps the ids.
	// Paimpol: the capacity window names its section by block numbers with the labels in the tooltip, the export by ids
	QVERIFY(capacityScopeTextFrom(data, capacity).startsWith("route0"));
	QVERIFY(capacityScopeTextFrom(data, capacity).endsWith(", blocks 36 to 72"));
	QCOMPARE(capacityScopeBlocksText(data, capacity), "First block 36: B0 10.210 to 10.510 km\nLast block 72: B0 21.010 to 21.235, B1 21.252 to 21.450 km");
	QVERIFY(capacityScopeLabelFrom(data, capacity).endsWith(" / @35-B0@ to @71-B0@-21.235000/@0-B1@-21.252000"));
	const std::vector<CapacityHeadwayEvidence> evidence = {{"@35-B0@", "@0-B0@", 12.0, 2.0, 10.0}};
	// Paimpol: the evidence names its blocks by labels in the tooltip, by short ids in the table and by ids with its figures in the export
	QCOMPARE(capacityEvidenceText(evidence, &data.sectionLabels), "B0 10.210 to 10.510 km / B0 0.000 to 0.230 km: leader offset 12 s, follower offset 2 s, headway 10 s");
	QCOMPARE(capacityEvidenceText(evidence), "@35-B0@/@0-B0@ (leader=12 s; follower=2 s; candidate=10 s)");
	QCOMPARE(capacityEvidenceBlocks(evidence), "35-B0 / 0-B0");
	askCapacityScope(data, capacity, [](QDialog& dialog) {
		const auto* from = dialog.findChild<QComboBox*>("capacityFromBlock");
		const auto* to = dialog.findChild<QComboBox*>("capacityToBlock");
		if (!from || !to)
			return;
		// Paimpol: the capacity chooser names the blocks of route0 by their labels and opens on the given range
		QCOMPARE(from->itemText(0), "1: B0 0.000 to 0.230 km");
		QCOMPARE(from->itemData(0, Qt::ToolTipRole).toString(), "0-B0");
		QCOMPARE(from->currentText(), "36: B0 10.210 to 10.510 km");
		QCOMPARE(from->currentData(Qt::ToolTipRole).toString(), "35-B0");
		QCOMPARE(to->currentText(), "72: B0 21.010 to 21.235, B1 21.252 to 21.450 km");
		QCOMPARE(to->currentData(Qt::ToolTipRole).toString(), "71-B0/0-B1");
		dialog.reject();
	});
	if (QTest::currentTestFailed())
		return;

	AreaCapacityScopeDialog area(data);
	auto* routes = area.findChild<QComboBox*>("areaCapacityRoute");
	if (routes)
		routes->setCurrentIndex(routes->findData(routeIndex));
	const auto* first = area.findChild<QComboBox*>("areaCapacityFirstBlock");
	// Paimpol: the capacity by area chooser names the blocks of route0 by their labels
	if (first == nullptr)
		QFAIL("Paimpol: the capacity by area chooser names the blocks of route0 by their labels");
	QCOMPARE(first->itemText(0), "1: B0 0.000 to 0.230 km");
	QCOMPARE(first->itemData(0, Qt::ToolTipRole).toString(), "0-B0");
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

void findCapacityDialogParts(QDialog& dialog, CapacityDialogParts& parts) {
	const auto combos = dialog.findChildren<QComboBox*>();
	parts.route = combos.value(0);
	parts.from = dialog.findChild<QComboBox*>("capacityFromBlock");
	parts.to = dialog.findChild<QComboBox*>("capacityToBlock");
	parts.cycleEnd = dialog.findChild<QComboBox*>("capacityCycleEnd");
	parts.list = dialog.findChild<QListWidget*>();
	parts.firstTrain = dialog.findChild<QLabel*>("capacityFirstTrain");
	parts.status = dialog.findChild<QLabel*>("capacityScopeStatus");
	parts.ok = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
	QVERIFY2(combos.size() == 4, "the dialog has the route, the first and the last block and the cycle-closing train");
	QVERIFY2(parts.complete(), "the capacity dialog has all its parts");
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

// --- Long identifiers ---

// What a scope dialog with long ids has to show: it is as wide as the screen allows and no wider, the body scrolls only
// vertically, the buttons stay below it and inside the dialog, and no choice reaches out of the visible body.
void checkScopeDialogFits(QDialog& dialog, const std::string& what) {
	// Nested layout requests can stay queued in a callback of the modal loop. Settle them before measuring.
	QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
	QApplication::processEvents();
	QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
	const QRect available = dialog.screen()->availableGeometry();
	const std::string within = what + ": within the screen";
	QVERIFY2(dialog.width() <= available.width() * 9 / 10, within.c_str());
	QVERIFY2(dialog.height() <= available.height() * 4 / 5, within.c_str());
	QVERIFY2(dialog.width() == dialog.maximumWidth(), (what + ": widened to show its choices").c_str());
	auto* scroll = dialog.findChild<QScrollArea*>("dialogBodyScroll");
	auto* buttons = dialog.findChild<QDialogButtonBox*>();
	if (scroll == nullptr || buttons == nullptr)
		QFAIL((what + ": body and buttons").c_str());
	QVERIFY2(scroll->horizontalScrollBar()->maximum() == 0, (what + ": no horizontal scrolling").c_str());
	const std::string below = what + ": the buttons stay below the body";
	QVERIFY2(buttons->isVisible(), below.c_str());
	QVERIFY2(buttons->geometry().bottom() <= dialog.height(), below.c_str());
	QVERIFY2(scroll->geometry().bottom() <= buttons->geometry().top(), below.c_str());
	for (const QComboBox* combo : dialog.findChildren<QComboBox*>())
		QVERIFY2(combo->mapTo(scroll->viewport(), QPoint(combo->width(), 0)).x() <= scroll->viewport()->width(), (what + ": a choice stays inside the body").c_str());
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
	QVERIFY2(!combos.isEmpty(), (what + ": the dialog has its route combo").c_str());
	if (opensNarrow)
		QVERIFY2(dialog.width() < dialog.maximumWidth(), (what + ": opens narrower than the long block ids need").c_str());
	combos[0]->setCurrentIndex(longRoute);
	checkScopeDialogFits(dialog, what + " after choosing the route with the long block ids");
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

} // namespace

class TestCompletedRun : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void agreesWithSimulation_data();
	void agreesWithSimulation();
	void analysesCapacity_data();
	void analysesCapacity();
	void analysesPartialTrains();
	void drawsDelays();
	void windowsExportTheirRun_data();
	void windowsExportTheirRun();
	void namesBlocksByLabels();
	void keepsExportsAfterLaterRuns();
	void leavesTrajectoryCellsEmpty();
	void skipsPlannedReferencesWithoutTime();
	void leavesTimetableCellsEmpty();
	void dropsIncompleteBlocks();
	void choosesCapacityReference();
	void limitsDefaultScope();
	void placesRepeatedStops();
	void keepsRepeatedResourceApart();
	void showsTrainsOfSharedBlocks();
	void mirrorsReversedRoutes();
	void placesDelaysAtStations();
	void ordersCapacityTrains();
	void analysesCapacityRange();
	void compressesDiagram();
	void compressesCopyOnePeriodLater();
	void compressesOtherRoute();
	void sharesResources();
	void listsUsedRoutes();
	void choosesBlockingTimeScope();
	void choosesCapacityScope();
	void picturesWholeTable();
	void fitsScopeDialogsWithLongIds();
	void keepsMacFooterOfScopeDialog();

private:
	const CapturedRun* runNow(const std::string& scene);
	const SceneRun* firstRun(const std::string& scene);

	// The output folder of the runs.
	QTemporaryDir m_output;
	// The first run of each scene, by the name of the scene.
	std::map<std::string, std::unique_ptr<SceneRun>> m_runs;
	// The latest run of a scene that had run before.
	std::unique_ptr<SceneRun> m_rerun;
};

// Runs the scene now and gives its data, or nullptr when it does not run. The first run of each scene is kept, with its
// exports and its windows.
const CapturedRun* TestCompletedRun::runNow(const std::string& scene) {
	auto latest = std::make_unique<SceneRun>();
	if (!runScene(EGTRAIN_SCENES_DIR "/" + scene, m_output.path().toStdString(), latest->run))
		return nullptr;
	if (m_runs.count(scene) > 0) {
		m_rerun = std::move(latest);
		return &m_rerun->run;
	}
	latest->exports = exportsOf(latest->run.data);
	latest->windows = openWindows(latest->run);
	const CapturedRun* run = &latest->run;
	m_runs[scene] = std::move(latest);
	return run;
}

// The first run of the scene, made now when the scene has not run yet, or nullptr when it does not run.
const SceneRun* TestCompletedRun::firstRun(const std::string& scene) {
	if (m_runs.count(scene) == 0 && runNow(scene) == nullptr)
		return nullptr;
	return m_runs[scene].get();
}

void TestCompletedRun::initTestCase() {
	QVERIFY2(m_output.isValid(), "cannot create a temporary output directory");
}

void TestCompletedRun::agreesWithSimulation_data() {
	addSceneRows();
}

// The data captured from the run agrees with the simulation, and the exports, the scope choices and the diagrams follow
// from it. The checks read the state that the run leaves in the simulation and the next run replaces.
void TestCompletedRun::agreesWithSimulation() {
	QFETCH(QString, scene);
	const CapturedRun* run = runNow(scene.toStdString());
	if (run == nullptr)
		QFAIL("the scene runs");
	compareCapture(*run);
	if (QTest::currentTestFailed())
		return;
	checkExports(*run);
	if (QTest::currentTestFailed())
		return;
	compareScopeChoices(*run);
	if (QTest::currentTestFailed())
		return;
	checkBlockingDiagrams(*run);
	if (QTest::currentTestFailed())
		return;
	checkRouteDiagrams(*run);
}

void TestCompletedRun::analysesCapacity_data() {
	addSceneRows();
}

void TestCompletedRun::analysesCapacity() {
	QFETCH(QString, scene);
	const SceneRun* first = firstRun(scene.toStdString());
	if (first == nullptr)
		QFAIL("the scene runs");
	checkCapacity(first->run);
}

void TestCompletedRun::analysesPartialTrains() {
	const SceneRun* first = firstRun("Milano_Brescia");
	if (first == nullptr)
		QFAIL("the scene runs");
	checkPartialTrains(first->run);
}

// The Delays charts of each scene, and in one of the scenes trains in opposite directions call at one station of a chart.
void TestCompletedRun::drawsDelays() {
	std::size_t sharedStations = 0;
	for (const char* scene : kScenes) {
		const SceneRun* first = firstRun(scene);
		if (first == nullptr)
			QFAIL("the scene runs");
		checkDelayDiagrams(first->run, sharedStations);
		if (QTest::currentTestFailed())
			return;
	}
	QVERIFY2(sharedStations > 0, "trains in opposite directions call at one station of a Delays chart");
}

void TestCompletedRun::windowsExportTheirRun_data() {
	addSceneRows();
}

// The windows opened right after the run export what the data of the run exports.
void TestCompletedRun::windowsExportTheirRun() {
	QFETCH(QString, scene);
	const SceneRun* first = firstRun(scene.toStdString());
	if (first == nullptr)
		QFAIL("the scene runs");
	const CapturedRun& run = first->run;
	const OpenWindows& windows = first->windows;
	const QStringList ids = trainIdsFrom(run.data);
	const std::string exported = run.scene + ": the windows export rows";
	QVERIFY2(!windows.timetableCsv.empty(), exported.c_str());
	QVERIFY2(!windows.trajectoryCsv.empty(), exported.c_str());
	checkSame(run.scene + ": timetable window and timetable.csv", timetableCsvFrom(run.data, ids), windows.timetableCsv);
	if (QTest::currentTestFailed())
		return;
	checkSame(run.scene + ": trajectory window and trajectory.csv", trajectoryCsvFrom(run.data, ids), windows.trajectoryCsv);
}

void TestCompletedRun::namesBlocksByLabels() {
	const SceneRun* first = firstRun("Paimpol");
	if (first == nullptr)
		QFAIL("the scene runs");
	checkLabelledChoosers(first->run);
}

// Every run has been replaced by the next one by now. The data of each must still give what it gave.
void TestCompletedRun::keepsExportsAfterLaterRuns() {
	std::vector<const SceneRun*> runs;
	for (const char* scene : kScenes) {
		const SceneRun* first = firstRun(scene);
		if (first == nullptr)
			QFAIL("the scene runs");
		runs.push_back(first);
	}
	for (std::size_t k = 0; k < runs.size(); ++k) {
		const SceneRun& earlier = *runs[k];
		checkSame(earlier.run.scene + ": exports of the run after later runs", earlier.exports, exportsOf(earlier.run.data));
		if (QTest::currentTestFailed())
			return;
		checkSame(earlier.run.scene + ": timetable window after later runs", earlier.windows.timetableCsv, earlier.windows.timetable->csvContent());
		if (QTest::currentTestFailed())
			return;
		checkSame(earlier.run.scene + ": trajectory window after later runs", earlier.windows.trajectoryCsv, earlier.windows.trajectory->csvContent());
		if (QTest::currentTestFailed())
			return;
		for (std::size_t other = k + 1; other < runs.size(); ++other) {
			const SceneRun& later = *runs[other];
			QVERIFY2(earlier.exports != later.exports, (earlier.run.scene + " and " + later.run.scene + " give different exports").c_str());
			const std::string windowExports = earlier.run.scene + " and " + later.run.scene + " give different window exports";
			QVERIFY2(earlier.windows.timetableCsv != later.windows.timetableCsv, windowExports.c_str());
			QVERIFY2(earlier.windows.trajectoryCsv != later.windows.trajectoryCsv, windowExports.c_str());
		}
	}
}

// A trajectory sample that the train did not record is an empty cell, and so is a value that is not finite.
void TestCompletedRun::leavesTrajectoryCellsEmpty() {
	const double nan = std::numeric_limits<double>::quiet_NaN();
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
	// trajectory rows of the hand-made train
	QCOMPARE(rows.size(), std::size_t(3));
	// first trajectory row
	QCOMPARE(rows[0], row({"T1", "OC-T1", "S", "2", f(6.0), f(100.0), f(1.0), f(5.0), f(2.0), f(1.0 * kEnergyMJToKWh), "B1"}));
	// second trajectory row has empty cells
	QCOMPARE(rows[1], row({"T1", "OC-T1", "S", "2", f(8.0), f(200.0), "", f(6.0), "", f(2.0 * kEnergyMJToKWh), ""}));
	// third trajectory row
	QCOMPARE(rows[2], row({"T1", "OC-T1", "S", "2", f(10.0), f(300.0), "", f(7.0), "", f(3.0 * kEnergyMJToKWh), "B2"}));
	QVERIFY2(trajectoryCsvFrom(data, QStringList()).empty(), "no visible train gives no trajectory text");
	data.trains[0].trajectory.segments.clear();
	QVERIFY2(trajectoryCsvFrom(data, QStringList{"T1"}).empty(), "a train without valid samples gives no trajectory text");
}

// A time that is absent, negative or not finite gives no planned reference; a stop that cannot be placed leaves a gap.
void TestCompletedRun::skipsPlannedReferencesWithoutTime() {
	const double nan = std::numeric_limits<double>::quiet_NaN();
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
	// the stop outside the path is counted as omitted
	QCOMPARE(omitted, 1);
	// planned references of the stops with times
	QCOMPARE(references.size(), std::size_t(4));
	// arrival of the second stop
	QCOMPARE(references[0].eventType, "arrival");
	QCOMPARE_EQ(references[0].time, 100.0);
	QCOMPARE_EQ(references[0].positionKm, 1.0);
	QCOMPARE(references[0].stationName, "B");
	// both times of the third stop
	QCOMPARE_EQ(references[1].time, 200.0);
	QCOMPARE_EQ(references[2].time, 210.0);
	QCOMPARE_EQ(references[2].positionKm, 2.0);
	// the stop that cannot be placed leaves a gap
	QVERIFY(std::isnan(references[3].time));
	QVERIFY(references[3].stationName.empty());
}

// A timetable value that is not available is an empty cell.
void TestCompletedRun::leavesTimetableCellsEmpty() {
	CompletedRunData data;
	data.timetable = {rowOf("T", "A", 1, 1, kAbsent, at(60.0), kAbsent, at(65.0))};
	data.timetable[0].departureDelaySeconds = at(5.0);
	const auto rows = dataLines(timetableCsvFrom(data, QStringList{"T"}));
	// timetable row with absent times
	QCOMPARE(rows.size(), std::size_t(1));
	QCOMPARE(rows[0], row({"T", "A", "1", "", "", f(60.0), "", f(65.0), "", f(5.0)}));
	QVERIFY2(timetableCsvFrom(data, QStringList{"U"}).empty(), "a train that is not visible has no timetable text");
}

// A block without a complete, ordered, finite record makes no segment.
void TestCompletedRun::dropsIncompleteBlocks() {
	const double nan = std::numeric_limits<double>::quiet_NaN();
	CompletedRunData data;
	CompletedTrain train = trainOn("T", 0);
	BlockingTimeDiagramInput incomplete = occupation("B1", 0.0, 10.0, 0.0, 100.0);
	incomplete.isComplete = false;
	train.blockTimes = {incomplete, occupation("B2", 10.0, nan, 100.0, 200.0), occupation("B3", 20.0, 30.0, 200.0, 300.0), occupation("B4", 30.0, 40.0, -1.0, 400.0)};
	train.blockTimes.push_back(occupation("B5", 40.0, 50.0, 400.0, 500.0));
	train.blockTimes.back().endClearTime = -1.0;
	data.trains = {train};
	const auto segments = blockTimesFrom(data);
	// only the complete, finite and cleared block makes a segment
	QCOMPARE(segments.size(), std::size_t(1));
	QCOMPARE(segments[0].blockId, "B3");
	QCOMPARE(segments[0].trainName, "T");
}

// The reference time of a capacity train: absent or non-finite entry times drop the train; boundary stops win over the
// canonical entry; an absent scheduled entry falls back to the profile.
void TestCompletedRun::choosesCapacityReference() {
	const double nan = std::numeric_limits<double>::quiet_NaN();
	CapacityAnalysisScope scope;
	scope.blockIds = {"B"};
	CompletedTrain train = trainOn("T", 0);
	train.blockTimes = {occupation("B", 10.0, 20.0, 1000.0, 1100.0, 12.0)};
	train.scheduledDepartureTime = 30.0;
	train.runStartTime = 5;
	CapacityAnalysisTrain result = capacityInputFrom(train, scope);
	// capacity train with a profile reference
	QCOMPARE(result.occupations.size(), std::size_t(1));
	QCOMPARE_EQ(result.profileReferenceTime, 12.0);
	// canonical scheduled entry
	QCOMPARE_EQ(result.scheduledReferenceTime, 30.0 + (12.0 - 5.0));
	QCOMPARE(result.referenceSource, "canonical scheduled entry + elapsed from RunStartTime");
	// label of the block entry
	QCOMPARE(result.referenceLabel, "block B entry");

	train.scheduledDepartureTime = -1.0;
	result = capacityInputFrom(train, scope);
	// fallback to the profile
	QCOMPARE_EQ(result.scheduledReferenceTime, 12.0);
	QCOMPARE(result.referenceSource, "profile reference fallback (scheduled entry unavailable)");

	train.scheduledDepartureTime = 30.0;
	train.stops = {stopAt("Far", "x", 1.006, 55.0, 56.0), stopAt("Near", "y", 1.003, 40.0, nan)};
	result = capacityInputFrom(train, scope);
	// a boundary stop within 5 m gives the authored arrival when the departure is absent
	QCOMPARE_EQ(result.scheduledReferenceTime, 40.0);
	QCOMPARE(result.referenceLabel, "Near boundary");
	QCOMPARE(result.referenceSource, "authored ScheduledArrival at boundary station (departure absent)");
	train.stops[1].scheduledDeparture = 50.0;
	result = capacityInputFrom(train, scope);
	// the departure wins over the arrival
	QCOMPARE_EQ(result.scheduledReferenceTime, 50.0);
	QCOMPARE(result.referenceSource, "authored ScheduledDeparture at boundary station");
	// The windows name the reference time in plain words; the export keeps the source as it is.
	// the windows name each reference source in plain words
	QCOMPARE(capacityReferenceText(result.referenceSource, result.referenceLabel), "planned departure at Near");
	QCOMPARE(capacityReferenceText("authored ScheduledArrival at boundary station (departure absent)", "Near boundary"), "planned arrival at Near");
	QCOMPARE(capacityReferenceText("canonical scheduled entry + elapsed from RunStartTime", "block B entry"), "planned entry time");
	QCOMPARE(capacityReferenceText("profile reference fallback (scheduled entry unavailable)", "block B entry"), "simulated entry time");
	QVERIFY(capacityReferenceText("", "").isEmpty());
	train.stops[1].scheduledDeparture = -1.0;
	train.stops[1].scheduledArrival = nan;
	result = capacityInputFrom(train, scope);
	// a boundary stop without a usable time leaves the canonical entry
	QCOMPARE_EQ(result.scheduledReferenceTime, 37.0);

	train.blockTimes[0].startRunTime = -1.0;
	QVERIFY2(capacityInputFrom(train, scope).occupations.empty(), "a negative entry time drops the occupations");
	train.blockTimes[0].startRunTime = nan;
	QVERIFY2(capacityInputFrom(train, scope).occupations.empty(), "a non-finite entry time drops the occupations");
	QVERIFY2(capacityInputFrom(train, CapacityAnalysisScope()).occupations.empty(), "a scope without blocks has no occupations");
}

void TestCompletedRun::limitsDefaultScope() {
	CompletedRunData data;
	data.settings.timestepSeconds = 2.0;
	data.settings.horizonSteps = 10.0;
	// the default scope ends at the horizon
	QCOMPARE_EQ(defaultBlockingTimeScopeFrom(data).endTime, 20.0);
	QCOMPARE_EQ(defaultBlockingTimeScopeFrom(data).startTime, 0.0);
	data.settings.horizonSteps = -4.0;
	// the default scope has no negative end
	QCOMPARE_EQ(defaultBlockingTimeScopeFrom(data).endTime, 0.0);
	QVERIFY2(capacityAnalysisCsv(CapacityAnalysisResult(), QString("label")).empty(), "a result that cannot be analysed has no text");
}

void TestCompletedRun::placesRepeatedStops() {
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
	// planned references of the three stops
	QCOMPARE(omitted, 0);
	QCOMPARE(references.size(), std::size_t(4));
	// first visit of A
	QCOMPARE(references[0].eventType, "departure");
	QCOMPARE_EQ(references[0].time, 60.0);
	QCOMPARE_EQ(references[0].positionKm, 0.0);
	QCOMPARE(references[0].stationName, "A");
	// stop B in journey order
	QCOMPARE(references[1].eventType, "arrival");
	QCOMPARE_EQ(references[1].positionKm, 3.0);
	QCOMPARE(references[2].eventType, "departure");
	// second visit of A
	QCOMPARE(references[3].eventType, "arrival");
	QCOMPARE_EQ(references[3].time, 200.0);
	QCOMPARE_EQ(references[3].positionKm, 6.0);
	QCOMPARE(references[3].stationName, "A");

	BlockingTimeScope scope = defaultBlockingTimeScopeFrom(data);
	scope.blockIds = {"b2"};
	omitted = 0;
	const auto second = plannedReferencesFrom(data, scope, paths, projections, omitted);
	// a block scope keeps the visit on that block only
	QCOMPARE(second.size(), std::size_t(1));
	QCOMPARE_EQ(second[0].time, 200.0);
	// a stop is on the route within its own block only
	QVERIFY(data.trains[0].stops[0].onRoute());
	QVERIFY(data.trains[0].stops[0].onRoute({"b0"}));
	QVERIFY(!data.trains[0].stops[0].onRoute({"b2"}));
	QVERIFY(!data.trains[0].stops[0].onRoute({"zz"}));
	// a station off the route is never on it
	QVERIFY(!stopAt("X", "n", 0.0, 0.0, 0.0).onRoute());
	QVERIFY(!stopAt("X", "n", 0.0, 0.0, 0.0).onRoute({"b0"}));

	const auto diagram = routeDiagramData(data, true, 0);
	QVERIFY2(diagram.has_value(), "timetable graph of the loop route");
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
	if (QTest::currentTestFailed())
		return;

	// The planned arrival of the first visit is absent, so its line starts with the second stop and runs to the second visit.
	const RouteDiagramSeries* plannedArrival = nullptr;
	for (const auto& series : diagram->series)
		if (series.event == "planned arrival")
			plannedArrival = &series;
	// a line of planned arrivals
	if (plannedArrival == nullptr)
		QFAIL("a line of planned arrivals");
	QVERIFY(plannedArrival->planned);
	QCOMPARE(plannedArrival->kind, RouteDiagramSeries::Kind::Event);
	// the planned arrival line has two points
	QCOMPARE(plannedArrival->points.size(), std::size_t(2));
	// second stop
	QCOMPARE_EQ(plannedArrival->points[0].positionKm, 3.0);
	QCOMPARE(plannedArrival->points[0].call, 1);
	QCOMPARE(plannedArrival->points[0].journey, 2);
	// the second visit of A is call 2
	QCOMPARE_EQ(plannedArrival->points[1].positionKm, 6.0);
	QCOMPARE(plannedArrival->points[1].call, 2);
	QCOMPARE(plannedArrival->points[1].station, "A");
	std::size_t dwells = 0;
	for (const auto& series : diagram->series)
		dwells += series.kind == RouteDiagramSeries::Kind::Dwell ? 1 : 0;
	// a dwell where arrival and departure both exist
	QCOMPARE(dwells, std::size_t(3));
}

void TestCompletedRun::keepsRepeatedResourceApart() {
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
	// the first section keeps the first envelope of X
	QVERIFY(diagram);
	QCOMPARE(diagram->segments.size(), std::size_t(1));
	QCOMPARE_EQ(diagram->segments[0].startTime, 10.0);
	QCOMPARE(diagram->segments[0].referenceSection, 1);
	// the extent of the first section
	QVERIFY(diagram);
	QCOMPARE(diagram->extents.size(), std::size_t(1));
	QCOMPARE(diagram->extents[0].id, "X");
	QCOMPARE_EQ(diagram->extents[0].startKm, 0.0);
	// the strip counts the sections of the whole route
	QVERIFY(diagram);
	QCOMPARE(diagram->routeSectionCount, 3);
	scope.sectionIndices = {2};
	diagram = blockingTimeDiagramData(data, scope);
	// the last section keeps the second envelope of X
	QVERIFY(diagram);
	QCOMPARE(diagram->segments.size(), std::size_t(1));
	QCOMPARE_EQ(diagram->segments[0].startTime, 30.0);
	QCOMPARE(diagram->segments[0].referenceSection, 3);
	scope.sectionIndices.clear();
	diagram = blockingTimeDiagramData(data, scope);
	// a scope without sections keeps both envelopes
	QVERIFY(diagram);
	QCOMPARE(diagram->segments.size(), std::size_t(2));
	scope.trainIds.clear();
	diagram = blockingTimeDiagramData(data, scope);
	// a route scope without a train list keeps the envelopes of every train
	QVERIFY(diagram);
	QCOMPARE(diagram->segments.size(), std::size_t(2));
	scope.routeIndex = 4;
	QVERIFY2(!blockingTimeDiagramData(data, scope), "a route that does not exist has no diagram");
}

void TestCompletedRun::showsTrainsOfSharedBlocks() {
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
	// a route scope shows a train of another route that occupies its blocks
	QVERIFY(diagram);
	QCOMPARE(diagram->referencePath.id, "r0");
	QCOMPARE(diagram->segments.size(), std::size_t(2));
	QCOMPARE(diagram->segments[0].trainName, "T");
	QCOMPARE(diagram->segments[1].trainName, "T");
	// and the planned stops of that train on its blocks
	QVERIFY(diagram);
	QVERIFY(!diagram->plannedReferences.empty());
	// A route without blocks has nothing in its scope.
	scope.blockIds.clear();
	scope.sectionIndices.clear();
	const auto empty = blockingTimeDiagramData(data, scope);
	// a route without blocks shows no train
	QVERIFY(empty);
	QVERIFY(empty->segments.empty());
	QVERIFY(empty->plannedReferences.empty());
}

void TestCompletedRun::mirrorsReversedRoutes() {
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
	QVERIFY2(paths.has_value(), "train paths on the east route");
	// the sample outside the route splits the line
	QCOMPARE(paths->series.size(), std::size_t(2));
	// trajectory of the west train
	QCOMPARE(paths->series[0].kind, RouteDiagramSeries::Kind::Trajectory);
	QCOMPARE(paths->series[0].routeId, "west");
	// three projected samples before the gap
	QCOMPARE(paths->series[0].points.size(), std::size_t(3));
	// the projection runs the other way
	QCOMPARE_EQ(paths->series[0].points[0].positionKm, 5.0);
	QCOMPARE_EQ(paths->series[0].points[1].positionKm, 4.0);
	QCOMPARE_EQ(paths->series[0].points[2].positionKm, 3.0);
	// time of the third sample
	QCOMPARE_EQ(paths->series[0].points[2].seconds, 2.0);
	// the line resumes after the gap
	QCOMPARE(paths->series[1].points.size(), std::size_t(1));
	QCOMPARE_EQ(paths->series[1].points[0].positionKm, 1.0);
	QCOMPARE_EQ(paths->series[1].points[0].seconds, 4.0);
	// the unprojectable sample is an export row without a position
	QCOMPARE(paths->csvRows.size(), std::size_t(5));
	QCOMPARE(paths->csvRows[3], std::vector<std::string>({"W", "east", "west", "trajectory", "", "", "", f(3.0), ""}));

	const auto timetable = routeDiagramData(data, true, 0);
	QVERIFY2(timetable.has_value(), "timetable graph on the east route");
	const RouteDiagramSeries* planned = nullptr;
	for (const auto& series : timetable->series)
		if (series.event == "planned arrival")
			planned = &series;
	// planned arrivals of the west train
	if (planned == nullptr)
		QFAIL("planned arrivals of the west train");
	QCOMPARE(planned->points.size(), std::size_t(2));
	// the stops are mirrored onto the east route
	QCOMPARE_EQ(planned->points[0].positionKm, 4.0);
	QCOMPARE_EQ(planned->points[1].positionKm, 2.0);
	QCOMPARE_EQ(planned->points[0].seconds, 10.0);

	const auto onItself = routeDiagramData(data, false, 1);
	// the reference route needs no projection
	QVERIFY(onItself);
	QCOMPARE(onItself->series.size(), std::size_t(1));
	QCOMPARE(onItself->series[0].points.size(), std::size_t(5));
	// positions are kept on the reference route
	QCOMPARE_EQ(onItself->series[0].points[3].positionKm, 9.0);

	// The blocking-time projection mirrors the envelope of the west train in the same way.
	data.trains[0].blockTimes = {occupation("w1", 10.0, 20.0, 2000.0, 4000.0)};
	BlockingTimeScope scope = defaultBlockingTimeScopeFrom(data);
	scope.endTime = 100.0;
	scope.routeIndex = 0;
	scope.trainIds = {"W"};
	const auto blocking = blockingTimeDiagramData(data, scope);
	// an envelope of the west train on the east route
	QVERIFY(blocking);
	QCOMPARE(blocking->segments.size(), std::size_t(1));
	// the ends of the envelope are mirrored
	QCOMPARE_EQ(blocking->segments[0].startPositionKm, 4.0);
	QCOMPARE_EQ(blocking->segments[0].endPositionKm, 2.0);
	QCOMPARE_EQ(blocking->segments[0].midPositionKm, 3.0);
}

void TestCompletedRun::placesDelaysAtStations() {
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
	// the Delays chart of the line
	QVERIFY(diagram);
	QCOMPARE(diagram->referencePath.id, "line");
	std::string actual;
	for (const DelayDiagramSeries& series : diagram->series)
		actual += pointsOf(series);
	checkSame("the delays at the stations of the line",
		"Up: 3|60|Bravo|2|1 4.5|30|Mike|3|1 6|90|Bravo|5|2 9|-30|Charlie|6|1\n"
		"Down: 9|120|Charlie|1|1 6|45|Bravo|2|1 4.5|15|Mike|3|1 3|20|Bravo|4|2 0|0|Alpha|5|1\n"
		"Shuttle: 3|5|Bravo|1|1 3|6|Bravo|2|2\n"
		"Unplanned:\n",
		actual);
	if (QTest::currentTestFailed())
		return;
	// the trains without a call at a station of the line
	QCOMPARE(diagram->withoutCalls, std::vector<std::string>({"Away", "Idle"}));
	// no Delays chart without a route with nodes
	QVERIFY(!delayDiagramData(data, 1));
	QVERIFY(!delayDiagramData(data, -1));
	QVERIFY(!delayDiagramData(data, 2));
}

void TestCompletedRun::ordersCapacityTrains() {
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
	// without a selection the trains come in their order on the blocks, and trains without occupations are left out
	QCOMPARE(ids(capacityInputsFrom(data, scope)), "abc");
	CompletedRunData reversed = data;
	std::reverse(reversed.trains.begin(), reversed.trains.end());
	// the order on the blocks does not follow the order of the run
	QCOMPARE(ids(capacityInputsFrom(reversed, scope)), "abc");
	scope.occurrenceIds = {"c", "a"};
	// the order follows the occurrence ids
	QCOMPARE(ids(capacityInputsFrom(data, scope)), "ca");
	scope.occurrenceIds = {"b", "missing", "d", "a"};
	// ids that are missing or have no occupation are dropped
	QCOMPARE(ids(capacityInputsFrom(data, scope)), "ba");
	scope.occurrenceIds = {"missing"};
	QVERIFY2(capacityInputsFrom(data, scope).empty(), "a selection of unknown ids gives no train");
	scope.occurrenceIds = {"a", "b"};
	QVERIFY2(capacityScopeIssueFrom(data, scope).empty(), "a unique entry has no issue");
	// label of the scope
	QCOMPARE(capacityScopeLabelFrom(data, scope).toStdString(), "r / B to B");
	// a scope of one block names its number
	QCOMPARE(capacityScopeTextFrom(data, scope), "r, block 1");
	CapacityAnalysisScope unnumbered = scope;
	unnumbered.sectionIndices.clear();
	// a scope without block numbers names its block count
	QCOMPARE(capacityScopeTextFrom(data, unnumbered), "r, 1 block");
	data.routes[0].corridor = "north";
	// label with a corridor
	QCOMPARE(capacityScopeLabelFrom(data, scope).toStdString(), "r / north / B to B");
	scope.sectionIndices = {0};
	// the window names a scope by its route, corridor and block number
	QCOMPARE(capacityScopeTextFrom(data, scope), "r (north), block 1");
	QCOMPARE(capacityScopeBlocksText(data, scope), "Block 1: B");
	scope.sectionIndices.clear();
	// The capacity windows name a period in hours or minutes when it has whole ones.
	// the period in hours, minutes or seconds
	QCOMPARE(capacityPeriodText(3600.0), "1 h");
	QCOMPARE(capacityPeriodText(7200.0), "2 h");
	QCOMPARE(capacityPeriodText(1800.0), "30 min");
	QCOMPARE(capacityPeriodText(5400.0), "90 min");
	QCOMPARE(capacityPeriodText(90.5), "90.5 s");
	scope.routeIndex = 7;
	// a route that does not exist
	QCOMPARE(capacityScopeLabelFrom(data, scope).toStdString(), "no selected route");
	QVERIFY(capacityScopeIssueFrom(data, scope).empty());

	// A train that enters the block twice takes its reference where it enters first.
	data.trains[1].blockTimes.push_back(occupation("B", 150.0, 160.0, 0.0, 100.0, 150.0));
	scope.routeIndex = 0;
	scope.sectionIndices = {0};
	const CapacityAnalysisTrain twice = capacityInputFrom(data.trains[1], scope);
	// a train that enters the block twice can be analysed from its first entry
	QVERIFY(capacityScopeIssueFrom(data, scope).empty());
	QCOMPARE(twice.occupations.size(), std::size_t(2));
	QCOMPARE_EQ(twice.profileReferenceTime, 100.0);
	QCOMPARE(twice.referenceLabel, "block B entry");

	const CapacityAnalysisScope pair = firstCapacityScopeWithPairFrom(data);
	// the first scope with two trains is closed by the first train one period later
	QCOMPARE(pair.routeIndex, 0);
	QCOMPARE(pair.blockIds, std::vector<std::string>({"B"}));
	QCOMPARE(pair.sectionIndices, std::vector<int>({0}));
	QCOMPARE(pair.occurrenceIds, std::vector<std::string>({"a", "b", "c"}));
	QVERIFY(pair.repeatFirstTrain);
	QVERIFY(pair.cycleEndOccurrenceId.empty());
	data.trains.resize(1);
	// no scope with two trains
	QCOMPARE(firstCapacityScopeWithPairFrom(data).routeIndex, -1);
}

// The analysed blocks are a range of the route, and every train with a usable occupation there is analysed, from where
// it enters the range.
void TestCompletedRun::analysesCapacityRange() {
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
	// the default scope is the whole route with every train that runs over a part of it
	QCOMPARE(whole.routeIndex, 0);
	QCOMPARE(whole.blockIds, std::vector<std::string>({"A", "B", "C", "D", "E"}));
	QCOMPARE(whole.sectionIndices, std::vector<int>({0, 1, 2, 3, 4}));
	QCOMPARE(whole.occurrenceIds, std::vector<std::string>({"a", "b", "c", "d", "w"}));
	QVERIFY(whole.repeatFirstTrain);
	const CapacityAnalysisScope middle = capacityRangeScopeFrom(data, 0, 3, 1);
	// a range given from its last block runs from B to D and leaves out c
	QCOMPARE(middle.blockIds, std::vector<std::string>({"B", "C", "D"}));
	QCOMPARE(middle.sectionIndices, std::vector<int>({1, 2, 3}));
	QCOMPARE(middle.occurrenceIds, std::vector<std::string>({"a", "b", "d", "w"}));
	// a range is cut to the route
	QCOMPARE(capacityRangeScopeFrom(data, 0, 3, 9).sectionIndices, std::vector<int>({3, 4}));
	const CapacityAnalysisScope missing = capacityRangeScopeFrom(data, 5, 0, 4);
	// no blocks or trains on a route that does not exist
	QCOMPARE(missing.routeIndex, 5);
	QVERIFY(missing.blockIds.empty());
	QVERIFY(missing.occurrenceIds.empty());

	// Over D and E, a enters at E, b at D and w at E, each where it runs first into the range.
	const CapacityAnalysisScope end = capacityRangeScopeFrom(data, 0, 3, 4);
	const std::vector<CapacityAnalysisTrain> inputs = capacityInputsFrom(data, end);
	// a, b and w run over D or E
	QCOMPARE(idsOf(inputs), std::vector<std::string>({"a", "b", "w"}));
	// each train takes its reference where it enters the range
	QCOMPARE_EQ(inputs[0].profileReferenceTime, 90.0);
	QCOMPARE(inputs[0].referenceLabel, "block E entry");
	QCOMPARE(inputs[0].occupations.size(), std::size_t(1));
	QCOMPARE_EQ(inputs[1].profileReferenceTime, 160.0);
	QCOMPARE(inputs[1].referenceLabel, "block D entry");
	QCOMPARE(inputs[1].occupations.size(), std::size_t(2));
	QCOMPARE_EQ(inputs[2].profileReferenceTime, 500.0);
	QCOMPARE(inputs[2].referenceLabel, "block E entry");
	const CapacityAnalysisResult partial = capacityAnalysisFrom(data, end);
	// trains that run over parts of the range are analysed
	QVERIFY(partial.analyzable);
	QCOMPARE(partial.cycleEndIdentity, "a (next period)");
	QCOMPARE_GT(partial.cycleTime, 0.0);
	// On C the record of d cannot be a reference, and it has no other record there.
	// a record without a usable StartRunTime gives no reference
	QCOMPARE(capacityRangeScopeFrom(data, 0, 2, 2).occurrenceIds, std::vector<std::string>({"a", "b"}));
	const CapacityAnalysisTrain d = capacityInputFrom(data.trains[3], middle);
	// another record of the train in the range gives the reference
	QCOMPARE(d.occupations.size(), std::size_t(2));
	QCOMPARE_EQ(d.profileReferenceTime, 400.0);

	// o runs on another route, but its records carry the ids of B and C of the first route.
	data.routes.push_back(routeOf("s", {{"m", 0.0}}, {{"B", 0.0, 1.0}, {"C", 1.0, 2.0}}));
	CompletedTrain other = trainOver("o", 600.0, {"B", "C"});
	other.routeIndex = 1;
	data.trains.push_back(other);
	const std::vector<std::string> withOther = capacityRangeScopeFrom(data, 0, 1, 2).occurrenceIds;
	QVERIFY2(std::find(withOther.begin(), withOther.end(), "o") != withOther.end(),
		"an occurrence of another route runs over the blocks whose ids its records carry");

	// X lies twice on the route. A range that holds one of them cannot be analysed, a range that holds both can.
	CompletedRunData looped;
	looped.routes = {routeOf("r", {{"n", 0.0}}, {{"X", 0.0, 1.0}, {"Y", 1.0, 2.0}, {"Z", 2.0, 3.0}, {"X", 3.0, 4.0}})};
	looped.trains = {trainOver("a", 0.0, {"X", "Y"}), trainOver("b", 100.0, {"X", "Y"})};
	QVERIFY2(capacityScopeIssueFrom(looped, capacityRangeScopeFrom(looped, 0, 0, 1)).find("also used outside the chosen blocks") != std::string::npos,
		"a range with a resource that occurs again outside it cannot be analysed");
	QVERIFY2(capacityScopeIssueFrom(looped, firstCapacityScopeWithPairFrom(looped)).empty(), "the whole route holds both");
}

// b follows a at the minimum headway, which C governs, so the compressed diagram moves its envelopes and its recorded
// samples 100 s later.
void TestCompletedRun::compressesDiagram() {
	const CompletedRunData data = compressedPairRun();
	const CapacityAnalysisScope scope = firstCapacityScopeWithPairFrom(data);
	// the two trains share B and C
	QCOMPARE(scope.blockIds, std::vector<std::string>({"B", "C"}));
	const CapacityAnalysisResult result = analyzeCapacity(capacityInputsFrom(data, scope));
	const auto diagram = compressedBlockingTimeDiagramData(data, scope, result);
	// the compressed envelopes on the sections of the scope, without planned references
	QVERIFY(diagram);
	QCOMPARE(diagram->referencePath.id, "r");
	QCOMPARE(diagram->extents.size(), std::size_t(2));
	QCOMPARE(diagram->segments.size(), std::size_t(4));
	QVERIFY(diagram->plannedReferences.empty());
	QVERIFY(diagram->plannedLines.empty());
	// the compressed strip counts the sections of the whole route
	QVERIFY(diagram);
	QCOMPARE(diagram->routeSectionCount, 2);
	const auto followerOnC = std::find_if(diagram->segments.begin(), diagram->segments.end(),
		[](const BlockingTimeDiagramSegment& segment) { return segment.trainName == "b" && segment.blockId == "C"; });
	// b moves by 100 s and touches a on C
	QVERIFY(followerOnC != diagram->segments.end());
	QCOMPARE_EQ(followerOnC->startTime, 700.0);
	QCOMPARE_EQ(followerOnC->originalStartTime, 600.0);
	QVERIFY(followerOnC->capacityCritical);
	// the samples of b move with its envelopes, and the one beyond them is left out
	QCOMPARE(diagram->trajectories.size(), std::size_t(1));
	QCOMPARE(diagram->trajectories[0].trainId, "b");
	QVERIFY((diagram->trajectories[0].points == std::vector<std::pair<double, double>>({{0.5, 700.0}, {1.5, 701.0}})));
	// the export rows of the samples have the moved times
	QCOMPARE(diagram->trajectoryRows.size(), std::size_t(2));
	QCOMPARE(diagram->trajectoryRows[1][8], f(701.0));
	// the export rows of the samples have the geo X of their position
	QCOMPARE(diagram->trajectoryRows.size(), std::size_t(2));
	QCOMPARE(diagram->trajectoryRows[0].size(), std::size_t(24));
	QCOMPARE(diagram->trajectoryRows[0][23], f(36.0));
	QCOMPARE(diagram->trajectoryRows[1][23], f(35.0));
	// the time axis spans the compressed envelopes, 500 s to 900 s, with a margin of 5 %
	QVERIFY(std::abs(diagram->timeRange.first - 480.0) < 1e-9);
	QVERIFY(std::abs(diagram->timeRange.second - 920.0) < 1e-9);
	QVERIFY2(!compressedBlockingTimeDiagramData(data, CapacityAnalysisScope(), result), "no diagram without a route");
	const auto empty = compressedBlockingTimeDiagramData(data, scope, CapacityAnalysisResult());
	// without envelopes the time axis spans 0 s to 1 s
	QVERIFY(empty);
	QVERIFY(empty->segments.empty());
	QVERIFY(empty->timeRange == std::make_pair(0.0, 1.0));
	QVERIFY2(diagram->repeatedTrains.empty(), "a result without the first train one period later repeats no train");
}

// With a period of 1000 s, a one period later enters at 1505 s and follows b at their minimum headway of 230 s, which C
// governs: at 855 s, a cycle of 350 s. The compressed diagram draws it on the route of a, with the samples of a moved
// 350 s.
void TestCompletedRun::compressesCopyOnePeriodLater() {
	CompletedRunData data = compressedPairRun();
	data.trains[0].trajectory.firstSample = 560;
	data.trains[0].trajectory.position = {500.0, 1500.0};
	data.trains[0].trajectory.segments = {{560, 561}};
	CapacityAnalysisScope scope = firstCapacityScopeWithPairFrom(data);
	scope.periodSeconds = 1000.0;
	QVERIFY2(scope.repeatFirstTrain, "the default scope is closed by the first train one period later");
	const CapacityAnalysisResult result = capacityAnalysisFrom(data, scope);
	// a one period later closes the cycle after b
	QVERIFY(result.analyzable);
	QCOMPARE(result.trainIdentities, std::vector<std::string>({"a", "b", "a (next period)"}));
	QCOMPARE(result.cycleEndIdentity, "a (next period)");
	QCOMPARE(result.compression.size(), std::size_t(3));
	QCOMPARE_EQ(result.compression[2].originalReference, 1505.0);
	QCOMPARE_EQ(result.compression[2].compressedReference, 855.0);
	QCOMPARE_EQ(result.cycleTime, 350.0);
	QVERIFY(std::abs(result.cyclePercentage - 35.0) < 1e-9);
	const auto diagram = compressedBlockingTimeDiagramData(data, scope, result);
	// the copy is drawn on the route of a and repeats a
	QVERIFY(diagram);
	QCOMPARE(diagram->segments.size(), std::size_t(6));
	QVERIFY(diagram->unmapped.empty());
	QVERIFY((diagram->repeatedTrains == std::map<std::string, std::string>({{"a (next period)", "a"}})));
	const auto copyOn = [&diagram](const std::string& block) {
		return std::find_if(diagram->segments.begin(), diagram->segments.end(),
			[&block](const BlockingTimeDiagramSegment& segment) { return segment.trainName == "a (next period)" && segment.blockId == block; });
	};
	// the copy occupies B from 850 s and C from 900 s, where it touches b
	QVERIFY(copyOn("B") != diagram->segments.end());
	QCOMPARE_EQ(copyOn("B")->startTime, 850.0);
	QCOMPARE_EQ(copyOn("B")->originalStartTime, 1500.0);
	QVERIFY(copyOn("C") != diagram->segments.end());
	QCOMPARE_EQ(copyOn("C")->startTime, 900.0);
	QCOMPARE_EQ(copyOn("C")->endTime, 1050.0);
	QVERIFY(copyOn("C")->capacityCritical);
	const auto samplesOf = [&diagram](const std::string& train) {
		std::vector<std::pair<double, double>> points;
		for (const auto& trajectory : diagram->trajectories)
			if (trajectory.trainId == train)
				points.insert(points.end(), trajectory.points.begin(), trajectory.points.end());
		return points;
	};
	// the copy has the samples of a, 350 s later
	QVERIFY((samplesOf("a") == std::vector<std::pair<double, double>>({{0.5, 560.0}, {1.5, 561.0}})));
	QVERIFY((samplesOf("a (next period)") == std::vector<std::pair<double, double>>({{0.5, 910.0}, {1.5, 911.0}})));
	// the time axis spans the envelopes of the copy, to 1050 s
	QVERIFY(std::abs(diagram->timeRange.first - 472.5) < 1e-9);
	QVERIFY(std::abs(diagram->timeRange.second - 1077.5) < 1e-9);
	QVERIFY2(capacityCsvFrom(data, scope).find("a (next period)") != std::string::npos, "the export names the copy");
	CapacityAnalysisScope single = scope;
	single.occurrenceIds = {"a"};
	const CapacityAnalysisResult alone = capacityAnalysisFrom(data, single);
	// one train gets no copy one period later, so it is not analysed and has no export
	QVERIFY(!alone.analyzable);
	QCOMPARE(alone.trainIdentities, std::vector<std::string>({"a"}));
	QVERIFY(capacityCsvFrom(data, single).empty());
}

// o runs on a route that shares no node with r, but its record of C shares the resource of C of r. Its envelope lies on
// C of r in the compressed diagram.
void TestCompletedRun::compressesOtherRoute() {
	CompletedRunData data = compressedPairRun();
	data.routes.push_back(routeOf("s", {{"m0", 0.0}, {"m1", 1.0}}, {{"C", 0.0, 1.0}}));
	CompletedTrain other = trainOn("o", 1);
	other.blockTimes = {occupation("C", 2000.0, 2100.0, 0.0, 1000.0, 2005.0)};
	data.trains.push_back(other);
	const CapacityAnalysisScope scope = firstCapacityScopeWithPairFrom(data);
	const CapacityAnalysisResult result = capacityAnalysisFrom(data, scope);
	// o is analysed with a and b
	QCOMPARE(scope.occurrenceIds, std::vector<std::string>({"a", "b", "o"}));
	QVERIFY(result.analyzable);
	const auto diagram = compressedBlockingTimeDiagramData(data, scope, result);
	const auto onC = diagram ? std::find_if(diagram->segments.begin(), diagram->segments.end(),
								   [](const BlockingTimeDiagramSegment& segment) { return segment.trainName == "o"; })
							 : std::vector<BlockingTimeDiagramSegment>::const_iterator();
	// the envelope of o lies on C of r
	QVERIFY(diagram);
	QVERIFY(onC != diagram->segments.end());
	QCOMPARE_EQ(onC->startPositionKm, 1.0);
	QCOMPARE_EQ(onC->endPositionKm, 2.0);
	QCOMPARE(onC->referenceSection, 2);
	QVERIFY(diagram->unmapped.empty());
	QCOMPARE(diagram->omitted, 0);
}

void TestCompletedRun::sharesResources() {
	// "X@sw@Y" holds the resources X and Y; a plain block shares a resource with it when the ids match.
	CompletedRunData data;
	CompletedTrain first = trainOn("first", 0);
	first.blockTimes = {occupation("X@sw@Y", 10.0, 30.0, 0.0, 200.0, 10.0)};
	CompletedTrain second = trainOn("second", 0);
	second.blockTimes = {occupation("Y", 20.0, 40.0, 100.0, 200.0, 20.0), occupation("Z", 40.0, 50.0, 200.0, 300.0, 40.0)};
	data.trains = {first, second};
	const auto segments = blockTimesFrom(data);
	// three segments
	QCOMPARE(segments.size(), std::size_t(3));
	// the composite and the plain block conflict
	QCOMPARE(segments[0].style, BlockingTimeSegmentStyle::Critical);
	QCOMPARE(segments[1].style, BlockingTimeSegmentStyle::Critical);
	QVERIFY2(segments[2].style != BlockingTimeSegmentStyle::Critical, "the unrelated block does not");
	CapacityAnalysisScope scope;
	scope.blockIds = {"Y"};
	const auto inputs = capacityInputsFrom(data, scope);
	// the composite block is an occupation of the resource Y
	QCOMPARE(inputs.size(), std::size_t(2));
	QCOMPARE(inputs[0].occupations.size(), std::size_t(1));
	QCOMPARE(inputs[0].occupations[0].blockId, "X@sw@Y");
	// only the matching block of the second train
	QCOMPARE(inputs.size(), std::size_t(2));
	QCOMPARE(inputs[1].occupations.size(), std::size_t(1));
	QCOMPARE(inputs[1].occupations[0].blockId, "Y");
	scope.blockIds = {"X"};
	const auto composite = capacityInputsFrom(data, scope);
	// the resource X is held by the composite block only
	QCOMPARE(composite.size(), std::size_t(1));
	QCOMPARE(composite[0].runtimeId, "first");
	scope.blockIds = {"Y", "Z"};
	const auto several = capacityInputsFrom(data, scope);
	// a scope of several blocks takes the records of each
	QCOMPARE(several.size(), std::size_t(2));
	QCOMPARE(several[0].occupations.size(), std::size_t(1));
	QCOMPARE(several[1].occupations.size(), std::size_t(2));
}

void TestCompletedRun::listsUsedRoutes() {
	CompletedRunData data;
	data.routes = {routeOf("r0", {}), routeOf("r1", {}), routeOf("r2", {})};
	for (int index : {1, 1, 0, 7, -1, 1, 2, 0})
		data.trains.push_back(trainOn("T" + std::to_string(data.trains.size()), index));
	const auto used = usedRouteReferencesFrom(data);
	QVERIFY2((used == std::vector<std::pair<int, std::string>>({{1, "r1"}, {0, "r0"}, {2, "r2"}})), "routes in order of first use, each once, without unknown indices");
	QVERIFY2(usedRouteReferencesFrom(CompletedRunData()).empty(), "no route in an empty run");
}

void TestCompletedRun::choosesBlockingTimeScope() {
	BlockingTimeScopeChoices choices;
	choices.caseEndSeconds = 600.0;
	choices.stepSeconds = 5.0;
	choices.routes = {{"r0", "north", {"a", "b", "c"}}, {"r1", "", {"x", "y"}}};

	BlockingTimeScope scope = defaultBlockingTimeScope(choices.caseEndSeconds);
	// the default scope is the whole run
	QCOMPARE_EQ(scope.startTime, 0.0);
	QCOMPARE_EQ(scope.endTime, 600.0);
	QCOMPARE(scope.routeIndex, -1);
	// the default scope has no negative end
	QCOMPARE_EQ(defaultBlockingTimeScope(-3.0).endTime, 0.0);

	// All routes: no block range and no train list.
	bool accepted = askScope(choices, scope, [](QDialog& dialog) {
		const auto spins = dialog.findChildren<QDoubleSpinBox*>();
		// the interval is limited to the run
		QCOMPARE(spins.size(), qsizetype(2));
		QCOMPARE_EQ(spins[0]->maximum(), 600.0);
		QCOMPARE_EQ(spins[0]->singleStep(), 5.0);
		spins[0]->setValue(100.0);
		spins[1]->setValue(250.0);
		press(dialog, QDialogButtonBox::Ok);
	});
	if (QTest::currentTestFailed())
		return;
	// the dialog accepts all routes with an interval
	QVERIFY(accepted);
	QCOMPARE(scope.routeIndex, -1);
	QVERIFY(scope.trainIds.empty());
	QVERIFY(scope.blockIds.empty());
	QVERIFY(scope.sectionIndices.empty());
	QCOMPARE_EQ(scope.startTime, 100.0);
	QCOMPARE_EQ(scope.endTime, 250.0);

	// A route with a range of its blocks gives no train list, so the diagram takes every train on those blocks.
	accepted = askScope(choices, scope, [](QDialog& dialog) {
		const auto combos = dialog.findChildren<QComboBox*>();
		// the dialog lists the routes and all routes
		QCOMPARE(combos.size(), qsizetype(3));
		QCOMPARE(combos[0]->count(), 3);
		for (const QComboBox* combo : combos)
			QVERIFY2(dynamic_cast<const ChoiceComboBox*>(combo), "the scope dialog shows its choices in choice combos");
		combos[0]->setCurrentIndex(1);
		// the routes show their corridor when they have one
		QCOMPARE(combos[0]->itemText(0), "All routes");
		QCOMPARE(combos[0]->itemText(1), "r0  (north)");
		QCOMPARE(combos[0]->itemText(2), "r1");
		// the blocks of the route are listed
		QCOMPARE(combos[1]->count(), 3);
		QCOMPARE(combos[1]->itemText(2), "3: c");
		QCOMPARE(combos[2]->currentIndex(), 2);
		combos[1]->setCurrentIndex(1);
		press(dialog, QDialogButtonBox::Ok);
	});
	if (QTest::currentTestFailed())
		return;
	// the dialog accepts a route and a range of its blocks, with no train list
	QVERIFY(accepted);
	QCOMPARE(scope.routeIndex, 0);
	QCOMPARE(scope.sectionIndices, std::vector<int>({1, 2}));
	QCOMPARE(scope.blockIds, std::vector<std::string>({"b", "c"}));
	QVERIFY(scope.trainIds.empty());

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
	if (QTest::currentTestFailed())
		return;
	// the blocks are taken in route order
	QVERIFY(accepted);
	QCOMPARE(scope.routeIndex, 1);
	QCOMPARE(scope.sectionIndices, std::vector<int>({0, 1}));
	QVERIFY(scope.trainIds.empty());

	// An empty interval is widened by one step, or moved back at the end of the run.
	accepted = askScope(choices, scope, [](QDialog& dialog) {
		const auto spins = dialog.findChildren<QDoubleSpinBox*>();
		spins[0]->setValue(40.0);
		spins[1]->setValue(40.0);
		press(dialog, QDialogButtonBox::Ok);
	});
	if (QTest::currentTestFailed())
		return;
	// an empty interval gets one step
	QVERIFY(accepted);
	QCOMPARE_EQ(scope.startTime, 40.0);
	QCOMPARE_EQ(scope.endTime, 45.0);
	accepted = askScope(choices, scope, [](QDialog& dialog) {
		const auto spins = dialog.findChildren<QDoubleSpinBox*>();
		spins[0]->setValue(600.0);
		spins[1]->setValue(600.0);
		press(dialog, QDialogButtonBox::Ok);
	});
	if (QTest::currentTestFailed())
		return;
	// an empty interval at the end of the run moves back one step
	QVERIFY(accepted);
	QCOMPARE_EQ(scope.startTime, 595.0);
	QCOMPARE_EQ(scope.endTime, 600.0);

	const BlockingTimeScope before = scope;
	accepted = askScope(choices, scope, [](QDialog& dialog) { press(dialog, QDialogButtonBox::Cancel); });
	if (QTest::currentTestFailed())
		return;
	// cancel keeps the scope
	QVERIFY(!accepted);
	QCOMPARE_EQ(scope.startTime, before.startTime);
	QCOMPARE_EQ(scope.endTime, before.endTime);

	// A run without routes still offers the interval.
	BlockingTimeScopeChoices none;
	none.caseEndSeconds = 30.0;
	BlockingTimeScope plain = defaultBlockingTimeScope(30.0);
	accepted = askScope(none, plain, [](QDialog& dialog) { press(dialog, QDialogButtonBox::Ok); });
	if (QTest::currentTestFailed())
		return;
	// a run without routes gives the whole run
	QVERIFY(accepted);
	QCOMPARE(plain.routeIndex, -1);
	QCOMPARE_EQ(plain.startTime, 0.0);
	QCOMPARE_EQ(plain.endTime, 30.0);
}

void TestCompletedRun::choosesCapacityScope() {
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
	QVERIFY2(!chooseCapacityAnalysisScope(nullptr, CompletedRunData(), none), "a run without routes has nothing to choose");

	// Without a given scope the dialog opens on the whole first route, with every train of it checked in their order on
	// the blocks, and the first train one period later closing the cycle.
	CapacityAnalysisScope scope;
	bool accepted = askCapacityScope(run, scope, [&](QDialog& dialog) {
		CapacityDialogParts parts;
		findCapacityDialogParts(dialog, parts);
		if (QTest::currentTestFailed() || !parts.complete())
			return;
		// the routes are listed
		QCOMPARE(entriesOf(parts.route), QStringList({"r  (north)", "s"}));
		for (const QLabel* label : dialog.findChildren<QLabel*>())
			QVERIFY2(!label->text().contains("Gdg"), "the capacity dialog names no station of one case");
		// the blocks run from the first to the last block of the route
		QCOMPARE(entriesOf(parts.from), QStringList({"1: B", "2: C"}));
		QCOMPARE(entriesOf(parts.to), entriesOf(parts.from));
		QCOMPARE(parts.from->itemData(1, Qt::ToolTipRole).toString(), "C");
		QCOMPARE(parts.from->currentIndex(), 0);
		QCOMPARE(parts.to->currentIndex(), 1);
		// the trains of the blocks are listed in their order, with their entry time, code and blocks
		QCOMPARE(rowsOf(parts.list), QStringList({rowA, rowB, rowC, rowE}));
		bool allChecked = true, tipped = true;
		for (int row = 0; row < parts.list->count(); ++row) {
			allChecked = allChecked && parts.list->item(row)->checkState() == Qt::Checked;
			tipped = tipped && parts.list->item(row)->toolTip() == parts.list->item(row)->text();
		}
		// every listed train is checked, with its row as the tooltip
		QVERIFY(allChecked);
		QVERIFY(tipped);
		// the first checked row starts the cycle
		QCOMPARE(parts.firstTrain->text(), "First train of the cycle: " + rowA);
		// the cycle is closed by the first train one period later or a checked row after the first
		QCOMPARE(entriesOf(parts.cycleEnd), QStringList({nextPeriod, rowB, rowC, rowE}));
		QCOMPARE(parts.cycleEnd->currentIndex(), 0);
		bool noteIsHelp = false;
		for (const QLabel* label : dialog.findChildren<QLabel*>())
			noteIsHelp |= label->text()
					== "Checked trains are analysed in list order. The cycle runs from the first checked train to the cycle-closing "
					   "train, the first train of the next period. Drag a row to change the order."
				&& label->property("dialogHelp").toBool();
		QVERIFY2(noteIsHelp, "the note of the dialog is a help note");
		// the dialog opens without a message
		QVERIFY(parts.status->isHidden());
		QVERIFY(parts.ok->isEnabled());
		parts.list->item(2)->setCheckState(Qt::Unchecked);
		// an unchecked row cannot close the cycle
		QCOMPARE(entriesOf(parts.cycleEnd), QStringList({nextPeriod, rowB, rowE}));
		dialog.findChild<QDoubleSpinBox*>()->setValue(7200.0);
		parts.ok->click();
	});
	if (QTest::currentTestFailed())
		return;
	// the dialog accepts the blocks, the checked trains and the first train one period later
	QVERIFY(accepted);
	QCOMPARE(scope.routeIndex, 0);
	QCOMPARE(scope.blockIds, std::vector<std::string>({"B", "C"}));
	QCOMPARE(scope.sectionIndices, std::vector<int>({0, 1}));
	QCOMPARE(scope.occurrenceIds, std::vector<std::string>({"a", "b", "e"}));
	QVERIFY(scope.repeatFirstTrain);
	QVERIFY(scope.cycleEndOccurrenceId.empty());
	QCOMPARE_EQ(scope.periodSeconds, 7200.0);

	// Fewer than two checked trains, or a cycle-closing train that shares no block with an earlier checked train, disable
	// OK and say why. A train listed before one that runs ahead of it is a warning only. The caller's scope stays as it
	// was on cancel.
	CapacityAnalysisScope kept;
	accepted = askCapacityScope(run, kept, [&](QDialog& dialog) {
		CapacityDialogParts parts;
		findCapacityDialogParts(dialog, parts);
		if (QTest::currentTestFailed() || !parts.complete() || parts.list->count() != 4)
			return;
		for (const int row : {1, 2, 3})
			parts.list->item(row)->setCheckState(Qt::Unchecked);
		// one checked train disables OK and says why
		QVERIFY(!parts.ok->isEnabled());
		QVERIFY(parts.status->isVisible());
		QCOMPARE(parts.status->property("dialogStatus").toString(), "error");
		QCOMPARE(parts.status->text(), "Check at least two trains.");
		// with one checked train only the first train one period later is left
		QCOMPARE(entriesOf(parts.cycleEnd), QStringList({nextPeriod}));
		parts.list->item(1)->setCheckState(Qt::Checked);
		// a second checked train enables OK and takes the message away
		QVERIFY(parts.ok->isEnabled());
		QVERIFY(parts.status->isHidden());
		for (const int row : {2, 3})
			parts.list->item(row)->setCheckState(Qt::Checked);
		parts.cycleEnd->setCurrentIndex(parts.cycleEnd->findData("e"));
		parts.list->item(0)->setCheckState(Qt::Unchecked);
		parts.list->item(1)->setCheckState(Qt::Unchecked);
		// the first checked row follows the checks, and e still closes the cycle
		QCOMPARE(parts.firstTrain->text(), "First train of the cycle: " + rowC);
		QCOMPARE(parts.cycleEnd->currentData().toString(), "e");
		// a cycle-closing train that shares no block with an earlier checked train disables OK
		QVERIFY(!parts.ok->isEnabled());
		QCOMPARE(parts.status->property("dialogStatus").toString(), "error");
		QCOMPARE(parts.status->text(), "e shares no block with an earlier checked train, so it closes no cycle. Choose another cycle-closing train.");
		QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
		QApplication::processEvents();
		QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
		const auto* scroll = dialog.findChild<QScrollArea*>("dialogBodyScroll");
		const auto* body = scroll->widget();
		// the first train and the status sit above the buttons, outside the scrolling body
		QVERIFY(!body->isAncestorOf(parts.status));
		QVERIFY(!body->isAncestorOf(parts.firstTrain));
		QCOMPARE_LE(parts.status->geometry().bottom(), dialog.findChild<QDialogButtonBox*>()->geometry().top());
		// the first train lines up with the list
		QCOMPARE(parts.firstTrain->mapTo(&dialog, QPoint()).x(), parts.list->mapTo(&dialog, QPoint()).x());
		// On a short screen the list gives up its height, not the body: with room for four rows of it, the body does not scroll.
		const int fourRows = 4 * parts.list->sizeHintForRow(0) + 2 * parts.list->frameWidth();
		dialog.resize(dialog.width(), dialog.height() - (parts.list->height() - fourRows));
		QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
		QApplication::processEvents();
		// the list, not the body, gives up height on a short screen
		QCOMPARE(scroll->verticalScrollBar()->maximum(), 0);
		parts.cycleEnd->setCurrentIndex(0);
		// the first train one period later closes the cycle
		QVERIFY(parts.ok->isEnabled());
		QVERIFY(parts.status->isHidden());
		parts.cycleEnd->setCurrentIndex(parts.cycleEnd->findData("e"));
		parts.list->item(0)->setCheckState(Qt::Checked);
		parts.list->item(1)->setCheckState(Qt::Checked);
		// with a and b checked e shares C with an earlier checked train
		QVERIFY(parts.ok->isEnabled());
		QVERIFY(parts.status->isHidden());
		// b is moved above a
		QVERIFY(parts.list->model()->moveRow(QModelIndex(), 1, QModelIndex(), 0));
		QCOMPARE(rowsOf(parts.list), QStringList({rowB, rowA, rowC, rowE}));
		// the first train and the cycle-closing trains follow the move
		QCOMPARE(parts.firstTrain->text(), "First train of the cycle: " + rowB);
		QCOMPARE(entriesOf(parts.cycleEnd), QStringList({nextPeriod, rowA, rowC, rowE}));
		QCOMPARE(parts.cycleEnd->currentData().toString(), "e");
		// a train listed after one that it runs ahead of is a warning
		QVERIFY(parts.ok->isEnabled());
		QVERIFY(parts.status->isVisible());
		QCOMPARE(parts.status->property("dialogStatus").toString(), "warning");
		QCOMPARE(parts.status->text(), "a runs ahead of b in these blocks; the analysis keeps the order of the list.");
		parts.list->item(0)->setCheckState(Qt::Unchecked);
		// e stays the cycle-closing train while it is a checked row after the first
		QCOMPARE(parts.cycleEnd->currentData().toString(), "e");
		parts.list->item(3)->setCheckState(Qt::Unchecked);
		// an unchecked cycle-closing train falls back to the first train one period later
		QCOMPARE(parts.cycleEnd->currentIndex(), 0);

		// Over C only, c is not listed, the others are listed in their order on C again and b stays unchecked.
		parts.from->setCurrentIndex(1);
		// another range lists its trains in their order, with the unchecked ones still unchecked
		QCOMPARE(rowsOf(parts.list), QStringList({"06:00:30  a (OC-a), block 2", "06:02:10  b (OC-b), block 2", rowE}));
		QCOMPARE(parts.list->item(1)->checkState(), Qt::Unchecked);
		QCOMPARE(parts.list->item(2)->checkState(), Qt::Unchecked);
		parts.to->setCurrentIndex(0);
		// a last block before the first block moves the first block
		QCOMPARE(parts.from->currentIndex(), 0);
		QCOMPARE(parts.to->currentIndex(), 0);
		parts.from->setCurrentIndex(1);
		// a first block after the last block moves the last block
		QCOMPARE(parts.from->currentIndex(), 1);
		QCOMPARE(parts.to->currentIndex(), 1);
		dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();
	});
	if (QTest::currentTestFailed())
		return;
	// cancel gives no scope
	QVERIFY(!accepted);
	QCOMPARE(kept.routeIndex, -1);
	QVERIFY(kept.blockIds.empty());
	QVERIFY(kept.occurrenceIds.empty());
	QVERIFY(kept.cycleEndOccurrenceId.empty());

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
		CapacityDialogParts parts;
		findCapacityDialogParts(dialog, parts);
		if (QTest::currentTestFailed() || !parts.complete())
			return;
		// the trains of the given scope come first, in their order, and the others are unchecked
		QCOMPARE(rowsOf(parts.list), QStringList({rowB, rowA, rowC, rowE}));
		QCOMPARE(parts.list->item(0)->checkState(), Qt::Checked);
		QCOMPARE(parts.list->item(1)->checkState(), Qt::Checked);
		QCOMPARE(parts.list->item(2)->checkState(), Qt::Unchecked);
		QCOMPARE(parts.list->item(3)->checkState(), Qt::Unchecked);
		// the cycle-closing train and the period of the given scope are chosen
		QCOMPARE(parts.cycleEnd->currentData().toString(), "a");
		QCOMPARE_EQ(dialog.findChild<QDoubleSpinBox*>()->value(), 1800.0);
		// the given order is a warning
		QCOMPARE(parts.status->property("dialogStatus").toString(), "warning");
		parts.ok->click();
	});
	if (QTest::currentTestFailed())
		return;
	// OK takes the given scope at once
	QVERIFY(accepted);
	QCOMPARE(given.blockIds, std::vector<std::string>({"B", "C"}));
	QCOMPARE(given.occurrenceIds, std::vector<std::string>({"b", "a"}));
	QCOMPARE(given.cycleEndOccurrenceId, "a");
	QVERIFY(!given.repeatFirstTrain);
	QCOMPARE_EQ(given.periodSeconds, 1800.0);

	// A given range of C only opens on it.
	CapacityAnalysisScope ofC;
	ofC.routeIndex = 0;
	ofC.blockIds = {"C"};
	ofC.sectionIndices = {1};
	ofC.repeatFirstTrain = true;
	accepted = askCapacityScope(run, ofC, [&](QDialog& dialog) {
		CapacityDialogParts parts;
		findCapacityDialogParts(dialog, parts);
		if (QTest::currentTestFailed() || !parts.complete())
			return;
		// the dialog opens on the given range with all its trains checked
		QCOMPARE(parts.from->currentIndex(), 1);
		QCOMPARE(parts.to->currentIndex(), 1);
		QCOMPARE(parts.list->count(), 3);
		QCOMPARE(parts.cycleEnd->currentIndex(), 0);
		parts.ok->click();
	});
	if (QTest::currentTestFailed())
		return;
	// OK takes the trains of the given range
	QVERIFY(accepted);
	QCOMPARE(ofC.blockIds, std::vector<std::string>({"C"}));
	QCOMPARE(ofC.occurrenceIds, std::vector<std::string>({"a", "b", "e"}));
	QVERIFY(ofC.repeatFirstTrain);

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
		CapacityDialogParts parts;
		findCapacityDialogParts(dialog, parts);
		if (QTest::currentTestFailed() || !parts.complete())
			return;
		// the dialog opens on the given route
		QCOMPARE(parts.route->currentIndex(), 1);
		QCOMPARE(entriesOf(parts.from), QStringList({"1: X"}));
		QCOMPARE(parts.list->count(), 2);
		parts.route->setCurrentIndex(0);
		// the first route lists its blocks and its trains
		QCOMPARE(entriesOf(parts.from), QStringList({"1: B", "2: C"}));
		QCOMPARE(parts.from->currentIndex(), 0);
		QCOMPARE(parts.to->currentIndex(), 1);
		QCOMPARE(rowsOf(parts.list), QStringList({rowA, rowB, rowC, rowE}));
		parts.list->item(2)->setCheckState(Qt::Unchecked);
		parts.route->setCurrentIndex(1);
		parts.route->setCurrentIndex(0);
		// an unchecked train stays unchecked when its route is chosen again
		QCOMPARE(parts.list->item(2)->checkState(), Qt::Unchecked);
		QCOMPARE(parts.list->item(1)->checkState(), Qt::Checked);
		parts.route->setCurrentIndex(1);
		parts.ok->click();
	});
	if (QTest::currentTestFailed())
		return;
	// the two trains of the given route are taken
	QVERIFY(accepted);
	QCOMPARE(chosen.routeIndex, 1);
	QCOMPARE(chosen.blockIds, std::vector<std::string>({"X"}));
	QCOMPARE(chosen.sectionIndices, std::vector<int>({0}));
	QCOMPARE(chosen.occurrenceIds, std::vector<std::string>({"x", "y"}));
	QVERIFY(chosen.repeatFirstTrain);
}

// The picture of a table shows every column and row, whatever the width of the table.
void TestCompletedRun::picturesWholeTable() {
	QTableWidget table;
	const QStringList headers = {"Train", "Operating code", "Start time (hh:mm:ss)", "Energy consumed (kWh)",
		"Energy consumed with regenerative braking (kWh)", "Substation request (kWh)", "Substation request with regenerative braking (kWh)"};
	table.setColumnCount(static_cast<int>(headers.size()));
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
	// the table is narrower and lower than its contents
	QCOMPARE_GT(columnsWidth, table.width());
	QCOMPARE_GT(rowsHeight, table.height());

	const QImage image = fullTableImage(table);
	const qreal ratio = image.devicePixelRatio();
	// the picture of the table spans all columns and rows
	QVERIFY(image.width() / ratio >= columnsWidth + table.verticalHeader()->width());
	QVERIFY(image.height() / ratio >= rowsHeight + table.horizontalHeader()->height());
	// The marked cell is the last of the last row. Without scroll bars it is drawn in the bottom right corner.
	bool marked = false;
	for (int x = image.width() - 12; x < image.width() - 2; ++x)
		for (int y = image.height() - 12; y < image.height() - 2; ++y)
			marked = marked || image.pixelColor(x, y) == marker;
	QVERIFY2(marked, "the last cell is drawn in the picture, with no scroll bar over it");
	// the picture leaves the size of the table as it was
	QCOMPARE(table.width(), 320);
	QCOMPARE(table.height(), 140);
}

// The scope dialogs of a run whose route has a 300-character id and 2000 blocks, at 10 pt, at 18 pt and at 1.25 times 18 pt,
// which covers a platform font that is wider than the macOS one. Each fits the screen, with the route chosen or not, and its
// body does not scroll sideways.
void TestCompletedRun::fitsScopeDialogsWithLongIds() {
	const QFont original = QApplication::font();
	// A failed check ends the slot, so the font of the application is restored on the way out.
	const auto restoreFont = qScopeGuard([&original]() { QApplication::setFont(original); });
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
			if (QTest::currentTestFailed())
				return;
			const auto combos = dialog.findChildren<QComboBox*>();
			if (combos.size() == 3) {
				combos[0]->setCurrentIndex(1);
				QApplication::processEvents();
				checkScopeDialogFits(dialog, "blocking-time scope with 2000 blocks at " + what);
				if (QTest::currentTestFailed())
					return;
			}
			press(dialog, QDialogButtonBox::Cancel);
		});
		if (QTest::currentTestFailed())
			return;
		QVERIFY2(!accepted, "the scope dialog with long ids is cancelled");

		CapacityAnalysisScope capacity;
		accepted = askCapacityScope(run, capacity, [&what](QDialog& dialog) {
			checkScopeDialogFits(dialog, "capacity options with 2000 blocks at " + what);
			if (QTest::currentTestFailed())
				return;
			dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();
		});
		if (QTest::currentTestFailed())
			return;
		QVERIFY2(!accepted, "the capacity dialog with long ids is cancelled");

		// At 18 pt and 22.5 pt the dialog is as wide as the screen allows from the start.
		const bool opensNarrow = points == 10.0;
		accepted = askScope(shortChoices, scope, [&what, opensNarrow](QDialog& dialog) {
			checkWidensForLongBlocks(dialog, 2, opensNarrow, "blocking-time scope at " + what);
			if (QTest::currentTestFailed())
				return;
			press(dialog, QDialogButtonBox::Cancel);
		});
		if (QTest::currentTestFailed())
			return;
		QVERIFY2(!accepted, "the scope dialog with short route ids is cancelled");
		accepted = askCapacityScope(shortRun, capacity, [&what, opensNarrow](QDialog& dialog) {
			checkWidensForLongBlocks(dialog, 1, opensNarrow, "capacity options at " + what);
			if (QTest::currentTestFailed())
				return;
			dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();
		});
		if (QTest::currentTestFailed())
			return;
		QVERIFY2(!accepted, "the capacity dialog with short route ids is cancelled");
	}
}

// With the footer of macOS the scope dialog opens with OK as the default and the focus on the route. Cancel with the
// focus leaves the default to OK, so Return accepts. The application style is put back at the end.
void TestCompletedRun::keepsMacFooterOfScopeDialog() {
	const QString styleName = QApplication::style()->name();
	const auto restoreStyle = qScopeGuard([&styleName]() { QApplication::setStyle(styleName); });
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
		if (okButton == nullptr || cancelButton == nullptr)
			QFAIL("the scope dialog has OK and Cancel");
		// the route has the focus when the scope dialog opens
		QCOMPARE(dialog.focusWidget(), dialog.findChildren<QComboBox*>().value(0));
		// OK is the default button of the scope dialog and Cancel is not
		QVERIFY(okButton->isDefault());
		QVERIFY(!cancelButton->isDefault());
		cancelButton->setFocus(Qt::TabFocusReason);
		QApplication::processEvents();
		// Cancel with the focus leaves the default to OK
		QVERIFY(okButton->isDefault());
		QVERIFY(!cancelButton->isDefault());
		QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
		QApplication::sendEvent(cancelButton, &enter);
	});
	if (QTest::currentTestFailed())
		return;
	QVERIFY2(accepted, "Return accepts the scope dialog while Cancel has the focus");
}

QTEST_MAIN(TestCompletedRun)
#include "test_completedrun.moc"
