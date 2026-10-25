#include "app/DispatchController.h"
#include "scene/SceneModel.h"
#include "simulation/InitialParameters.h"
#include "simulation/Infrastructure.h"
#include "simulation/Optimisation.h"
#include "simulation/Passengers.h"
#include "simulation/RollingStock.h"
#include "simulation/RunControl.h"
#include "simulation/SimulationRun.h"
#include "simulation/SimulationWorker.h"
#include "util/Log.h"
#ifdef signals
#undef signals
#endif

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTextStream>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kHorizonSeconds = 300;
constexpr int kStopAtSnapshot = 10;

// Written by the energy stage, before the first stop check after the loop.
const char* const kEnergyFile = "EnergyConsumptionPerTrain.txt";
// Written by the last stages of the end-of-run output.
const char* const kLateFiles[] = {"TrainTrajectories/TimetablePoints.txt", "TrainTrajectories/BlockingTimes.txt"};

enum class StopAt { Never,
	BeforeStart,
	Started,
	TenthSnapshot,
	Postprocessing,
	Returned,
	// A slot of executionBegan or executionPostprocessing throws instead of stopping the run.
	ThrowAtBegan,
	ThrowAtPostprocessing };

struct RunObservation {
	bool prepared = false;
	std::atomic<int> snapshots{0};
	std::atomic<int> lastStep{-1};
	std::atomic<int> statusFileDuringLoop{0};
	std::atomic<int> postprocessing{0};
	std::atomic<int> returned{0};
	std::atomic<int> failed{0};
	std::atomic<bool> cancelled{false};
	bool completed = false;
	bool stopRequested = false;
	std::string error;
};

// How a run is driven. Without a mode it runs in a SimulationWorker and a stop is a request.
struct RunMode {
	// The run has no SimulationWorker and uses the thread of the caller, as with -g 0. Such a run has
	// no control before it starts, so it cannot be stopped before the start.
	bool headless;
	// A stop goes through the command queue of the control instead of a request function.
	bool stopByCommand;
};
const RunMode kWorkerRequest{false, false};
const RunMode kWorkerCommand{false, true};
const RunMode kHeadlessRequest{true, false};
const RunMode kHeadlessCommand{true, true};

// Runs the prepared scene the way RunController::startSimulation does and requests
// the stop at the given point. A folder in chosenDuringRun is set as the output
// folder of the next run once the worker thread has started.
void runScene(const SceneModel& scene, const QString& outputDir, StopAt stopAt, RunObservation& observed,
	const QString& chosenDuringRun = QString(), int horizonSeconds = kHorizonSeconds,
	const RunMode& mode = kWorkerRequest) {
	initial_variables.GUI = 0;
	initial_variables.durationOverride = true;
	initial_variables.times = horizonSeconds;
	initial_variables.OutputMainFolder = outputDir.toStdString();
	initial_variables.InputMainFolder.clear();

	observed.prepared = !hasErrors(simulation.prepareScene(scene)) && simulation.run().numTrains > 0;
	if (!observed.prepared)
		return;

	// A passenger row for the status file, with no journey or train interaction.
	Passenger probe;
	probe.ID = "status-probe";
	probe.IsIntheNetwork = true;
	simulation.mutableRun().passengers.push_back(probe);

	SimulationWorker* worker = nullptr;
	QThread thread;
	if (!mode.headless) {
		worker = new SimulationWorker();
		worker->moveToThread(&thread);
		QObject::connect(&thread, &QThread::started, worker, &SimulationWorker::run);
	}

	const auto stopRun = [&] {
		const auto control = worker ? worker->control() : simulation.runControl();
		if (mode.stopByCommand)
			control->submit({RunCommandKind::Stop});
		else if (worker)
			worker->requestStop();
		else
			control->requestStop();
	};
	QObject context;
	QObject::connect(&simulation, &DispatchController::snapshotAvailable, &context, [&] {
		// The mailbox only emits again once the previous snapshot has been taken.
		const auto snapshot = simulation.takeSimulationSnapshot();
		if (snapshot)
			observed.lastStep = snapshot->timestep;
		if (QFileInfo::exists(outputDir + "/PassengerStatus/PassengerStatus.txt"))
			++observed.statusFileDuringLoop;
		if (++observed.snapshots == kStopAtSnapshot && stopAt == StopAt::TenthSnapshot)
			stopRun(); }, Qt::DirectConnection);
	QObject::connect(&simulation, &DispatchController::executionBegan, &context, [&] {
		if (stopAt == StopAt::ThrowAtBegan)
			throw std::runtime_error("injected"); }, Qt::DirectConnection);
	QObject::connect(&simulation, &DispatchController::executionPostprocessing, &context, [&] {
		++observed.postprocessing;
		if (stopAt == StopAt::ThrowAtPostprocessing)
			throw std::runtime_error("injected");
		if (stopAt == StopAt::Postprocessing)
			stopRun(); }, Qt::DirectConnection);
	QObject::connect(&simulation, &DispatchController::executionFailed, &context, [&](qint64) { ++observed.failed; }, Qt::DirectConnection);
	QObject::connect(&simulation, &DispatchController::executionReturned, &context, [&](qint64, bool cancelled) {
		++observed.returned;
		observed.cancelled = cancelled;
		if (stopAt == StopAt::Returned)
			stopRun(); }, Qt::DirectConnection);
	if (mode.headless) {
		simulation.runSimulation();
		observed.completed = simulation.lastRunCompleted();
		observed.stopRequested = simulation.runControl()->isStopRequested();
		observed.error = simulation.lastRunError();
		return;
	}
	if (stopAt == StopAt::Started)
		QObject::connect(worker, &SimulationWorker::simulationStarted, &context, stopRun, Qt::DirectConnection);

	QEventLoop loop;
	QObject::connect(worker, &SimulationWorker::simulationFinished, &loop, &QEventLoop::quit);
	if (stopAt == StopAt::BeforeStart)
		stopRun();
	thread.start();
	if (!chosenDuringRun.isEmpty())
		simulation.setNextRunOutputFolder(chosenDuringRun.toStdString());
	loop.exec();
	thread.quit();
	thread.wait();
	observed.completed = simulation.lastRunCompleted();
	observed.stopRequested = worker->isStopRequested();
	observed.error = simulation.lastRunError();
	delete worker;
}

