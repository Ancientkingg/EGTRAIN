#include "app/FollowAvailability.h"

#include <QTest>

#include <string>

namespace {
GuiTrainState trainEnteringAt(int departureTime) {
	GuiTrainState train;
	train.index = 3;
	train.departureTime = departureTime;
	train.routeAxisPosition = 0.0;
	train.wagonHeadPositions = {10.0, 5.0};
	train.wagonTailPositions = {8.0, 3.0};
	return train;
}

FollowAvailabilityInput runOf(const GuiTrainState& train, int timestep) {
	FollowAvailabilityInput in;
	in.sceneHasServices = true;
	in.hasRun = true;
	in.train = &train;
	in.timestep = timestep;
	in.clockOffsetSeconds = 8 * 3600;
	in.drawable = true;
	in.name = "Rail-1";
	return in;
}

FollowAvailabilityInput withFollow(FollowAvailabilityInput in, bool on) {
	in.followOn = on;
	return in;
}

FollowAvailabilityInput inReplay(FollowAvailabilityInput in) {
	in.replay = true;
	return in;
}

GuiTrainState finishedTrain(const GuiTrainState& train) {
	GuiTrainState finished = train;
	finished.outOfSimulation = true;
	return finished;
}
}

class TestFollowAvailability : public QObject {
	Q_OBJECT

private slots:
	void noServices();
	void noServicesWithoutRun();
	void noRun();
	void trainMissingFromSnapshot();
	void notEnteredBeforeEntryTime();
	void statusClauseNamesState();
	void enteringAfterEndOfRun();
	void runningAtEntryTime();
	void noPositionIsNotEntered();
	void finished();
	void glidesOnlyInLiveRun();
	void layerAndGeometryOnlyMatterInNetwork();
	void wordingFollowsFollow_data();
	void wordingFollowsFollow();
	void phaseDependsOnDisplayedTimeOnly_data();
	void phaseDependsOnDisplayedTimeOnly();
	void clockWraps();

private:
	// A train that enters at 1550 s and runs until it leaves the network, and the same train after it has left.
	const GuiTrainState m_train = trainEnteringAt(1550);
	const GuiTrainState m_finished = finishedTrain(m_train);
};

// A case without services keeps the existing text, whatever else is known.
void TestFollowAvailability::noServices() {
	FollowAvailabilityInput in;
	in.hasRun = true;
	const FollowAvailability a = followAvailability(in);
	// A case without services is reported as having no trains.
	QCOMPARE(a.phase, FollowPhase::NoServices);
	QCOMPARE(a.entryText, "No trains to follow");
	QCOMPARE(a.statusText, "This case has no trains to follow.");
	QVERIFY(!a.controlEnabled);
	QVERIFY(!a.canArm);
	QVERIFY(!a.switchOff);
	QVERIFY(!a.canAct);
}

// A case without services that has not run says the same, not that it has to run first.
void TestFollowAvailability::noServicesWithoutRun() {
	const FollowAvailability a = followAvailability(FollowAvailabilityInput{});
	// A case without services that has not run is reported as having no trains.
	QCOMPARE(a.phase, FollowPhase::NoServices);
	QCOMPARE(a.entryText, "No trains to follow");
	QVERIFY(!a.controlEnabled);
	QVERIFY(!a.canArm);
	QVERIFY(!a.canAct);
}

// Services but no run yet. The control is not offered, and the sentence says why.
void TestFollowAvailability::noRun() {
	FollowAvailabilityInput in;
	in.sceneHasServices = true;
	const FollowAvailability a = followAvailability(in);
	// A case that has not run is reported as such.
	QCOMPARE(a.phase, FollowPhase::NoRun);
	QCOMPARE(a.entryText, "Run the case first");
	QCOMPARE(a.statusText, "Run the case first, then you can follow a train.");
	QVERIFY(!a.controlEnabled);
	QVERIFY(!a.canArm);
	QVERIFY(!a.switchOff);
	QVERIFY(!a.canAct);
	in.followOn = true;
	const FollowAvailability on = followAvailability(in);
	// Follow that was on in a case that has not run is switched off.
	QCOMPARE(on.statusText, a.statusText);
	QVERIFY(on.switchOff);
	QVERIFY(!on.canAct);
}

