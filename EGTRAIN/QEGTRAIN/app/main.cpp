#include <QCoreApplication>
#include "app/DispatchController.h"
#include "app/MainWindow.h"
#include "app/OpenRequestRelay.h"
#include "app/SceneOpenRequest.h"
#include "app/SceneOutputDirectory.h"
#include "scene/SceneBundle.h"
#include "scene/SceneCompatibility.h"
#include "scene/SceneValidator.h"
#if EGTRAIN_BUILD_TESTS
#include "tests/e2e/E2ERunner.h"
#endif
#include "update/SelfUpdater.h"
#include "update/UpdateSettings.h"
#include "widgets/ConsoleWidget.h"
#include <algorithm>
#include <climits>
#include <optional>
#include "util/portability.h"
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QStringList>
#include <QTimer>

//
extern InitialParameters initial_variables;
extern int numRegions;
using namespace std;

// Logging logger(initial_variables.full_path, boost::log::trivial::trace,initial_variables.enabled_log,initial_variables.log_to_file);

// check if command line option was entered
bool cmdOptionEntered(char** begin, char** end, const std::string& option) {
	return std::find(begin, end, option) != end;
}

// get command line argument (-option value)
char* getCmdOption(char** begin, char** end, const std::string& option) {
	char** it = std::find(begin, end, option);
	if (it != end && ++it != end) {
		return *it;
	}
	return 0;
}

// Whether this is the creator acceptance run of a test build, which starts without a case study.
static bool creatorAcceptanceRun() {
#if EGTRAIN_BUILD_TESTS
	return e2e::wantsCreatorAcceptance();
#else
	return false;
#endif
}

// The options that take a value. It must name every option that is read with getCmdOption: the
// value of an option that is missing here would count as a file name.
static const QStringList optionsWithValue = {"-n", "-h", "-b", "-c", "-g", "-pax", "-TSM", "-RC", "--seed", "--scene", "--live", "--live-every"};

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
		std::cout << "Please enter ID number of Case study(1: Netherlands, 2: Paimpol, 3: Copenhagen, 4: Milano-Brescia, 5: Assignment, 6: Lebanon):";
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
			bufferTime = std::atof(argument);
			initial_variables.bufferTimeOverride = true;
		}
	}

	// recovery time
	if (cmdOptionEntered(argv, argv + argc, "-c")) {
		char* argument = getCmdOption(argv, argv + argc, "-c");
		if (argument) {
			recoveryTimePercentage = std::atof(argument);
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
			initial_variables.PAX_GUI = 0;
		}
	}

	// Traffic state: send it to tcp://127.0.0.1:5555 if enabled
	if (cmdOptionEntered(argv, argv + argc, "-TSM")) {
		char* argument = getCmdOption(argv, argv + argc, "-TSM");
		if (argument) {
			initial_variables.TSM = std::atoi(argument);
		}
	} else if (promptForMissingOptions) {
		std::cout << "Do you want EGTRAIN to send the traffic state to an external module that listens on port 5555 (1: send, 0: do not send)?  :";
		std::cin >> initial_variables.TSM;
	} else {
		initial_variables.TSM = 0;
	}
	// Route choice: send the passengers' state to tcp://127.0.0.1:5556 if enabled
	if (cmdOptionEntered(argv, argv + argc, "-RC")) {
		char* argument = getCmdOption(argv, argv + argc, "-RC");
		if (argument) {
			initial_variables.RChoice = std::atoi(argument);
		}
	} else if (promptForMissingOptions) {
		std::cout << "Do you want EGTRAIN to send the passengers' route choice requests to an external module that listens on port 5556 (1: send, 0: do not send)?  :";
		std::cin >> initial_variables.RChoice;
	} else {
		initial_variables.RChoice = 0;
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
// script started, in a message box.
static void reportCannotOpen(const QString& what, const QString& reason, bool showBox) {
	const QString message = QStringLiteral("Cannot open %1: %2").arg(what, reason);
	std::cerr << "ERROR: " << message.toStdString() << '\n';
	if (showBox && !updatesSuppressedByEnvironment())
		QMessageBox::warning(nullptr, "Cannot Open Case Study", message);
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
	const QStringList positional = positionalArguments(arguments, optionsWithValue);
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

Logger owl;
int main(int argc, char* argv[]) {
	beginStartupTiming();
	const std::string loggingProblems = installConsoleLogging();
	if (!loggingProblems.empty())
		std::cerr << loggingProblems;
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
		std::cerr << "ERROR: Unknown case study id. Valid ids: 1 Netherlands, 2 Paimpol, 3 Copenhagen, 4 Milano_Brescia, 5 Assignment_Gvc_Gdg_Ut, 6 Lebanon.\n";
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
		QApplication application(argc, argv);
		if (!updatesSuppressedByEnvironment()) {
			// The second sweep also removes the staging folder that a Windows update helper keeps while it runs.
			SelfUpdater::cleanupStaleStaging();
			QTimer::singleShot(20000, &application, []() { SelfUpdater::cleanupStaleStaging(); });
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
		const auto preload = [&effectiveDurationOverride](const QString& path) {
			const qint64 preloadStarted = startupTimingEnabled() ? startupTimingNowNanoseconds() : 0;
			SceneLoadResult loaded = loadScenePath(path.toStdString());
			if (startupTimingEnabled())
				recordStartupTiming(QStringLiteral("canonical_load"), 0, -1,
					startupTimingNowNanoseconds() - preloadStarted, QStringLiteral("startup_preload"),
					QStringLiteral("main"), true);
			if (!hasErrors(loaded.diagnostics)) {
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
			reportCannotOpen(scenePath, QString::fromStdString(toDisplayText(loaded.diagnostics.front())), true);
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
		InputMainFolder.clear();
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
	InputMainFolder.clear();
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
	if (numRegions <= 0)
		return 1;
	simulation.printLastTrainServicePathDiagram();
	return 0;
}