bool exists(const QTemporaryDir& dir, const std::string& relative) {
	return QFileInfo::exists(dir.filePath(QString::fromStdString(relative)));
}

QStringList statusLines(const QTemporaryDir& output) {
	QFile file(output.filePath("PassengerStatus/PassengerStatus.txt"));
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return {};
	QTextStream stream(&file);
	QStringList lines;
	while (!stream.atEnd())
		lines.push_back(stream.readLine());
	return lines;
}

// The status file of a run that reached the loop holds the step of the last snapshot. A failed check ends this function
// only, so the caller checks QTest::currentTestFailed() when more checks follow.
void checkFinalStatus(const RunObservation& observed, const QTemporaryDir& output) {
	// No status file during the loop.
	QCOMPARE(observed.statusFileDuringLoop.load(), 0);
	const auto lines = statusLines(output);
	// Status file has header and passenger row.
	QCOMPARE(lines.size(), qsizetype(2));
	// Status header.
	QVERIFY(!lines.isEmpty());
	QVERIFY(lines.front().startsWith("Time PaxID IsInNetwork"));
	const auto row = QString::number(observed.lastStep + initial_variables.startingSimulationTime)
		+ " status-probe 1 None None None None None None None None";
	// Status row holds the last step.
	QCOMPARE(lines.size(), qsizetype(2));
	QCOMPARE(lines[1], row);
}

// A run that stopped before its output: no post-processing and none of its files. A failed check ends this function
// only, so the caller checks QTest::currentTestFailed() when more checks follow.
void checkStoppedBeforeOutput(const RunObservation& observed, const QTemporaryDir& output) {
	QVERIFY2(observed.prepared, "scene prepared with trains");
	// Post-processing never began.
	QCOMPARE(observed.postprocessing.load(), 0);
	// executionReturned emitted once.
	QCOMPARE(observed.returned.load(), 1);
	QVERIFY2(observed.cancelled, "run reported as cancelled");
	QVERIFY2(!observed.completed, "run not completed");
	QVERIFY2(observed.stopRequested, "stop request kept");
	QVERIFY2(!exists(output, kEnergyFile), "no energy output");
	for (const char* file : kLateFiles)
		QVERIFY2(!exists(output, file), (std::string("no ") + file).c_str());
}

enum class Gate {
	Open,
	Stopped,
	Blocked
};

