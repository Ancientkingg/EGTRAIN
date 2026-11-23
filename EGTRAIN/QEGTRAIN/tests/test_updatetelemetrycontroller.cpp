#include "app/UpdateTelemetryController.h"

#include "app/MainWindowTestHooks.h"
#include "support/StderrCapture.h"
#include "update/ReleaseInfo.h"
#include "update/SelfUpdater.h"
#include "update/UpdateChecker.h"
#include "update/UpdateSettings.h"
#include "util/Log.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QEventLoop>
#include <QMenu>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QPushButton>
#include <QScopeGuard>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <optional>
#include <vector>

static SceneDiagnostic errorWithCode(const char* code) {
	SceneDiagnostic diagnostic;
	diagnostic.severity = SceneSeverity::Error;
	diagnostic.code = code;
	return diagnostic;
}

static SceneDiagnostic warningWithCode(const char* code) {
	SceneDiagnostic diagnostic = errorWithCode(code);
	diagnostic.severity = SceneSeverity::Warning;
	return diagnostic;
}

// Runs the event loop for a while, so that a dialog that was wrongly opened would be answered.
static void waitFor(int milliseconds) {
	QEventLoop loop;
	QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
	loop.exec();
}

// Presses the button with this text of the first message box that shows, and counts the boxes.
static void pressInMessageBox(int& shown, const QString& buttonText) {
	shown = 0;
	QTimer::singleShot(50, qApp, [&shown, buttonText] {
		for (QWidget* widget : QApplication::topLevelWidgets()) {
			auto* box = qobject_cast<QMessageBox*>(widget);
			if (!box || !box->isVisible())
				continue;
			++shown;
			for (QAbstractButton* button : box->buttons()) {
				if (button->text() == buttonText)
					button->click();
			}
			break;
		}
	});
}

// Reads the default button of the first message box that shows and closes the box with Later.
static void readDefaultButton(QString& defaultText) {
	defaultText.clear();
	QTimer::singleShot(50, qApp, [&defaultText] {
		for (QWidget* widget : QApplication::topLevelWidgets()) {
			auto* box = qobject_cast<QMessageBox*>(widget);
			if (!box || !box->isVisible())
				continue;
			if (QAbstractButton* button = box->defaultButton())
				defaultText = button->text();
			for (QAbstractButton* button : box->buttons()) {
				if (button->text() == "Later")
					button->click();
			}
			break;
		}
	});
}

// The default button of the prompt that offers this release, or an empty text when no prompt opened.
static QString defaultButtonOfAvailablePrompt(UpdateChecker& checker, const StableRelease& release) {
	QString defaultText;
	readDefaultButton(defaultText);
	UpdateCheckResult result;
	result.success = true;
	result.release = release;
	emit checker.finished(result);
	return defaultText;
}

static UpdateTelemetryController* startedController = nullptr;

static bool refuseTelemetry(UpdateTelemetryController& controller) {
	startedController = &controller;
	return false;
}

class TestUpdateTelemetryController : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanup();
	void mapsDiagnosticErrors();
	void mapsEveryErrorCodeOrNamesItAsUnmapped();
	void updateActionsAndFirstStartPrompt();
	void refusedStartLeavesTelemetryOff();
	void promptNamesItsDefault();
	void suppressedUpdateFailureIsWritten();
	void updateEndsLoopWithoutClosingWindows();

private:
	QTemporaryDir m_settingsDir;
};

void TestUpdateTelemetryController::initTestCase() {
	QVERIFY2(m_settingsDir.isValid(), "no temporary directory");
	// The settings of the controller are the ones of the test, and an environment that suppresses updates is lifted.
	QCoreApplication::setOrganizationName("EGTRAIN-test");
	QCoreApplication::setApplicationName("test_updatetelemetrycontroller");
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
	QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, m_settingsDir.path());
	const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	for (const QString& key : environment.keys()) {
		if (key.startsWith("QEGTRAIN_"))
			qunsetenv(key.toLocal8Bit().constData());
	}
}

