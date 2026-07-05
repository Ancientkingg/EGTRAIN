#include <QCoreApplication>
#include "app/DispatchController.h"
#include "app/MainWindow.h"
#include "io/geocoding.h"
#include <algorithm>
#include "util/portability.h"
#include <QDir>
#include <QStandardPaths>

#define PORT_NUMBER 9002

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

void parseCmdOptions(int argc, char* argv[]) {
	// When launched as a macOS .app bundle, stdin is not a terminal.
	// Skip interactive prompts and use sensible defaults so the GUI can open.
	bool interactive = isatty(STDIN_FILENO);

	if (argc < 2 && !interactive) {
		initial_variables.set_case(1);  // Netherlands
		initial_variables.GUI = 1;
		initial_variables.PAX_GUI = 0;
		initial_variables.TSM = 0;
		initial_variables.RChoice = 0;
		return;
	}

	// no options entered
	if (argc < 2) {
		std::cout << "No arguments inserted. \n";
	}

	// select case study
	if (cmdOptionEntered(argv, argv + argc, "-n")) {
		char* argument = getCmdOption(argv, argv + argc, "-n");
		if (argument) {
			initial_variables.set_case(std::atoi(argument));
			initial_variables.nArgProvided = true;
		}
	} else {
		int case_study;
		std::cout << "Please enter ID number of Case study(1: Netherlands, 2: Paimpol, 3: Copenhagen, 4: Brescia, 5: Assignment):";
		std::cin >> case_study;
		initial_variables.set_case(case_study);
	}

	// simulation horizon
	if (cmdOptionEntered(argv, argv + argc, "-h")) {
		char* argument = getCmdOption(argv, argv + argc, "-h");
		if (argument) {
			initial_variables.times = std::atoi(argument);
		}

	} else {
		char answer;
		do {
			std::cout << "The duration of the simulation of the " << initial_variables.name << " is " << initial_variables.times << " seconds.\n";
			std::cout << "Do you want to change it (y:yes, n:no): ";
			std::cin >> answer;
		} while (!cin.fail() && answer != 'y' && answer != 'n');
		if (answer == 'n' || answer == 'N')
			std::cout << "Simulation period set to " << initial_variables.times << '\n';
		else {
			std::cout << "Specify simulation period length[in seconds]:";
			std::cin >> initial_variables.times;
		}
	}

	// time update interval (dispatching tool)
	if (cmdOptionEntered(argv, argv + argc, "-i")) {
		char* argument = getCmdOption(argv, argv + argc, "-i");
		if (argument) {
			dispatchingTool->timeUpdateInterval = std::atoi(argument);
		}
	}

	// # tracklines
	if (cmdOptionEntered(argv, argv + argc, "-t")) {
		char* argument = getCmdOption(argv, argv + argc, "-t");
		if (argument) {
			numTrackLines = std::atoi(argument);
		}
	}

	// # routes
	if (cmdOptionEntered(argv, argv + argc, "-r")) {
		char* argument = getCmdOption(argv, argv + argc, "-r");
		if (argument) {
			N_Routes = std::atoi(argument);
		}
	}

	// buffer time
	if (cmdOptionEntered(argv, argv + argc, "-b")) {
		char* argument = getCmdOption(argv, argv + argc, "-b");
		if (argument) {
			bufferTime = std::atof(argument);
		}
	}

	// recovery time
	if (cmdOptionEntered(argv, argv + argc, "-c")) {
		char* argument = getCmdOption(argv, argv + argc, "-c");
		if (argument) {
			recoveryTimePercentage = std::atof(argument);
		}
	}

	// utilization of GUI
	if (cmdOptionEntered(argv, argv + argc, "-g")) {
		char* argument = getCmdOption(argv, argv + argc, "-g");
		if (argument) {
			initial_variables.GUI = std::atoi(argument);
		}
	} else {
		std::cout << "It seems you have not selected if you need a GUI. Please insert your choice [1: GUI , 0: no GUI] :";
		std::cin >> initial_variables.GUI;
	}

	// utilization of passenger GUI
	if (initial_variables.GUI) {
		if (cmdOptionEntered(argv, argv + argc, "-pax")) {
			char* argument = getCmdOption(argv, argv + argc, "-pax");
			if (argument) {
				initial_variables.PAX_GUI = std::atoi(argument);
			}
		} else {
			std::cout << "You have not selected if you want to use the Passenger GUI. Please insert your choice [1: Pax GUI on, 0: Pax GUI off]: ";
			std::cin >> initial_variables.PAX_GUI;
		}
	}

	// Traffic State Monitoring (share on port 5555 if enabled)
	if (cmdOptionEntered(argv, argv + argc, "-TSM")) {
		char* argument = getCmdOption(argv, argv + argc, "-TSM");
		if (argument) {
			initial_variables.TSM = std::atoi(argument);
		}
	} else {
		std::cout << "Do you want EGTRAIN to share the traffic state at port 5555 (1:share , 0 :do not share)?  :";
		std::cin >> initial_variables.TSM;
	}
	// RTTP (receive on port 5556 if enabled)
	if (cmdOptionEntered(argv, argv + argc, "-RC")) {
		char* argument = getCmdOption(argv, argv + argc, "-RC");
		if (argument) {
			initial_variables.RChoice = std::atoi(argument);
		}
	} else {
		std::cout << "Do you want EGTRAIN to share passengers' state with the Route Choice at port 5556 (1:share , 0 :do not share)?  :";
		std::cin >> initial_variables.RChoice;
	}
}

