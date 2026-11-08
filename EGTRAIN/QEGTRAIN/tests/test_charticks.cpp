// Tests of the tick and label rules of the charts. Text is 7 px wide per character, so every expected value below follows
// from the rule by hand.

#include "diagrams/ChartTicks.h"

#include <QTest>

#include <cmath>
#include <limits>

namespace {

double sevenPerCharacter(const QString& text) {
	return 7.0 * static_cast<double>(text.size());
}

// Keeps the characters that fit and marks the cut, as a font would.
QString cutToWidth(const QString& text, double width) {
	return text.left(static_cast<int>(width / 7.0)) + QStringLiteral("...");
}

// One line per label, so that a failure names the label.
QStringList describe(const std::vector<std::pair<double, QString>>& labels) {
	QStringList lines;
	for (const auto& label : labels) lines << QStringLiteral("%1 %2").arg(label.first).arg(label.second);
	return lines;
}

StationTick station(double position, const QStringList& spellings, int calls = 0) {
	StationTick tick;
	tick.position = position;
	tick.spellings = spellings;
	tick.calls = calls;
	return tick;
}

// Plot from 100 to 900 px in a chart of 1000 px whose axes start at 50 px.
constexpr QRectF kStationPlot(100, 0, 800, 300);

std::vector<std::pair<double, QString>> labelsOf(const std::vector<StationTick>& stations, double low, double high, bool reverse = false) {
	return labelledStations(stations, low, high, reverse, kStationPlot, 50, 1000, sevenPerCharacter, cutToWidth);
}

// Nine stations 100 px apart on an axis of 0 to 8, each with a name of ten characters, 70 px, and `calls` of each.
std::vector<StationTick> crowdedStations(const std::vector<int>& calls = {}) {
	std::vector<StationTick> stations;
	for (int i = 0; i < 9; ++i)
		stations.push_back(station(i, {QStringLiteral("Station-%1%1").arg(i)}, i < static_cast<int>(calls.size()) ? calls[static_cast<std::size_t>(i)] : 0));
	return stations;
}

constexpr long long kStartAt071040 = 7 * 3600 + 10 * 60 + 40;

QStringList clockLabels(double low, double high, double pixelsPerSecond, long long startOffset = kStartAt071040) {
	return describe(clockTicks(low, high, clockTickInterval(low, high), startOffset, pixelsPerSecond, sevenPerCharacter));
}

} // namespace

class TestChartTicks : public QObject {
	Q_OBJECT

private slots:
	void roundTickStepsCutToThirtyOneBits();
	void anchorIsZeroAcrossZero();
	void anchorIsInsideARangeWithoutZero();
	void decimalsFollowTheStep();
	void noPlanForAnUnusableRange();
	void widensToRoundTicks_data();
	void widensToRoundTicks();
	void noWideningOfAnUnusableRange();

	void namesEveryStationThatFits();
	void takesTheLongestSpellingThatFits();
	void namesEverySecondStationWhenNotAllFit();
	void busiestStationsTakeTheLabels();
	void reversedAxisRunsRightToLeft();
	void leavesOutStationsOutsideTheRange();
	void elidesTheMiddleStationWhenNoneFits();
	void noStationLabelsOnAnEmptyAxis();

	void clockIntervalFollowsTheSpan_data();
	void clockIntervalFollowsTheSpan();
	void runStartKeepsItsSeconds();
	void roundTickNearTheRunStartNeedsRoom();
	void runStartOnARoundTickHasOneTick();
	void withoutTheRunStartEveryRoundTickStays();
	void ticksBeforeTheRunStartComeFirst();
	void shortStepsKeepTheirSeconds();
};

void TestChartTicks::roundTickStepsCutToThirtyOneBits() {
	// A span of 1 in eight steps takes a step of 0.2 = 1.6 * 2^-3, so the interval is whole multiples of 2^-33: floor(0.2 * 2^33).
	const auto fifth = roundTickPlan(0, 1);
	QVERIFY(fifth.has_value());
	QCOMPARE_EQ(fifth->interval, 1717986918.0 / std::ldexp(1.0, 33));
	QVERIFY(fifth->interval < 0.2);
	// 20 fits in 31 bits, so it stays as it is.
	const auto twenty = roundTickPlan(150, 250);
	QVERIFY(twenty.has_value());
	QCOMPARE_EQ(twenty->interval, 20.0);
	// A span of 0.01 takes a step of 0.002 = 1.024 * 2^-9, whose 31st and 32nd significant bits are both 1, so a cut to 30
	// or to 32 bits gives another interval than floor(0.002 * 2^39) * 2^-39.
	const auto twoThousandths = roundTickPlan(0, 0.01);
	QVERIFY(twoThousandths.has_value());
	QCOMPARE_EQ(twoThousandths->interval, 1099511627.0 / std::ldexp(1.0, 39));
}

