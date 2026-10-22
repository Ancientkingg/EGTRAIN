#include <QApplication>
#include <QCoreApplication>
#include "app/CannotOpenMessage.h"
#include "app/CrashNote.h"
#include "app/DispatchController.h"
#include "app/GuardedApplication.h"
#include "app/MainWindow.h"
#include "app/OpenRequestRelay.h"
#include "app/SceneOpenRequest.h"
#include "app/SceneOutputDirectory.h"
#include "scene/SceneBundle.h"
#include "scene/SceneCompatibility.h"
#include "scene/SceneValidator.h"
#include "simulation/SimulationRun.h"
#if EGTRAIN_BUILD_TESTS
#include "tests/e2e/E2ERunner.h"
#endif
#include "update/SelfUpdater.h"
#include "update/UpdateSettings.h"
#include "update/UpdateTiming.h"
#include "util/Log.h"
#include "widgets/ConsoleWidget.h"
#include "widgets/LightPalette.h"
#include "widgets/WidgetStyle.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <exception>
#include <iomanip>
#include <iterator>
#include <optional>
#include "util/portability.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPalette>
#include <QStringList>
#include <QStyle>
#include <QStyleHints>
#include <QTimer>
#include <QToolTip>

using namespace std;

// The console output of this file is deliberate and does not go through the logging core, so no
// logging rule can hide it: the questions of --interactive, the errors of the options, the
// diagnostics of a scene and the lines that scripts read. tools/e2e/test_logging_contract.py
// counts it.

// check if command line option was entered
bool cmdOptionEntered(char** begin, char** end, const std::string& option) {
	return std::find(begin, end, option) != end;
}

// get command line argument (-option value)
char* getCmdOption(char** begin, char** end, const std::string& option) {
	char** it = std::find(begin, end, option);
	if (it != end && std::next(it) != end) {
		return *std::next(it); // NOLINT(clang-analyzer-security.ArrayBound): the option is followed by its value
	}
	return nullptr;
}

// EGTRAIN has one light look. A dark system palette would show through wherever a colour comes from the palette, such as
// selected rows, tooltips and charts, so the application then takes the light standard palette of its style. On macOS
// Info.plist keeps the light Aqua appearance for the native controls, menus and title bars.
static void keepLightPalette() {
	const QPalette palette = QApplication::palette();
	if (palette.color(QPalette::Window).lightness() >= palette.color(QPalette::WindowText).lightness())
		return;
	QPalette light = QApplication::style()->standardPalette();
	// A style can take its standard palette from the desktop as well, Fusion's too from Qt 6.5.
	if (light.color(QPalette::Window).lightness() < light.color(QPalette::WindowText).lightness())
		light = lightFusionPalette();
	QApplication::setPalette(light);
	QToolTip::setPalette(light);
}

/// The desktop can hand Qt its palette after start as well, as a dark XDG desktop portal does, so every new application
/// palette is checked again.
class LightPaletteKeeper : public QObject {
public:
	using QObject::QObject;
	/// Checks the palette once Qt has handled a change of the application palette, as Qt sets the tooltip palette of a
	/// new theme last.
	bool eventFilter(QObject* watched, QEvent* event) override {
		if (watched == QCoreApplication::instance() && event->type() == QEvent::ApplicationPaletteChange)
			QTimer::singleShot(0, this, &keepLightPalette);
		return QObject::eventFilter(watched, event);
	}
};

static void keepLightLook() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
	QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
#endif
	keepLightPalette();
	QCoreApplication::instance()->installEventFilter(new LightPaletteKeeper(QCoreApplication::instance()));
}

// Whether this is the creator acceptance run of a test build, which starts without a case study.
static bool creatorAcceptanceRun() {
#if EGTRAIN_BUILD_TESTS
	return e2e::wantsCreatorAcceptance();
#else
	return false;
#endif
}