// A failed check ends its slot before the slot takes its hooks and the sink back.
void TestUpdateTelemetryController::cleanup() {
	mainWindowTestHooks().telemetryStart = nullptr;
	mainWindowTestHooks().dialogsSuppressed = false;
	eglog::setSink({});
	SelfUpdater::setTestHook(SelfUpdateCapability(), QString());
}

void TestUpdateTelemetryController::mapsDiagnosticErrors() {
	const auto error = [](std::vector<SceneDiagnostic> diagnostics) { return telemetryDiagnosticError(diagnostics); };
	QVERIFY2(!error({}), "no diagnostic maps to no error");
	QVERIFY2(!error({warningWithCode("scene.json.parse")}), "a warning maps to no error");
	QVERIFY2(!error({errorWithCode("scene.save.write")}), "an error whose code is ambiguous maps to no error");
	QVERIFY2(error({errorWithCode("scene.version.unsupported")}) == telemetry::Error::UnsupportedFormat,
		"an unsupported version is an unsupported format");
	QVERIFY2(error({errorWithCode("scene.json.parse")}) == telemetry::Error::InvalidInput, "a parse error is an invalid input");
	for (const char* code : {"scene.demand.total", "scene.demand.id.conflict", "scene.passenger.dwell.threshold.invalid", "scene.passenger.transfers.invalid",
			 "scene.passengers.results"})
		QVERIFY2(error({errorWithCode(code)}) == telemetry::Error::InvalidInput, code);
	QVERIFY2(error({errorWithCode("scene.bundle.file.read")}) == telemetry::Error::IoFailure,
		"a file that cannot be read is an input failure");
	QVERIFY2(error({errorWithCode("scene.bundle.temp"), errorWithCode("scene.json.parse")}) == telemetry::Error::InvalidInput,
		"an invalid input outranks a failure to read");
	QVERIFY2(error({errorWithCode("scene.json.parse"), errorWithCode("scene.units.unsupported"), errorWithCode("scene.bundle.temp")})
			== telemetry::Error::UnsupportedFormat,
		"an unsupported format outranks the other errors");
}

// The codes of the errors that the sources report are each either mapped by telemetryDiagnosticError() or named here. A
// new code that is in neither fails, so that whoever adds it decides whether it is invalid input.
void TestUpdateTelemetryController::mapsEveryErrorCodeOrNamesItAsUnmapped() {
	// Codes under these prefixes are not invalid input: the failures of an export, which writes what the scene holds,
	// and the checks of the legacy import and of the native builder, whose input the telemetry does not classify.
	const QStringList unmappedPrefixes{"scene.export.", "scene.import.", "scene.native."};
	// Codes that no rule maps today. Mapping one changes what the telemetry records, so it is a decision of its own.
	const QSet<QString> unmapped{"scene.passenger.walking.invalid", "scene.scenario.file.missing", "scene.scenario.root.type",
		"scene.train.capacity"};
	const QRegularExpression report(QStringLiteral("\\berror\\(\\s*\"(scene\\.[a-z0-9_.]+)\"|SceneSeverity::Error,\\s*\"(scene\\.[a-z0-9_.]+)\""));
	QSet<QString> codes;
	const QString root = QDir(QStringLiteral(EGTRAIN_TEST_SOURCE_DIR)).filePath("..");
	for (const char* folder : {"scene", "simulation", "app"}) {
		QDirIterator files(QDir(root).filePath(folder), {"*.cpp"}, QDir::Files, QDirIterator::Subdirectories);
		while (files.hasNext()) {
			QFile file(files.next());
			QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
			const QString text = QString::fromUtf8(file.readAll());
			for (auto match = report.globalMatch(text); match.hasNext();) {
				const QRegularExpressionMatch found = match.next();
				codes.insert(found.captured(found.capturedStart(1) >= 0 ? 1 : 2));
			}
		}
	}
	QVERIFY2(codes.size() > 100, "the error codes were not found in the sources");
	for (const QString& code : codes) {
		if (unmapped.contains(code) || std::any_of(unmappedPrefixes.begin(), unmappedPrefixes.end(), [&code](const QString& prefix) { return code.startsWith(prefix); }))
			continue;
		const std::vector<SceneDiagnostic> diagnostics{errorWithCode(code.toUtf8().constData())};
		QVERIFY2(telemetryDiagnosticError(diagnostics).has_value(),
			qPrintable(code + " is not in the invalid input codes of telemetryDiagnosticError()"));
	}
}

