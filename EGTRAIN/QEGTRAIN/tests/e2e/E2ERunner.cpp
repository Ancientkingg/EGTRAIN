#include "tests/e2e/MainWindowTestAccess.h"

#include "tests/e2e/E2ERunner.h"
#include "app/MainWindowTestHooks.h"
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QSettings>
#include <QStatusBar>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <iostream>

namespace {

// A scenario starts delayMs after the first show of the window when its variable is set.
struct Scenario {
	const char* variable;
	int delayMs;
	void (*run)(MainWindow&);
};

constexpr const char* kVisualPolish = "QEGTRAIN_E2E_VISUAL_POLISH";
constexpr const char* kCreatorAcceptance = "QEGTRAIN_E2E_CREATOR_ACCEPTANCE";

const Scenario kScenarios[] = {
	{kVisualPolish, 2600, &MainWindowTestAccess::runVisualPolish},
	{"QEGTRAIN_E2E_STATION_OVERLAYS", 1000, &MainWindowTestAccess::runStationOverlay},
	// let the startup case load and the docks settle before the smoke runs
	{"QEGTRAIN_E2E_EDITOR_SMOKE", 1000, &MainWindowTestAccess::runEditorSmoke},
	{"QEGTRAIN_E2E_SCENE_RUN", 1000, &MainWindowTestAccess::runSceneRender},
	{"QEGTRAIN_E2E_TRACK_PREVIEW", 1000, &MainWindowTestAccess::runTrackPreview},
	{"QEGTRAIN_E2E_LEGACY_IMPORT", 1000, &MainWindowTestAccess::runLegacyImport},
	{"QEGTRAIN_E2E_SCENE_DROP", 100, &MainWindowTestAccess::runSceneDrop},
	{kCreatorAcceptance, 0, &MainWindowTestAccess::runCreatorAcceptance},
};

// QEGTRAIN_E2E_OPEN_REQUEST names a file that the run hands to the application as the system does
// when it opens a document with the program: a QFileOpenEvent. QEGTRAIN_E2E_OPEN_REQUEST_AT says
// when. At "launch", the default, the event comes as the window first shows, before the startup
// callback has run. At "startup" it comes a moment later, while the program still starts, as when
// the system starts the program for the file. At "chooser" it comes once the case chooser is open,
// from inside the chooser. At "running" it comes once the start has settled, as when the program
// already runs.
constexpr const char* kOpenRequest = "QEGTRAIN_E2E_OPEN_REQUEST";
constexpr const char* kOpenRequestAt = "QEGTRAIN_E2E_OPEN_REQUEST_AT";
constexpr int kStartupOpenRequestDelayMs = 50;
constexpr int kRunningOpenRequestDelayMs = 1000;
constexpr int kChooserPollMs = 20;
constexpr int kOpenRequestStatusDelayMs = 3000;

bool anyScenarioRequested() {
	for (const Scenario& scenario : kScenarios) {
		if (qEnvironmentVariableIsSet(scenario.variable))
			return true;
	}
	return qEnvironmentVariableIsSet(kOpenRequest);
}

const char* chooserState(const MainWindow& window) {
	return window.findChild<QDialog*>(QStringLiteral("caseChooserDialog")) ? "yes" : "no";
}

void sendOpenRequest(MainWindow& window, const QString& file) {
	// Whether the chooser is open when the event comes.
	std::fprintf(stdout, "E2E_OPEN_REQUEST_SENT chooser=%s\n", chooserState(window));
	std::fflush(stdout);
	QFileOpenEvent event(QUrl::fromLocalFile(file));
	QCoreApplication::sendEvent(QCoreApplication::instance(), &event);
	// The status line tells whether the file opened, and the chooser must have gone.
	QTimer::singleShot(kOpenRequestStatusDelayMs, &window, [&window] {
		std::fprintf(stdout, "E2E_OPEN_REQUEST_CHOOSER_AFTER=%s\n", chooserState(window));
		std::fprintf(stdout, "E2E_OPEN_REQUEST_STATUS=%s\n", window.statusBar()->currentMessage().toUtf8().constData());
		std::fflush(stdout);
	});
}

// Sends the event from the event loop of the chooser, as soon as the chooser is open.
void sendOpenRequestWhenChooserOpens(MainWindow& window, const QString& file) {
	auto* poll = new QTimer(&window);
	QObject::connect(poll, &QTimer::timeout, &window, [&window, poll, file] {
		if (!window.findChild<QDialog*>(QStringLiteral("caseChooserDialog")))
			return;
		poll->stop();
		poll->deleteLater();
		sendOpenRequest(window, file);
	});
	poll->start(kChooserPollMs);
}

void startScenarios(MainWindow& window) {
	for (const Scenario& scenario : kScenarios) {
		if (!qEnvironmentVariableIsSet(scenario.variable))
			continue;
		const auto run = scenario.run;
		QTimer::singleShot(scenario.delayMs, &window, [&window, run] { run(window); });
	}
	if (!qEnvironmentVariableIsSet(kOpenRequest))
		return;
	const QString file = qEnvironmentVariable(kOpenRequest);
	const QString at = qEnvironmentVariable(kOpenRequestAt);
	if (at == QLatin1String("chooser"))
		sendOpenRequestWhenChooserOpens(window, file);
	else if (at == QLatin1String("startup"))
		QTimer::singleShot(kStartupOpenRequestDelayMs, &window, [&window, file] { sendOpenRequest(window, file); });
	else if (at == QLatin1String("running"))
		QTimer::singleShot(kRunningOpenRequestDelayMs, &window, [&window, file] { sendOpenRequest(window, file); });
	else
		sendOpenRequest(window, file);
}

} // namespace