// A run whose displayed snapshot does not hold the train.
void TestFollowAvailability::trainMissingFromSnapshot() {
	const GuiTrainState none;
	FollowAvailabilityInput in = runOf(none, 0);
	in.train = nullptr;
	const FollowAvailability off = followAvailability(in);
	// A train missing from the snapshot is reported as not entered.
	QCOMPARE(off.phase, FollowPhase::NotEntered);
	QCOMPARE(off.entryText, "Rail-1 (not entered yet)");
	QCOMPARE(off.statusText, "Rail-1 has not entered the network yet.");
	QVERIFY(off.controlEnabled);
	QVERIFY(off.canArm);
	QVERIFY(!off.switchOff);
	QVERIFY(!off.canAct);
	const FollowAvailability on = followAvailability(withFollow(in, true));
	// Follow on for a train missing from the snapshot is worded for the armed control.
	QCOMPARE(on.entryText, off.entryText);
	QCOMPARE(on.statusText, "Rail-1 has not entered the network yet. Follow starts when it enters.");
	QVERIFY(on.canArm);
	QVERIFY(!on.switchOff);
	QVERIFY(!on.canAct);
}

// Not entered: one second before the entry time, with the scheduled time on the clock.
// The list entry says that the time is the schedule, and does not change with Follow.
void TestFollowAvailability::notEnteredBeforeEntryTime() {
	const FollowAvailability off = followAvailability(runOf(m_train, 1549));
	// A train before its entry time is reported as not entered with Follow off.
	QCOMPARE(off.phase, FollowPhase::NotEntered);
	QCOMPARE(off.entryText, "Rail-1 (scheduled 08:25:50)");
	QCOMPARE(off.statusText, "Rail-1 is scheduled to enter at 08:25:50.");
	QVERIFY(off.controlEnabled);
	QVERIFY(off.canArm);
	QVERIFY(!off.switchOff);
	QVERIFY(!off.canAct);
	const FollowAvailability on = followAvailability(withFollow(runOf(m_train, 1549), true));
	// A train before its entry time is reported as not entered with Follow on.
	QCOMPARE(on.phase, FollowPhase::NotEntered);
	QCOMPARE(on.entryText, off.entryText);
	QCOMPARE(on.statusText, "Rail-1 is scheduled to enter at 08:25:50. Follow starts when it enters.");
	QVERIFY(on.canArm);
	QVERIFY(!on.switchOff);
	QVERIFY(!on.canAct);
}

// The status bar shows one clause: the state of the train, or that the view follows it.
void TestFollowAvailability::statusClauseNamesState() {
	FollowAvailabilityInput noRun;
	noRun.sceneHasServices = true;
	FollowAvailabilityInput ended = inReplay(runOf(m_train, 1200));
	ended.replayEndTime = 1549;
	// A status clause names the state of the train.
	QCOMPARE(followAvailability(FollowAvailabilityInput{}).statusClause, "No trains to follow");
	QCOMPARE(followAvailability(noRun).statusClause, "Run the case first");
	QCOMPARE(followAvailability(runOf(m_train, 1549)).statusClause, "Rail-1 enters at 08:25:50");
	QCOMPARE(followAvailability(withFollow(runOf(m_train, 1549), true)).statusClause, "Rail-1 enters at 08:25:50");
	QCOMPARE(followAvailability(ended).statusClause, "Rail-1 enters after this run");
	QCOMPARE(followAvailability(runOf(m_train, 2000)).statusClause, "Rail-1 is running");
	QCOMPARE(followAvailability(withFollow(runOf(m_train, 2000), true)).statusClause, "Following Rail-1");
}

