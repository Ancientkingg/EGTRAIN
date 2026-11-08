// Compares the replay of a completed run with the live snapshots of the same run.
//
// Each case runs a scene in this process with snapshots enabled. A direct connection takes every snapshot that the
// simulation publishes and keeps those of the seconds that the case samples. When the run has ended, every second that
// the history of the run keeps is compared, field by field and bit for bit, with the live snapshot of that second. The
// scenes are the line fixture of tests/fixtures/scenes, Scenes/Paimpol with passengers and Scenes/Milano_Brescia.

#include "app/DispatchController.h"
#include "app/GuiReplayHistory.h"
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
	// Keep every second, or only multiples of 65 and the last step, so that the large scenes do not hold every snapshot.
	bool everySecond = false;
};

bool sampled(const Case& c, const GuiSimulationSnapshot& snapshot) {
	return c.everySecond || snapshot.timestep % 65 == 0 || snapshot.timestep == snapshot.totalTimesteps - 1;
}

struct CaseRun {
	Kept kept;
	GuiReplayHistory history;
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
	c.everySecond = true;
	return c;
}

Case sceneCase(const std::string& name, bool passengers) {
	Case c;
	c.name = name;
	c.sceneDir = EGTRAIN_SCENES_DIR "/" + name;
	c.passengers = passengers;
	return c;
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
		const auto keepSnapshot = [&c, &run]() {
			if (auto snapshot = simulation.takeSimulationSnapshot())
				if (sampled(c, *snapshot))
					run.kept[snapshot->timestep] = std::move(snapshot);
		};
		QObject::connect(&simulation, &DispatchController::snapshotAvailable, &receiver, keepSnapshot, Qt::DirectConnection);
		simulation.runSimulation();
		QObject::disconnect(&simulation, &DispatchController::snapshotAvailable, &receiver, nullptr);
	}
	run.history = simulation.takeReplayCandidate();
	run.lastStep = static_cast<int>(simulation.run().settings.times) - 1;
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

	int compared = 0;
	for (const auto& [second, live] : run.kept) {
		if (second % GuiReplayHistory::cadenceSeconds != 0 && second != history.lastTime())
			continue;
		const std::shared_ptr<const GuiSimulationSnapshot> replay = history.atOrBefore(second);
		const QString at = what + " at second " + QString::number(second);
		QVERIFY2(replay, qPrintable(at + ": no replay frame"));
		QVERIFY2(replay->timestep == second, qPrintable(at + ": the replay frame is of second " + QString::number(replay->timestep)));
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
	first.history.clear();
	simulation.resetReplayCandidate();
	QVERIFY2(first.history.empty(), "the first history is not empty after clear()");

	CaseRun second;
	runCase(lineCase(), output.path(), second);
	QVERIFY(!QTest::currentTestFailed());
	compareWithLive("line fixture after Paimpol", second, kLineTrains);
	QVERIFY2(first.history.empty(), "the second run filled the first history");
}

QTEST_GUILESS_MAIN(TestReplayLive)
#include "test_replaylive.moc"
