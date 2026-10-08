#include "app/DispatchController.h"
#include "scene/SceneModel.h"
#include "simulation/InitialParameters.h"
#include "simulation/Infrastructure.h"
#include "simulation/Optimisation.h"
#include "simulation/RollingStock.h"
#include "simulation/SimulationWorker.h"
#ifdef signals
#undef signals
#endif

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <iostream>
#include <string>

Logger owl;

namespace {

bool expect(bool condition, const std::string& message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

constexpr int kHorizonSeconds = 300;
constexpr int kStopAtSnapshot = 10;

// Written by the energy stage, before the first stop check after the loop.
const char* const kEnergyFile = "EnergyConsumptionPerTrain.txt";
// Written by the last stages of the end-of-run output.
const char* const kLateFiles[] = {"TrainTrajectories/TimetablePoints.txt", "TrainTrajectories/BlockingTimes.txt"};

enum class StopAt { Never, BeforeStart, Started, TenthSnapshot, Postprocessing, Returned };

struct RunObservation {
	bool prepared = false;
	std::atomic<int> snapshots{0};
	std::atomic<int> postprocessing{0};
	std::atomic<int> returned{0};
	std::atomic<bool> cancelled{false};
	bool completed = false;
	bool stopRequested = false;
};

// Runs the prepared scene the way MainWindow::startSimulation does and requests
// the stop at the given point.
void runScene(const SceneModel& scene, const QString& outputDir, StopAt stopAt, RunObservation& observed) {
	initial_variables.GUI = 0;
	initial_variables.TSM = 0;
	initial_variables.RChoice = 0;
	initial_variables.durationOverride = true;
	initial_variables.times = kHorizonSeconds;
	initial_variables.OutputMainFolder = outputDir.toStdString();
	InputMainFolder.clear();
	initial_variables.InputMainFolder.clear();

	observed.prepared = !hasErrors(simulation.prepareScene(scene)) && numRegions > 0;
	if (!observed.prepared)
		return;

	auto* worker = new SimulationWorker();
	QThread thread;
	worker->moveToThread(&thread);
	QObject::connect(&thread, &QThread::started, worker, &SimulationWorker::run);

	const auto stopWorker = [worker] { worker->requestStop(); };
	QObject context;
	QObject::connect(&simulation, &DispatchController::snapshotAvailable, &context, [&] {
		// The mailbox only emits again once the previous snapshot has been taken.
		simulation.takeSimulationSnapshot();
		if (++observed.snapshots == kStopAtSnapshot && stopAt == StopAt::TenthSnapshot)
			stopWorker();
	}, Qt::DirectConnection);
	QObject::connect(&simulation, &DispatchController::executionPostprocessing, &context, [&] {
		++observed.postprocessing;
		if (stopAt == StopAt::Postprocessing)
			stopWorker();
	}, Qt::DirectConnection);
	QObject::connect(&simulation, &DispatchController::executionReturned, &context, [&](qint64, bool cancelled) {
		++observed.returned;
		observed.cancelled = cancelled;
		if (stopAt == StopAt::Returned)
			stopWorker();
	}, Qt::DirectConnection);
	if (stopAt == StopAt::Started)
		QObject::connect(worker, &SimulationWorker::simulationStarted, &context, stopWorker, Qt::DirectConnection);

	QEventLoop loop;
	QObject::connect(worker, &SimulationWorker::simulationFinished, &loop, &QEventLoop::quit);
	if (stopAt == StopAt::BeforeStart)
		worker->requestStop();
	thread.start();
	loop.exec();
	thread.quit();
	thread.wait();
	observed.completed = simulation.lastRunCompleted();
	observed.stopRequested = worker->isStopRequested();
	delete worker;
}

bool exists(const QTemporaryDir& dir, const std::string& relative) {
	return QFileInfo::exists(dir.filePath(QString::fromStdString(relative)));
}

bool stoppedBeforeOutput(const char* name, const RunObservation& observed, const QTemporaryDir& output) {
	bool ok = true;
	ok &= expect(observed.prepared, std::string(name) + ": scene prepared with trains");
	ok &= expect(observed.postprocessing == 0, std::string(name) + ": post-processing never began");
	ok &= expect(observed.returned == 1, std::string(name) + ": executionReturned emitted once");
	ok &= expect(observed.cancelled, std::string(name) + ": run reported as cancelled");
	ok &= expect(!observed.completed, std::string(name) + ": run not completed");
	ok &= expect(observed.stopRequested, std::string(name) + ": stop request kept");
	ok &= expect(!exists(output, kEnergyFile), std::string(name) + ": no energy output");
	for (const char* file : kLateFiles)
		ok &= expect(!exists(output, file), std::string(name) + ": no " + file);
	return ok;
}

} // namespace

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	if (argc < 2) {
		std::cerr << "usage: test_simulationworker SCENE_DIR\n";
		return 2;
	}
	bool ok = true;

