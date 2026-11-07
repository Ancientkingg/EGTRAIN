#include "tests/e2e/MainWindowTestAccess.h"

#include "app/DispatchController.h"
#include "editors/CaseSettingsPanel.h"
#include "scene/SceneBundle.h"
#include "simulation/SimulationWorker.h"
#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QLineEdit>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

// <base>/Output/<name> as an absolute path.
QString outputFolderOf(const QString& base, const std::string& name) {
	return QDir::cleanPath(QDir(base).absoluteFilePath(QStringLiteral("Output/") + QString::fromStdString(name)));
}

QString currentOutputFolder() {
	return QDir::cleanPath(QString::fromStdString(initial_variables.OutputMainFolder));
}

struct OutputFolderRun {
	QTemporaryDir temp;
	QString base;
	QString secondPath;
	QString thirdPath;
	QString chosen;
	std::string secondName = "Output Folder Second";
	std::string renamedName = "Output Folder Renamed";
	std::string thirdName = "Output Folder Third";
	QStringList failures;
	int attempts = 0;
};

constexpr int kPollMs = 100;
constexpr int kMaxPolls = 100;
// Long enough for a train of Paimpol to enter, so that the run writes a row into TrainServicePathDiagram.txt.
constexpr double kDiagramRunSeconds = 600.0;

} // namespace