Logger owl;
int main(int argc, char* argv[]) {
	QCoreApplication::setOrganizationName("EGTRAIN");
	QCoreApplication::setApplicationName("EGTRAIN");

	// Passenger passeng(1, 2, 3, "type", 3, "stop type", "location", 4, "stopmode", TRUE, 2.3, 2.4, "prev stop", 33, 0.4, 3, 0.342);

	LoggerSettings settings;
	LoggerSettings settings2;
	LoggerSettings settings3;
	settings.filename = "filename.log";
	settings.b_overwriteFile = true;
	settings.b_stdout = false;
	settings.b_datetime = false;

	settings2.filename = "filename2.log";
	settings2.b_overwriteFile = true;
	settings2.b_stdout = false;
	settings2.b_datetime = true;

	settings3.filename = "filename3.log";
	settings3.b_overwriteFile = true;
	settings3.b_stdout = false;
	settings3.b_datetime = true;

	logger.init(settings3);

	owl.init(settings);
	owl << owl.prefix(__FILE__, __LINE__);

	// owl.prefix(__FILE__, __LINE__);
	Logger owl2;
	owl2.init(settings2);
	owl2 << owl2.prefix(__FILE__, __LINE__);
	// OwlInit(settings);

	owl << "asdfa asdgdfssssssssssssssssssssssssssssssssss as " << initial_variables.GUI << std::endl;
	eglogger << "fouble: " << 1.234 << '\n';
	owl << "int " << 1345 << std::endl;
	owl2 << "asdfasdfasdfasdfasdf" << 65 << std::endl;
	eglogger << "fouble: " << 1.234;

	// parse command line arguments
	parseCmdOptions(argc, argv);
	if (initial_variables.InputMainFolder.empty()) {
		std::cout << "ERROR: Unknown case study id. Valid ids: 1 Netherlands, 2 Paimpol, 3 Copenhagen, 4 Brescia, 5 Assignment.\n";
		return 1;
	}
	// initial_variables.set_case(3);
	std::cout << "Graphical user interface (GUI): " << initial_variables.GUI << "\n";
	if (initial_variables.GUI) {
		std::cout << "Passenger GUI: " << initial_variables.PAX_GUI << std::endl;
	}

	InputMainFolder = initial_variables.InputMainFolder;
	auto resolvePackagedInputFolder = []() {
		QString inputPath = QString::fromStdString(InputMainFolder);
#ifdef __APPLE__
		QString bundledPath = QDir(QCoreApplication::applicationDirPath() + "/../Resources")
		                          .filePath(inputPath);
		if (QDir(bundledPath).exists())
			InputMainFolder = bundledPath.toStdString();
#else
		if (!QDir(inputPath).exists() && QDir(inputPath).isRelative()) {
			QString candidate = QDir(QCoreApplication::applicationDirPath()).filePath(inputPath);
			if (QDir(candidate).exists())
				InputMainFolder = candidate.toStdString();
		}
#endif
	};

	char test;
	char* filepath;
	filepath = (char*)"Input_EGTRAIN/railml_input/Ostsachsen_V220.railml";

	RailML a(filepath);

	// std::cout << a.filename;
	// a.read_ocps();
	a.read_ocps();
	a.read_attibutes_of_nodes((char*)"timetable");
	a.read_attibutes_of_nodes((char*)"ocp");

	// times = 8000;

	if (initial_variables.GUI) {
		numTrackLines = initial_variables.numTrackLines;
		N_Routes = initial_variables.N_Routes;
		bufferTime = initial_variables.bufferTime;
		recoveryTimePercentage = initial_variables.recoveryTimePercentage;

		// define vector sizes with length of simulation from user input
		train_route = std::vector<Route>(N_Routes);

		// start application
		QApplication a(argc, argv);
		resolvePackagedInputFolder();

		// resolve output folder to a writable absolute path and ensure it exists
		{
			QString outRel = QString::fromStdString(initial_variables.OutputMainFolder);
			QString outAbs = outRel;
			if (QDir(outRel).isRelative()) {
				QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
				if (base.isEmpty())
					base = QDir::homePath() + "/EGTRAIN";
				outAbs = base + "/" + outRel;
			}
			QDir().mkpath(outAbs);
			initial_variables.OutputMainFolder = outAbs.toStdString();
		}

		// initialize GUI

		// w.actionLoad_Network();

		// reads and processes station coordinates
		readStationInfo();

		// start EGTRAIN
		simulation.setupEgtrain();
		// prepare simulation
		simulation.prepareSimulation();
		MainWindow w;

		// launch GUI
		w.openGUI();

		return a.exec();
	} else {
		QCoreApplication a(argc, argv);
		resolvePackagedInputFolder();

		numTrackLines = initial_variables.numTrackLines;
		N_Routes = initial_variables.N_Routes;
		bufferTime = initial_variables.bufferTime;
		recoveryTimePercentage = initial_variables.recoveryTimePercentage;

		// parse command line arguments
		// parseCmdOptions(argc, argv);

		// define vector sizes with length of simulation from user input
		train_route = std::vector<Route>(N_Routes);

		// reads and processes station coordinates
		readStationInfo();

		// start EGTRAIN
		simulation.setupEgtrain();
		// prepare simulation
		simulation.prepareSimulation();

		// run simulation
		simulation.runSimulation();
		if (numRegions <= 0)
			return 1;
	}

	// print last services
	simulation.printLastTrainServicePathDiagram();

	return 0;
}
