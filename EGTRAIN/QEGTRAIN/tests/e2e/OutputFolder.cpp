#include "tests/e2e/MainWindowTestAccess.h"

#include "app/DispatchController.h"
#include "scene/SceneBundle.h"
#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <memory>
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

} // namespace

// The folder that a run writes to follows the scene that the window holds. It creates a new case and opens a
// second scene in the window, renames that scene in Case Settings and runs it, then picks a folder as Choose
// Output Folder does and opens a third scene, which must leave that folder in force. QEGTRAIN_OUTPUT_DIR names
// the base folder. It prints E2E_OUTPUT_FOLDER_OK and exits with 0, or prints the failures and exits with 2.
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
		if (w.m_worker && w.m_workerThread && w.m_workerThread->isRunning()) {
			QObject::connect(w.m_workerThread, &QThread::finished, qApp, [code]() { QCoreApplication::exit(code); });
			w.m_worker->requestStop();
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
	if (!w.m_caseNameEdit) {
		fail(QStringLiteral("Case Settings has no name field"));
		finish();
		return;
	}
	w.m_caseNameEdit->setText(QString::fromStdString(run->renamedName));
	w.commitCaseSettings();
	if (w.m_document->mutableModel().name != run->renamedName) {
		fail(QStringLiteral("the second scene was not renamed"));
		finish();
		return;
	}
	w.runScene();
	if (!w.m_worker) {
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

	// The third scene opens once the run has stopped. The folder that was chosen stays in force.
	const QString thirdFolder = outputFolderOf(run->base, run->thirdName);
	simulation.setNextRunOutputFolder(run->chosen.toStdString());
	if (!w.m_session->requestOpenScene(run->thirdPath)) {
		fail(QStringLiteral("the third scene was not accepted"));
		finish();
		return;
	}
	auto* poll = new QTimer(&w);
	QObject::connect(poll, &QTimer::timeout, &w, [&w, run, thirdFolder, fail, finish, poll]() {
		if (w.m_worker || w.m_document->mutableModel().name != run->thirdName) {
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
		w.runScene();
		if (!w.m_worker)
			fail(QStringLiteral("the run of the third scene did not start"));
		else if (currentOutputFolder() != QDir::cleanPath(run->chosen))
			fail(QStringLiteral("the run of the third scene writes to %1, not %2").arg(currentOutputFolder(), run->chosen));
		else if (!QDir(run->chosen + QStringLiteral("/TrainTrajectories")).exists())
			fail(QStringLiteral("the run of the third scene prepared no files in %1").arg(run->chosen));
		finish();
	});
	poll->start(kPollMs);
}
