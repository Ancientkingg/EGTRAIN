#include "diagrams/TractionCurve.h"

#include <algorithm>

std::vector<std::pair<double, double>> sampleTractionCurve(
	const std::vector<std::array<double, 5>>& curve, int stepsPerInterval) {
	std::vector<std::pair<double, double>> points;
	const int steps = std::max(1, stepsPerInterval);
	for (const std::array<double, 5>& row : curve) {
		const double lower = row[0];
		const double upper = row[1];
		const double c0 = row[2];
		const double c1 = row[3];
		const double c2 = row[4];
		if (upper < lower)
			continue;
		const auto effortAt = [&](double v) { return c0 + c1 * v + c2 * v * v; };
		if (upper == lower) {
			points.emplace_back(lower, effortAt(lower));
			continue;
		}
		for (int i = 0; i <= steps; ++i) {
			const double v = lower + (upper - lower) * static_cast<double>(i) / steps;
			points.emplace_back(v, effortAt(v));
		}
	}
	return points;
}

std::string tractionAssociationWarning(bool unitDefined, bool hasCurve, bool hasTractionFile) {
	if (!unitDefined)
		return "This unit is not defined in the train units.";
	if (!hasCurve)
		return "This unit has no traction curve to plot.";
	if (!hasTractionFile)
		return "This unit has a traction curve but no recorded T_LITRA source file.";
	return std::string();
}