// The options of the command line. The table drives --help and the check for unknown options. An option that
// takes a value names it in value; one without a value has nullptr there. An option that is read with
// getCmdOption must have a value, or the value would count as a file name.
struct CommandLineOption {
	const char* name;
	const char* value;
	const char* defaultText;
	const char* description;
};

static const CommandLineOption commandLineOptions[] = {
	{"-n", "ID", "1", "Case study number, 1 to 5"},
	{"--scene", "PATH", "the case study of -n", "Scene folder or .egscene file to load"},
	{"-h", "SECONDS", "from the scene", "Simulation horizon; this is not help"},
	{"-b", "SECONDS", "from the scene", "Buffer time"},
	{"-c", "PERCENT", "from the scene", "Recovery time"},
	{"-g", "0|1", "1", "0 runs without a window and exits when the run ends"},
	{"-pax", "0|1", "last choice of View > Show Passengers", "1 shows the passengers; read only when -g is 1"},
	{"--interactive", nullptr, "off", "Asks on standard input for -n, -g and -pax when they are left out"},
	{"-interactive", nullptr, "off", "Same as --interactive"},
	{"--detailed-trajectories", nullptr, "off", "Also writes the per-train files"},
	{"--live", "PORT", "off", "Publishes the run state on a ZeroMQ socket; 0 takes a free port"},
	{"--live-every", "N", "1", "With --live: sends a state frame every N steps"},
	{"--live-commands", nullptr, "off", "With --live: accepts pause, resume and stop commands"},
	{"--seed", "N", "789350715", "Seed of the random draws, 1 to 2147483646"},
	{"--help", nullptr, "off", "Prints this list and exits"},
};

static const CommandLineOption* findOption(const QString& name) {
	for (const CommandLineOption& option : commandLineOptions)
		if (name == QLatin1String(option.name))
			return &option;
	return nullptr;
}

static QStringList valueOptionNames() {
	QStringList names;
	for (const CommandLineOption& option : commandLineOptions)
		if (option.value)
			names.append(QLatin1String(option.name));
	return names;
}

static void printHelp() {
	std::cout << "Usage: QEGTRAIN [options] [file.egscene]\n\nOptions:\n";
	for (const CommandLineOption& option : commandLineOptions) {
		const std::string usage = option.value ? std::string(option.name) + ' ' + option.value : option.name;
		std::cout << "  " << std::left << std::setw(25) << usage << ' ' << option.description << " (default: " << option.defaultText << ")\n";
	}
}

// Reports the first option that is not in the table and returns false. It reads argv after the application object
// took its own options out. macOS adds -psn_* and -NS* to some launches.
static bool onlyKnownOptions(int argc, char* argv[]) {
	bool skipValue = false;
	for (int i = 1; i < argc; ++i) {
		const QString argument = QString::fromLocal8Bit(argv[i]);
		if (skipValue) {
			skipValue = false;
		} else if (argument.startsWith(QLatin1Char('-'))) {
			const CommandLineOption* option = findOption(argument);
			if (option) {
				skipValue = option->value != nullptr;
			} else if (!argument.startsWith(QLatin1String("-psn_")) && !argument.startsWith(QLatin1String("-NS"))) {
				std::cerr << "ERROR: Unknown option " << argument.toStdString() << ". Run QEGTRAIN --help to list the options.\n";
				return false;
			}
		}
	}
	return true;
}

