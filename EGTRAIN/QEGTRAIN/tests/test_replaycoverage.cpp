#include "app/ReplayCoverage.h"

#include <QObject>
#include <QTest>

#include <memory>
#include <string>

namespace {
ReplayCoverage coverage(ReplayCoverageKind kind, int first, int last) {
	ReplayCoverage out;
	out.kind = kind;
	out.firstTime = first;
	out.lastTime = last;
	out.clockOffsetSeconds = 8 * 3600;
	return out;
}

// A frame with a few hundred values that change, so that a few of them fill a small budget.
std::shared_ptr<const GuiSimulationSnapshot> frameAt(int time, int total) {
	auto frame = std::make_shared<GuiSimulationSnapshot>();
	frame->timestep = time;
	frame->totalTimesteps = total;
	frame->signalStates.resize(200);
	for (GuiSignalState& signal : frame->signalStates)
		signal.code = time % 3;
	return frame;
}

bool mentionsInternals(const std::string& text) {
	return text.find("evict") != std::string::npos || text.find("payload") != std::string::npos || text.find("budget") != std::string::npos;
}
}

class TestReplayCoverage : public QObject {
	Q_OBJECT

private slots:
	void wholeRun();
	void droppedFirstPart();
	void coverageIgnoresSelectedTime_data();
	void coverageIgnoresSelectedTime();
	void unavailable();
	void boundaries();
	void plainWords_data();
	void plainWords();
	void coverageOfHistory();
};

// A run that is kept whole: no text under the controls, since nothing is missing. The selected time is a clock time
// after the start of the run, 08:00:00.
void TestReplayCoverage::wholeRun() {
	const ReplayCoverage whole = coverage(ReplayCoverageKind::WholeRun, 0, 7999);
	const ReplayBarText idle = replayBarText(whole);
	// The row of a whole run is worded as such.
	QCOMPARE(idle.selected, std::string("Replay"));
	QVERIFY(idle.coverage.empty());
	QCOMPARE(idle.startTip, std::string("Go to the start of the run"));
	QVERIFY(idle.usable);
	const ReplayBarText shown = replayBarText(whole, 1234);
	// The row of a whole run says the selected time.
	QCOMPARE(shown.selected, std::string("08:20:34"));
	QVERIFY(shown.coverage.empty());
	QCOMPARE(shown.startTip, idle.startTip);
	QVERIFY(shown.usable);
	// The first second is taken for a selected time.
	QCOMPARE(replayBarText(whole, 0).selected, std::string("08:00:00"));
}

// A run whose first part was dropped: where the replay starts as a clock time, why, and a tooltip for Start. 7220 s
// after 08:00:00 is 10:00:20.
void TestReplayCoverage::droppedFirstPart() {
	const ReplayCoverage later = coverage(ReplayCoverageKind::FromLater, 7220, 7999);
	const ReplayBarText idle = replayBarText(later);
	// The row of a run with a dropped first part is worded as such.
	QCOMPARE(idle.selected, std::string("Replay"));
	QCOMPARE(idle.coverage, std::string("Replay starts at 10:00:20. Earlier times did not fit in memory."));
	QVERIFY(idle.usable);
	const ReplayBarText shown = replayBarText(later, 7500);
	// The row of a run with a dropped first part says the selected time.
	QCOMPARE(shown.selected, std::string("10:05:00"));
	QCOMPARE(shown.coverage, idle.coverage);
	// Start says that it goes to the start of the replay.
	QCOMPARE(idle.startTip, std::string("Go to the start of the replay (10:00:20)"));
	QCOMPARE(shown.startTip, idle.startTip);
}

// The second text is the same for every selected time, so the height of the row does not change while the user seeks.
void TestReplayCoverage::coverageIgnoresSelectedTime_data() {
	QTest::addColumn<int>("time");
	for (const int time : {7220, 7225, 7999})
		QTest::addRow("%d", time) << time;
}

void TestReplayCoverage::coverageIgnoresSelectedTime() {
	QFETCH(int, time);
	const ReplayCoverage later = coverage(ReplayCoverageKind::FromLater, 7220, 7999);
	// The coverage does not depend on the selected time.
	QCOMPARE(replayBarText(later, time).coverage, replayBarText(later).coverage);
}

// Unavailable: the reason, and nothing to use.
void TestReplayCoverage::unavailable() {
	const ReplayBarText none = replayBarText(coverage(ReplayCoverageKind::Unavailable, 0, 0));
	// A run that cannot be replayed is worded as such.
	QCOMPARE(none.selected, std::string("No replay"));
	QCOMPARE(none.coverage, std::string("This run is too large to replay."));
	QCOMPARE(none.startTip, std::string("Go to the start of the run"));
	QVERIFY(!none.usable);
	const ReplayBarText asked = replayBarText(coverage(ReplayCoverageKind::Unavailable, 0, 0), 500);
	// An unavailable replay says no selected time.
	QCOMPARE(asked.selected, none.selected);
	QCOMPARE(asked.coverage, none.coverage);
}

