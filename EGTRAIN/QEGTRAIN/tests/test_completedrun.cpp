// Tests of the completed-run data of diagrams/CompletedRunData.h.
//
// Two committed scenes run in this process, one after the other. The data captured from the simulation globals
// must give the exports and diagram inputs that the functions give that read the globals, and it must not change
// when the next scene replaces the globals. A result window opened for a scene must keep exporting that scene. Hand-made
// data then pins the adapters on cases that the scenes do not hold: missing times, repeated stops, reversed routes, the
// order of the capacity trains and shared resources.
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
#include "util/CsvWriter.h"
#include "util/Logger.hpp"
#include "util/TrajectoryUtil.h"
#ifdef signals
#undef signals
#endif

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
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

Logger owl;

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

std::string describe(const CapacityAnalysisTrain& t) {
	std::ostringstream text;
	text << t.runtimeId << '|' << t.operatingCode << '|' << num(t.profileReferenceTime) << '|' << num(t.scheduledReferenceTime)
		 << '|' << t.referenceLabel << '|' << t.referenceSource << '\n';
	for (const auto& occupation : t.occupations)
		text << "  " << describe(occupation) << '\n';
	return text.str();
}

std::string describe(const std::vector<CapacityAnalysisTrain>& trains) {
	std::string text;
	for (const auto& train : trains)
		text += describe(train);
	return text;
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

// --- The readers that take the run from the simulation globals ---
// These are the readers that MainWindow used for the route and blocking-time diagrams, with the chart replaced by
// the data that goes into it.

// Where a stop of the runtime train lies on the reference route.
std::optional<double> legacyStopPosition(const Train& train, int index, const RouteDiagramPath& source,
	const RouteDiagramProjection& projection) {
	if (train.Stations.empty() || index < 0 || index >= train.numStations)
		return {};
	const Node& stop = train.Stations[index];
	if (stop.sceneNodeId.empty())
		return {};
	for (const auto& node : source.nodes)
		if (node.nodeId == stop.sceneNodeId && std::abs(node.positionKm - stop.X) < 1e-6)
			return projection.map(node.positionKm);
	return {};
}

std::vector<std::pair<int, std::string>> legacyUsedRouteReferences() {
	std::vector<std::pair<int, std::string>> used;
	for (int i = 0; i < numRegions; ++i) {
		const int index = regional_train[i].indexOfRoute;
		if (index >= 0 && index < static_cast<int>(train_route.size())
			&& std::none_of(used.begin(), used.end(), [index](const auto& route) { return route.first == index; }))
			used.emplace_back(index, train_route[index].ID);
	}
	return used;
}

std::optional<BlockingTimeDiagramData> legacyBlockingTimeDiagram(const SceneModel& scene, const BlockingTimeScope& scope) {
	const std::vector<BlockingTimeDiagramSegment> allSegments = buildAllBlockingTimeSegments();
	const auto scoped = scope.routeIndex >= 0 && scope.trainIds.empty()
		? std::vector<BlockingTimeDiagramSegment>()
		: filterBlockingTimeDiagramSegments(allSegments, scope.trainIds, scope.blockIds,
			  scope.startTime, scope.endTime);
	const int referenceIndex = scope.routeIndex >= 0 ? scope.routeIndex : [&]() {
		for (int i = 0; i < numRegions; ++i)
			if ((scope.trainIds.empty() || std::find(scope.trainIds.begin(), scope.trainIds.end(), regional_train[i].trainDescription) != scope.trainIds.end())
				&& regional_train[i].indexOfRoute >= 0
				&& regional_train[i].indexOfRoute < static_cast<int>(train_route.size()))
				return regional_train[i].indexOfRoute;
		return -1;
	}();
	if (referenceIndex < 0)
		return std::nullopt;
	const RouteDiagramPath referencePath = routeDiagramPath(train_route[referenceIndex], &scene);
	std::map<int, RouteDiagramPath> paths;
	std::map<int, RouteDiagramProjection> projections;
	std::map<std::string, RouteDiagramProjection> byTrain;
	for (int i = 0; i < numRegions; ++i) {
		const Train& train = regional_train[i];
		const int index = train.indexOfRoute;
		if (index < 0 || index >= static_cast<int>(train_route.size()))
			continue;
		if (!paths.count(index)) {
			paths.emplace(index, routeDiagramPath(train_route[index], &scene));
			projections.emplace(index, buildRouteDiagramProjection(paths.at(index), referencePath));
		}
		byTrain.emplace(train.trainDescription, projections.at(index));
	}
	int omitted = 0;
	std::vector<BlockingTimeDiagramSegment> unmapped;
	auto segments = projectBlockingSegments(scoped, byTrain, omitted, &unmapped);
	const auto allExtents = buildBlockSectionExtents(blockSectionInputsForRoute(train_route[referenceIndex]));
	const auto extents = scopedBlockSectionExtents(allExtents, scope.sectionIndices);
	matchBlockingSegmentsToSections(segments, allExtents, referencePath.id);
	if (!scope.sectionIndices.empty())
		segments.erase(std::remove_if(segments.begin(), segments.end(),
						   [&extents](const auto& segment) { return !blockingSegmentInSections(segment, extents); }),
			segments.end());
	const auto allPlannedReferences = buildBlockingTimePlannedReferences(scope, paths, projections, omitted);
	BlockingTimeDiagramData result;
	result.plannedReferences = filterBlockingTimePlannedReferences(allPlannedReferences, scope.startTime, scope.endTime);
	result.plannedLines = clipBlockingTimePlannedReferences(allPlannedReferences, scope.startTime, scope.endTime);

	for (int i = 0; i < numRegions; ++i) {
		const Train& train = regional_train[i];
		if (train.earliestActiveTrajectoryIndex < 0)
			continue;
		const auto projection = byTrain.find(train.trainDescription);
		if (projection == byTrain.end())
			continue;
		for (const auto& valid : validTrajectorySegments(train.instant_spatial_position,
				 train.earliestActiveTrajectoryIndex, train.End_Time)) {
			bool open = false;
			for (int t = valid.first; t <= valid.last; ++t) {
				const double time = t * timestep;
				const double sourceKm = routeDiagramTrajectoryKm(train.instant_spatial_position[t]);
				const auto x = projection->second.map(sourceKm);
				const bool inScope = x && time >= scope.startTime && time <= scope.endTime
					&& std::any_of(segments.begin(), segments.end(), [&](const auto& block) {
						   return block.trainName == train.trainDescription && time >= block.startTime
							   && time <= block.endTime && *x >= std::min(block.startPositionKm, block.endPositionKm)
							   && *x <= std::max(block.startPositionKm, block.endPositionKm);
					   });
				if (!inScope) {
					open = false;
					continue;
				}
				if (!open) {
					result.trajectories.push_back({train.trainDescription, {}});
					open = true;
				}
				result.trajectories.back().points.emplace_back(*x, time);
				result.trajectoryRows.push_back({train.trainDescription, "", "", "", csv::formatDouble(*x),
					"recorded sample", "", "", csv::formatDouble(time), "", "", "", "", "", "", "", "", "", "", "", "", "recorded trajectory"});
			}
		}
	}
	result.referencePath = referencePath;
	result.segments = std::move(segments);
	result.unmapped = std::move(unmapped);
	result.extents = extents;
	result.omitted = omitted;
	return result;
}

std::optional<RouteDiagramData> legacyRouteDiagram(const SceneModel& scene, bool timetable, int referenceIndex) {
	if (referenceIndex < 0 || referenceIndex >= static_cast<int>(train_route.size()))
		return std::nullopt;
	const Route& reference = train_route[referenceIndex];
	RouteDiagramData result;
	result.referencePath = routeDiagramPath(reference, &scene);
	if (result.referencePath.nodes.empty())
		return std::nullopt;
	std::map<int, RouteDiagramPath> paths;
	std::map<int, RouteDiagramProjection> projections;
	for (int i = 0; i < numRegions; ++i) {
		const int routeIndex = regional_train[i].indexOfRoute;
		if (routeIndex < 0 || routeIndex >= static_cast<int>(train_route.size()) || paths.count(routeIndex))
			continue;
		paths.emplace(routeIndex, routeDiagramPath(train_route[routeIndex], &scene));
		projections.emplace(routeIndex, buildRouteDiagramProjection(paths.at(routeIndex), result.referencePath));
	}
	const auto position = [&](const Train& train, double sourceKm) -> std::optional<double> {
		const auto projection = projections.find(train.indexOfRoute);
		return projection == projections.end() ? std::optional<double>() : projection->second.map(sourceKm);
	};
	const auto record = [&](const Train& train, const char* kind, const std::string& station,
							int journey, int call, const RunResultValue& time, std::optional<double> x) {
		result.csvRows.push_back({train.trainDescription, reference.ID, train_route[train.indexOfRoute].ID,
			kind, station, journey ? std::to_string(journey) : "", call ? std::to_string(call) : "",
			time.available ? csv::formatDouble(time.value) : "", x ? csv::formatDouble(*x) : ""});
	};
	const auto newSeries = [](RouteDiagramSeries::Kind kind, const Train& train, int index, const std::string& event, bool planned) {
		RouteDiagramSeries series;
		series.kind = kind;
		series.trainId = train.trainDescription;
		series.trainIndex = index;
		series.event = event;
		series.planned = planned;
		return series;
	};
	if (timetable) {
		const auto rows = buildTimetableResults(runResultTrainPointers());
		for (int i = 0; i < numRegions; ++i) {
			const Train& train = regional_train[i];
			const auto path = paths.find(train.indexOfRoute);
			const auto projection = projections.find(train.indexOfRoute);
			if (path == paths.end() || projection == projections.end())
				continue;
			for (int kind = 0; kind < 4; ++kind) {
				bool open = false;
				for (const auto& row : rows) {
					if (row.trainId != train.trainDescription)
						continue;
					const int stopIndex = row.journeyIndex - 1;
					const auto x = legacyStopPosition(train, stopIndex, path->second, projection->second);
					const RunResultValue time = kind == 0 ? row.plannedArrivalSeconds : kind == 1 ? row.plannedDepartureSeconds
						: kind == 2																  ? row.simulatedArrivalSeconds
																								  : row.simulatedDepartureSeconds;
					const char* event = kind == 0 ? "planned arrival" : kind == 1 ? "planned departure"
						: kind == 2												  ? "simulated arrival"
																				  : "simulated departure";
					record(train, event, row.stationId, row.journeyIndex, row.callIndex, time, x);
					if (!x || !time.available) {
						open = false;
						continue;
					}
					if (!open) {
						result.series.push_back(newSeries(RouteDiagramSeries::Kind::Event, train, i, event, kind < 2));
						open = true;
					}
					result.series.back().points.push_back({*x, time.value, row.stationId, row.journeyIndex, row.callIndex});
				}
			}
			for (const auto& row : rows) {
				if (row.trainId != train.trainDescription)
					continue;
				const auto x = legacyStopPosition(train, row.journeyIndex - 1, path->second, projection->second);
				for (int planned = 0; planned < 2; ++planned) {
					const auto arrival = planned ? row.plannedArrivalSeconds : row.simulatedArrivalSeconds;
					const auto departure = planned ? row.plannedDepartureSeconds : row.simulatedDepartureSeconds;
					if (!x || !arrival.available || !departure.available)
						continue;
					RouteDiagramSeries dwell = newSeries(RouteDiagramSeries::Kind::Dwell, train, i, planned ? "planned dwell" : "simulated dwell", planned != 0);
					dwell.points.push_back({*x, arrival.value, row.stationId, row.journeyIndex, row.callIndex});
					dwell.points.push_back({*x, departure.value, row.stationId, row.journeyIndex, row.callIndex});
					result.series.push_back(std::move(dwell));
				}
			}
		}
	} else {
		for (int i = 0; i < numRegions; ++i) {
			const Train& train = regional_train[i];
			if (train.indexOfRoute < 0 || train.indexOfRoute >= static_cast<int>(train_route.size()))
				continue;
			if (train.earliestActiveTrajectoryIndex < 0)
				continue;
			for (const auto& valid : validTrajectorySegments(train.instant_spatial_position,
					 train.earliestActiveTrajectoryIndex, train.End_Time)) {
				bool open = false;
				for (int t = valid.first; t <= valid.last; ++t) {
					const double sourceKm = routeDiagramTrajectoryKm(train.instant_spatial_position[t]);
					const auto x = position(train, sourceKm);
					const RunResultValue time{true, t * timestep};
					record(train, "trajectory", "", 0, 0, time, x);
					if (!x) {
						open = false;
						continue;
					}
					if (!open) {
						RouteDiagramSeries line = newSeries(RouteDiagramSeries::Kind::Trajectory, train, i, std::string(), false);
						line.routeId = train_route[train.indexOfRoute].ID;
						result.series.push_back(std::move(line));
						open = true;
					}
					result.series.back().points.push_back({*x, time.value, std::string(), 0, 0});
				}
			}
		}
	}
	return result;
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
	initial_variables.TSM = 0;
	initial_variables.RChoice = 0;
	initial_variables.PAX_GUI = false;
	initial_variables.exportDetailedTrajectories = false;
	initial_variables.OutputMainFolder = outputDir;
	InputMainFolder.clear();
	initial_variables.InputMainFolder.clear();

	CoutSilencer silencer;
	const std::vector<SceneDiagnostic> diagnostics = simulation.prepareScene(run.loaded.scene);
	if (hasErrors(diagnostics)) {
		std::cerr << "cannot prepare " << sceneDir << "\n";
		return false;
	}
	simulation.setSnapshotsEnabled(false);
	simulation.runSimulation();
	if (numRegions <= 0) {
		std::cerr << sceneDir << " ran no train\n";
		return false;
	}
	// No committed scene has a reversed route. Marking every other route shows the capture both ways.
	for (std::size_t r = 0; r < train_route.size(); ++r)
		train_route[r].reversed_direction = r % 2 == 0;
	RunProvenance provenance;
	provenance.caseName = run.loaded.scene.name;
	run.data = captureCompletedRun(&run.loaded.scene, initial_variables.startingSimulationTime, provenance);
	return true;
}

// --- Old and new side by side ---

void compareCapture(const CapturedRun& run) {
	const std::string scene = run.scene;
	const CompletedRunData& data = run.data;
	check(data.settings.timestepSeconds == timestep, scene + ": timestep");
	check(data.settings.horizonSteps == initial_variables.times, scene + ": horizon");
	check(data.settings.horizonSeconds() == initial_variables.times * timestep, scene + ": horizon in seconds");
	check(data.settings.startOffsetSeconds == initial_variables.startingSimulationTime, scene + ": start offset");
	check(data.settings.caseName == run.loaded.scene.name && data.provenance.caseName == run.loaded.scene.name, scene + ": case name");
	check(static_cast<int>(data.trains.size()) == numRegions, scene + ": train count");
	check(data.results.trains.size() == buildRunResults(runResultTrainPointers(), timestep).trains.size(), scene + ": result rows");
	check(!data.timetable.empty(), scene + ": timetable rows");

	check(data.routes.size() == train_route.size(), scene + ": route count");
	for (std::size_t r = 0; r < data.routes.size() && r < train_route.size(); ++r) {
		const CompletedRoute& route = data.routes[r];
		check(route.id == train_route[r].ID && route.corridor == train_route[r].corridor, scene + ": route identity " + std::to_string(r));
		check(route.reversedDirection == (r % 2 == 0), scene + ": route direction " + std::to_string(r));
		const auto inputs = blockSectionInputsForRoute(train_route[r]);
		bool same = inputs.size() == route.blocks.size();
		for (std::size_t b = 0; same && b < inputs.size(); ++b)
			same = inputs[b].id == route.blocks[b].id && inputs[b].startKm == route.blocks[b].startKm && inputs[b].endKm == route.blocks[b].endKm;
		check(same, scene + ": route blocks " + std::to_string(r));
		checkSame(scene + ": route path " + std::to_string(r), describe(routeDiagramPath(train_route[r], &run.loaded.scene)), describe(route.path));
	}

	for (int i = 0; i < numRegions && i < static_cast<int>(data.trains.size()); ++i) {
		const Train& train = regional_train[i];
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
		if (captured.routeIndex >= 0 && captured.routeIndex < static_cast<int>(train_route.size())) {
			const Route& route = train_route[captured.routeIndex];
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

void compareExports(const CapturedRun& run) {
	const std::string scene = run.scene;
	const CompletedRunData& data = run.data;
	const QStringList ids = allTrainIds();
	check(trainIdsFrom(data) == ids, scene + ": train ids");

	std::vector<QStringList> selections = {ids, QStringList(), QStringList{"no such train"}};
	if (!ids.isEmpty()) {
		selections.push_back(QStringList{ids.first()});
		selections.push_back(QStringList{ids.last()});
		selections.push_back(QStringList{ids.first(), ids.last()});
	}
	for (std::size_t k = 0; k < selections.size(); ++k) {
		const std::string label = scene + ", selection " + std::to_string(k);
		checkSame(label + ": trajectory.csv", buildTrajectoryCsv(selections[k]), trajectoryCsvFrom(data, selections[k]));
		checkSame(label + ": timetable.csv", buildTimetableCsv(selections[k]), timetableCsvFrom(data, selections[k]));
		checkSame(label + ": blocking_time.csv", buildBlockingTimeCsv(selections[k]), blockingTimeCsvFrom(data, selections[k]));
	}
	check(!trajectoryCsvFrom(data, ids).empty(), scene + ": trajectory.csv has rows");
	check(!timetableCsvFrom(data, ids).empty(), scene + ": timetable.csv has rows");
	check(!blockingTimeCsvFrom(data, ids).empty(), scene + ": blocking_time.csv has rows");
	const auto oldSegments = buildAllBlockingTimeSegments();
	const auto newSegments = blockTimesFrom(data);
	check(!oldSegments.empty(), scene + ": blocking-time segments exist");
	bool sameSegments = oldSegments.size() == newSegments.size();
	for (std::size_t k = 0; sameSegments && k < oldSegments.size(); ++k)
		sameSegments = describe(oldSegments[k]) == describe(newSegments[k]);
	check(sameSegments, scene + ": all blocking-time segments");
	const BlockingTimeScope oldScope = defaultBlockingTimeScope(initial_variables.times * timestep);
	const BlockingTimeScope newScope = defaultBlockingTimeScopeFrom(data);
	check(oldScope.startTime == newScope.startTime && oldScope.endTime == newScope.endTime && oldScope.routeIndex == newScope.routeIndex
			&& oldScope.trainIds == newScope.trainIds && oldScope.blockIds == newScope.blockIds && oldScope.sectionIndices == newScope.sectionIndices,
		scene + ": default blocking-time scope");
}

// What the scope dialog offers must be what the simulation holds.
void compareScopeChoices(const CapturedRun& run) {
	const std::string scene = run.scene;
	const BlockingTimeScopeChoices choices = blockingTimeScopeChoicesFrom(run.data);
	check(choices.caseEndSeconds == initial_variables.times * timestep && choices.stepSeconds == timestep, scene + ": scope choices extent");
	check(choices.routes.size() == train_route.size(), scene + ": scope choice routes");
	for (std::size_t r = 0; r < choices.routes.size() && r < train_route.size(); ++r) {
		const Route& route = train_route[r];
		std::vector<std::string> blockIds;
		for (int b = 0; b < route.N_Block_Sections; ++b)
			blockIds.push_back(route.sequence_of_block_sections[b].ID);
		check(choices.routes[r].id == route.ID && choices.routes[r].corridor == route.corridor && choices.routes[r].blockIds == blockIds,
			scene + ": scope choice route " + std::to_string(r));
	}
	check(static_cast<int>(choices.trains.size()) == numRegions, scene + ": scope choice trains");
	for (int i = 0; i < numRegions && i < static_cast<int>(choices.trains.size()); ++i)
		check(choices.trains[i].id == regional_train[i].trainDescription && choices.trains[i].routeIndex == regional_train[i].indexOfRoute
				&& choices.trains[i].routeId == regional_train[i].TrainRouteID,
			scene + ": scope choice train " + std::to_string(i));
}

std::vector<BlockingTimeScope> blockingScopes() {
	std::vector<BlockingTimeScope> scopes;
	const BlockingTimeScope whole = defaultBlockingTimeScope(initial_variables.times * timestep);
	scopes.push_back(whole);
	BlockingTimeScope window = whole;
	window.trainIds = {regional_train[0].trainDescription};
	window.startTime = whole.endTime * 0.25;
	window.endTime = whole.endTime * 0.75;
	scopes.push_back(window);
	BlockingTimeScope late = whole;
	late.startTime = whole.endTime * 0.5;
	scopes.push_back(late);
	for (const auto& used : legacyUsedRouteReferences()) {
		const Route& route = train_route[used.first];
		BlockingTimeScope scope = whole;
		scope.routeIndex = used.first;
		for (int i = 0; i < numRegions; ++i)
			if (regional_train[i].indexOfRoute == used.first || regional_train[i].TrainRouteID == route.ID)
				scope.trainIds.push_back(regional_train[i].trainDescription);
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

void compareBlockingDiagrams(const CapturedRun& run) {
	const QStringList ids = allTrainIds();
	const auto scopes = blockingScopes();
	std::size_t withSegments = 0, withPlanned = 0, withTrajectory = 0, withSections = 0;
	for (std::size_t k = 0; k < scopes.size(); ++k) {
		const auto expected = legacyBlockingTimeDiagram(run.loaded.scene, scopes[k]);
		const auto actual = blockingTimeDiagramData(run.data, scopes[k]);
		checkSame(run.scene + ": blocking-time diagram of scope " + std::to_string(k), describe(expected, ids), describe(actual, ids));
		if (!actual)
			continue;
		withSegments += actual->segments.empty() ? 0 : 1;
		withPlanned += actual->plannedReferences.empty() ? 0 : 1;
		withTrajectory += actual->trajectories.empty() ? 0 : 1;
		withSections += actual->extents.empty() ? 0 : 1;
	}
	check(withSegments > 1 && withPlanned > 0 && withTrajectory > 0 && withSections > 0,
		run.scene + ": the scopes cover envelopes, planned references, trajectories and sections");
}

CapacityAnalysisScope capacityScopeOf(int routeIndex, const std::vector<int>& sections) {
	CapacityAnalysisScope scope;
	scope.routeIndex = routeIndex;
	for (int index : sections) {
		scope.sectionIndices.push_back(index);
		scope.blockIds.push_back(train_route[routeIndex].sequence_of_block_sections[index].ID);
	}
	return scope;
}

void compareCapacity(const CapturedRun& run) {
	const std::string scene = run.scene;
	const CompletedRunData& data = run.data;
	std::size_t pairs = 0;
	for (int r = 0; r < static_cast<int>(train_route.size()); ++r) {
		const int count = train_route[r].N_Block_Sections;
		std::vector<CapacityAnalysisScope> scopes;
		for (int b = 0; b < count; ++b)
			scopes.push_back(capacityScopeOf(r, {b}));
		if (count > 1)
			scopes.push_back(capacityScopeOf(r, blockSectionRangeIndices(0, count - 1, count)));
		for (std::size_t k = 0; k < scopes.size(); ++k) {
			const std::string label = scene + ": capacity scope " + std::to_string(r) + "/" + std::to_string(k);
			const auto expected = capacityTrainsForScope(scopes[k]);
			checkSame(label + " inputs", describe(expected), describe(capacityInputsFrom(data, scopes[k])));
			check(capacityScopeIssue(scopes[k]) == capacityScopeIssueFrom(data, scopes[k]), label + " issue");
			check(capacityScopeLabel(scopes[k]) == capacityScopeLabelFrom(data, scopes[k]), label + " label");
			pairs += expected.size() > 1 ? 1 : 0;
			for (int i = 0; i < numRegions; ++i)
				checkSame(label + " train " + std::to_string(i), describe(capacityTrainForScope(regional_train[i], scopes[k])),
					describe(capacityInputFrom(data.trains[i], scopes[k])));
		}
	}
	check(capacityScopeLabel(CapacityAnalysisScope()) == capacityScopeLabelFrom(data, CapacityAnalysisScope()), scene + ": label without a route");

	const CapacityAnalysisScope first = firstCapacityScopeWithPair();
	const CapacityAnalysisScope firstFromData = firstCapacityScopeWithPairFrom(data);
	check(first.routeIndex == firstFromData.routeIndex && first.blockIds == firstFromData.blockIds
			&& first.sectionIndices == firstFromData.sectionIndices && first.occurrenceIds == firstFromData.occurrenceIds
			&& first.cycleEndOccurrenceId == firstFromData.cycleEndOccurrenceId && first.periodSeconds == firstFromData.periodSeconds,
		scene + ": first scope with a pair");
	if (first.routeIndex < 0) {
		std::cout << scene << ": no scope with two trains, so no capacity analysis\n";
		return;
	}
	check(pairs > 0, scene + ": scopes with a pair");
	CapacityAnalysisScope scope = first;
	std::vector<CapacityAnalysisScope> analysed = {scope};
	scope.cycleEndOccurrenceId = scope.occurrenceIds.back();
	analysed.push_back(scope);
	scope.occurrenceIds = {scope.occurrenceIds.back(), "no such train", scope.occurrenceIds.front()};
	scope.cycleEndOccurrenceId = scope.occurrenceIds.front();
	analysed.push_back(scope);
	bool anyCsv = false;
	for (std::size_t k = 0; k < analysed.size(); ++k) {
		const std::string expected = capacityScopeIssue(analysed[k]).empty()
			? capacityAnalysisCsv(analyzeCapacity(capacityTrainsForScope(analysed[k]), analysed[k].periodSeconds, analysed[k].cycleEndOccurrenceId),
				  capacityScopeLabel(analysed[k]))
			: std::string();
		checkSame(scene + ": capacity_analysis.csv of scope " + std::to_string(k), expected, capacityCsvFrom(data, analysed[k]));
		anyCsv = anyCsv || !expected.empty();
	}
	check(anyCsv, scene + ": a capacity_analysis.csv exists");
}

void compareRouteDiagrams(const CapturedRun& run) {
	const std::string scene = run.scene;
	const auto expectedUsed = legacyUsedRouteReferences();
	check(expectedUsed == usedRouteReferencesFrom(run.data), scene + ": used routes");
	check(!expectedUsed.empty(), scene + ": routes are used");
	std::size_t withSeries = 0;
	for (const auto& used : expectedUsed)
		for (bool timetable : {false, true}) {
			const auto expected = legacyRouteDiagram(run.loaded.scene, timetable, used.first);
			const auto actual = routeDiagramData(run.data, timetable, used.first);
			checkSame(scene + ": " + (timetable ? "timetable graph" : "train paths") + " on " + used.second, describe(expected), describe(actual));
			withSeries += actual && !actual->series.empty() ? 1 : 0;
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

// Runs the scope dialog on hand-made choices and lets `answer` act on it once it is open.
bool askScope(const BlockingTimeScopeChoices& choices, BlockingTimeScope& scope, const std::function<void(QDialog&)>& answer) {
	QTimer::singleShot(0, [&answer]() {
		auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
		check(dialog && dialog->windowTitle() == "Blocking-time scope", "the scope dialog opens");
		if (dialog)
			answer(*dialog);
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
		check(dialog && dialog->windowTitle() == "Capacity analysis", "the capacity dialog opens");
		if (dialog)
			answer(*dialog);
	});
	return chooseCapacityAnalysisScope(nullptr, run, scope);
}

// Closes the message box that the next click on OK opens.
void expectMessage(const std::string& what) {
	QTimer::singleShot(0, [what]() {
		auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
		check(box, what);
		if (box)
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

	// Fewer than two occurrences, or a closing occurrence that is not after the first one, keeps the dialog open.
	CapacityAnalysisScope kept;
	accepted = askCapacityScope(run, kept, [](QDialog& dialog) {
		const auto combos = dialog.findChildren<QComboBox*>();
		const auto list = dialog.findChild<QListWidget*>();
		auto* ok = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
		if (combos.size() != 4 || !list)
			return;
		combos[2]->setCurrentIndex(0);
		list->item(1)->setCheckState(Qt::Unchecked);
		list->item(2)->setCheckState(Qt::Unchecked);
		expectMessage("fewer than two occurrences are refused");
		ok->click();
		list->item(1)->setCheckState(Qt::Checked);
		combos[3]->setCurrentIndex(1);
		expectMessage("a closing occurrence that is the first row is refused");
		ok->click();
		check(dialog.isVisible(), "the dialog stays open after a refusal");
		dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();
	});
	check(!accepted, "cancel gives no scope");
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
		compareExports(run);
		compareScopeChoices(run);
		compareBlockingDiagrams(run);
		compareCapacity(run);
		compareRouteDiagrams(run);
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

	if (failures)
		std::cerr << failures << " checks failed\n";
	return failures ? 1 : 0;
}