// Runs beginStep on another thread. A gate that is still closed after five seconds is released with
// a stop and reported as Blocked.
Gate gateOutcome(RunControl& control) {
	std::atomic<bool> done{false};
	std::atomic<bool> proceed{false};
	std::thread gate([&] {
		proceed = control.beginStep();
		done = true;
	});
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!done && std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	const bool blocked = !done;
	if (blocked)
		control.requestStop();
	gate.join();
	if (blocked)
		return Gate::Blocked;
	return proceed ? Gate::Open : Gate::Stopped;
}

std::chrono::steady_clock::duration timeOfBeginStep(RunControl& control, bool& proceed) {
	const auto begin = std::chrono::steady_clock::now();
	proceed = control.beginStep();
	return std::chrono::steady_clock::now() - begin;
}

} // namespace

// The runs share the controller of the process and the settings of the application, in the order of the slots. The scene
// is Scenes/Assignment_Gvc_Gdg_Ut.
class TestSimulationWorker : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void controlKeepsRequests();
	void workerPassesRequestsToControl();
	void commandWaitsForStepTop();
	void lastQueuedCommandDecides();
	void stopCommandActsLikeRequest();
	void queueHoldsLimitedCommands();
	void queuedResumeEndsPause();
	void queuedStopEndsPauseAndRun();
	void resumeDuringPauseIsApplied();
	void queuedDelayAppliesBeforeWait();
	void stopIsCheckedBeforeDelay();
	void controllerKeepsParent();
	void workerRunsGivenController();
	void stopBeforeStart();
	void stopAtStart();
	void stopInLoop();
	void controlRun();
	void runWithoutSnapshots();
	void stopInPostprocessing();
	void stopAfterCompletion();
	void queuedStopBeforeStart();
	void queuedStopInLoop();
	void stopRequestWithoutWorker();
	void queuedStopWithoutWorker();
	void reusedOutputFolder();
	void exceptionInSlotFailsRun_data();
	void exceptionInSlotFailsRun();
	void blockedTempFolderFailsRun();
	void blockedOutputFolderFailsPreparation();
	void folderChosenDuringRun();

private:
	SceneModel m_scene;
};

void TestSimulationWorker::initTestCase() {
	SceneLoadResult loaded = loadScene(EGTRAIN_SCENES_DIR "/Assignment_Gvc_Gdg_Ut");
	QVERIFY2(!hasErrors(loaded.diagnostics), "scene loads");
	m_scene = std::move(loaded.scene);
}

void TestSimulationWorker::controlKeepsRequests() {
	RunControl control;
	// New control: nothing requested.
	QVERIFY(!control.isStopRequested());
	QVERIFY(!control.wasCancellationRequested());
	QVERIFY(!control.isPauseRequested());
	QCOMPARE(control.delayMs(), 0);
	control.requestPause();
	QVERIFY2(control.isPauseRequested(), "pause request");
	control.requestResume();
	QVERIFY2(!control.isPauseRequested(), "resume request");
	control.setDelayMs(25);
	// Delay request.
	QCOMPARE(control.delayMs(), 25);
	control.requestPause();
	control.requestStop();
	// Stop request: stops, records the cancellation and lifts the pause.
	QVERIFY(control.isStopRequested());
	QVERIFY(control.wasCancellationRequested());
	QVERIFY(!control.isPauseRequested());
}

void TestSimulationWorker::workerPassesRequestsToControl() {
	SimulationWorker worker;
	const auto control = worker.control();
	worker.requestPause();
	// Worker: pause reaches the control.
	QVERIFY(worker.isPauseRequested());
	QVERIFY(control->isPauseRequested());
	worker.requestResume();
	// Worker: resume reaches the control.
	QVERIFY(!worker.isPauseRequested());
	QVERIFY(!control->isPauseRequested());
	worker.setDelayMs(7);
	// Worker: delay reaches the control.
	QCOMPARE(worker.delayMs(), 7);
	QCOMPARE(control->delayMs(), 7);
	// Worker: no stop yet.
	QVERIFY(!worker.isStopRequested());
	QVERIFY(!worker.wasCancellationRequested());
	worker.requestStop();
	// Worker: stop reaches the control.
	QVERIFY(worker.isStopRequested());
	QVERIFY(worker.wasCancellationRequested());
	QVERIFY(control->isStopRequested());
}

