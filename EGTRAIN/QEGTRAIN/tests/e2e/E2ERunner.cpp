#include "tests/e2e/MainWindowTestAccess.h"

#include "tests/e2e/E2ERunner.h"
#include "app/MainWindowTestHooks.h"
#include <QCoreApplication>
#include <QDir>
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

const Scenario kScenarios[] = {
	{"QEGTRAIN_E2E_VISUAL_POLISH", 2600, &MainWindowTestAccess::runVisualPolish},
	{"QEGTRAIN_E2E_STATION_OVERLAYS", 1000, &MainWindowTestAccess::runStationOverlay},
	// let the startup case load and the docks settle before the smoke runs
	{"QEGTRAIN_E2E_EDITOR_SMOKE", 1000, &MainWindowTestAccess::runEditorSmoke},
	{"QEGTRAIN_E2E_SCENE_RUN", 1000, &MainWindowTestAccess::runSceneRender},
	{"QEGTRAIN_E2E_TRACK_PREVIEW", 1000, &MainWindowTestAccess::runTrackPreview},
	{"QEGTRAIN_E2E_LEGACY_IMPORT", 1000, &MainWindowTestAccess::runLegacyImport},
	{"QEGTRAIN_E2E_SCENE_DROP", 100, &MainWindowTestAccess::runSceneDrop},
};

// QEGTRAIN_E2E_OPEN_REQUEST names a file that the run hands to the application as the system does
// when it opens a document with the program: a QFileOpenEvent. QEGTRAIN_E2E_OPEN_REQUEST_AT says
// when. At "launch", the default, the event comes as the window first shows, as when the system
// starts the program for the file. At "running" it comes once the start has settled, as when the
// program already runs.
constexpr const char* kOpenRequest = "QEGTRAIN_E2E_OPEN_REQUEST";
constexpr const char* kOpenRequestAt = "QEGTRAIN_E2E_OPEN_REQUEST_AT";
constexpr int kRunningOpenRequestDelayMs = 1000;
constexpr int kOpenRequestStatusDelayMs = 3000;

bool anyScenarioRequested() {
	for (const Scenario& scenario : kScenarios) {
		if (qEnvironmentVariableIsSet(scenario.variable))
			return true;
	}
	return qEnvironmentVariableIsSet(kOpenRequest);
}

void sendOpenRequest(MainWindow& window, const QString& file) {
	QFileOpenEvent event(QUrl::fromLocalFile(file));
	QCoreApplication::sendEvent(QCoreApplication::instance(), &event);
	std::fprintf(stdout, "E2E_OPEN_REQUEST_SENT\n");
	std::fflush(stdout);
	// The status line tells whether the file opened.
	QTimer::singleShot(kOpenRequestStatusDelayMs, &window, [&window] {
		std::fprintf(stdout, "E2E_OPEN_REQUEST_STATUS=%s\n", window.statusBar()->currentMessage().toUtf8().constData());
		std::fflush(stdout);
	});
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
	if (qEnvironmentVariable(kOpenRequestAt) == QLatin1String("running"))
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
	if (!openRequestAt.isEmpty() && openRequestAt != QLatin1String("launch") && openRequestAt != QLatin1String("running")) {
		std::cerr << "ERROR: QEGTRAIN_E2E_OPEN_REQUEST_AT must be launch or running.\n";
		return false;
	}
	mainWindowTestHooks().dialogsSuppressed = anyScenarioRequested();
	mainWindowTestHooks().afterFirstShow = &startScenarios;
	return true;
}

} // namespace e2e
