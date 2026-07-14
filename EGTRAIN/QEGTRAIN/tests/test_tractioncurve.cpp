#include "diagrams/TractionCurve.h"

#include <cmath>
#include <iostream>

static bool ok = true;

static bool expect(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "failed: " << message << "\n";
		ok = false;
	}
	return condition;
}

static bool closeTo(double a, double b) {
	return std::fabs(a - b) < 1e-6;
}

int main() {
	// An empty curve yields no points and cannot be plotted.
	expect(sampleTractionCurve({}).empty(), "empty curve has no samples");

	// A constant interval: F(v) = C0 for every sampled speed.
	{
		const auto pts = sampleTractionCurve({{0.0, 10.0, 170000.0, 0.0, 0.0}}, 4);
		expect(pts.size() == 5, "constant interval samples steps+1 points");
		expect(closeTo(pts.front().first, 0.0) && closeTo(pts.back().first, 10.0), "interval spans lower to upper");
		bool allConstant = true;
		for (const auto& p : pts)
			allConstant = allConstant && closeTo(p.second, 170000.0);
		expect(allConstant, "constant coefficient gives flat effort");
	}

	// A quadratic interval evaluates C0 + C1 v + C2 v^2 at the endpoints.
	{
		const auto pts = sampleTractionCurve({{10.0, 35.0, 279080.0, -13637.0, 207.1}}, 5);
		expect(pts.size() == 6, "quadratic interval samples steps+1 points");
		const double atTen = 279080.0 + (-13637.0) * 10.0 + 207.1 * 100.0;
		const double atThirtyFive = 279080.0 + (-13637.0) * 35.0 + 207.1 * 35.0 * 35.0;
		expect(closeTo(pts.front().second, atTen), "effort at lower speed matches formula");
		expect(closeTo(pts.back().second, atThirtyFive), "effort at upper speed matches formula");
	}

	// A degenerate row whose upper speed is below the lower speed is skipped.
	expect(sampleTractionCurve({{20.0, 5.0, 1.0, 0.0, 0.0}}).empty(), "reversed interval is skipped");

	// A single point row (upper equals lower) yields one sample.
	{
		const auto pts = sampleTractionCurve({{12.0, 12.0, 100.0, 0.0, 0.0}});
		expect(pts.size() == 1 && closeTo(pts[0].first, 12.0) && closeTo(pts[0].second, 100.0),
			   "point interval yields a single sample");
	}

	// Association warnings.
	expect(tractionAssociationWarning(true, true, true).empty(), "complete association has no warning");
	expect(!tractionAssociationWarning(false, false, false).empty(), "undefined unit warns");
	expect(tractionAssociationWarning(true, false, true).find("no traction curve") != std::string::npos,
		   "missing curve warns");
	expect(tractionAssociationWarning(true, true, false).find("no recorded") != std::string::npos,
		   "curve without source file warns about the mismatch");

	if (!ok)
		return 1;
	std::cout << "all TractionCurve tests passed\n";
	return 0;
}