void TestChartTicks::anchorIsZeroAcrossZero() {
	const std::vector<std::pair<double, double>> ranges{{-30, 70}, {0, 100}, {-100, 0}};
	for (const auto& range : ranges) {
		const auto plan = roundTickPlan(range.first, range.second);
		QVERIFY(plan.has_value());
		QCOMPARE_EQ(plan->anchor, 0.0);
	}
}

void TestChartTicks::anchorIsInsideARangeWithoutZero() {
	// The middle of 1 to 1.3 is 1.15, a multiple of the step of 0.05.
	const auto low = roundTickPlan(1, 1.3);
	QVERIFY(low.has_value());
	QCOMPARE(low->anchor, 1.15);
	// The middle of 150 to 250 is 200, a multiple of the step of 20.
	const auto high = roundTickPlan(150, 250);
	QVERIFY(high.has_value());
	QCOMPARE(high->anchor, 200.0);
	// The middle of -9 to -2 rounds to the nearest step of 1.
	const auto negative = roundTickPlan(-9, -2);
	QVERIFY(negative.has_value());
	QCOMPARE(negative->anchor, -6.0);
}

void TestChartTicks::decimalsFollowTheStep() {
	QCOMPARE(roundTickDecimals(0.05), 2);
	QCOMPARE(roundTickDecimals(0.1), 1);
	QCOMPARE(roundTickDecimals(0.001), 3);
	QCOMPARE(roundTickDecimals(1), 0);
	QCOMPARE(roundTickDecimals(20), 0);
	const auto fine = roundTickPlan(1, 1.3);
	QVERIFY(fine.has_value());
	QCOMPARE(fine->decimals, 2);
	const auto coarse = roundTickPlan(150, 250);
	QVERIFY(coarse.has_value());
	QCOMPARE(coarse->decimals, 0);
}

void TestChartTicks::noPlanForAnUnusableRange() {
	const double infinity = std::numeric_limits<double>::infinity();
	QVERIFY(!roundTickPlan(5, 5).has_value());
	QVERIFY(!roundTickPlan(7, 3).has_value());
	QVERIFY(!roundTickPlan(0, infinity).has_value());
	QVERIFY(!roundTickPlan(std::numeric_limits<double>::quiet_NaN(), 1).has_value());
}

void TestChartTicks::widensToRoundTicks_data() {
	QTest::addColumn<double>("low");
	QTest::addColumn<double>("high");
	QTest::addColumn<double>("start");
	QTest::addColumn<double>("end");
	// Ten steps of 1 are too many, so the step is 2.
	QTest::newRow("from zero") << 0.0 << 9.3 << 0.0 << 10.0;
	QTest::newRow("margin below zero") << -0.01 << 9.3 << 0.0 << 10.0;
	// Steps of 0.1, 0.2 and 0.5 give 65, 33 and 14 steps, and a step of 1 gives eight.
	QTest::newRow("away from zero") << 3.0 << 9.3 << 2.0 << 10.0;
	// An end on a tick moves out to the next tick, and one within a thousandth of the range of a tick counts as on it.
	QTest::newRow("end on a tick") << 0.0 << 10.0 << 0.0 << 12.0;
	QTest::newRow("end just below a tick") << 0.0 << 9.995 << 0.0 << 12.0;
	// Nine steps of 1 are one too many.
	QTest::newRow("nine steps") << 0.0 << 8.9 << 0.0 << 10.0;
	// Below zero by more than a tenth of a step of 2 the start is a tick below zero.
	QTest::newRow("well below zero") << -3.0 << 9.0 << -4.0 << 10.0;
}

