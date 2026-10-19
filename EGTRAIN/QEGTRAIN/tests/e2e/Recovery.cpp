#include "tests/e2e/MainWindowTestAccess.h"

#include "recovery/RecoveryController.h"
#include "scene/SceneBundle.h"
#include "scene/SceneHistory.h"
#include "simulation/SimulationWorker.h"
#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QStatusBar>
#include <QString>
#include <QThread>
#include <QTimer>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {

constexpr const char* kEditedName = "Recovery smoke case";
constexpr const char* kEditedDescription = "Recovery smoke description";
constexpr const char* kEditedBaseTime = "09:15:30";
constexpr int kWaitMs = 15000;
constexpr int kRunWaitMs = 180000;

bool waitFor(const std::function<bool()>& done, int milliseconds = kWaitMs) {
	QElapsedTimer waited;
	waited.start();
	while (!done() && waited.elapsed() < milliseconds) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QThread::msleep(5);
	}
	return done();
}

void printLine(const QString& text) {
	std::fprintf(stdout, "%s\n", text.toUtf8().constData());
	std::fflush(stdout);
}

// Ends the process as a crash does: no destructor runs and no close event reaches the window.
[[noreturn]] void crash() {
	printLine(QStringLiteral("E2E_RECOVERY_CRASH"));
	std::_Exit(137);
}

// Ends the process with an exception that nothing catches, thrown on another thread as a failed run would throw it.
void throwOnThread() {
	printLine(QStringLiteral("E2E_RECOVERY_CRASH"));
	std::thread([] { throw std::runtime_error("injected crash"); }).join();
}

// Prints and closes what the start shows about a crash of the last run: the notice, or the recovery prompt with the crash
// note when QEGTRAIN_E2E_RECOVERY_ANSWER leaves the prompt open. The line printed ends with the path of the note.
void closeCrashDialogs() {
	for (QWidget* widget : QApplication::topLevelWidgets()) {
		if (!widget->isVisible())
			continue;
		auto* notice = qobject_cast<QMessageBox*>(widget);
		if (notice && notice->objectName() == QLatin1String("crashNotice")) {
			printLine(QStringLiteral("E2E_RECOVERY_CRASH_NOTICE ") + notice->text().section(QLatin1Char('\n'), 1, 1));
			notice->button(QMessageBox::Ok)->click();
		} else if (widget->objectName() == QLatin1String("recoveryPrompt")) {
			const auto* note = widget->findChild<QLabel*>(QStringLiteral("recoveryCrashNote"));
			printLine(QStringLiteral("E2E_RECOVERY_CRASH_NOTE ") + (note ? note->text().section(QLatin1Char('\n'), 1, 1) : QString()));
			if (auto* later = widget->findChild<QPushButton*>(QStringLiteral("recoveryLaterButton")))
				later->click();
		}
	}
}

// Clicks the button of the message box that opens in the next moments.
void answerMessageBox(QObject* context, QMessageBox::StandardButton button) {
	QTimer::singleShot(25, context, [button]() {
		for (QWidget* widget : QApplication::topLevelWidgets()) {
			auto* box = qobject_cast<QMessageBox*>(widget);
			if (!box || !box->isVisible())
				continue;
			if (QAbstractButton* clicked = box->button(button))
				clicked->click();
			break;
		}
	});
}

// Accepts the file dialog that opens in the next moments with the file at path. The folder and the file name that it
// offered are written to start.
void acceptFileDialog(QObject* context, const QString& path, const std::shared_ptr<QString>& start) {
	auto* poll = new QTimer(context);
	QObject::connect(poll, &QTimer::timeout, context, [poll, path, start]() {
		QFileDialog* dialog = nullptr;
		for (QWidget* widget : QApplication::topLevelWidgets()) {
			auto* candidate = qobject_cast<QFileDialog*>(widget);
			if (candidate && candidate->isVisible())
				dialog = candidate;
		}
		if (!dialog)
			return;
		poll->stop();
		poll->deleteLater();
		QLineEdit* name = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
		*start = QStringLiteral("dir=%1|name=%2").arg(dialog->directory().absolutePath(), name ? name->text() : QString());
		const QFileInfo target(path);
		dialog->setDirectory(target.absolutePath());
		dialog->selectFile(target.fileName());
		if (name)
			name->setText(target.fileName());
		QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
	});
	poll->start(25);
}

} // namespace