void TestSimulationWorker::commandWaitsForStepTop() {
	RunControl control;
	control.submit({RunCommandKind::Pause});
	QVERIFY2(!control.isPauseRequested(), "command: nothing happens before the step top");
	control.applyQueuedCommands();
	QVERIFY2(control.isPauseRequested(), "command: applied at the step top");
	control.requestResume();
	control.applyQueuedCommands();
	QVERIFY2(!control.isPauseRequested(), "command: applied once");
}

void TestSimulationWorker::lastQueuedCommandDecides() {
	RunControl control;
	control.submit({RunCommandKind::Pause});
	control.submit({RunCommandKind::Resume});
	control.applyQueuedCommands();
	QVERIFY2(!control.isPauseRequested(), "commands: pause then resume leaves the run running");
	control.submit({RunCommandKind::Resume});
	control.submit({RunCommandKind::Pause});
	control.applyQueuedCommands();
	QVERIFY2(control.isPauseRequested(), "commands: resume then pause leaves the run paused");
}

void TestSimulationWorker::stopCommandActsLikeRequest() {
	RunControl control;
	control.submit({RunCommandKind::Pause});
	control.submit({RunCommandKind::Stop});
	control.applyQueuedCommands();
	// Stop command: acts like a stop request.
	QVERIFY(control.isStopRequested());
	QVERIFY(control.wasCancellationRequested());
	QVERIFY(!control.isPauseRequested());
}

void TestSimulationWorker::queueHoldsLimitedCommands() {
	RunControl control;
	bool accepted = true;
	for (std::size_t i = 1; i <= RunControl::kCommandQueueSize; ++i)
		accepted &= control.submit({RunCommandKind::SetDelayMs, static_cast<int>(i)});
	QVERIFY2(accepted, "queue: holds kCommandQueueSize commands");
	QVERIFY2(!control.submit({RunCommandKind::SetDelayMs, 99}), "queue: refuses a command when full");
	control.applyQueuedCommands();
	// Queue: the last accepted command wins, the refused one is not applied.
	QCOMPARE(control.delayMs(), static_cast<int>(RunControl::kCommandQueueSize));
	QVERIFY2(control.submit({RunCommandKind::SetDelayMs, 5}), "queue: takes commands again once emptied");
	control.applyQueuedCommands();
	// Queue: the new command is applied.
	QCOMPARE(control.delayMs(), 5);
}

void TestSimulationWorker::queuedResumeEndsPause() {
	RunControl control;
	control.requestPause();
	control.submit({RunCommandKind::Resume});
	QVERIFY2(gateOutcome(control) == Gate::Open, "step top: a queued resume ends a pause");
}

void TestSimulationWorker::queuedStopEndsPauseAndRun() {
	RunControl control;
	control.requestPause();
	control.submit({RunCommandKind::Stop});
	// Step top: a queued stop ends a pause and the run.
	QVERIFY(gateOutcome(control) == Gate::Stopped);
	QVERIFY(control.wasCancellationRequested());
}

void TestSimulationWorker::resumeDuringPauseIsApplied() {
	RunControl control;
	control.requestPause();
	std::thread sender([&] {
		std::this_thread::sleep_for(std::chrono::milliseconds(120));
		control.submit({RunCommandKind::Resume});
	});
	const Gate outcome = gateOutcome(control);
	sender.join();
	QVERIFY2(outcome == Gate::Open, "step top: a resume that arrives during a pause is applied");
}

void TestSimulationWorker::queuedDelayAppliesBeforeWait() {
	RunControl control;
	control.submit({RunCommandKind::SetDelayMs, 60});
	bool proceed = false;
	const auto elapsed = timeOfBeginStep(control, proceed);
	// Step top: a queued delay is applied before the delay is waited for.
	QVERIFY(proceed);
	QVERIFY(elapsed >= std::chrono::milliseconds(40));
}

void TestSimulationWorker::stopIsCheckedBeforeDelay() {
	RunControl control;
	control.setDelayMs(5000);
	control.requestStop();
	bool proceed = true;
	const auto elapsed = timeOfBeginStep(control, proceed);
	// Step top: a stop is checked before the delay.
	QVERIFY(!proceed);
	QVERIFY(elapsed < std::chrono::milliseconds(2000));
}

void TestSimulationWorker::controllerKeepsParent() {
	QObject parent;
	DispatchController child(&parent);
	// DispatchController keeps its parent.
	QCOMPARE(child.parent(), &parent);
}