void TestUpdateTelemetryController::updateActionsAndFirstStartPrompt() {
	QWidget window;
	UpdateTelemetryController controller(&window, [] { return true; }, [] {});
	// a controller starts without a sender
	QCOMPARE(controller.window(), &window);
	QVERIFY(!controller.sender());
	controller.stopTelemetry();
	controller.startUpdateCheck(true);
	controller.maybePromptForUpdateChecks();

	controller.setupUpdateActions(nullptr);
	QVERIFY2(!window.findChild<QAction*>("actionCheckForUpdates"), "a controller without a Help menu has no update entries");

	QMenu help;
	controller.setupUpdateActions(&help);
	auto* check = window.findChild<QAction*>("actionCheckForUpdates");
	auto* automatic = window.findChild<QAction*>("actionAutomaticallyCheckForUpdates");
	// the update entries follow a separator in the Help menu
	QVERIFY(check);
	QVERIFY(automatic);
	QCOMPARE(help.actions().size(), 3);
	QVERIFY(help.actions().at(0)->isSeparator());
	QCOMPARE(help.actions().at(1), check);
	QCOMPARE(help.actions().at(2), automatic);
	// automatic checks are off until the user chose them
	QVERIFY(automatic);
	QVERIFY(automatic->isCheckable());
	QVERIFY(!automatic->isChecked());

	automatic->setChecked(true);
	QSettings settings;
	QVERIFY2(readUpdateCheckState(settings) == UpdateCheckState::Enabled, "turning automatic checks on is saved");
	automatic->setChecked(false);
	QSettings later;
	QVERIFY2(readUpdateCheckState(later) == UpdateCheckState::Disabled, "turning automatic checks off is saved");

	int shown = 0;
	pressInMessageBox(shown, "Don't Check Automatically");
	{
		QSettings unknown;
		writeUpdateCheckState(unknown, UpdateCheckState::Unknown);
		unknown.sync();
	}
	controller.maybePromptForUpdateChecks();
	QSettings answered;
	// the first start asks about automatic checks and saves the answer
	QCOMPARE(shown, 1);
	QCOMPARE(readUpdateCheckState(answered), UpdateCheckState::Disabled);

	pressInMessageBox(shown, "Don't Check Automatically");
	controller.maybePromptForUpdateChecks();
	// Checks that no prompt opens within this time.
	waitFor(150);
	QVERIFY2(shown == 0, "a saved answer is not asked again");
}

void TestUpdateTelemetryController::refusedStartLeavesTelemetryOff() {
	QWidget window;
	UpdateTelemetryController controller(&window, [] { return true; }, [] {});
	mainWindowTestHooks().telemetryStart = &refuseTelemetry;
	controller.startTelemetryAfterInitialConsent();
	mainWindowTestHooks().telemetryStart = nullptr;
	// a start that the scripted run refuses leaves telemetry off
	QCOMPARE(startedController, &controller);
	QVERIFY(!controller.sender());
}

// Enter must never turn off the update checks, so the prompt names its default.
void TestUpdateTelemetryController::promptNamesItsDefault() {
	QCoreApplication::setApplicationVersion("1.0.0");
	QWidget window;
	UpdateTelemetryController controller(&window, [] { return true; }, [] {});
	QMenu help;
	controller.setupUpdateActions(&help);
	auto* checker = controller.findChild<UpdateChecker*>();
	QVERIFY2(checker, "the update entries make a checker");
	const QString key = "windows-x64-installer";
	const QString package = "EGTRAIN-Setup-1.2.3-x64.exe";
	StableRelease release;
	release.version = SemanticVersion{1, 2, 3};
	release.tag = "v1.2.3";
	release.releasePage = QUrl("https://egtrain.samuelbruin.com/releases/1.2.3/");
	release.notes = "Notes";

	SelfUpdateCapability unsupported;
	unsupported.reason = "Not packaged.";
	SelfUpdater::setTestHook(unsupported, "http://127.0.0.1");
	QVERIFY2(defaultButtonOfAvailablePrompt(*checker, release) == "Open Release Page",
		"the prompt without an update package makes Open Release Page the default");

	SelfUpdateCapability supported;
	supported.supported = true;
	supported.manifestKey = key;
	SelfUpdater::setTestHook(supported, "http://127.0.0.1");
	release.assets.append({"update-manifest.json", QUrl("http://127.0.0.1/v1.2.3/update-manifest.json")});
	release.assets.append({package, QUrl("http://127.0.0.1/v1.2.3/" + package)});
	QVERIFY2(defaultButtonOfAvailablePrompt(*checker, release) == "Update and Restart",
		"the prompt with an update package makes Update and Restart the default");
	SelfUpdater::setTestHook(SelfUpdateCapability(), QString());
}

