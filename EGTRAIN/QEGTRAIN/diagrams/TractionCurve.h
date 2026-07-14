#ifndef TRACTIONCURVE_H
#define TRACTIONCURVE_H

#include <array>
#include <string>
#include <utility>
#include <vector>

// Helpers for plotting a train unit's tractive-effort curve and for reporting a
// missing or mismatched rolling-stock / traction association.

// Sample tractive effort F(v) = C0 + C1 v + C2 v^2 across each speed interval of
// a traction curve. Each row is {lowerSpeed, upperSpeed, C0, C1, C2} in m/s and
// newtons. Returns (speed m/s, effort N) points, ordered by interval. Rows whose
// upper speed is below the lower speed are skipped. An empty curve yields no
// points.
std::vector<std::pair<double, double>> sampleTractionCurve(
	const std::vector<std::array<double, 5>>& curve, int stepsPerInterval = 16);

// Describe a missing or mismatched traction association for a composition unit.
// An empty string means the association is complete and the curve can be
// plotted.
std::string tractionAssociationWarning(bool unitDefined, bool hasCurve, bool hasTractionFile);

#endif // TRACTIONCURVE_H