// A worker runs the controller that it is given, and the controller of the process when it is given none. This slot runs
// before the scene runs, while the process has prepared no scene, so each run is turned down for want of trains, and the
// controller that was run says so.
void TestSimulationWorker::workerRunsGivenController() {
	QObject parent;
	DispatchController child(&parent);
	int turnedDownByChild = 0;
	int turnedDownByProcess = 0;
	const auto childConnection = QObject::connect(&child, &DispatchController::executionRejected, [&] { ++turnedDownByChild; });
	const auto processConnection = QObject::connect(&simulation, &DispatchController::executionRejected, [&] { ++turnedDownByProcess; });
	eglog::setSink([](eglog::Category, eglog::Level, const std::string&) {});
	const auto restore = qScopeGuard([&] {
		eglog::setSink({});
		QObject::disconnect(childConnection);
		QObject::disconnect(processConnection);
	});
	SimulationWorker workerOfChild(child);
	workerOfChild.run();
	// A worker runs the controller that it is given.
	QCOMPARE(turnedDownByChild, 1);
	QCOMPARE(turnedDownByProcess, 0);
	SimulationWorker workerOfProcess;
	workerOfProcess.run();
	// A worker without a controller runs the controller of the process.
	QCOMPARE(turnedDownByChild, 1);
	QCOMPARE(turnedDownByProcess, 1);
}

void TestSimulationWorker::stopBeforeStart() {
	QTemporaryDir output;
	RunObservation observed;
	runScene(m_scene, output.path(), StopAt::BeforeStart, observed);
	// Stop before start: no step published.
	QCOMPARE(observed.snapshots.load(), 0);
	checkStoppedBeforeOutput(observed, output);
	if (QTest::currentTestFailed())
		return;
	QVERIFY2(!exists(output, "PassengerStatus/PassengerStatus.txt"), "stop before start: no status file");
}

void TestSimulationWorker::stopAtStart() {
	QTemporaryDir output;
	RunObservation observed;
	runScene(m_scene, output.path(), StopAt::Started, observed);
	// Stop at simulationStarted: no step published.
	QCOMPARE(observed.snapshots.load(), 0);
	checkStoppedBeforeOutput(observed, output);
	if (QTest::currentTestFailed())
		return;
	QVERIFY2(!exists(output, "PassengerStatus/PassengerStatus.txt"), "stop at simulationStarted: no status file");
}

void TestSimulationWorker::stopInLoop() {
	QTemporaryDir output;
	RunObservation observed;
	runScene(m_scene, output.path(), StopAt::TenthSnapshot, observed);
	// Stop in the loop: only a few more steps.
	QCOMPARE_GE(observed.snapshots.load(), kStopAtSnapshot);
	QCOMPARE_LE(observed.snapshots.load(), kStopAtSnapshot + 2);
	checkStoppedBeforeOutput(observed, output);
	if (QTest::currentTestFailed())
		return;
	checkFinalStatus(observed, output);
}

void TestSimulationWorker::controlRun() {
	QTemporaryDir output;
	RunObservation observed;
	runScene(m_scene, output.path(), StopAt::Never, observed);
	QVERIFY2(observed.prepared, "control run: scene prepared with trains");
	// Control run: every step published.
	QCOMPARE(observed.snapshots.load(), kHorizonSeconds);
	// Control run: one pass through the stages.
	QCOMPARE(observed.postprocessing.load(), 1);
	QCOMPARE(observed.returned.load(), 1);
	// Control run: completed, not cancelled.
	QVERIFY(!observed.cancelled);
	QVERIFY(observed.completed);
	QVERIFY(!observed.stopRequested);
	QVERIFY2(exists(output, kEnergyFile), (std::string("control run: ") + kEnergyFile).c_str());
	for (const char* file : kLateFiles)
		QVERIFY2(exists(output, file), (std::string("control run: ") + file).c_str());
	QVERIFY2(!simulation.takeReplayCandidate().empty(), "control run: replay history recorded");
	checkFinalStatus(observed, output);
}

