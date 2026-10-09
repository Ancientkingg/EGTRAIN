#include "diagrams/RouteDiagramCoordinates.h"
#include <cmath>
#include <iostream>

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
	std::cout << "route coordinate projection passed\n";
	return 0;
}