// In a scripted run an update that fails shows no box: it writes the message to stderr and logs it as a warning in the
// category update. An automatic check that fails is logged there too.
void TestUpdateTelemetryController::suppressedUpdateFailureIsWritten() {
	QWidget window;
	UpdateTelemetryController controller(&window, [] { return true; }, [] {});
	QMenu help;
	controller.setupUpdateActions(&help);
	auto* updater = controller.findChild<SelfUpdater*>();
	auto* checker = controller.findChild<UpdateChecker*>();
	QVERIFY2(updater, "the update entries make an updater");
	QVERIFY2(checker, "the update entries make a checker");
	QStringList warnings;
	eglog::setSink([&warnings](eglog::Category category, eglog::Level level, const std::string& text) {
		if (category == eglog::Category::Update && level == eglog::Level::Warning)
			warnings << QString::fromStdString(text);
	});
	mainWindowTestHooks().dialogsSuppressed = true;
	QTemporaryDir folder;
	QVERIFY(folder.isValid());
	int shown = 0;
	pressInMessageBox(shown, "OK");
	StderrCapture capture(folder.filePath("stderr.txt"));
	emit updater->finished(false, "The package could not be unpacked.");
	const QString written = capture.text();
	// Checks that no box opens within this time.
	waitFor(150);
	// the failure shows no box
	QCOMPARE(shown, 0);
	// the failure is logged as a warning in the category update
	QCOMPARE(warnings, QStringList("EGTRAIN Update: The package could not be unpacked.\n"));
#if !defined(_WIN32)
	// the message goes to stderr
	QVERIFY2(written.contains("QEGTRAIN_DIALOG_SUPPRESSED: EGTRAIN Update: The package could not be unpacked.\n"),
		qPrintable(written));
#endif

	warnings.clear();
	UpdateCheckResult failed;
	failed.error = "No connection.";
	emit checker->finished(failed);
	// an automatic check that fails is logged as a warning in the category update
	QCOMPARE(warnings, QStringList("Automatic update check failed: No connection.\n"));
}

// A window that asks about unsaved changes and stays open gets no close event when an update ends the application.
// The slot runs the event loop of the application, so it is the last one.
void TestUpdateTelemetryController::updateEndsLoopWithoutClosingWindows() {
	class StayOpenWindow : public QWidget {
	public:
		int closeEvents = 0;

	protected:
		void closeEvent(QCloseEvent* event) override {
			++closeEvents;
			event->ignore();
		}
	};
	StayOpenWindow window;
	window.show();
	int beforeQuit = 0;
	UpdateTelemetryController controller(&window, [] { return true; }, [&beforeQuit] { ++beforeQuit; });
	QTimer::singleShot(0, &controller, &UpdateTelemetryController::quitForUpdate);
	// Ends the event loop with another code when the update does not end it.
	QTimer fallback;
	fallback.setSingleShot(true);
	QObject::connect(&fallback, &QTimer::timeout, [] { QCoreApplication::exit(1); });
	fallback.start(2000);
	const int exitCode = QCoreApplication::exec();
	// an update runs beforeQuit and ends the event loop without closing the windows
	QCOMPARE(exitCode, 0);
	QCOMPARE(window.closeEvents, 0);
	QCOMPARE(beforeQuit, 1);
}

QTEST_MAIN(TestUpdateTelemetryController)
#include "test_updatetelemetrycontroller.moc"