// A run that nothing draws: no snapshot and no replay history, the same stages and files.
void TestSimulationWorker::runWithoutSnapshots() {
	QTemporaryDir output;
	RunObservation observed;
	simulation.setSnapshotsEnabled(false);
	runScene(m_scene, output.path(), StopAt::Never, observed);
	simulation.setSnapshotsEnabled(true);
	QVERIFY2(observed.prepared, "run without snapshots: scene prepared with trains");
	// Run without snapshots: no step published.
	QCOMPARE(observed.snapshots.load(), 0);
	QVERIFY(!simulation.takeSimulationSnapshot());
	QVERIFY2(simulation.takeReplayCandidate().empty(), "run without snapshots: no replay history");
	QVERIFY2(exists(output, "PassengerStatus/PassengerStatus.txt"), "run without snapshots: status file exists");
	// Run without snapshots: one pass through the stages, completed.
	QCOMPARE(observed.postprocessing.load(), 1);
	QCOMPARE(observed.returned.load(), 1);
	QVERIFY(observed.completed);
	QVERIFY2(exists(output, kEnergyFile), (std::string("run without snapshots: ") + kEnergyFile).c_str());
	for (const char* file : kLateFiles)
		QVERIFY2(exists(output, file), (std::string("run without snapshots: ") + file).c_str());
}

void TestSimulationWorker::stopInPostprocessing() {
	QTemporaryDir output;
	RunObservation observed;
	runScene(m_scene, output.path(), StopAt::Postprocessing, observed);
	QVERIFY2(observed.prepared, "stop in post-processing: scene prepared with trains");
	// Stop in post-processing: loop finished.
	QCOMPARE(observed.snapshots.load(), kHorizonSeconds);
	// Stop in post-processing: executionReturned emitted once.
	QCOMPARE(observed.postprocessing.load(), 1);
	QCOMPARE(observed.returned.load(), 1);
	// Stop in post-processing: cancelled, not completed.
	QVERIFY(observed.cancelled);
	QVERIFY(!observed.completed);
	QVERIFY(observed.stopRequested);
	for (const char* file : kLateFiles)
		QVERIFY2(!exists(output, file), (std::string("stop in post-processing: no ") + file).c_str());
}

void TestSimulationWorker::stopAfterCompletion() {
	QTemporaryDir output;
	RunObservation observed;
	runScene(m_scene, output.path(), StopAt::Returned, observed);
	QVERIFY2(observed.prepared, "stop after completion: scene prepared with trains");
	// Stop after completion: run stays completed.
	QCOMPARE(observed.returned.load(), 1);
	QVERIFY(!observed.cancelled);
	QVERIFY(observed.completed);
	QVERIFY2(observed.stopRequested, "stop after completion: stop request kept");
	for (const char* file : kLateFiles)
		QVERIFY2(exists(output, file), (std::string("stop after completion: ") + file).c_str());
}

// A stop that is queued as a command takes effect at the top of a step, like a request.
void TestSimulationWorker::queuedStopBeforeStart() {
	QTemporaryDir output;
	RunObservation observed;
	runScene(m_scene, output.path(), StopAt::BeforeStart, observed, QString(), kHorizonSeconds, kWorkerCommand);
	// Queued stop before start: no step published.
	QCOMPARE(observed.snapshots.load(), 0);
	checkStoppedBeforeOutput(observed, output);
	if (QTest::currentTestFailed())
		return;
	QVERIFY2(!exists(output, "PassengerStatus/PassengerStatus.txt"), "queued stop before start: no status file");
}

void TestSimulationWorker::queuedStopInLoop() {
	QTemporaryDir output;
	RunObservation observed;
	runScene(m_scene, output.path(), StopAt::TenthSnapshot, observed, QString(), kHorizonSeconds, kWorkerCommand);
	// Queued stop in the loop: no step after the command.
	QCOMPARE(observed.snapshots.load(), kStopAtSnapshot);
	checkStoppedBeforeOutput(observed, output);
	if (QTest::currentTestFailed())
		return;
	checkFinalStatus(observed, output);
}

// A run without a worker, as with -g 0, has a control too.
void TestSimulationWorker::stopRequestWithoutWorker() {
	QTemporaryDir output;
	RunObservation observed;
	runScene(m_scene, output.path(), StopAt::TenthSnapshot, observed, QString(), kHorizonSeconds, kHeadlessRequest);
	// Stop request without a worker: no step after the request.
	QCOMPARE(observed.snapshots.load(), kStopAtSnapshot);
	checkStoppedBeforeOutput(observed, output);
	if (QTest::currentTestFailed())
		return;
	checkFinalStatus(observed, output);
}