void TestChartTicks::widensToRoundTicks() {
	QFETCH(double, low);
	QFETCH(double, high);
	QFETCH(double, start);
	QFETCH(double, end);
	const auto range = widenedTickRange(low, high);
	QVERIFY(range.has_value());
	QCOMPARE(range->first, start);
	QCOMPARE(range->second, end);
}

void TestChartTicks::noWideningOfAnUnusableRange() {
	QVERIFY(!widenedTickRange(5, 5).has_value());
	QVERIFY(!widenedTickRange(7, 3).has_value());
	QVERIFY(!widenedTickRange(0, std::numeric_limits<double>::infinity()).has_value());
}

void TestChartTicks::namesEveryStationThatFits() {
	// Three labels at 100, 500 and 900 px have a share of 800 / 4 - 16 = 184 px.
	const auto labels = labelsOf({station(0, {"Alpha"}), station(5, {"Beta"}), station(10, {"Gamma"})}, 0, 10);
	QCOMPARE(describe(labels), QStringList({"0 Alpha", "5 Beta", "10 Gamma"}));
}

void TestChartTicks::takesTheLongestSpellingThatFits() {
	// The share is 184 px: 30 characters are 210 px and 20 are 140 px. The first station has a room of 2 * (100 - 51) = 98 px.
	const QString tooLong(30, 'x'), fits(20, 'y');
	const auto labels = labelsOf({station(0, {fits, "Start"}), station(5, {tooLong, fits, "Mid"}), station(10, {"End"})}, 0, 10);
	QCOMPARE(describe(labels), QStringList({"0 Start", "5 " + fits, "10 End"}));
}

void TestChartTicks::namesEverySecondStationWhenNotAllFit() {
	// Nine labels have a share of 800 / 10 - 16 = 64 px, too little for 70 px; five have 800 / 6 - 16 = 117 px.
	const auto labels = labelsOf(crowdedStations(), 0, 8);
	QCOMPARE(describe(labels), QStringList({"0 Station-00", "2 Station-22", "4 Station-44", "6 Station-66", "8 Station-88"}));
}

void TestChartTicks::busiestStationsTakeTheLabels() {
	// The stride gives five labels, and neighbours keep 30 px apart, so the five busiest stations take them.
	const auto labels = labelsOf(crowdedStations({0, 5, 0, 4, 0, 3, 0, 2, 1}), 0, 8);
	QCOMPARE(describe(labels), QStringList({"1 Station-11", "3 Station-33", "5 Station-55", "7 Station-77", "8 Station-88"}));
}

void TestChartTicks::reversedAxisRunsRightToLeft() {
	const std::vector<StationTick> stations{station(0, {"Alpha"}), station(5, {"Beta"}), station(10, {"Terminus Central"})};
	// Sixteen characters are 112 px: room enough at the right end, where the room is the share of 184 px.
	QCOMPARE(describe(labelsOf(stations, 0, 10)), QStringList({"0 Alpha", "5 Beta", "10 Terminus Central"}));
	// Reversed, the last station lies at the left end with a room of 98 px and stays unlabelled, and the order is by pixel.
	QCOMPARE(describe(labelsOf(stations, 0, 10, true)), QStringList({"5 Beta", "0 Alpha"}));
}

void TestChartTicks::leavesOutStationsOutsideTheRange() {
	// Both ends lie just past the axis, near enough to it to have room for their labels, and the station at 6 has no spelling.
	const auto labels = labelsOf({station(-0.1, {"Before"}), station(5, {"Inside"}), station(10.1, {"After"}), station(6, {})}, 0, 10);
	QCOMPARE(describe(labels), QStringList({"5 Inside"}));
}

void TestChartTicks::elidesTheMiddleStationWhenNoneFits() {
	// In a plot of 100 px even one label has a share of only 100 / 2 - 16 = 34 px, and every spelling is 154 px or more.
	const QString name(21, 'n');
	QString elidedText;
	double elidedWidth = 0;
	const auto labels = labelledStations({station(1, {name + "a"}), station(2, {name + "bcd", name + "b"}), station(3, {name + "c"})}, 0, 4, false,
		QRectF(100, 0, 100, 300), 50, 1000, sevenPerCharacter, [&](const QString& text, double width) {
			elidedText = text;
			elidedWidth = width;
			return cutToWidth(text, width);
		});
	QCOMPARE(describe(labels), QStringList({"2 nnnn..."}));
	QCOMPARE(elidedText, name + "b");
	QCOMPARE(elidedWidth, 34.0);
}