// A completed run that ends before the entry time: the train never enters it, so the sentence does not
// say that Follow starts, whether Follow is on or off. A train that enters at the last time of the run
// keeps the usual sentence.
void TestFollowAvailability::enteringAfterEndOfRun() {
	FollowAvailabilityInput ended = inReplay(runOf(m_train, 1200));
	ended.replayEndTime = 1549;
	const std::string afterRun = "Rail-1 is scheduled to enter at 08:25:50, after the end of this run, so the view cannot follow it.";
	const FollowAvailability off = followAvailability(ended);
	// A train scheduled after the end of a completed run is not promised that Follow starts.
	QCOMPARE(off.phase, FollowPhase::NotEntered);
	QCOMPARE(off.entryText, "Rail-1 (scheduled 08:25:50)");
	QCOMPARE(off.statusText, afterRun);
	QVERIFY(off.controlEnabled);
	QVERIFY(off.canArm);
	QVERIFY(!off.switchOff);
	QVERIFY(!off.canAct);
	const FollowAvailability on = followAvailability(withFollow(ended, true));
	// A train scheduled after the end of a completed run is not promised that Follow starts, with Follow on.
	QCOMPARE(on.statusText, afterRun);
	QVERIFY(on.canArm);
	QVERIFY(!on.switchOff);
	QVERIFY(!on.canAct);
	ended.replayEndTime = 1550;
	// A train that enters at the last time of a completed run keeps its sentence.
	QCOMPARE(followAvailability(ended).statusText, "Rail-1 is scheduled to enter at 08:25:50.");
}

// Running: the second the train enters. Only a Follow that is on moves the view.
void TestFollowAvailability::runningAtEntryTime() {
	const FollowAvailability off = followAvailability(runOf(m_train, 1550));
	// A train at its entry time is reported as running with Follow off.
	QCOMPARE(off.phase, FollowPhase::Running);
	QCOMPARE(off.entryText, "Rail-1 (running)");
	QCOMPARE(off.statusText, "Rail-1 is running.");
	QVERIFY(off.controlEnabled);
	QVERIFY(off.canArm);
	QVERIFY(!off.switchOff);
	QVERIFY(!off.canAct);
	const FollowAvailability on = followAvailability(withFollow(runOf(m_train, 1550), true));
	// A train at its entry time is reported as running with Follow on.
	QCOMPARE(on.phase, FollowPhase::Running);
	QCOMPARE(on.entryText, off.entryText);
	QCOMPARE(on.statusText, "Rail-1 is running. The view follows it.");
	QVERIFY(on.canArm);
	QVERIFY(!on.switchOff);
	QVERIFY(on.canAct);
}

// A train past its entry time without a position has not entered yet.
void TestFollowAvailability::noPositionIsNotEntered() {
	GuiTrainState noPosition = trainEnteringAt(100);
	noPosition.routeAxisPosition = -9999.0;
	// A train without a position is not reported as running.
	QCOMPARE(followAvailability(runOf(noPosition, 200)).phase, FollowPhase::NotEntered);
	GuiTrainState noWagons = trainEnteringAt(100);
	noWagons.wagonHeadPositions.clear();
	// A train without wagon positions is not reported as running.
	QCOMPARE(followAvailability(runOf(noWagons, 200)).phase, FollowPhase::NotEntered);
}

// Finished: the last second in the network and the first second out of it.
// The stale position of a finished train does not make it run.
void TestFollowAvailability::finished() {
	// The last second in the network is running.
	QCOMPARE(followAvailability(runOf(m_train, 2999)).phase, FollowPhase::Running);
	const FollowAvailability live = followAvailability(runOf(m_finished, 3000));
	// A finished train in a live run is refused.
	QCOMPARE(live.phase, FollowPhase::Finished);
	QCOMPARE(live.entryText, "Rail-1 (finished)");
	QCOMPARE(live.statusText, "Rail-1 has left the network. Follow cannot be switched on for it.");
	QVERIFY(live.controlEnabled);
	QVERIFY(!live.canArm);
	QVERIFY(!live.switchOff);
	QVERIFY(!live.canAct);
	const FollowAvailability liveOn = followAvailability(withFollow(runOf(m_finished, 3000), true));
	// Follow that was on for a train that left a live run is switched off.
	QCOMPARE(liveOn.phase, FollowPhase::Finished);
	QCOMPARE(liveOn.entryText, live.entryText);
	QCOMPARE(liveOn.statusText, "Rail-1 has left the network. Follow is switched off.");
	QVERIFY(!liveOn.canArm);
	QVERIFY(liveOn.switchOff);
	QVERIFY(!liveOn.canAct);
	const FollowAvailability back = followAvailability(inReplay(runOf(m_finished, 3000)));
	// A finished train in a replay is accepted.
	QCOMPARE(back.phase, FollowPhase::Finished);
	QCOMPARE(back.entryText, "Rail-1 (finished)");
	QCOMPARE(back.statusText, "Rail-1 has left the network at this time.");
	QVERIFY(back.controlEnabled);
	QVERIFY(back.canArm);
	QVERIFY(!back.switchOff);
	QVERIFY(!back.canAct);
	const FollowAvailability stay = followAvailability(withFollow(inReplay(runOf(m_finished, 3000)), true));
	// Follow stays on for a train that finished in a replay.
	QCOMPARE(stay.phase, FollowPhase::Finished);
	QCOMPARE(stay.entryText, back.entryText);
	QCOMPARE(stay.statusText, "Rail-1 has left the network at this time. "
							  "Follow stays on and continues when you go back in the replay.");
	QVERIFY(stay.canArm);
	QVERIFY(!stay.switchOff);
	QVERIFY(!stay.canAct);
}