// The run after a stopped run starts with a new control.
void TestSimulationWorker::queuedStopWithoutWorker() {
	{
		QTemporaryDir output;
		RunObservation observed;
		runScene(m_scene, output.path(), StopAt::TenthSnapshot, observed, QString(), kHorizonSeconds, kHeadlessCommand);
		// Queued stop without a worker: no step after the command.
		QCOMPARE(observed.snapshots.load(), kStopAtSnapshot);
		checkStoppedBeforeOutput(observed, output);
		if (QTest::currentTestFailed())
			return;
		checkFinalStatus(observed, output);
		if (QTest::currentTestFailed())
			return;
	}
	QTemporaryDir output;
	RunObservation observed;
	runScene(m_scene, output.path(), StopAt::Never, observed, QString(), kHorizonSeconds, kHeadlessRequest);
	QVERIFY2(observed.prepared, "run without a worker after a stop: scene prepared with trains");
	// Run without a worker after a stop: every step, completed, no stop.
	QCOMPARE(observed.snapshots.load(), kHorizonSeconds);
	QVERIFY(observed.completed);
	QVERIFY(!observed.stopRequested);
	QVERIFY2(!simulation.runControl()->wasCancellationRequested(), "run without a worker after a stop: not cancelled");
	for (const char* file : kLateFiles)
		QVERIFY2(exists(output, file), (std::string("run without a worker after a stop: ") + file).c_str());
}

void TestSimulationWorker::reusedOutputFolder() {
	QTemporaryDir output;
	// Allow a few hundred seconds after the first terminal arrival at 1659 s.
	constexpr int kArrivalHorizonSeconds = 2000;
	const char* const files[] = {"TrainTrajectories/TrainServicePathDiagram.txt"};
	QByteArray firstContents[std::size(files)];
	const auto readOutput = [&](const char* name, bool& opened) {
		QFile file(output.filePath(QString::fromUtf8(name)));
		opened = file.open(QIODevice::ReadOnly | QIODevice::Text);
		return file.readAll();
	};
	simulation.setSnapshotsEnabled(false);
	const auto enableSnapshots = qScopeGuard([] { simulation.setSnapshotsEnabled(true); });
	for (int run = 0; run < 2; ++run) {
		RunObservation observed;
		runScene(m_scene, output.path(), StopAt::Never, observed, QString(), kArrivalHorizonSeconds);
		// Reused output folder: run prepared and completed.
		QVERIFY(observed.prepared);
		QVERIFY(observed.completed);
		simulation.printLastTrainServicePathDiagram();
		for (std::size_t i = 0; i < std::size(files); ++i) {
			bool opened = false;
			const QByteArray content = readOutput(files[i], opened);
			QVERIFY2(opened, (std::string(files[i]) + ": opens for reading").c_str());
			QVERIFY2(content.count('\n') > 0, (std::string(files[i]) + ": contains at least one line").c_str());
			if (run == 0)
				firstContents[i] = content;
			else
				QVERIFY2(content == firstContents[i], (std::string(files[i]) + ": second run equals first (lines " + std::to_string(firstContents[i].count('\n')) + ", " + std::to_string(content.count('\n')) + ")").c_str());
		}
	}
	QVERIFY2(!QFileInfo::exists(output.filePath("Rescheduling")), "reused output folder: a run creates no Rescheduling folder");
	simulation.setSnapshotsEnabled(true);
	SceneModel invalid = m_scene;
	invalid.services.clear();
	const auto diagnostics = simulation.prepareScene(invalid);
	QVERIFY2(hasErrors(diagnostics), "reused output folder: scene without services fails preparation");
	for (std::size_t i = 0; i < std::size(files); ++i) {
		bool opened = false;
		const QByteArray content = readOutput(files[i], opened);
		QVERIFY2(opened, (std::string(files[i]) + ": opens for reading").c_str());
		QVERIFY2(content == firstContents[i], (std::string(files[i]) + ": failed preparation keeps earlier output").c_str());
	}
}

void TestSimulationWorker::exceptionInSlotFailsRun_data() {
	QTest::addColumn<bool>("headless");
	QTest::newRow("in a worker") << false;
	QTest::newRow("without a worker") << true;
}

