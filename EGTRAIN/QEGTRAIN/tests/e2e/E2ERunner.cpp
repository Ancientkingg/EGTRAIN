#include "tests/e2e/MainWindowTestAccess.h"

#include "app/DispatchController.h"
#include "diagrams/DiagramWindow.h"
#include "simulation/RollingStock.h"
#include "tests/e2e/E2ERunner.h"
#include "app/MainWindowTestHooks.h"
#include "telemetry/TelemetryConsent.h"
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QPalette>
#include <QSettings>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QToolTip>
#include <QUrl>
#include <algorithm>
#include <cmath>
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
constexpr const char* kDarkPalette = "QEGTRAIN_E2E_DARK_PALETTE";
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
	{"QEGTRAIN_E2E_OUTPUT_FOLDER", 1000, &MainWindowTestAccess::runOutputFolder},
	{"QEGTRAIN_E2E_RECOVERY", 1000, &MainWindowTestAccess::runRecovery},
	{"QEGTRAIN_E2E_CAPTURES", 1000, &MainWindowTestAccess::runCaptures},
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

// The variables of a run that goes on without a scenario and has no user to answer a dialog.
constexpr const char* kRunVariables[] = {"QEGTRAIN_AUTOSTART", "QEGTRAIN_E2E_EXPORT_DIR", "QEGTRAIN_E2E_ROUTE_DIAGRAM",
	"QEGTRAIN_E2E_OPERATIONAL_COMPLETION"};