	QObject parent;
	DispatchController child(&parent);
	ok &= expect(child.parent() == &parent, "DispatchController keeps its parent");

	SceneLoadResult loaded = loadScene(argv[1]);
	if (!expect(!hasErrors(loaded.diagnostics), "scene loads"))
		return 1;

	{
		QTemporaryDir output;
		RunObservation observed;
		runScene(loaded.scene, output.path(), StopAt::BeforeStart, observed);
		ok &= expect(observed.snapshots == 0, "stop before start: no step published");
		ok &= stoppedBeforeOutput("stop before start", observed, output);
	}
	{
		QTemporaryDir output;
		RunObservation observed;
		runScene(loaded.scene, output.path(), StopAt::Started, observed);
		ok &= expect(observed.snapshots == 0, "stop at simulationStarted: no step published");
		ok &= stoppedBeforeOutput("stop at simulationStarted", observed, output);
	}
	{
		QTemporaryDir output;
		RunObservation observed;
		runScene(loaded.scene, output.path(), StopAt::TenthSnapshot, observed);
		ok &= expect(observed.snapshots >= kStopAtSnapshot && observed.snapshots <= kStopAtSnapshot + 2,
				"stop in the loop: only a few more steps (" + std::to_string(observed.snapshots) + ")");
		ok &= stoppedBeforeOutput("stop in the loop", observed, output);
	}
	{
		QTemporaryDir output;
		RunObservation observed;
		runScene(loaded.scene, output.path(), StopAt::Never, observed);
		ok &= expect(observed.prepared, "control run: scene prepared with trains");
		ok &= expect(observed.snapshots == kHorizonSeconds, "control run: every step published");
		ok &= expect(observed.postprocessing == 1 && observed.returned == 1, "control run: one pass through the stages");
		ok &= expect(!observed.cancelled && observed.completed && !observed.stopRequested,
				"control run: completed, not cancelled");
		ok &= expect(exists(output, kEnergyFile), std::string("control run: ") + kEnergyFile);
		for (const char* file : kLateFiles)
			ok &= expect(exists(output, file), std::string("control run: ") + file);
	}
	{
		QTemporaryDir output;
		RunObservation observed;
		runScene(loaded.scene, output.path(), StopAt::Postprocessing, observed);
		ok &= expect(observed.prepared, "stop in post-processing: scene prepared with trains");
		ok &= expect(observed.snapshots == kHorizonSeconds, "stop in post-processing: loop finished");
		ok &= expect(observed.postprocessing == 1 && observed.returned == 1,
				"stop in post-processing: executionReturned emitted once");
		ok &= expect(observed.cancelled && !observed.completed && observed.stopRequested,
				"stop in post-processing: cancelled, not completed");
		for (const char* file : kLateFiles)
			ok &= expect(!exists(output, file), std::string("stop in post-processing: no ") + file);
	}
	{
		QTemporaryDir output;
		RunObservation observed;
		runScene(loaded.scene, output.path(), StopAt::Returned, observed);
		ok &= expect(observed.prepared, "stop after completion: scene prepared with trains");
		ok &= expect(observed.returned == 1 && !observed.cancelled && observed.completed,
				"stop after completion: run stays completed");
		ok &= expect(observed.stopRequested, "stop after completion: stop request kept");
		for (const char* file : kLateFiles)
			ok &= expect(exists(output, file), std::string("stop after completion: ") + file);
	}

	if (ok)
		std::cout << "simulation worker stop handling passed\n";
	return ok ? 0 : 1;
}