// An exception in a slot ends the run as failed, with or without a worker, and the next run completes.
void TestSimulationWorker::exceptionInSlotFailsRun() {
	QFETCH(bool, headless);
	const RunMode& mode = headless ? kHeadlessRequest : kWorkerRequest;
	for (const StopAt throwAt : {StopAt::ThrowAtBegan, StopAt::ThrowAtPostprocessing}) {
		const std::string name = std::string(throwAt == StopAt::ThrowAtBegan ? "throw at the start" : "throw after the steps")
			+ ": the run failed with the text of the exception, without executionReturned";
		QTemporaryDir output;
		RunObservation observed;
		runScene(m_scene, output.path(), throwAt, observed, QString(), kHorizonSeconds, mode);
		QVERIFY2(observed.prepared, name.c_str());
		QVERIFY2(observed.error == "injected", (name + " (" + observed.error + ")").c_str());
		QVERIFY2(observed.failed == 1, name.c_str());
		QVERIFY2(observed.returned == 0, name.c_str());
		QVERIFY2(!observed.completed, name.c_str());
	}
	QTemporaryDir output;
	RunObservation observed;
	runScene(m_scene, output.path(), StopAt::Never, observed, QString(), kHorizonSeconds, mode);
	// The run after a failed run completes.
	QVERIFY(observed.completed);
	QCOMPARE(observed.error, std::string());
	QCOMPARE(observed.failed.load(), 0);
}

// A detailed run whose TEMP folder cannot be made fails and still finishes.
void TestSimulationWorker::blockedTempFolderFailsRun() {
	QTemporaryDir output;
	QFile blocker(output.filePath("TEMP"));
	QVERIFY2(blocker.open(QIODevice::WriteOnly), "blocked TEMP: a file takes the place of the folder");
	blocker.close();
	initial_variables.exportDetailedTrajectories = true;
	RunObservation observed;
	runScene(m_scene, output.path(), StopAt::Never, observed);
	initial_variables.exportDetailedTrajectories = false;
	// Blocked TEMP: the run failed and names the folder.
	QVERIFY(observed.prepared);
	QCOMPARE(observed.failed.load(), 1);
	QVERIFY(!observed.completed);
	QVERIFY2(observed.error.rfind("Cannot create the folder ", 0) == 0, observed.error.c_str());
	QVERIFY2(observed.error.find("TEMP") != std::string::npos, observed.error.c_str());
}

// An output folder that cannot be made is an error of the preparation.
void TestSimulationWorker::blockedOutputFolderFailsPreparation() {
	QTemporaryDir output;
	QFile blocker(output.filePath("blocker"));
	QVERIFY2(blocker.open(QIODevice::WriteOnly), "blocked output folder: a file takes the place of the folder");
	blocker.close();
	simulation.setNextRunOutputFolder(blocker.fileName().toStdString());
	const std::vector<SceneDiagnostic> diagnostics = simulation.prepareScene(m_scene);
	simulation.setNextRunOutputFolder({});
	QVERIFY2(std::any_of(diagnostics.begin(), diagnostics.end(),
				 [](const SceneDiagnostic& diagnostic) {
					 return diagnostic.code == "run.output.folder" && diagnostic.severity == SceneSeverity::Error;
				 }),
		"blocked output folder: the preparation reports run.output.folder");
}

// Last, because the chosen folder stays in the controller for later runs.
void TestSimulationWorker::folderChosenDuringRun() {
	QTemporaryDir output;
	QTemporaryDir chosen;
	const auto forgetFolder = qScopeGuard([] { simulation.setNextRunOutputFolder({}); });
	RunObservation observed;
	runScene(m_scene, output.path(), StopAt::Never, observed, chosen.path());
	// Folder chosen during a run: run completed.
	QVERIFY(observed.prepared);
	QVERIFY(observed.completed);
	// Folder chosen during a run: the run keeps its folder.
	QCOMPARE(initial_variables.OutputMainFolder, output.path().toStdString());
	QVERIFY2(exists(output, kEnergyFile), (std::string("folder chosen during a run: ") + kEnergyFile).c_str());
	for (const char* file : kLateFiles)
		QVERIFY2(exists(output, file), (std::string("folder chosen during a run: ") + file).c_str());
	QVERIFY2(QDir(chosen.path()).isEmpty(), "folder chosen during a run: nothing written there");

	QVERIFY2(!hasErrors(simulation.prepareScene(m_scene)), "next run: scene prepared");
	// Next run: uses the chosen folder.
	QCOMPARE(initial_variables.OutputMainFolder, chosen.path().toStdString());
	QVERIFY2(exists(chosen, "TrainTrajectories"), "next run: output tree created in the chosen folder");
}

QTEST_GUILESS_MAIN(TestSimulationWorker)
#include "test_simulationworker.moc"
