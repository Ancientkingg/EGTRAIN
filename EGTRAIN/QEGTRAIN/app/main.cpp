#include <QCoreApplication>
#include "app/DispatchController.h"
#include "app/MainWindow.h"
#include "scene/SceneBundle.h"
#include "scene/SceneValidator.h"
#include <algorithm>
#include "util/portability.h"
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStringList>

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

bool interactivePromptModeEnabled(int argc, char* argv[]) {
	return cmdOptionEntered(argv, argv + argc, "--interactive") ||
		cmdOptionEntered(argv, argv + argc, "-interactive");
}

void parseCmdOptions(int argc, char* argv[]) {
	// Keep the legacy questionnaire opt-in so normal launches open the GUI.
	const bool promptForMissingOptions = interactivePromptModeEnabled(argc, argv);
	const bool creatorAcceptance = qEnvironmentVariableIsSet("QEGTRAIN_E2E_CREATOR_ACCEPTANCE");

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

	// Traffic State Monitoring (share on port 5555 if enabled)
	if (cmdOptionEntered(argv, argv + argc, "-TSM")) {
		char* argument = getCmdOption(argv, argv + argc, "-TSM");
		if (argument) {
			initial_variables.TSM = std::atoi(argument);
		}
	} else if (promptForMissingOptions) {
		std::cout << "Do you want EGTRAIN to share the traffic state at port 5555 (1:share , 0 :do not share)?  :";
		std::cin >> initial_variables.TSM;
	} else {
		initial_variables.TSM = 0;
	}
	// RTTP (receive on port 5556 if enabled)
	if (cmdOptionEntered(argv, argv + argc, "-RC")) {
		char* argument = getCmdOption(argv, argv + argc, "-RC");
		if (argument) {
			initial_variables.RChoice = std::atoi(argument);
		}
	} else if (promptForMissingOptions) {
		std::cout << "Do you want EGTRAIN to share passengers' state with the Route Choice at port 5556 (1:share , 0 :do not share)?  :";
		std::cin >> initial_variables.RChoice;
	} else {
		initial_variables.RChoice = 0;
	}
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
		QDir(applicationDir).filePath("../../EGTRAIN/QEGTRAIN/Scenes/" + name)
	};
	for (const QString& candidate : candidates)
		if (QDir(candidate).exists())
			return QDir(candidate).absolutePath();
	return candidates.front();
}

QString resolveOutputDirectory(const std::string& sceneName) {
	QString base = qEnvironmentVariable("QEGTRAIN_OUTPUT_DIR");
	if (base.isEmpty()) {
		base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
		if (base.isEmpty())
			base = QDir::homePath() + "/EGTRAIN";
	}
	const QString suffix = QString::fromStdString(sceneOutputDirectoryComponent(sceneName));
	const QString output = QDir(base).filePath("Output/" + suffix);
	QDir().mkpath(output);
	return QDir(output).absolutePath();
}

void printSceneDiagnostics(const std::vector<SceneDiagnostic>& diagnostics) {
	for (const SceneDiagnostic& diagnostic : diagnostics)
		std::cerr << toDisplayText(diagnostic) << '\n';
}

Logger owl;
int main(int argc, char* argv[]) {
	QCoreApplication::setOrganizationName("EGTRAIN");
	QCoreApplication::setApplicationName("EGTRAIN");
	parseCmdOptions(argc, argv);

	const char* sceneArgument = getCmdOption(argv, argv + argc, "--scene");
	const bool sceneOption = cmdOptionEntered(argv, argv + argc, "--scene");
	if (sceneOption && (!sceneArgument || sceneArgument[0] == '-')) {
		std::cerr << "ERROR: --scene requires a scene path or case study.\n";
		return 1;
	}
	const std::string defaultName = initial_variables.name;
	const bool creatorAcceptance = qEnvironmentVariableIsSet("QEGTRAIN_E2E_CREATOR_ACCEPTANCE");
	if (!creatorAcceptance && defaultName.empty()) {
		std::cerr << "ERROR: Unknown case study id. Valid ids: 1 Netherlands, 2 Paimpol, 3 Copenhagen, 4 Milano_Brescia, 5 Assignment_Gvc_Gdg_Ut, 6 Lebanon.\n";
		return 1;
	}
	if (sceneOption)
		initial_variables.nArgProvided = true;

	const bool gui = initial_variables.GUI != 0;
	if (gui)
		std::cout << "Graphical user interface (GUI): 1\n";
	else
		std::cout << "Graphical user interface (GUI): 0\n";

	if (gui) {
		QApplication application(argc, argv);
		if (creatorAcceptance) {
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
			return application.exec();
		}
		const QString scenePath = resolveScenePath(
			sceneOption ? QString::fromLocal8Bit(sceneArgument) : QString(), defaultName);
		SceneLoadResult loaded = loadScenePath(scenePath.toStdString());
		if (!hasErrors(loaded.diagnostics)) {
			const std::vector<SceneDiagnostic> runnable = validateRunnableScene(loaded.scene);
			loaded.diagnostics.insert(loaded.diagnostics.end(), runnable.begin(), runnable.end());
		}
		if (hasErrors(loaded.diagnostics)) {
			printSceneDiagnostics(loaded.diagnostics);
			QMessageBox::critical(nullptr, "Cannot Start EGTRAIN",
								  QString::fromStdString(toDisplayText(loaded.diagnostics.front())));
			return 1;
		}
		initial_variables.OutputMainFolder = resolveOutputDirectory(loaded.scene.name).toStdString();
		InputMainFolder.clear();
		initial_variables.InputMainFolder.clear();
		MainWindow window;
		if (!window.openSceneDirectory(scenePath))
			return 1;
		window.openGUI();
		return application.exec();
	}

	QCoreApplication application(argc, argv);
	const QString scenePath = resolveScenePath(
		sceneOption ? QString::fromLocal8Bit(sceneArgument) : QString(), defaultName);
	SceneLoadResult loaded = loadScenePath(scenePath.toStdString());
	if (!hasErrors(loaded.diagnostics)) {
		const std::vector<SceneDiagnostic> runnable = validateRunnableScene(loaded.scene);
		loaded.diagnostics.insert(loaded.diagnostics.end(), runnable.begin(), runnable.end());
	}
	if (hasErrors(loaded.diagnostics)) {
		printSceneDiagnostics(loaded.diagnostics);
		return 1;
	}
	initial_variables.OutputMainFolder = resolveOutputDirectory(loaded.scene.name).toStdString();
	InputMainFolder.clear();
	initial_variables.InputMainFolder.clear();
	const std::vector<SceneDiagnostic> diagnostics = simulation.prepareScene(loaded.scene);
	printSceneDiagnostics(diagnostics);
	if (hasErrors(diagnostics))
		return 1;
	simulation.runSimulation();
	if (numRegions <= 0)
		return 1;
	simulation.printLastTrainServicePathDiagram();
	return 0;
}
