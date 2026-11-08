// Compares the replay of a completed run with the live snapshots of the same run.
//
// Each case runs a scene in this process with snapshots enabled. A direct connection takes every snapshot that the
// simulation publishes and keeps those of the sampled seconds: every 13th second, both sides of every keyframe of the
// history (seconds 1, 299, 301 and so on) and the last step. When the run has ended, the case captures the completed run,
// gives the history its train source, frees the samples of the trains as the window does, and compares the replay of every
// kept second, field by field, with the live snapshot of that second. The scenes are the line fixture of
// tests/fixtures/scenes, Scenes/Paimpol with passengers and Scenes/Milano_Brescia.

#include "app/DispatchController.h"
#include "app/GuiReplayHistory.h"
#include "app/ReplayTrajectories.h"
#include "diagrams/CompletedRunData.h"
#include "scene/SceneModel.h"
#include "simulation/GuiSimulationSnapshot.h"
#include "simulation/InitialParameters.h"
#include "simulation/Simulation.h"
#include "simulation/SimulationRun.h"
#include "support/SnapshotCompare.h"
#ifdef signals
#undef signals
#endif

#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

// The fixture is one track of this length (tests/characterization).
constexpr double kFixtureLengthKm = 16.0;
// The line fixture has two trains in the run that the case selects.
constexpr std::size_t kLineTrains = 2;
const std::string kLineDir = EGTRAIN_TEST_SOURCE_DIR "/fixtures/scenes/line";

class CoutSilencer {
public:
	CoutSilencer() : saved_(std::cout.rdbuf(sink_.rdbuf())) {}
	~CoutSilencer() { std::cout.rdbuf(saved_); }

private:
	std::ostringstream sink_;
	std::streambuf* saved_;
};

using Kept = std::map<int, std::shared_ptr<const GuiSimulationSnapshot>>;

// What one case runs, and which live seconds it keeps for the comparison.
struct Case {
	std::string name;
	std::string sceneDir;
	std::string scenario;
	SceneRunSelection selection;
	bool signalArea = false;
	bool passengers = false;
};

// The seconds that a case keeps: spread over the run, and on both sides of every keyframe (every 300 seconds, from 0).
bool sampled(const GuiSimulationSnapshot& snapshot) {
	const int t = snapshot.timestep;
	return t % 13 == 0 || t % 300 == 1 || t % 300 == 299 || t == snapshot.totalTimesteps - 1;
}

struct CaseRun {
	Kept kept;
	GuiReplayHistory history;
	// The completed run that the history shares its samples with, as the window keeps it.
	std::shared_ptr<const CompletedRunData> completed;
	int lastStep = 0;
};

Case lineCase() {
	Case c;
	c.name = "line fixture";
	c.sceneDir = kLineDir;
	c.scenario = "signal-failure-forward";
	c.selection.insert({"F1", 1});
	c.selection.insert({"F2", 1});
	c.signalArea = true;
	return c;
}

Case sceneCase(const std::string& name, bool passengers) {
	Case c;
	c.name = name;
	c.sceneDir = EGTRAIN_SCENES_DIR "/" + name;
	c.passengers = passengers;
	return c;
}

template <class T> void releaseSamples(std::vector<T>& samples) {
	std::vector<T>().swap(samples);
}