// The folder that a run writes to follows the scene that the window holds. It creates a new case and opens a
// second scene in the window, renames that scene in Case Settings and runs it, then picks a folder as Choose
// Output Folder does and opens a third scene, which must leave that folder in force. Then a run that an exception ends
// must fail and leave the third scene open and editable, and a run whose TrainServicePathDiagram.txt cannot be written
// must say so in the status bar. A run whose output folder is a file must not start and must leave the results, the
// replay and the canvas of that run as they are. A run whose TrainServicePathDiagram.txt of an earlier run cannot be
// removed must fail with the scene open and editable and the track preview drawn again. Both say in the status bar that
// the run did not start. A run whose results cannot be captured must end without results, with the scene open and
// editable and with what waited for the run done, and, where an exception can leave a slot, a run whose finish throws
// must still release its worker. QEGTRAIN_OUTPUT_DIR names the base folder. It prints E2E_OUTPUT_FOLDER_OK and exits
// with 0, or prints the failures and exits with 2.
void MainWindowTestAccess::runOutputFolder(MainWindow& w) {
	if (scenarioStarted)
		return;
	scenarioStarted = true;

	auto run = std::make_shared<OutputFolderRun>();
	const auto fail = [run](const QString& message) { run->failures << message; };
	// Stops the run that is still going, then ends the process.
	const auto finish = [&w, run]() {
		const bool ok = run->failures.isEmpty();
		if (ok) {
			std::fprintf(stdout, "E2E_OUTPUT_FOLDER_OK\n");
			std::fflush(stdout);
		} else {
			std::fprintf(stderr, "E2E_OUTPUT_FOLDER_FAIL: %s\n", run->failures.join(", ").toUtf8().constData());
			std::fflush(stderr);
		}
		const int code = ok ? 0 : 2;
		if (w.m_runController->m_worker && w.m_runController->m_workerThread && w.m_runController->m_workerThread->isRunning()) {
			QObject::connect(w.m_runController->m_workerThread, &QThread::finished, qApp, [code]() { QCoreApplication::exit(code); });
			w.m_runController->m_worker->requestStop();
			return;
		}
		QCoreApplication::exit(code);
	};

	run->base = qEnvironmentVariable("QEGTRAIN_OUTPUT_DIR");
	if (run->base.isEmpty() || !run->temp.isValid()) {
		fail(QStringLiteral("QEGTRAIN_OUTPUT_DIR or the temporary directory is missing"));
		finish();
		return;
	}
	const std::string startupName = w.m_document->mutableModel().name;
	const QString startupFolder = outputFolderOf(run->base, startupName);
	if (currentOutputFolder() != startupFolder)
		fail(QStringLiteral("the start-up scene does not write to %1").arg(startupFolder));

	run->secondPath = run->temp.filePath(QStringLiteral("second.egscene"));
	run->thirdPath = run->temp.filePath(QStringLiteral("third.egscene"));
	run->chosen = run->temp.filePath(QStringLiteral("chosen"));
	const auto writeBundle = [&w, fail](const QString& path, const std::string& name) {
		SceneModel copy = w.m_document->mutableModel();
		copy.name = name;
		if (!saveSceneBundle(copy, path.toStdString()).success())
			fail(QStringLiteral("the bundle of %1 was not written").arg(QString::fromStdString(name)));
	};
	writeBundle(run->secondPath, run->secondName);
	writeBundle(run->thirdPath, run->thirdName);
	if (!run->failures.isEmpty()) {
		finish();
		return;
	}

	// The start-up scene is saved, so a new case replaces it without asking to save.
	w.m_session->newScene();
	if (!w.m_session->m_sceneDir.isEmpty() || w.m_document->mutableModel().name == startupName) {
		fail(QStringLiteral("the new case was not created"));
		finish();
		return;
	}
	const QString newFolder = outputFolderOf(run->base, w.m_document->mutableModel().name);
	if (currentOutputFolder() != newFolder)
		fail(QStringLiteral("after creating a case the folder is %1, not %2").arg(currentOutputFolder(), newFolder));

	const QString secondFolder = outputFolderOf(run->base, run->secondName);
	if (!w.openSceneDirectory(run->secondPath) || w.m_document->mutableModel().name != run->secondName) {
		fail(QStringLiteral("the second scene did not open"));
		finish();
		return;
	}
	if (currentOutputFolder() != secondFolder)
		fail(QStringLiteral("after opening the second scene the folder is %1, not %2").arg(currentOutputFolder(), secondFolder));

	// A rename in Case Settings moves the folder of the run. A folder left by an earlier run must not make the check pass.
	const QString renamedFolder = outputFolderOf(run->base, run->renamedName);
	QDir(renamedFolder).removeRecursively();
	if (!caseSettingsPanel(w)->nameEdit()) {
		fail(QStringLiteral("Case Settings has no name field"));
		finish();
		return;
	}
	caseSettingsPanel(w)->nameEdit()->setText(QString::fromStdString(run->renamedName));
	w.commitCaseSettings();
	if (w.m_document->mutableModel().name != run->renamedName) {
		fail(QStringLiteral("the second scene was not renamed"));
		finish();
		return;
	}
	w.m_runController->runScene();
	if (!w.m_runController->m_worker) {
		fail(QStringLiteral("the run of the renamed scene did not start"));
		finish();
		return;
	}
	if (currentOutputFolder() != renamedFolder)
		fail(QStringLiteral("the run of the renamed scene writes to %1, not %2").arg(currentOutputFolder(), renamedFolder));
	if (!QDir(renamedFolder + QStringLiteral("/TrainTrajectories")).exists())
		fail(QStringLiteral("the run of the renamed scene prepared no files in %1").arg(renamedFolder));
	// The rename is not saved. Opening the next scene must not ask about it.
	w.m_document->setDirty(false);

	// Stops the run of the third scene, then runs it into an exception, into a completed run whose service path diagram
	// cannot be written and into the two runs that do not start.
	const auto checkFailedRuns = [&w, run, fail]() {
		const auto runEnded = [&w]() {
			for (int i = 0; i < kMaxPolls && w.m_runController->m_worker; ++i) {
				QEventLoop loop;
				QTimer::singleShot(kPollMs, &loop, &QEventLoop::quit);
				loop.exec();
			}
			return !w.m_runController->m_worker;
		};
		const auto caseUsable = [&w, run]() {
			return w.m_document->isLoaded() && w.m_document->isEditable() && w.m_document->mutableModel().name == run->thirdName
				&& w.findChild<QAction*>(QStringLiteral("actionRun"))->isEnabled() && w.m_runController->state() == RunController::RunState::Failed;
		};
		w.m_runController->m_worker->requestStop();
		if (!runEnded()) {
			fail(QStringLiteral("the run of the third scene did not stop"));
			return;
		}
		simulation.setNextRunOutputFolder(run->chosen.toStdString());
		// The condition keeps MSVC from warning about unreachable code after a slot that always throws.
		const bool inject = !run->chosen.isEmpty();
		const auto injected = QObject::connect(&simulation, &DispatchController::executionBegan, &simulation, [inject] {
			if (inject)
				throw std::runtime_error("injected"); }, Qt::DirectConnection);
		w.m_runController->runScene();
		const bool started = w.m_runController->m_worker;
		const bool ended = runEnded();
		QObject::disconnect(injected);
		if (!started || !ended || simulation.lastRunError() != "injected" || !caseUsable())
			fail(QStringLiteral("a run that an exception ended did not fail with the case usable"));
		else if (w.statusBar()->currentMessage() != QStringLiteral("Simulation failed"))
			fail(QStringLiteral("a run that an exception ended shows the status \"%1\"").arg(w.statusBar()->currentMessage()));

		// A folder of the name of the diagram, made once the run has begun, keeps the diagram from being written.
		const QString diagram = run->chosen + QStringLiteral("/TrainTrajectories/TrainServicePathDiagram.txt");
		const auto blocked = QObject::connect(&simulation, &DispatchController::executionBegan, &simulation, [diagram] { QDir().mkpath(diagram); }, Qt::DirectConnection);
		initial_variables.times = kDiagramRunSeconds;
		initial_variables.durationOverride = true;
		w.m_runController->runScene();
		const bool diagramRunStarted = w.m_runController->m_worker;
		const bool diagramRunEnded = runEnded();
		QObject::disconnect(blocked);
		initial_variables.durationOverride = false;
		const QString unwritten = QStringLiteral("Simulation complete, but TrainServicePathDiagram.txt could not be written");
		if (!diagramRunStarted || !diagramRunEnded || w.m_runController->state() != RunController::RunState::Completed)
			fail(QStringLiteral("the run whose service path diagram cannot be written did not complete"));
		else if (w.statusBar()->currentMessage() != unwritten)
			fail(QStringLiteral("a run whose service path diagram cannot be written shows the status \"%1\"").arg(w.statusBar()->currentMessage()));

		// An output folder that is a file is found before the completed run is let go, so its results stay.
		QFile blocker(run->temp.filePath(QStringLiteral("blocker")));
		if (!blocker.open(QIODevice::WriteOnly))
			fail(QStringLiteral("the file that blocks the output folder was not written"));
		blocker.close();
		const auto& diagnostics = w.m_runController->m_runtimeDiagnostics;
		const auto reported = [&diagnostics](const char* code) {
			return std::any_of(diagnostics.begin(), diagnostics.end(), [code](const SceneDiagnostic& diagnostic) { return diagnostic.code == code; });
		};
		const auto completed = w.m_runController->m_completedRun;
		simulation.setNextRunOutputFolder(blocker.fileName().toStdString());
		w.m_runController->runScene();
		if (w.m_runController->m_worker || !completed || w.m_runController->m_completedRun != completed || !w.m_runController->resultsAvailable()
			|| w.m_completedReplay.empty() || w.m_showingTrackPreview || reported("run.output.folder"))
			fail(QStringLiteral("a run whose output folder is a file did not keep the results of the run before"));
		if (!w.statusBar()->currentMessage().startsWith(QStringLiteral("Run not started: Cannot create the output folder ")))
			fail(QStringLiteral("a run whose output folder is a file shows the status \"%1\"").arg(w.statusBar()->currentMessage()));

		// A TrainServicePathDiagram.txt that cannot be removed is found once the canvas of the completed run is gone, which the
		// track preview then replaces.
		QDir().mkpath(diagram + QStringLiteral("/kept"));
		simulation.setNextRunOutputFolder(run->chosen.toStdString());
		w.m_runController->runScene();
		if (w.m_runController->m_worker || !reported("run.output.file") || !caseUsable())
			fail(QStringLiteral("a run whose service path diagram cannot be removed did not fail with the case usable"));
		if (!w.m_showingTrackPreview || !w.m_presenter->previewShown())
			fail(QStringLiteral("a run whose service path diagram cannot be removed left no track preview"));
		if (!w.statusBar()->currentMessage().startsWith(QStringLiteral("Run not started: Cannot write ")))
			fail(QStringLiteral("a run whose service path diagram cannot be removed shows the status \"%1\"").arg(w.statusBar()->currentMessage()));
		QDir(diagram).removeRecursively();

		// The capture of the results throws once the run has completed and a Close, New or Open has come to wait for it.
		simulation.setNextRunOutputFolder(run->temp.filePath(QStringLiteral("capture")).toStdString());
		initial_variables.times = kDiagramRunSeconds;
		initial_variables.durationOverride = true;
		bool deferredRan = false;
		w.m_runController->m_beforeResultCapture = [&w, &deferredRan, inject] {
			w.m_runController->deferUntilRunStopped([&deferredRan] { deferredRan = true; });
			if (inject)
				throw std::runtime_error("capture failed");
		};
		w.m_runController->runScene();
		const bool captureRunStarted = w.m_runController->m_worker;
		const bool captureRunEnded = runEnded();
		w.m_runController->m_beforeResultCapture = nullptr;
		w.m_runController->m_afterRunAction = nullptr;
		initial_variables.durationOverride = false;
		if (!captureRunStarted || !captureRunEnded || !deferredRan || w.m_runController->m_completedRun || !caseUsable())
			fail(QStringLiteral("a run whose results cannot be captured did not end without results, with the case usable and the "
								"waiting action done"));
		else if (w.statusBar()->currentMessage() != QStringLiteral("Simulation ended without results"))
			fail(QStringLiteral("a run whose results cannot be captured shows the status \"%1\"").arg(w.statusBar()->currentMessage()));

#ifndef Q_OS_MACOS
		// A run whose finish throws after the capture still releases its worker. The exception passes on to the guard of
		// the window, which reports it; on macOS it cannot leave the slot.
		const auto throwing = QObject::connect(w.m_runController, &RunController::runProgressEnded, w.m_runController, [inject] {
			if (inject)
				throw std::runtime_error("finish failed");
		});
		initial_variables.durationOverride = true;
		w.m_runController->runScene();
		const bool throwingRunStarted = w.m_runController->m_worker;
		const bool throwingRunEnded = runEnded();
		QObject::disconnect(throwing);
		initial_variables.durationOverride = false;
		if (!throwingRunStarted || !throwingRunEnded || !w.m_document->isEditable()
			|| !w.findChild<QAction*>(QStringLiteral("actionRun"))->isEnabled())
			fail(QStringLiteral("a run whose finish throws did not release its worker"));
#endif
	};

	// The third scene opens once the run has stopped. The folder that was chosen stays in force.
	const QString thirdFolder = outputFolderOf(run->base, run->thirdName);
	simulation.setNextRunOutputFolder(run->chosen.toStdString());
	if (!w.m_session->requestOpenScene(run->thirdPath)) {
		fail(QStringLiteral("the third scene was not accepted"));
		finish();
		return;
	}
	auto* poll = new QTimer(&w);
	QObject::connect(poll, &QTimer::timeout, &w, [&w, run, thirdFolder, fail, finish, checkFailedRuns, poll]() {
		if (w.m_runController->m_worker || w.m_document->mutableModel().name != run->thirdName) {
			if (++run->attempts < kMaxPolls)
				return;
			fail(QStringLiteral("the third scene did not open"));
			poll->stop();
			finish();
			return;
		}
		poll->stop();
		if (currentOutputFolder() == thirdFolder)
			fail(QStringLiteral("opening the third scene replaced the chosen folder with %1").arg(thirdFolder));
		w.m_runController->runScene();
		if (!w.m_runController->m_worker)
			fail(QStringLiteral("the run of the third scene did not start"));
		else if (currentOutputFolder() != QDir::cleanPath(run->chosen))
			fail(QStringLiteral("the run of the third scene writes to %1, not %2").arg(currentOutputFolder(), run->chosen));
		else if (!QDir(run->chosen + QStringLiteral("/TrainTrajectories")).exists())
			fail(QStringLiteral("the run of the third scene prepared no files in %1").arg(run->chosen));
		if (run->failures.isEmpty())
			checkFailedRuns();
		finish();
	});
	poll->start(kPollMs);
}