// The view glides to a train in a live run and cuts to it in a replay, and only while it moves
// to the train at all.
void TestFollowAvailability::glidesOnlyInLiveRun() {
	const FollowAvailability live = followAvailability(withFollow(runOf(m_train, 1550), true));
	// The view glides to a train that runs in a live run.
	QVERIFY(live.canAct);
	QVERIFY(live.glide);
	const FollowAvailability replay = followAvailability(inReplay(withFollow(runOf(m_train, 1550), true)));
	// The view does not glide to a train in a replay.
	QVERIFY(replay.canAct);
	QVERIFY(!replay.glide);
	QVERIFY2(!followAvailability(runOf(m_train, 1550)).glide, "the view glided to a train with Follow off");
	QVERIFY2(!followAvailability(withFollow(runOf(m_train, 1549), true)).glide, "the view glided to a train that has not entered");
	FollowAvailabilityInput hidden = withFollow(runOf(m_train, 2000), true);
	hidden.trainsLayerVisible = false;
	QVERIFY2(!followAvailability(hidden).glide, "the view glided to a train hidden by the layer switch");
	QVERIFY2(!followAvailability(withFollow(runOf(m_finished, 3000), true)).glide, "the view glided to a train that has left");
}

// The Trains layer and a missing geometry only matter for a train in the network.
void TestFollowAvailability::layerAndGeometryOnlyMatterInNetwork() {
	FollowAvailabilityInput hidden = runOf(m_train, 2000);
	hidden.trainsLayerVisible = false;
	const FollowAvailability off = followAvailability(hidden);
	// A train hidden by the layer switch is reported as hidden with Follow off.
	QCOMPARE(off.phase, FollowPhase::HiddenByLayer);
	QCOMPARE(off.entryText, "Rail-1 (hidden)");
	QCOMPARE(off.statusText, "Rail-1 is running, but the Trains layer is switched off. Turn on the Trains layer to follow it.");
	QVERIFY(off.canArm);
	QVERIFY(!off.switchOff);
	QVERIFY(!off.canAct);
	hidden.followOn = true;
	const FollowAvailability on = followAvailability(hidden);
	// A train hidden by the layer switch is reported as hidden with Follow on.
	QCOMPARE(on.phase, FollowPhase::HiddenByLayer);
	QCOMPARE(on.entryText, off.entryText);
	QCOMPARE(on.statusText, "Rail-1 is running, but the Trains layer is switched off, so the view does not follow it.");
	QVERIFY(on.canArm);
	QVERIFY(!on.switchOff);
	QVERIFY(!on.canAct);
	FollowAvailabilityInput blank = withFollow(runOf(m_train, 2000), true);
	blank.drawable = false;
	const FollowAvailability b = followAvailability(blank);
	// A train without a drawn position is reported as such.
	QCOMPARE(b.phase, FollowPhase::NotDrawable);
	QCOMPARE(b.entryText, "Rail-1 (no position)");
	QCOMPARE(b.statusText, "Rail-1 is running, but it has no position on the map to follow.");
	QVERIFY(b.canArm);
	QVERIFY(!b.switchOff);
	QVERIFY(!b.canAct);
	blank.followOn = false;
	// The sentence for a train without a drawn position does not depend on Follow.
	QCOMPARE(followAvailability(blank).statusText, b.statusText);
	hidden.train = &m_finished;
	// The layer switch does not change the phase of a finished train.
	QCOMPARE(followAvailability(hidden).phase, FollowPhase::Finished);
	hidden.train = &m_train;
	hidden.timestep = 1000;
	// The layer switch does not change the phase of a train that has not entered.
	QCOMPARE(followAvailability(hidden).phase, FollowPhase::NotEntered);
	FollowAvailabilityInput both = runOf(m_train, 2000);
	both.trainsLayerVisible = false;
	both.drawable = false;
	// A train hidden by the layer is not reported as not drawable.
	QCOMPARE(followAvailability(both).phase, FollowPhase::HiddenByLayer);
}