void parseCmdOptions(int argc, char* argv[]) {
	// Keep the legacy questionnaire opt-in so normal launches open the GUI.
	const bool promptForMissingOptions = cmdOptionEntered(argv, argv + argc, "--interactive") || cmdOptionEntered(argv, argv + argc, "-interactive");
	const bool creatorAcceptance = creatorAcceptanceRun();

	// no options entered
	if (argc < 2) {
		std::cout << "No arguments. Using defaults (GUI mode, case study 1).\n";
	}

	// select case study
	if (creatorAcceptance) {
		initial_variables.name.clear();
	} else if (cmdOptionEntered(argv, argv + argc, "-n")) {
		char* argument = getCmdOption(argv, argv + argc, "-n");
		if (argument) {
			initial_variables.set_case(std::atoi(argument));
			initial_variables.nArgProvided = true;
		}
	} else if (promptForMissingOptions) {
		int case_study;
		std::cout << "Please enter ID number of Case study(1: Netherlands, 2: Paimpol, 3: Copenhagen, 4: Milano-Brescia, 5: Assignment):";
		std::cin >> case_study;
		initial_variables.set_case(case_study);
	} else {
		initial_variables.set_case(1); // Netherlands
	}

	// simulation horizon
	if (cmdOptionEntered(argv, argv + argc, "-h")) {
		char* argument = getCmdOption(argv, argv + argc, "-h");
		if (argument) {
			initial_variables.times = std::atoi(argument);
			initial_variables.durationOverride = true;
		}
	}

	// buffer time
	if (cmdOptionEntered(argv, argv + argc, "-b")) {
		char* argument = getCmdOption(argv, argv + argc, "-b");
		if (argument) {
			initial_variables.bufferTime = std::atof(argument);
			initial_variables.bufferTimeOverride = true;
		}
	}

	// recovery time
	if (cmdOptionEntered(argv, argv + argc, "-c")) {
		char* argument = getCmdOption(argv, argv + argc, "-c");
		if (argument) {
			initial_variables.recoveryTimePercentage = std::atof(argument);
			initial_variables.recoveryTimeOverride = true;
		}
	}

	// utilization of GUI
	if (cmdOptionEntered(argv, argv + argc, "-g")) {
		char* argument = getCmdOption(argv, argv + argc, "-g");
		if (argument) {
			initial_variables.GUI = std::atoi(argument);
		}
	} else if (promptForMissingOptions) {
		std::cout << "It seems you have not selected if you need a GUI. Please insert your choice [1: GUI , 0: no GUI] :";
		std::cin >> initial_variables.GUI;
	} else {
		initial_variables.GUI = 1;
	}

	// utilization of passenger GUI
	if (initial_variables.GUI) {
		if (cmdOptionEntered(argv, argv + argc, "-pax")) {
			char* argument = getCmdOption(argv, argv + argc, "-pax");
			if (argument) {
				initial_variables.PAX_GUI = std::atoi(argument);
			}
		} else if (promptForMissingOptions) {
			std::cout << "You have not selected if you want to use the Passenger GUI. Please insert your choice [1: Pax GUI on, 0: Pax GUI off]: ";
			std::cin >> initial_variables.PAX_GUI;
		} else {
			initial_variables.PAX_GUI = MainWindow::savedShowPassengers();
		}
	}

	initial_variables.exportDetailedTrajectories = cmdOptionEntered(argv, argv + argc, "--detailed-trajectories");
}

// Reads a whole number from low to high from the text of an option. Returns false when the text is none.
static bool readWholeNumber(const char* text, int low, int high, int& value) {
	bool valid = false;
	const int number = text ? QString::fromLocal8Bit(text).toInt(&valid) : 0;
	if (!valid || number < low || number > high)
		return false;
	value = number;
	return true;
}

// The live stream options: --live <port>, and with it --live-every <steps> and --live-commands. Prints the
// reason on standard error and returns false when an option is not valid.
static bool readLiveOptions(int argc, char* argv[]) {
	const bool everyEntered = cmdOptionEntered(argv, argv + argc, "--live-every");
	const bool commands = cmdOptionEntered(argv, argv + argc, "--live-commands");
	if (!cmdOptionEntered(argv, argv + argc, "--live")) {
		if (everyEntered || commands) {
			std::cerr << "ERROR: --live-every and --live-commands need --live.\n";
			return false;
		}
		return true;
	}
	if (!readWholeNumber(getCmdOption(argv, argv + argc, "--live"), 0, 65535, initial_variables.livePort)) {
		std::cerr << "ERROR: --live requires a port from 0 to 65535; 0 takes a free port.\n";
		return false;
	}
	if (everyEntered && !readWholeNumber(getCmdOption(argv, argv + argc, "--live-every"), 1, INT_MAX, initial_variables.liveEvery)) {
		std::cerr << "ERROR: --live-every requires a whole number of 1 or more.\n";
		return false;
	}
	initial_variables.liveCommands = commands;
	return true;
}

