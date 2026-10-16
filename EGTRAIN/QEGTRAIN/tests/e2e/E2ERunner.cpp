#include "tests/e2e/MainWindowTestAccess.h"

#include "tests/e2e/E2ERunner.h"
#include "app/MainWindowTestHooks.h"
#include <QDir>
#include <QSettings>
#include <QString>
#include <QTimer>
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

bool anyScenarioRequested() {
	for (const Scenario& scenario : kScenarios) {
		if (qEnvironmentVariableIsSet(scenario.variable))
			return true;
	}
	return false;
}

void startScenarios(MainWindow& window) {
	for (const Scenario& scenario : kScenarios) {
		if (!qEnvironmentVariableIsSet(scenario.variable))
			continue;
		const auto run = scenario.run;
		QTimer::singleShot(scenario.delayMs, &window, [&window, run] { run(window); });
	}
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
	mainWindowTestHooks().dialogsSuppressed = anyScenarioRequested();
	mainWindowTestHooks().afterFirstShow = &startScenarios;
	return true;
}

} // namespace e2e
