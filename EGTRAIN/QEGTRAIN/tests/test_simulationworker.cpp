#include "app/DispatchController.h"
#include "scene/SceneModel.h"
#include "simulation/InitialParameters.h"
#include "simulation/Infrastructure.h"
#include "simulation/Optimisation.h"
#include "simulation/Passengers.h"
#include "simulation/RollingStock.h"
#include "simulation/SimulationWorker.h"
#ifdef signals
#undef signals
#endif

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QThread>
#include <QTextStream>

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

enum class StopAt { Never,
	BeforeStart,
	Started,
	TenthSnapshot,
	Postprocessing,
	Returned };

struct RunObservation {
	bool prepared = false;
	std::atomic<int> snapshots{0};
	std::atomic<int> lastStep{-1};
	std::atomic<int> statusFileDuringLoop{0};
	std::atomic<int> postprocessing{0};
	std::atomic<int> returned{0};
	std::atomic<bool> cancelled{false};
	bool completed = false;
	bool stopRequested = false;
};

// Runs the prepared scene the way MainWindow::startSimulation does and requests
// the stop at the given point. A folder in chosenDuringRun is set as the output
// folder of the next run once the worker thread has started.
void runScene(const SceneModel& scene, const QString& outputDir, StopAt stopAt, RunObservation& observed,
	const QString& chosenDuringRun = QString(), int horizonSeconds = kHorizonSeconds) {
	initial_variables.GUI = 0;
	initial_variables.TSM = 0;
	initial_variables.RChoice = 0;
	initial_variables.durationOverride = true;
	initial_variables.times = horizonSeconds;
	initial_variables.OutputMainFolder = outputDir.toStdString();
	InputMainFolder.clear();
	initial_variables.InputMainFolder.clear();

	observed.prepared = !hasErrors(simulation.prepareScene(scene)) && numRegions > 0;
	if (!observed.prepared)
		return;

	// A passenger row for the status file, with no journey or train interaction.
	Passenger probe;
	probe.ID = "status-probe";
	probe.IsIntheNetwork = true;
	AllDailyPassengers.push_back(probe);

	auto* worker = new SimulationWorker();
	QThread thread;
	worker->moveToThread(&thread);
	QObject::connect(&thread, &QThread::started, worker, &SimulationWorker::run);

	const auto stopWorker = [worker] { worker->requestStop(); };
	QObject context;
	QObject::connect(&simulation, &DispatchController::snapshotAvailable, &context, [&] {
		// The mailbox only emits again once the previous snapshot has been taken.
		const auto snapshot = simulation.takeSimulationSnapshot();
		if (snapshot)
			observed.lastStep = snapshot->timestep;
		if (QFileInfo::exists(outputDir + "/PassengerStatus/PassengerStatus.txt"))
			++observed.statusFileDuringLoop;
		if (++observed.snapshots == kStopAtSnapshot && stopAt == StopAt::TenthSnapshot)
			stopWorker(); }, Qt::DirectConnection);
	QObject::connect(&simulation, &DispatchController::executionPostprocessing, &context, [&] {
		++observed.postprocessing;
		if (stopAt == StopAt::Postprocessing)
			stopWorker(); }, Qt::DirectConnection);
	QObject::connect(&simulation, &DispatchController::executionReturned, &context, [&](qint64, bool cancelled) {
		++observed.returned;
		observed.cancelled = cancelled;
		if (stopAt == StopAt::Returned)
			stopWorker(); }, Qt::DirectConnection);
	if (stopAt == StopAt::Started)
		QObject::connect(worker, &SimulationWorker::simulationStarted, &context, stopWorker, Qt::DirectConnection);

	QEventLoop loop;
	QObject::connect(worker, &SimulationWorker::simulationFinished, &loop, &QEventLoop::quit);
	if (stopAt == StopAt::BeforeStart)
		worker->requestStop();
	thread.start();
	if (!chosenDuringRun.isEmpty())
		simulation.setNextRunOutputFolder(chosenDuringRun.toStdString());
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

bool finalStatus(const char* name, const RunObservation& observed, const QTemporaryDir& output) {
	bool ok = true;
	ok &= expect(observed.statusFileDuringLoop == 0, std::string(name) + ": no status file during the loop");
	const auto lines = statusLines(output);
	ok &= expect(lines.size() == 2, std::string(name) + ": status file has header and passenger row");
	ok &= expect(!lines.isEmpty() && lines.front().startsWith("Time PaxID IsInNetwork"),
		std::string(name) + ": status header");
	const auto row = QString::number(observed.lastStep + initial_variables.startingSimulationTime)
		+ " status-probe 1 None None None None None None None None";
	ok &= expect(lines.size() == 2 && lines[1] == row, std::string(name) + ": status row holds the last step");
	return ok;
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
		ok &= expect(!exists(output, "PassengerStatus/PassengerStatus.txt"), "stop before start: no status file");
	}
	{
		QTemporaryDir output;
		RunObservation observed;
		runScene(loaded.scene, output.path(), StopAt::Started, observed);
		ok &= expect(observed.snapshots == 0, "stop at simulationStarted: no step published");
		ok &= stoppedBeforeOutput("stop at simulationStarted", observed, output);
		ok &= expect(!exists(output, "PassengerStatus/PassengerStatus.txt"), "stop at simulationStarted: no status file");
	}
	{
		QTemporaryDir output;
		RunObservation observed;
		runScene(loaded.scene, output.path(), StopAt::TenthSnapshot, observed);
		ok &= expect(observed.snapshots >= kStopAtSnapshot && observed.snapshots <= kStopAtSnapshot + 2,
			"stop in the loop: only a few more steps (" + std::to_string(observed.snapshots) + ")");
		ok &= stoppedBeforeOutput("stop in the loop", observed, output);
		ok &= finalStatus("stop in the loop", observed, output);
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
		ok &= expect(!simulation.takeReplayCandidate().empty(), "control run: replay history recorded");
		ok &= finalStatus("control run", observed, output);
	}
	{
		// A run that nothing draws: no snapshot and no replay history, the same stages and files.
		QTemporaryDir output;
		RunObservation observed;
		simulation.setSnapshotsEnabled(false);
		runScene(loaded.scene, output.path(), StopAt::Never, observed);
		simulation.setSnapshotsEnabled(true);
		ok &= expect(observed.prepared, "run without snapshots: scene prepared with trains");
		ok &= expect(observed.snapshots == 0 && !simulation.takeSimulationSnapshot(),
			"run without snapshots: no step published");
		ok &= expect(simulation.takeReplayCandidate().empty(), "run without snapshots: no replay history");
		ok &= expect(exists(output, "PassengerStatus/PassengerStatus.txt"), "run without snapshots: status file exists");
		ok &= expect(observed.postprocessing == 1 && observed.returned == 1 && observed.completed,
			"run without snapshots: one pass through the stages, completed");
		ok &= expect(exists(output, kEnergyFile), std::string("run without snapshots: ") + kEnergyFile);
		for (const char* file : kLateFiles)
			ok &= expect(exists(output, file), std::string("run without snapshots: ") + file);
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

	{
		QTemporaryDir output;
		// Allow a few hundred seconds after the first terminal arrival at 1659 s.
		constexpr int kArrivalHorizonSeconds = 2000;
		const char* const files[] = {"TrainTrajectories/TrainServicePathDiagram.txt", "Rescheduling/EGTRAINOutput.txt"};
		QByteArray firstContents[2];
		const auto readOutput = [&](const char* name) {
			QFile file(output.filePath(QString::fromUtf8(name)));
			ok &= expect(file.open(QIODevice::ReadOnly | QIODevice::Text), std::string(name) + ": opens for reading");
			return file.readAll();
		};
		simulation.setSnapshotsEnabled(false);
		for (int run = 0; run < 2; ++run) {
			RunObservation observed;
			runScene(loaded.scene, output.path(), StopAt::Never, observed, QString(), kArrivalHorizonSeconds);
			ok &= expect(observed.prepared && observed.completed, "reused output folder: run prepared and completed");
			simulation.printLastTrainServicePathDiagram();
			for (int i = 0; i < 2; ++i) {
				const QByteArray content = readOutput(files[i]);
				ok &= expect(content.count('\n') > 0, std::string(files[i]) + ": contains at least one line");
				if (run == 0)
					firstContents[i] = content;
				else
					ok &= expect(content == firstContents[i], std::string(files[i]) + ": second run equals first (lines " + std::to_string(firstContents[i].count('\n')) + ", " + std::to_string(content.count('\n')) + ")");
			}
		}
		simulation.setSnapshotsEnabled(true);
		SceneModel invalid = loaded.scene;
		invalid.services.clear();
		const auto diagnostics = simulation.prepareScene(invalid);
		ok &= expect(hasErrors(diagnostics), "reused output folder: scene without services fails preparation");
		for (int i = 0; i < 2; ++i)
			ok &= expect(readOutput(files[i]) == firstContents[i], std::string(files[i]) + ": failed preparation keeps earlier output");
	}

	{
		// Last, because the chosen folder stays in the controller for later runs.
		QTemporaryDir output;
		QTemporaryDir chosen;
		RunObservation observed;
		runScene(loaded.scene, output.path(), StopAt::Never, observed, chosen.path());
		ok &= expect(observed.prepared && observed.completed, "folder chosen during a run: run completed");
		ok &= expect(initial_variables.OutputMainFolder == output.path().toStdString(),
			"folder chosen during a run: the run keeps its folder");
		ok &= expect(exists(output, kEnergyFile), std::string("folder chosen during a run: ") + kEnergyFile);
		for (const char* file : kLateFiles)
			ok &= expect(exists(output, file), std::string("folder chosen during a run: ") + file);
		ok &= expect(QDir(chosen.path()).isEmpty(), "folder chosen during a run: nothing written there");

		ok &= expect(!hasErrors(simulation.prepareScene(loaded.scene)), "next run: scene prepared");
		ok &= expect(initial_variables.OutputMainFolder == chosen.path().toStdString(),
			"next run: uses the chosen folder");
		ok &= expect(exists(chosen, "TrainTrajectories"), "next run: output tree created in the chosen folder");
		simulation.setNextRunOutputFolder({});
	}

	if (ok)
		std::cout << "simulation worker stop handling passed\n";
	return ok ? 0 : 1;
}