void TestFollowAvailability::wordingFollowsFollow_data() {
	QTest::addColumn<bool>("left");
	QTest::addColumn<int>("time");
	QTest::addColumn<bool>("layerVisible");
	QTest::newRow("not entered") << false << 1000 << true;
	QTest::newRow("running") << false << 2000 << true;
	QTest::newRow("hidden by layer") << false << 2000 << false;
	QTest::newRow("finished") << true << 3000 << true;
}

// The list entry of a phase does not depend on Follow or on the replay, and no
// sentence that mentions Follow reads the same for both states of the control.
void TestFollowAvailability::wordingFollowsFollow() {
	QFETCH(bool, left);
	QFETCH(int, time);
	QFETCH(bool, layerVisible);
	FollowAvailabilityInput in = runOf(left ? m_finished : m_train, time);
	in.trainsLayerVisible = layerVisible;
	const FollowAvailability off = followAvailability(in);
	in.followOn = true;
	in.replay = true;
	const FollowAvailability on = followAvailability(in);
	// The list entry does not change with the state of Follow.
	QCOMPARE(off.entryText, on.entryText);
	// A status clause has one clause.
	QCOMPARE(off.statusClause.find_first_of(".,"), std::string::npos);
	QCOMPARE(on.statusClause.find_first_of(".,"), std::string::npos);
	QVERIFY2(off.statusText != on.statusText, "a sentence about Follow did not change with the state of Follow");
	QVERIFY2(on.statusText.find("switched on now") == std::string::npos, "a sentence for Follow on offers to switch it on");
	// A sentence for Follow off does not say that it is on.
	QVERIFY(off.statusText.find("Follow stays on") == std::string::npos);
	QVERIFY(off.statusText.find("Follow starts") == std::string::npos);
	QVERIFY(off.statusText.find("The view follows") == std::string::npos);
}

void TestFollowAvailability::phaseDependsOnDisplayedTimeOnly_data() {
	QTest::addColumn<int>("time");
	QTest::addColumn<FollowPhase>("expected");
	QTest::newRow("0 s") << 0 << FollowPhase::NotEntered;
	QTest::newRow("1549 s") << 1549 << FollowPhase::NotEntered;
	QTest::newRow("1550 s") << 1550 << FollowPhase::Running;
	QTest::newRow("2999 s") << 2999 << FollowPhase::Running;
	QTest::newRow("back to 1549 s") << 1549 << FollowPhase::NotEntered;
	QTest::newRow("back to 0 s") << 0 << FollowPhase::NotEntered;
}

// The phase depends on the displayed frame only: stepping back across both boundaries gives the same answers.
void TestFollowAvailability::phaseDependsOnDisplayedTimeOnly() {
	QFETCH(int, time);
	QFETCH(FollowPhase, expected);
	// The phase does not depend on an earlier time.
	QCOMPARE(followPhase(runOf(m_train, time)), expected);
}

// The clock wraps and shows the same format as the rest of the window.
void TestFollowAvailability::clockWraps() {
	const GuiTrainState late = trainEnteringAt(3600);
	FollowAvailabilityInput in = runOf(late, 0);
	in.clockOffsetSeconds = 23 * 3600;
	// The entry time is formatted like other clock times.
	QCOMPARE(followAvailability(in).entryText, "Rail-1 (scheduled 00:00:00)");
}

QTEST_APPLESS_MAIN(TestFollowAvailability)
#include "test_followavailability.moc"