// Runs the case in this process as the window does: snapshots on, a clear replay candidate, every published snapshot
// taken (the mailbox signals again only after a take), and the history taken after the run.
void runCase(const Case& c, const QString& outputDir, CaseRun& run) {
	SceneLoadResult loaded = loadScene(c.sceneDir);
	QVERIFY2(!hasErrors(loaded.diagnostics), qPrintable(QString::fromStdString("cannot load " + c.name)));
	if (c.signalArea) {
		SceneSignallingArea area;
		area.id = "area.all";
		area.startKm = 0.0;
		area.endKm = kFixtureLengthKm;
		area.level = 0;
		loaded.scene.signallingAreas = {area};
	}

	initial_variables.GUI = 0;
	initial_variables.PAX_GUI = c.passengers;
	initial_variables.exportDetailedTrajectories = false;
	initial_variables.OutputMainFolder = outputDir.toStdString();
	initial_variables.InputMainFolder.clear();

	{
		CoutSilencer silencer;
		const bool prepared = !hasErrors(simulation.prepareScene(loaded.scene, c.scenario, c.selection));
		QVERIFY2(prepared, qPrintable(QString::fromStdString("cannot prepare " + c.name)));
		simulation.setSnapshotsEnabled(true);
		simulation.resetReplayCandidate();

		QObject receiver;
		const auto keepSnapshot = [&run]() {
			if (auto snapshot = simulation.takeSimulationSnapshot())
				if (sampled(*snapshot))
					run.kept[snapshot->timestep] = std::move(snapshot);
		};
		QObject::connect(&simulation, &DispatchController::snapshotAvailable, &receiver, keepSnapshot, Qt::DirectConnection);
		simulation.runSimulation();
		QObject::disconnect(&simulation, &DispatchController::snapshotAvailable, &receiver, nullptr);
	}
	run.history = simulation.takeReplayCandidate();
	run.lastStep = static_cast<int>(simulation.run().settings.times) - 1;

	// What RunController::finishRun does: the completed run is captured, the history takes over what it needs of the
	// samples, and then the samples are freed. The comparison below can only pass when the history reads nothing of the run.
	run.completed = std::make_shared<const CompletedRunData>(captureCompletedRun(simulation.run(), nullptr, 0, RunProvenance()));
	run.history.setTrains(makeGuiReplayTrains(simulation.run(), replayTrajectoriesOf(run.completed)));
	for (Train& train : simulation.mutableRun().trains) {
		releaseSamples(train.instant_train_speed);
		releaseSamples(train.instant_spatial_position);
		releaseSamples(train.instant_block_section_occupied);
		releaseSamples(train.instant_train_power_consumption);
		releaseSamples(train.instant_train_tractive_effort);
		releaseSamples(train.instant_train_energy_consumption);
		releaseSamples(train.BX);
	}
}

// The history covers the run, and every kept second that it keeps equals the live snapshot of that second. A scene with
// expectedTrains above 0 must show exactly that many trains.
void compareWithLive(const std::string& name, const CaseRun& run, std::size_t expectedTrains = 0) {
	const GuiReplayHistory& history = run.history;
	const QString what = QString::fromStdString(name);
	QVERIFY2(!history.empty(), qPrintable(what + ": the history is empty"));
	QVERIFY2(!history.truncated(), qPrintable(what + ": the history is truncated"));
	QVERIFY2(!history.oversize(), qPrintable(what + ": the history is oversize"));
	QVERIFY2(history.firstTime() == 0, qPrintable(what + ": the history does not start at 0"));
	QVERIFY2(history.lastTime() == run.lastStep,
		qPrintable(what + ": the history ends at " + QString::number(history.lastTime()) + ", the run at " + QString::number(run.lastStep)));
	QVERIFY2(run.kept.count(run.lastStep) == 1, qPrintable(what + ": the last step was not kept"));
	QVERIFY2(history.trains(), qPrintable(what + ": the history has no train source"));
	QVERIFY2(history.sharedBytes() > 0, qPrintable(what + ": the history shares no samples with the completed run"));
	qInfo().noquote() << what << "owned payload" << history.payloadBytes() << "bytes, shared samples" << history.sharedBytes()
					  << "bytes, intervals" << history.intervalCount();

	int compared = 0;
	for (const auto& [second, live] : run.kept) {
		const std::shared_ptr<const GuiSimulationSnapshot> replay = history.at(second);
		const QString at = what + " at second " + QString::number(second);
		QVERIFY2(replay, qPrintable(at + ": no replay snapshot"));
		QVERIFY2(replay->timestep == second, qPrintable(at + ": the replay is of second " + QString::number(replay->timestep)));
		if (expectedTrains > 0)
			QVERIFY2(replay->trains.size() == expectedTrains && live->trains.size() == expectedTrains,
				qPrintable(at + ": wrong train count " + QString::number(replay->trains.size())));
		const std::string difference = firstSnapshotDifference(*replay, *live);
		QVERIFY2(difference.empty(), qPrintable(at + ": " + QString::fromStdString(difference)));
		++compared;
	}
	QVERIFY2(compared > 0, qPrintable(what + ": no second was compared"));
}

bool anyKept(const Kept& kept, const std::function<bool(const GuiSimulationSnapshot&)>& test) {
	return std::any_of(kept.begin(), kept.end(), [&test](const auto& entry) { return test(*entry.second); });
}