// The crash and recovery runs. QEGTRAIN_E2E_RECOVERY names the script: edit-then-crash, edit-then-throw, new-then-crash,
// open-then-segv, close-after-discard, start-again or fail-to-autosave. QEGTRAIN_RECOVERY_DIR names the folder for the copies, and the script of
// tools/e2e/recovery_smoke.py that starts the runs decides their order. A script that ends normally prints E2E_RECOVERY_OK and
// exits with 0; one that finds something wrong prints E2E_RECOVERY_FAIL and exits with 2.
void MainWindowTestAccess::runRecovery(MainWindow& w) {
	if (scenarioStarted)
		return;
	scenarioStarted = true;

	// Three edits of the case settings, each as the editor commits it.
	const auto editThreeTimes = [&w]() {
		w.m_caseNameEdit->setText(kEditedName);
		w.commitPendingCaseSettings();
		w.m_caseDescriptionEdit->setText(kEditedDescription);
		w.commitPendingCaseSettings();
		w.m_caseBaseTimeEdit->setText(kEditedBaseTime);
		w.commitPendingCaseSettings();
	};
	// Waits until the autosave has written the copy of the newest edit.
	const auto flushAutosave = [&w]() {
		return waitFor([&w] { return w.m_recovery->isIdle(); });
	};

	// The window ends with edits that its autosave has written, as a crash would leave it.
	const auto crashAfterEdits = [&](bool newCase, bool uncaught) -> QString {
		if (newCase) {
			w.m_session->newScene();
			if (!w.m_document->isDirty() || !w.m_session->m_sceneDir.isEmpty())
				return QStringLiteral("the new case was not created");
		} else if (w.m_document->isDirty()) {
			return QStringLiteral("the scene had edits before the script made its own");
		}
		editThreeTimes();
		if (!flushAutosave())
			return QStringLiteral("the autosave did not write the copy");
		printLine(QStringLiteral("E2E_RECOVERY_WRITTEN"));
		if (uncaught)
			throwOnThread();
		crash();
	};

	// The window closes after the user chose Discard, which must leave no copy.
	const auto closeAfterDiscard = [&]() -> QString {
		editThreeTimes();
		if (!flushAutosave())
			return QStringLiteral("the autosave did not write the copy");
		answerMessageBox(&w, QMessageBox::Discard);
		w.close();
		if (w.isVisible())
			return QStringLiteral("the window did not close after Discard");
		return {};
	};

	// What the window holds after the start, and Save As of a recovered scene when QEGTRAIN_E2E_RECOVERY_SAVE_AS names a file.
	// The window closes at the end, which it can only do without a question when the scene is saved.
	const auto startAgain = [&]() -> QString {
		// A prompt that opened has been answered before the script runs when QEGTRAIN_E2E_RECOVERY_ANSWER is set; otherwise
		// closeCrashDialogs closes it.
		closeCrashDialogs();
		waitFor([] { return QApplication::activeModalWidget() == nullptr; });
		const bool opened = w.m_session->m_sceneDir.isEmpty() && w.m_document->isDirty() && !w.m_history->isAtSavedPoint() && !w.m_history->canUndo();
		printLine(QStringLiteral("E2E_RECOVERY_STATE prompted=%1|opened=%2|name=%3|description=%4|base_time=%5|hint=%6")
				.arg(recoveryPromptSeen ? 1 : 0)
				.arg(opened ? 1 : 0)
				.arg(QString::fromStdString(w.m_document->mutableModel().name), QString::fromStdString(w.m_document->mutableModel().description),
					QString::fromStdString(w.m_document->mutableModel().baseTime), w.m_recoveredSourceHint));
		const QString saveAs = qEnvironmentVariable("QEGTRAIN_E2E_RECOVERY_SAVE_AS");
		if (opened) {
			if (saveAs.isEmpty())
				return QStringLiteral("a recovered scene is open and the script names no file for Save As");
			const QString target = QFileInfo(saveAs).absoluteFilePath();
			const auto start = std::make_shared<QString>();
			acceptFileDialog(&w, target, start);
			const bool saved = w.m_session->saveSceneToCurrentDir();
			printLine(QStringLiteral("E2E_RECOVERY_SAVE_AS_START %1").arg(*start));
			const SceneLoadResult reloaded = loadScenePath(target.toStdString());
			if (!saved || w.m_document->isDirty() || QFileInfo(w.m_session->m_sceneDir).absoluteFilePath() != target || !w.m_session->m_sceneIsBundle
				|| !w.m_recoveredSourceHint.isEmpty() || reloaded.scene.name != w.m_document->mutableModel().name)
				return QStringLiteral("Save As did not leave a saved scene at the file that was chosen");
			printLine(QStringLiteral("E2E_RECOVERY_SAVED name=%1").arg(QString::fromStdString(reloaded.scene.name)));
		}
		if (!w.close() || w.isVisible())
			return QStringLiteral("the window did not close");
		return {};
	};

	// The folder for the copies cannot be made. The window says so once, and the editing and a run go on.
	const auto failToAutosave = [&]() -> QString {
		int unavailable = 0;
		const QMetaObject::Connection counter = QObject::connect(w.statusBar(), &QStatusBar::messageChanged,
			[&unavailable](const QString& message) { unavailable += message.startsWith(QLatin1String("Autosave unavailable")) ? 1 : 0; });
		editThreeTimes();
		const bool flushed = flushAutosave();
		w.m_caseNameEdit->setText(QStringLiteral("Recovery smoke case again"));
		w.commitPendingCaseSettings();
		const bool flushedAgain = flushAutosave();
		QObject::disconnect(counter);
		if (!flushed || !flushedAgain)
			return QStringLiteral("the autosave did not finish");
		if (unavailable != 1)
			return QStringLiteral("the window reported the failed autosave %1 times").arg(unavailable);
		if (!w.m_document->isDirty() || w.m_document->mutableModel().name != "Recovery smoke case again")
			return QStringLiteral("the failed autosave stopped the editing");
		w.m_runController->runScene();
		if (!w.m_runController->m_worker)
			return QStringLiteral("the run did not start");
		if (!waitFor([&w] { return !w.m_runController->m_worker && w.m_runController->m_resultsAvailable; }, kRunWaitMs))
			return QStringLiteral("the run did not complete");
		return {};
	};

	const QString script = qEnvironmentVariable("QEGTRAIN_E2E_RECOVERY");
	QString failure;
	if (!w.m_recovery)
		failure = QStringLiteral("the window keeps no copies; QEGTRAIN_RECOVERY_DIR is missing");
	else if (script == QLatin1String("edit-then-crash"))
		failure = crashAfterEdits(false, false);
	else if (script == QLatin1String("edit-then-throw"))
		failure = crashAfterEdits(false, true);
	else if (script == QLatin1String("new-then-crash"))
		failure = crashAfterEdits(true, false);
	else if (script == QLatin1String("open-then-segv")) {
		printLine(QStringLiteral("E2E_RECOVERY_CRASH"));
		std::raise(SIGSEGV);
	} else if (script == QLatin1String("close-after-discard"))
		failure = closeAfterDiscard();
	else if (script == QLatin1String("start-again"))
		failure = startAgain();
	else if (script == QLatin1String("fail-to-autosave"))
		failure = failToAutosave();
	else
		failure = QStringLiteral("unknown script ") + script;
	if (!failure.isEmpty()) {
		std::fprintf(stderr, "E2E_RECOVERY_FAIL: %s\n", failure.toUtf8().constData());
		std::fflush(stderr);
		QCoreApplication::exit(2);
		return;
	}
	printLine(QStringLiteral("E2E_RECOVERY_OK"));
	QCoreApplication::exit(0);
}