// Ends the live stream when main returns, on every path, before the log and the other globals go.
struct LiveStreamGuard {
	~LiveStreamGuard() { simulation.stopLive(); }
};

// Starts the live stream when --live was given. The endpoint file goes to the output folder. A stream that
// cannot start is reported on standard error and, in a window launch that no script started, in a message box.
static bool startLiveStream(const std::string& outputFolder, bool showBox) {
	if (initial_variables.livePort < 0)
		return true;
	std::string error;
	if (!simulation.startLive(initial_variables.livePort, initial_variables.liveEvery, initial_variables.liveCommands, outputFolder, &error)) {
		const QString message = QStringLiteral("Cannot start the live stream: %1").arg(QString::fromStdString(error));
		std::cerr << "ERROR: " << message.toStdString() << '\n';
		if (showBox && !updatesSuppressedByEnvironment())
			QMessageBox::critical(nullptr, "Cannot Start EGTRAIN", message);
		return false;
	}
	std::cerr << "Live stream on " << simulation.liveEndpoint() << "; the endpoints are in " << outputFolder << "/live-endpoint.json\n";
	return true;
}

QString resolveScenePath(const QString& requested, const std::string& defaultName) {
	if (!requested.isEmpty())
		return QFileInfo(requested).absoluteFilePath();
	const QString name = QString::fromStdString(defaultName);
	const QString applicationDir = QCoreApplication::applicationDirPath();
	const QStringList candidates = {
		QDir(applicationDir).filePath("Scenes/" + name),
		QDir(applicationDir).filePath("../Resources/Scenes/" + name),
		QDir(applicationDir).filePath("../share/EGTRAIN/Scenes/" + name),
		QDir(applicationDir).filePath("../../EGTRAIN/QEGTRAIN/Scenes/" + name)};
	for (const QString& candidate : candidates)
		if (QDir(candidate).exists())
			return QDir(candidate).absolutePath();
	return candidates.front();
}

QString resolveOutputDirectory(const std::string& sceneName) {
	const QString output = sceneOutputDirectory(sceneName);
	QDir().mkpath(output);
	return output;
}

void printSceneDiagnostics(const std::vector<SceneDiagnostic>& diagnostics) {
	for (const SceneDiagnostic& diagnostic : diagnostics)
		std::cerr << toDisplayText(diagnostic) << '\n';
}

// A scene that cannot be opened is reported on standard error and, in a window launch that no
// script started, in a message box. A scene that could not be read gets the message that the window shows for it;
// a scene of another version or one that cannot run keeps the plain error.
static void reportCannotOpen(const QString& what, const QString& reason, bool showBox, bool damaged = false) {
	const QString message = QStringLiteral("Cannot open %1: %2").arg(what, reason);
	std::cerr << "ERROR: " << message.toStdString() << '\n';
	if (!showBox || updatesSuppressedByEnvironment())
		return;
	if (damaged)
		QMessageBox::critical(nullptr, cannot_open::title(), cannot_open::text(what, reason));
	else
		QMessageBox::warning(nullptr, cannot_open::title(), message);
}

// The scene that the command line names. The arguments come from the application object, not
// from the narrow argv, so a file name that is not in the local code page stays intact.
// --scene wins over a file name.
struct SceneRequest {
	QString path;
	bool positional = false; // path came from a file name argument
	bool rejected = false;	 // a file name argument was reported and path is empty
};