void TestChartTicks::noStationLabelsOnAnEmptyAxis() {
	QVERIFY(labelsOf({station(0, {"Alpha"})}, 0, 0).empty());
	QVERIFY(labelledStations({station(1, {"Alpha"})}, 0, 2, false, QRectF(100, 0, 0, 300), 50, 1000, sevenPerCharacter, cutToWidth).empty());
	QVERIFY(labelsOf({}, 0, 10).empty());
}

void TestChartTicks::clockIntervalFollowsTheSpan_data() {
	QTest::addColumn<double>("span");
	QTest::addColumn<double>("interval");
	// The shortest clock step that gives at most eight steps.
	QTest::newRow("two minutes") << 120.0 << 30.0;
	QTest::newRow("five minutes") << 300.0 << 60.0;
	QTest::newRow("an hour") << 3600.0 << 600.0;
	// Past eight steps of twelve hours: a sixth of the span, 166,667 s, rounded up in its first digit.
	QTest::newRow("beyond the clock steps") << 1000000.0 << 200000.0;
}

void TestChartTicks::clockIntervalFollowsTheSpan() {
	QFETCH(double, span);
	QFETCH(double, interval);
	QCOMPARE(clockTickInterval(100, 100 + span), interval);
}

void TestChartTicks::runStartKeepsItsSeconds() {
	// 1200 s in steps of 300 s. Round ticks fall at 07:15:00, 260 s after the run start, and on: at a pixel per second the
	// first one lies 260 px from the run start, far more than its 35 px.
	QCOMPARE(clockLabels(0, 1200, 1), QStringList({"0 07:10:40", "260 07:15", "560 07:20", "860 07:25", "1160 07:30"}));
}

void TestChartTicks::roundTickNearTheRunStartNeedsRoom() {
	// At 0.1 px per second the tick lies 26 px from the run start, less than the 35 px of "07:15", and is left out. The tick
	// a whole step on stays, wherever it lies.
	QCOMPARE(clockLabels(0, 1200, 0.1), QStringList({"0 07:10:40", "560 07:20", "860 07:25", "1160 07:30"}));
	// At 0.135 px per second the tick lies 35.1 px from the run start, just enough.
	QCOMPARE(clockLabels(0, 1200, 0.135), QStringList({"0 07:10:40", "260 07:15", "560 07:20", "860 07:25", "1160 07:30"}));
	// A plot without size leaves it out too.
	QCOMPARE(clockLabels(0, 1200, 0), QStringList({"0 07:10:40", "560 07:20", "860 07:25", "1160 07:30"}));
}

void TestChartTicks::runStartOnARoundTickHasOneTick() {
	// 07:10:00 is a multiple of the step of 300 s, so the run start and the round tick are one tick, on a whole minute.
	constexpr long long startAt0710 = 7 * 3600 + 10 * 60;
	QCOMPARE(clockLabels(0, 1200, 1, startAt0710), QStringList({"0 07:10", "300 07:15", "600 07:20", "900 07:25", "1200 07:30"}));
}

void TestChartTicks::withoutTheRunStartEveryRoundTickStays() {
	// The range starts after the run start, so nothing competes with a round tick and pixels do not count.
	QCOMPARE(clockLabels(100, 1200, 0), QStringList({"260 07:15", "560 07:20", "860 07:25", "1160 07:30"}));
}

void TestChartTicks::ticksBeforeTheRunStartComeFirst() {
	// The round tick at 07:10:00 lies 40 px before the run start, more than its 35 px, and the ticks stay in the order of time.
	QCOMPARE(clockLabels(-300, 900, 1), QStringList({"-40 07:10", "0 07:10:40", "260 07:15", "560 07:20", "860 07:25"}));
}

void TestChartTicks::shortStepsKeepTheirSeconds() {
	// 120 s in steps of 30 s, under a minute apart, so whole minutes keep their seconds too.
	QCOMPARE(clockLabels(0, 120, 10), QStringList({"0 07:10:40", "20 07:11:00", "50 07:11:30", "80 07:12:00", "110 07:12:30"}));
}

QTEST_APPLESS_MAIN(TestChartTicks)
#include "test_charticks.moc"