// Some consecutive pair of kept seconds differs in the value that `differs` reads.
bool changesBetweenKept(const Kept& kept, const std::function<bool(const GuiSimulationSnapshot&, const GuiSimulationSnapshot&)>& differs) {
	const GuiSimulationSnapshot* previous = nullptr;
	for (const auto& entry : kept) {
		if (previous && differs(*previous, *entry.second))
			return true;
		previous = entry.second.get();
	}
	return false;
}

}

class TestReplayLive : public QObject {
	Q_OBJECT

private slots:
	void lineFixture();
	void paimpolWithPassengers();
	void milanoBrescia();
	void secondRunReplacesFirst();
};

void TestReplayLive::lineFixture() {
	QTemporaryDir output;
	QVERIFY(output.isValid());
	CaseRun run;
	runCase(lineCase(), output.path(), run);
	QVERIFY(!QTest::currentTestFailed());
	QVERIFY2(anyKept(run.kept,
				 [](const GuiSimulationSnapshot& s) {
					 return std::any_of(s.signalStates.begin(), s.signalStates.end(), [](const GuiSignalState& g) { return g.failed; })
						 && std::any_of(s.sectionStates.begin(), s.sectionStates.end(), [](const GuiSectionState& g) { return g.blocked; });
				 }),
		"no second shows a failed signal and a blocked section");
	compareWithLive("line fixture", run, kLineTrains);
}

void TestReplayLive::paimpolWithPassengers() {
	QTemporaryDir output;
	QVERIFY(output.isValid());
	CaseRun run;
	runCase(sceneCase("Paimpol", true), output.path(), run);
	QVERIFY(!QTest::currentTestFailed());
	QVERIFY2(changesBetweenKept(run.kept,
				 [](const GuiSimulationSnapshot& a, const GuiSimulationSnapshot& b) {
					 for (std::size_t i = 0; i < std::min(a.passengers.size(), b.passengers.size()); ++i)
						 if (a.passengers[i].status != b.passengers[i].status)
							 return true;
					 return false;
				 }),
		"no passenger changes status between two kept seconds");
	QVERIFY2(changesBetweenKept(run.kept,
				 [](const GuiSimulationSnapshot& a, const GuiSimulationSnapshot& b) {
					 for (std::size_t i = 0; i < std::min(a.trains.size(), b.trains.size()); ++i)
						 if (a.trains[i].currentOnboardPassengers != b.trains[i].currentOnboardPassengers)
							 return true;
					 return false;
				 }),
		"no train changes its onboard passengers between two kept seconds");
	compareWithLive("Paimpol", run);
}

void TestReplayLive::milanoBrescia() {
	QTemporaryDir output;
	QVERIFY(output.isValid());
	CaseRun run;
	runCase(sceneCase("Milano_Brescia", false), output.path(), run);
	QVERIFY(!QTest::currentTestFailed());
	compareWithLive("Milano_Brescia", run);
}

// What the window does when the next run starts (MainWindow::clearReplay): the history is cleared and the candidate reset.
void TestReplayLive::secondRunReplacesFirst() {
	QTemporaryDir output;
	QVERIFY(output.isValid());
	CaseRun first;
	runCase(sceneCase("Paimpol", false), output.path(), first);
	QVERIFY(!QTest::currentTestFailed());
	QVERIFY(!first.history.empty());
	const std::weak_ptr<const GuiReplayTrains> firstSource = first.history.trains();
	const std::weak_ptr<const CompletedRunData> firstCompleted = first.completed;
	QVERIFY2(!firstSource.expired(), "the first history has no train source");
	first.history.clear();
	simulation.resetReplayCandidate();
	QVERIFY2(first.history.empty(), "the first history is not empty after clear()");
	QVERIFY2(!first.history.trains(), "the first history keeps its train source after clear()");
	QVERIFY2(firstSource.expired(), "the train source of the first run is alive after clear()");
	QVERIFY2(!firstCompleted.expired(), "the completed run of the first run expired while the test holds it");
	first.completed.reset();
	QVERIFY2(firstCompleted.expired(), "the completed run of the first run is alive after the history and the test dropped it");

	CaseRun second;
	runCase(lineCase(), output.path(), second);
	QVERIFY(!QTest::currentTestFailed());
	compareWithLive("line fixture after Paimpol", second, kLineTrains);
	QVERIFY2(first.history.empty(), "the second run filled the first history");
}

QTEST_GUILESS_MAIN(TestReplayLive)
#include "test_replaylive.moc"