static SceneRequest readSceneRequest(bool sceneOption, bool showBox) {
	SceneRequest request;
	const QStringList arguments = QCoreApplication::arguments();
	if (sceneOption) {
		request.path = optionValue(arguments, QStringLiteral("--scene"));
		return request;
	}
	const QStringList positional = positionalArguments(arguments, valueOptionNames());
	if (positional.isEmpty())
		return request;
	const SceneOpenChoice choice = classifyOpenPaths(positional);
	if (!choice.rejection.isEmpty()) {
		reportCannotOpen(positional.join(QStringLiteral(", ")), choice.rejection, showBox);
		request.rejected = true;
		return request;
	}
	request.path = choice.path;
	request.positional = true;
	return request;
}

static int runMain(int argc, char* argv[]) {
	beginStartupTiming();
	const std::string loggingProblems = installConsoleLogging();
	if (!loggingProblems.empty())
		std::cerr << loggingProblems;
	// Before the application object exists, so it works without a display.
	if (cmdOptionEntered(argv, argv + argc, "--help")) {
		printHelp();
		return 0;
	}
	// Before --interactive asks anything and before the value can count as a file name.
	if (cmdOptionEntered(argv, argv + argc, "-TSM") || cmdOptionEntered(argv, argv + argc, "-RC")) {
		std::cerr << "ERROR: -TSM and -RC were removed. Use --live <port> for the live stream.\n";
		return 1;
	}
	QCoreApplication::setOrganizationName("EGTRAIN");
	QCoreApplication::setApplicationName("EGTRAIN");
	QCoreApplication::setApplicationVersion(QStringLiteral(EGTRAIN_APP_VERSION));
#if EGTRAIN_BUILD_TESTS
	if (!e2e::configureProcess())
		return 1;
#endif
	parseCmdOptions(argc, argv);

	if (cmdOptionEntered(argv, argv + argc, "--seed")) {
		const char* seedArgument = getCmdOption(argv, argv + argc, "--seed");
		bool seedValid = false;
		const qlonglong seed = seedArgument ? QString::fromLocal8Bit(seedArgument).toLongLong(&seedValid) : 0;
		if (!seedValid || seed < 1 || seed > static_cast<qlonglong>(kMaxRandomSeed)) {
			std::cerr << "ERROR: --seed requires a whole number from 1 to " << kMaxRandomSeed << ".\n";
			return 1;
		}
		initial_variables.randomSeed = static_cast<unsigned long>(seed);
	}
	if (!readLiveOptions(argc, argv))
		return 1;

	const char* sceneArgument = getCmdOption(argv, argv + argc, "--scene");
	const bool sceneOption = cmdOptionEntered(argv, argv + argc, "--scene");
	if (sceneOption && (!sceneArgument || sceneArgument[0] == '-')) {
		std::cerr << "ERROR: --scene requires a scene path or case study.\n";
		return 1;
	}
	if (startupTimingEnabled() && !sceneOption) {
		std::cerr << "ERROR: QEGTRAIN_STARTUP_TIMING requires an explicit --scene.\n";
		return 1;
	}
	const std::string defaultName = initial_variables.name;
	const bool creatorAcceptance = creatorAcceptanceRun();
	if (!creatorAcceptance && defaultName.empty()) {
		std::cerr << "ERROR: Unknown case study id. Valid ids: 1 Netherlands, 2 Paimpol, 3 Copenhagen, 4 Milano_Brescia, 5 Assignment_Gvc_Gdg_Ut.\n";
		return 1;
	}
	if (sceneOption)
		initial_variables.nArgProvided = true;

	const bool gui = initial_variables.GUI != 0;
	const std::optional<double> effectiveDurationOverride = initial_variables.durationOverride
		? std::optional<double>(initial_variables.times)
		: std::nullopt;
	if (gui)
		std::cout << "Graphical user interface (GUI): 1\n";
	else
		std::cout << "Graphical user interface (GUI): 0\n";

	if (gui) {
		const bool styleChosen = widgetStyleChosen(argc, argv);
		GuardedApplication application(argc, argv);
		// Before the palettes, because a new style brings its own.
		preferWindowsVistaStyle(styleChosen);
#if EGTRAIN_BUILD_TESTS
		e2e::configureApplication();
#endif
		keepLightLook();
		// Before the first message box, so that a scene that cannot be opened is reported in the look of the window.
		QFile theme(QStringLiteral(":/styles/egtrain.qss"));
		if (theme.open(QIODevice::ReadOnly | QIODevice::Text))
			application.setStyleSheet(QString::fromUtf8(theme.readAll()));
		installCrashHandler(crashNoteFolder());
		if (!onlyKnownOptions(argc, argv))
			return 1;
		if (!updatesSuppressedByEnvironment()) {
			// The second sweep also removes the staging folder that a Windows update helper keeps while it runs,
			// and the backup of the previous installation. The first sweep leaves the backup: the helper may still
			// need it while it watches this start (update/UpdateTiming.h).
			SelfUpdater::cleanupStaleStaging();
			QTimer::singleShot(std::chrono::milliseconds(kSecondSweepDelayMs), &application, []() {
				SelfUpdater::cleanupStaleStaging();
				SelfUpdater::removeUpdateBackup();
			});
		}
#if EGTRAIN_BUILD_TESTS
		if (creatorAcceptance)
			return e2e::runCreatorAcceptanceMain();
#endif
		const SceneRequest request = readSceneRequest(sceneOption, true);
		// A launch that only names a file, as the system starts it, hands the file to the instance that
		// runs, when there is one.
		const bool relayAllowed = OpenRequestRelay::allowedByEnvironment();
		if (relayAllowed && request.positional && QCoreApplication::arguments().size() == 2
			&& OpenRequestRelay::forward(OpenRequestRelay::defaultServerName(), OpenRequestRelay::currentApplicationDir(), request.path))
			return 0;
		bool loadFailed = false; // the last preload could not read the scene
		const auto preload = [&effectiveDurationOverride, &loadFailed](const QString& path) {
			const qint64 preloadStarted = startupTimingEnabled() ? startupTimingNowNanoseconds() : 0;
			SceneLoadResult loaded = loadScenePath(path.toStdString());
			if (startupTimingEnabled())
				recordStartupTiming(QStringLiteral("canonical_load"), 0, -1,
					startupTimingNowNanoseconds() - preloadStarted, QStringLiteral("startup_preload"),
					QStringLiteral("main"), true);
			loadFailed = hasErrors(loaded.diagnostics);
			if (!loadFailed) {
				const std::vector<SceneDiagnostic> runnable = validateRunnableScene(loaded.scene, {},
					effectiveDurationOverride);
				loaded.diagnostics.insert(loaded.diagnostics.end(), runnable.begin(), runnable.end());
			}
			return loaded;
		};
		QString scenePath = resolveScenePath(request.path, defaultName);
		const SceneCompatibilityProbeResult compatibility = probeSceneCompatibility(scenePath.toStdString());
		// --scene hands a scene of another version to the window without loading it. The window explains the
		// rejection in its dialogs and offers a copy upgrade when a migration is registered.
		const bool windowReports = !request.positional
			&& (isRejectedForVersion(compatibility) || compatibility.classification == SceneCompatibilityClass::OlderMigratable);
		SceneLoadResult loaded;
		if (windowReports)
			printSceneDiagnostics(compatibility.diagnostics);
		else if (isRejectedForVersion(compatibility))
			loaded.diagnostics = compatibility.diagnostics;
		else
			loaded = preload(scenePath);
		bool openedFromFileName = request.positional;
		if (openedFromFileName && hasErrors(loaded.diagnostics)) {
			// A file name that does not load is reported and the usual start follows.
			printSceneDiagnostics(loaded.diagnostics);
			reportCannotOpen(scenePath, QString::fromStdString(toDisplayText(loaded.diagnostics.front())), true, loadFailed);
			openedFromFileName = false;
			scenePath = resolveScenePath(QString(), defaultName);
			loaded = preload(scenePath);
		}
		if (hasErrors(loaded.diagnostics)) {
			printSceneDiagnostics(loaded.diagnostics);
			QMessageBox::critical(nullptr, "Cannot Start EGTRAIN",
				QString::fromStdString(toDisplayText(loaded.diagnostics.front())));
			return 1;
		}
		// An opened file counts as an explicit scene, so the chooser stays closed.
		if (openedFromFileName)
			initial_variables.nArgProvided = true;
		if (!windowReports) {
			setStartupTimingPreloadIdentity(scenePath, loaded);
			initial_variables.OutputMainFolder = resolveOutputDirectory(loaded.scene.name).toStdString();
		}
		initial_variables.InputMainFolder.clear();
		if (!startLiveStream(windowReports ? resolveOutputDirectory(defaultName).toStdString() : initial_variables.OutputMainFolder, true))
			return 2;
		const LiveStreamGuard liveStream;
		MainWindow window;
		if (startupTimingEnabled())
			recordStartupTiming(QStringLiteral("application_initialization"), 0, -1,
				startupTimingNowNanoseconds(), QStringLiteral("main_to_mainwindow_constructed"),
				QStringLiteral("main"), true, true);
		if (!window.openSceneDirectory(scenePath))
			return 1;
		// The window connects to the relay when it first shows. Requests wait until the event loop runs.
		OpenRequestRelay relay(&window);
		window.openGUI();
		if (relayAllowed)
			relay.listen(OpenRequestRelay::defaultServerName(), OpenRequestRelay::currentApplicationDir());
		return application.exec();
	}

	QCoreApplication application(argc, argv);
	if (!onlyKnownOptions(argc, argv))
		return 1;
	const SceneRequest request = readSceneRequest(sceneOption, false);
	if (request.rejected)
		return 1;
	const QString scenePath = resolveScenePath(request.path, defaultName);
	const SceneCompatibilityProbeResult compatibility = probeSceneCompatibility(scenePath.toStdString());
	if (isRejectedForVersion(compatibility)) {
		printSceneDiagnostics(compatibility.diagnostics);
		return 1;
	}
	SceneLoadResult loaded = loadScenePath(scenePath.toStdString());
	if (!hasErrors(loaded.diagnostics)) {
		const std::vector<SceneDiagnostic> runnable = validateRunnableScene(loaded.scene, {},
			effectiveDurationOverride);
		loaded.diagnostics.insert(loaded.diagnostics.end(), runnable.begin(), runnable.end());
	}
	if (hasErrors(loaded.diagnostics)) {
		printSceneDiagnostics(loaded.diagnostics);
		return 1;
	}
	initial_variables.OutputMainFolder = resolveOutputDirectory(loaded.scene.name).toStdString();
	initial_variables.InputMainFolder.clear();
	if (!startLiveStream(initial_variables.OutputMainFolder, false))
		return 2;
	const LiveStreamGuard liveStream;
	const std::vector<SceneDiagnostic> diagnostics = simulation.prepareScene(loaded.scene);
	printSceneDiagnostics(diagnostics);
	if (hasErrors(diagnostics))
		return 1;
	// No window reads the snapshots of this run.
	simulation.setSnapshotsEnabled(false);
	simulation.runSimulation();
	if (simulation.run().numTrains <= 0 || !simulation.lastRunError().empty())
		return 1;
	// The line that scripts read for a completed run, without a newline. It does not go through the logging core,
	// so no logging rule hides it.
	if (simulation.lastRunCompleted())
		std::cout << "\n End of Simulation";
	simulation.printLastTrainServicePathDiagram();
	return 0;
}

// An exception that nothing else catches, for example at startup before the event loop runs, is logged and ends the
// program with exit code 70.
int main(int argc, char* argv[]) {
	try {
		return runMain(argc, argv);
	} catch (const std::exception& error) {
		EG_LOG(AppUi, Error) << "EGTRAIN stopped because of an unexpected error: " << error.what() << '\n';
		eglog::flush();
		return 70;
	}
}