bool anyScenarioRequested() {
	for (const Scenario& scenario : kScenarios) {
		if (qEnvironmentVariableIsSet(scenario.variable))
			return true;
	}
	for (const char* variable : kRunVariables) {
		if (qEnvironmentVariableIsSet(variable))
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

// QEGTRAIN_E2E_PRIVACY_DIALOG is 1 for the first prompt of the privacy dialog and settings for the saved choices. The
// dialog is then a preview, and the settings are the isolated ones of QEGTRAIN_E2E_SETTINGS_DIR.
bool privacyPreviewRequested() {
	const QString mode = qEnvironmentVariable("QEGTRAIN_E2E_PRIVACY_DIALOG");
	return (mode == QLatin1String("1") || mode == QLatin1String("settings"))
		&& !qEnvironmentVariable("QEGTRAIN_E2E_SETTINGS_DIR").isEmpty();
}

// The context of a preview is fake: no sender exists and the address is never contacted.
bool previewPrivacy(TelemetryContext& context, bool& initialPrompt) {
	context = {true, true, true, QStringLiteral("https://127.0.0.1/consent-preview"), QStringLiteral("1")};
	if (qEnvironmentVariable("QEGTRAIN_E2E_PRIVACY_DIALOG") == QLatin1String("settings"))
		initialPrompt = false;
	return true;
}

bool startNoTelemetry(UpdateTelemetryController&) {
	return false;
}

// The contrast ratio of two colours as WCAG 2 defines it.
double contrast(const QColor& first, const QColor& second) {
	const auto luminance = [](const QColor& color) {
		const auto channel = [](qreal value) { return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4); };
		return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
	};
	const double one = luminance(first);
	const double other = luminance(second);
	return (std::max(one, other) + 0.05) / (std::min(one, other) + 0.05);
}

// After a start with the dark palette of QEGTRAIN_E2E_DARK_PALETTE, and the same palette once more after start, the
// application must keep its light look: light fields with dark text, a chart in light colours, readable selected rows
// and a light tooltip.
void checkLightLook() {
	QStringList failures;
	const QPalette palette = QApplication::palette();
	DiagramWindow diagram(QStringLiteral("Light look"));
	auto* chart = new QChart;
	diagram.setChart(chart);
	const QColor chartBase = chart->backgroundBrush().color();
	if (chartBase.lightness() < 200 || contrast(chartBase, chart->titleBrush().color()) < 4.5)
		failures << QStringLiteral("the chart is dark: background %1, text %2").arg(chartBase.name(), chart->titleBrush().color().name());
	if (palette.color(QPalette::Window).lightness() < 200 || palette.color(QPalette::Base).lightness() < 200)
		failures << QStringLiteral("the palette is dark: window %1, base %2")
						.arg(palette.color(QPalette::Window).name(), palette.color(QPalette::Base).name());
	if (contrast(palette.color(QPalette::Base), palette.color(QPalette::Text)) < 4.5)
		failures << QStringLiteral("text on the base colour is not readable");
	if (contrast(palette.color(QPalette::Highlight), palette.color(QPalette::HighlightedText)) < 3.0)
		failures << QStringLiteral("selected rows are not readable: %1 on %2")
						.arg(palette.color(QPalette::HighlightedText).name(), palette.color(QPalette::Highlight).name());
	const QColor tipColor = QToolTip::palette().color(QPalette::ToolTipBase);
	if (tipColor.lightness() < 200)
		failures << QStringLiteral("the tooltip is dark: %1").arg(tipColor.name());
	if (failures.isEmpty())
		std::fprintf(stdout, "E2E_LIGHT_LOOK_OK\n");
	else
		std::fprintf(stdout, "E2E_LIGHT_LOOK_FAIL: %s\n", failures.join(QStringLiteral("; ")).toUtf8().constData());
	std::fflush(stdout);
}

void startScenarios(MainWindow& window) {
	MainWindowTestAccess::connectRunSignals(window);
	// The scripts of Dialogs.cpp act on the dialogs for which the variables of their scenario are set.
	mainWindowTestHooks().dialogShown = [&window](QDialog& dialog, const char* kind) {
		MainWindowTestAccess::dialogShown(window, dialog, kind);
	};
	mainWindowTestHooks().dialogClosed = [&window](QDialog& dialog, const char* kind) {
		MainWindowTestAccess::dialogClosed(window, dialog, kind);
	};
	// A window without a scene has no run to start.
	if (!mainWindowTestHooks().suppressStartupPrompts)
		MainWindowTestAccess::autostart(window);
	// A desktop can also hand over its dark palette after start, as a dark XDG desktop portal does.
	if (qEnvironmentVariableIsSet(kDarkPalette))
		QTimer::singleShot(0, &window, [&window] {
			e2e::configureApplication();
			QTimer::singleShot(0, &window, [] { checkLightLook(); });
		});
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
	mainWindowTestHooks().snapshotDelivered = &MainWindowTestAccess::snapshotDelivered;
	mainWindowTestHooks().snapshotDrawn = &MainWindowTestAccess::checkSignalHeads;
	mainWindowTestHooks().runFinished = &MainWindowTestAccess::runFinished;
	mainWindowTestHooks().routeDiagramBuilt = &MainWindowTestAccess::routeDiagramBuilt;
	// A script that answers the run review needs it to open although dialogs are suppressed.
	mainWindowTestHooks().askRunReview = qEnvironmentVariableIsSet("QEGTRAIN_E2E_REVIEW_CANCEL");
	if (privacyPreviewRequested()) {
		mainWindowTestHooks().privacyPreview = &previewPrivacy;
		mainWindowTestHooks().telemetryStart = &startNoTelemetry;
	}
#ifdef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_TELEMETRY_SMOKE")) {
		if (!mainWindowTestHooks().telemetryStart)
			mainWindowTestHooks().telemetryStart = &MainWindowTestAccess::startTelemetrySmoke;
		mainWindowTestHooks().observationStep = &MainWindowTestAccess::telemetryObservationStep;
		mainWindowTestHooks().windowDestroyed = &MainWindowTestAccess::telemetryWindowDestroyed;
	}
#endif
	mainWindowTestHooks().afterFirstShow = &startScenarios;
	return true;
}

void configureApplication() {
	if (!qEnvironmentVariableIsSet(kDarkPalette))
		return;
	// A dark palette with the selection and tooltip colours of the Breeze Dark scheme, which a KDE desktop hands to Qt.
	QPalette dark(QColor(0x31, 0x36, 0x3b));
	dark.setColor(QPalette::Highlight, QColor(0x3d, 0xae, 0xe9));
	dark.setColor(QPalette::HighlightedText, Qt::white);
	dark.setColor(QPalette::ToolTipBase, QColor(0x31, 0x36, 0x3b));
	dark.setColor(QPalette::ToolTipText, QColor(0xef, 0xf0, 0xf1));
	QApplication::setPalette(dark);
	QToolTip::setPalette(dark);
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
	// Like a folder from Choose Output Folder, this one stays in force when the window opens or creates a scene.
	initial_variables.OutputMainFolder = output.toStdString();
	simulation.setNextRunOutputFolder(output.toStdString());
	initial_variables.InputMainFolder.clear();
	MainWindow window;
	window.openGUI();
	return QCoreApplication::exec();
}

} // namespace e2e