// Boundaries: a run of one frame, the smallest dropped part, and a clock that wraps over midnight.
void TestReplayCoverage::boundaries() {
	const ReplayBarText one = replayBarText(coverage(ReplayCoverageKind::WholeRun, 0, 0));
	// A whole run of one frame is worded as such.
	QVERIFY(one.coverage.empty());
	QVERIFY(one.usable);
	// A whole run of one frame says the selected time.
	QCOMPARE(replayBarText(coverage(ReplayCoverageKind::WholeRun, 0, 0), 0).selected, std::string("08:00:00"));
	const ReplayBarText last = replayBarText(coverage(ReplayCoverageKind::FromLater, 7999, 7999));
	// A replay of only the last frame is worded as such.
	QCOMPARE(last.coverage, std::string("Replay starts at 10:13:19. Earlier times did not fit in memory."));
	QCOMPARE(last.startTip, std::string("Go to the start of the replay (10:13:19)"));
	QVERIFY(last.usable);
	const ReplayBarText one5 = replayBarText(coverage(ReplayCoverageKind::FromLater, 5, 7999));
	QVERIFY2(one5.coverage.find("Replay starts at 08:00:05.") != std::string::npos, "a dropped part of one frame was not worded as such");
	ReplayCoverage wrap = coverage(ReplayCoverageKind::FromLater, 1200, 4000);
	wrap.clockOffsetSeconds = 23 * 3600 + 50 * 60;
	// The clock wraps over midnight.
	QVERIFY(replayBarText(wrap).coverage.find("Replay starts at 00:10:00.") != std::string::npos);
	QVERIFY(replayBarText(wrap).startTip.find("(00:10:00)") != std::string::npos);
	QCOMPARE(replayBarText(wrap, 1200).selected, std::string("00:10:00"));
	ReplayCoverage start = coverage(ReplayCoverageKind::FromLater, 600, 4000);
	start.clockOffsetSeconds = 0;
	QVERIFY2(replayBarText(start).coverage.find("Replay starts at 00:10:00.") != std::string::npos, "a run that starts at midnight was not worded as such");
}

// The sentences use plain words for the memory.
void TestReplayCoverage::plainWords_data() {
	QTest::addColumn<ReplayCoverageKind>("kind");
	QTest::newRow("whole run") << ReplayCoverageKind::WholeRun;
	QTest::newRow("from later") << ReplayCoverageKind::FromLater;
	QTest::newRow("unavailable") << ReplayCoverageKind::Unavailable;
}

void TestReplayCoverage::plainWords() {
	QFETCH(ReplayCoverageKind, kind);
	const ReplayBarText text = replayBarText(coverage(kind, 100, 900), 300);
	// No sentence uses a word of the implementation.
	QVERIFY(!mentionsInternals(text.selected));
	QVERIFY(!mentionsInternals(text.coverage));
	QVERIFY(!mentionsInternals(text.startTip));
}

// The coverage of a real history: whole, with its first part dropped, too large and empty.
void TestReplayCoverage::coverageOfHistory() {
	GuiReplayHistory whole;
	for (int time = 0; time <= 40; time += 5)
		whole.record(frameAt(time, 41));
	const ReplayCoverage kept = replayCoverageOf(whole, 3600);
	// The coverage of a whole history is read from it.
	QCOMPARE(kept.kind, ReplayCoverageKind::WholeRun);
	QCOMPARE(kept.firstTime, 0);
	QCOMPARE(kept.lastTime, 40);
	QCOMPARE(kept.clockOffsetSeconds, 3600);
	QVERIFY2(replayBarText(kept).coverage.empty(), "the row of a whole history was not worded as such");

	GuiReplayHistory reference;
	for (int time = 0; time <= 10; time += 5)
		reference.record(frameAt(time, 41));
	GuiReplayHistory small(reference.payloadBytes());
	for (int time = 0; time <= 40; time += 5)
		small.record(frameAt(time, 41));
	// The small history drops its first part.
	QVERIFY(small.truncated());
	QVERIFY(small.firstTime() > 0);
	const ReplayCoverage dropped = replayCoverageOf(small, 3600);
	// The coverage of a history with a dropped first part is read from it.
	QCOMPARE(dropped.kind, ReplayCoverageKind::FromLater);
	QCOMPARE(dropped.firstTime, small.firstTime());
	QCOMPARE(dropped.lastTime, 40);
	const ReplayBarText row = replayBarText(dropped, 40);
	const std::string start = formatSimTime(small.firstTime(), 3600);
	// The row of a history with a dropped first part says where it starts.
	QCOMPARE(row.selected, std::string("01:00:40"));
	QCOMPARE(row.coverage, "Replay starts at " + start + ". Earlier times did not fit in memory.");
	QCOMPARE(row.startTip, "Go to the start of the replay (" + start + ")");

	GuiReplayHistory tiny(1u << 20);
	auto large = std::make_shared<GuiSimulationSnapshot>();
	large->passengers.resize(1);
	large->passengers.front().id = std::string(2u << 20, 'x');
	tiny.record(large);
	const ReplayCoverage refused = replayCoverageOf(tiny, 3600);
	// The coverage of a history that refused a frame is read from it.
	QVERIFY(tiny.oversize());
	QCOMPARE(refused.kind, ReplayCoverageKind::Unavailable);
	const ReplayBarText refusedText = replayBarText(refused);
	// The row of a history that refused a frame is worded as such.
	QCOMPARE(refusedText.selected, std::string("No replay"));
	QCOMPARE(refusedText.coverage, std::string("This run is too large to replay."));

	const GuiReplayHistory empty;
	// A history without frames is not taken for a replay.
	QCOMPARE(replayCoverageOf(empty, 0).kind, ReplayCoverageKind::Unavailable);
}

QTEST_APPLESS_MAIN(TestReplayCoverage)
#include "test_replaycoverage.moc"