namespace e2e {

bool configureProcess() {
	const QString settingsDir = qEnvironmentVariable("QEGTRAIN_E2E_SETTINGS_DIR");
	if (!settingsDir.isEmpty()) {
		if (!QDir().mkpath(settingsDir)) {
			std::cerr << "ERROR: Could not create QEGTRAIN_E2E_SETTINGS_DIR.\n";
			return false;
		}
		QSettings::setDefaultFormat(QSettings::IniFormat);
		const QString absoluteSettingsDir = QDir(settingsDir).absolutePath();
		QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, absoluteSettingsDir);
		QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, absoluteSettingsDir);
	}
	const QString openRequestAt = qEnvironmentVariable(kOpenRequestAt);
	if (!openRequestAt.isEmpty() && openRequestAt != QLatin1String("launch") && openRequestAt != QLatin1String("startup")
		&& openRequestAt != QLatin1String("chooser") && openRequestAt != QLatin1String("running")) {
		std::cerr << "ERROR: QEGTRAIN_E2E_OPEN_REQUEST_AT must be launch, startup, chooser or running.\n";
		return false;
	}
	mainWindowTestHooks().dialogsSuppressed = anyScenarioRequested();
	// The creator acceptance run starts without a scene and drives the capacity scope dialog itself.
	if (wantsCreatorAcceptance()) {
		mainWindowTestHooks().suppressStartupPrompts = true;
		mainWindowTestHooks().askCapacityScope = true;
	}
	// The visual polish scenario also starts from the frames of a run.
	if (qEnvironmentVariableIsSet(kVisualPolish))
		mainWindowTestHooks().frameRendered = &MainWindowTestAccess::visualPolishFrame;
	mainWindowTestHooks().afterFirstShow = &startScenarios;
	return true;
}

bool wantsCreatorAcceptance() {
	return qEnvironmentVariableIsSet(kCreatorAcceptance);
}

int runCreatorAcceptanceMain() {
	const QString output = QFileInfo(qEnvironmentVariable("QEGTRAIN_E2E_OUT")).absoluteFilePath();
	if (output.isEmpty() || output == QFileInfo(QString()).absoluteFilePath()) {
		std::cerr << "ERROR: QEGTRAIN_E2E_OUT is required for creator acceptance.\n";
		return 1;
	}
	if (!QDir().mkpath(output)) {
		std::cerr << "ERROR: Could not create QEGTRAIN_E2E_OUT.\n";
		return 1;
	}
	initial_variables.OutputMainFolder = output.toStdString();
	InputMainFolder.clear();
	initial_variables.InputMainFolder.clear();
	MainWindow window;
	window.openGUI();
	return QCoreApplication::exec();
}

} // namespace e2e
