#include "diagrams/CompletedRunData.h"
#include "diagrams/RouteDiagramCoordinates.h"
#include <cmath>
#include <iostream>
#include <limits>

int main() {
	const RouteDiagramPath reference{"main", {{"a", "A", "Alpha", 42}, {"b", "B", "Beta", 47}, {"c", "C", "Gamma", 53}}};
	const RouteDiagramPath reverse{"return", {{"c", "C", "Gamma", 5}, {"b", "B", "Beta", 11}, {"a", "A", "Alpha", 16}}};
	const auto projection = buildRouteDiagramProjection(reverse, reference);
	const auto middle = projection.map(11);
	if (!middle || std::abs(*middle - 47) > 1e-8 || projection.map(4) || projection.map(17)) return 1;
	const auto identity = buildRouteDiagramProjection(reference, reference);
	if (!identity.identity || identity.map(42) != 42) return 2;
	const RouteDiagramPath inconsistent{"main", {{"a", "A", "Alpha", 43}, {"b", "B", "Beta", 48}, {"c", "C", "Gamma", 54}}};
	if (buildRouteDiagramProjection(inconsistent, reference).identity) return 3;
	const RouteDiagramPath unrelated{"other", {{"x", "", "Alpha", 1}, {"y", "", "Beta", 2}}};
	if (buildRouteDiagramProjection(unrelated, reference).map(1)) return 4;
	const RouteDiagramPath repeated{"return", {{"c", "C", "Gamma", 5}, {"b", "B", "Beta", 11}, {"a", "A", "Alpha", 16}, {"b", "B", "Beta", 21}}};
	if (buildRouteDiagramProjection(repeated, reference).map(11)) return 5;
	const RouteDiagramPath duplicateReference{"main", {{"a", "A", "Alpha", 42}, {"b", "B", "Beta", 47}, {"c", "C", "Gamma", 53}, {"b", "B", "Beta", 58}}};
	if (buildRouteDiagramProjection(reverse, duplicateReference).map(11)) return 6;
	const RouteDiagramPath colocated{"same", {{"n1", "one", "Central", -2}, {"n2", "two", "Central", -2}, {"n3", "zero", "Zero", 0}}};
	const auto labels = routeDiagramStationLabels(colocated);
	if (labels.size() != 2 || labels.at(-2).find("one") == std::string::npos
		|| labels.at(-2).find("two") == std::string::npos || labels.at(0).find("zero") == std::string::npos)
		return 7;
	if (routeDiagramTrajectoryKm(42000) != 42 || routeDiagramTrajectoryKm(-2000) != -2)
		return 8;
	const std::vector<std::vector<std::string>> rows = {
		{"A", "ref", "forward", "planned arrival", "one", "1", "1", "", "-2"},
		{"B", "ref", "reverse", "trajectory", "", "", "", "0", "0"}};
	const std::string csv = buildRouteDiagramCsv(rows, {"A"});
	if (csv.find("Train,Reference route,Source route,Event,Station,Journey order,Call,Elapsed time[s],Reference route X[km]") != 0
		|| csv.find("A,ref,forward,planned arrival,one,1,1,,-2") == std::string::npos
		|| csv.find("B,ref,") != std::string::npos
		|| buildRouteDiagramCsv(rows, {}).find("A,ref,") != std::string::npos) return 9;
	const RouteDiagramPath compact{"labels", {{"n", "Paimpol", "Paimpol", 0}, {"m", "Unnamed", "", 1}}};
	const auto compactLabels = routeDiagramStationLabels(compact);
	if (compactLabels.at(0) != "Paimpol" || compactLabels.at(1) != "Unnamed") return 10;
	// Distance range of a section chart, exactly: usable sections first, else the whole reference path.
	const auto closeTo = [](const std::pair<double, double>& range, double low, double high) {
		return std::abs(range.first - low) < 1e-12 && std::abs(range.second - high) < 1e-12;
	};
	const double nan = std::numeric_limits<double>::quiet_NaN();
	if (!closeTo(blockSectionAxisRange(buildBlockSectionExtents({{"@a@", 4.0, 5.0}, {"@b@", 5.0, 6.0}}), reference), 4.0, 6.0)) return 11;
	if (!closeTo(blockSectionAxisRange(buildBlockSectionExtents({{"@a@", 6.0, 4.0}}), reference), 4.0, 6.0)) return 12;
	if (!closeTo(blockSectionAxisRange(buildBlockSectionExtents({{"@a@", 1.0, 1.001}}), reference), 1.0, 1.02)) return 13;
	const auto unusable = buildBlockSectionExtents({{"@zero@", 3.0, 3.0}, {"@nan@", 1.0, nan}});
	if (!closeTo(blockSectionAxisRange(unusable, reference), 42.0, 53.0)) return 14;
	const RouteDiagramPath falling{"back", {{"c", "C", "Gamma", 53}, {"b", "B", "Beta", 47}, {"a", "A", "Alpha", 42}}};
	if (!closeTo(blockSectionAxisRange({}, falling), 42.0, 53.0)) return 15;
	const RouteDiagramPath returning{"loop", {{"a", "A", "Alpha", 2}, {"b", "B", "Beta", 8}, {"c", "C", "Gamma", -2}, {"n", "", "", nan}}};
	if (!closeTo(blockSectionAxisRange(unusable, returning), -2.0, 8.0)) return 16;
	if (!closeTo(blockSectionAxisRange(unusable, RouteDiagramPath{}), 0.0, 1.0)) return 17;
	// A stop lies where its node is on the path at the stored abscissa, mapped onto the reference route.
	CompletedStop stop;
	stop.nodeId = "b";
	stop.routeX = 11;
	const auto stopOnReference = routeDiagramStopPosition(stop, reverse, projection);
	if (!stopOnReference || std::abs(*stopOnReference - 47) > 1e-8) return 18;
	stop.routeX = 11.5;
	if (routeDiagramStopPosition(stop, reverse, projection)) return 19;
	stop.routeX = 11;
	stop.nodeId = "z";
	if (routeDiagramStopPosition(stop, reverse, projection)) return 20;
	stop.nodeId.clear();
	if (routeDiagramStopPosition(stop, reverse, projection)) return 21;
	std::cout << "route coordinate projection passed\n";
	return 0;
}
