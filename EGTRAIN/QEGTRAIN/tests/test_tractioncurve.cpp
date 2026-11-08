#include "diagrams/TractionCurve.h"

#include <QTest>

#include <cmath>
#include <string>

static bool closeTo(double a, double b) {
	return std::fabs(a - b) < 1e-6;
}

class TestTractionCurve : public QObject {
	Q_OBJECT

private slots:
	void emptyCurveHasNoSamples();
	void constantIntervalIsFlat();
	void quadraticIntervalFollowsFormula();
	void reversedIntervalIsSkipped();
	void pointIntervalYieldsOneSample();
	void warnsAboutAssociation();
};

void TestTractionCurve::emptyCurveHasNoSamples() {
	// An empty curve yields no points and cannot be plotted.
	QVERIFY2(sampleTractionCurve({}).empty(), "empty curve has no samples");
}

// A constant interval: F(v) = C0 for every sampled speed.
void TestTractionCurve::constantIntervalIsFlat() {
	const auto pts = sampleTractionCurve({{0.0, 10.0, 170000.0, 0.0, 0.0}}, 4);
	// constant interval samples steps+1 points
	QCOMPARE(pts.size(), std::size_t(5));
	// interval spans lower to upper
	QVERIFY(closeTo(pts.front().first, 0.0));
	QVERIFY(closeTo(pts.back().first, 10.0));
	bool allConstant = true;
	for (const auto& p : pts)
		allConstant = allConstant && closeTo(p.second, 170000.0);
	QVERIFY2(allConstant, "constant coefficient gives flat effort");
}

// A quadratic interval evaluates C0 + C1 v + C2 v^2 at the endpoints.
void TestTractionCurve::quadraticIntervalFollowsFormula() {
	const auto pts = sampleTractionCurve({{10.0, 35.0, 279080.0, -13637.0, 207.1}}, 5);
	// quadratic interval samples steps+1 points
	QCOMPARE(pts.size(), std::size_t(6));
	const double atTen = 279080.0 + (-13637.0) * 10.0 + 207.1 * 100.0;
	const double atThirtyFive = 279080.0 + (-13637.0) * 35.0 + 207.1 * 35.0 * 35.0;
	QVERIFY2(closeTo(pts.front().second, atTen), "effort at lower speed matches formula");
	QVERIFY2(closeTo(pts.back().second, atThirtyFive), "effort at upper speed matches formula");
}

void TestTractionCurve::reversedIntervalIsSkipped() {
	// A degenerate row whose upper speed is below the lower speed is skipped.
	QVERIFY2(sampleTractionCurve({{20.0, 5.0, 1.0, 0.0, 0.0}}).empty(), "reversed interval is skipped");
}

void TestTractionCurve::pointIntervalYieldsOneSample() {
	// A single point row (upper equals lower) yields one sample.
	const auto pts = sampleTractionCurve({{12.0, 12.0, 100.0, 0.0, 0.0}});
	// point interval yields a single sample
	QCOMPARE(pts.size(), std::size_t(1));
	QVERIFY(closeTo(pts[0].first, 12.0));
	QVERIFY(closeTo(pts[0].second, 100.0));
}

void TestTractionCurve::warnsAboutAssociation() {
	QVERIFY2(tractionAssociationWarning(true, true).empty(), "unit with curve has no warning when provenance is absent");
	QVERIFY2(!tractionAssociationWarning(false, false).empty(), "undefined unit warns");
	QVERIFY2(tractionAssociationWarning(true, false).find("no traction curve") != std::string::npos, "missing curve warns");
}

QTEST_APPLESS_MAIN(TestTractionCurve)
#include "test_tractioncurve.moc"
