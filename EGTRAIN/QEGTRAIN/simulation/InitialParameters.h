#ifndef INITIALPARAMETERS_H
#define INITIALPARAMETERS_H
#include "io/RailMLParser.h"
// #include <boost/filesystem.hpp>

// namespace fs = boost::filesystem;

// Here you define all initial parameters needed to run simulation
//
// class InitialParameters
//{
// public:
//	bool GUI = 1; //if we want to have GUI
//	//
//	//Log parameters
//
//	//log file path
//	fs::path logging_dir;
//	//fs::path dir = "logging";
//	//fs::path file = "Logfile.log";
//	//fs::path full_path = dir / file;
//
//	bool enabled_log = true;	// if you want to disable logging
//	bool log_to_file = false;	//if you want log to pass to file
//	/**********
//	 // Netherlands Network
//
//
//	double times = 8000;
//	int numTrackLines = 268;
//	int N_Routes = 74;
//	int bufferTime = 0;
//	int recoveryTimePercentage = 0;
//	std::string InputMainFolder = "Input/Input_EGTRAIN_Netherlands";       //This is the Folder of all the input of EGTRAIN
//	std::string OutputMainFolder = "Output/Output_EGTRAIN_Netherlands";
//		*/
//	//**********
//	 // Paimpol Network
//
//	double times = 8000;
//	int numTrackLines = 6;
//	int N_Routes = 8;
//	int bufferTime = 0;
//	int recoveryTimePercentage = 0;
//	std::string InputMainFolder = "Input/Input_EGTRAIN_Paimpol";
//	std::string OutputMainFolder = "Output/Output_EGTRAIN_Paimpol";
//
//
//	//**********
//	/* Copenhagen Network
//
//	double times = 2800;
//	int numTrackLines = 1;
//	int N_Routes = 1;
//	int bufferTime = 0;
//	int recoveryTimePercentage = 0;
//	 */
//
//	//std::string InputMainFolder = "Input/Input_EGTRAIN_Paimpol";
//	//std::string OutputMainFolder = "Output/Output_EGTRAIN_Paimpol";
//	//std::string InputMainFolder = "Input/Input_EGTRAIN_Paimpol2";
//	//std::string OutputMainFolder = "Output/Output_EGTRAIN_Paimpol2";
//	//std::string InputMainFolder = "Input/Input_EGTRAIN_Copenhagen";
//	//std::string OutputMainFolder = "Output/Output_EGTRAIN_Copenhagen";
//
//
//
//
//	//fs::path logging_foder = "logging";
//	//fs::path OutputDir = OutputMainFolder;
//	//logging_dir = OutputDir / logging_foder;
//
//
//	//Constructor
//	 InitialParameters(){
//
//
//	}
//};
//

class InitialParameters {
public:
	std::string name;
	int startingSimulationTime; // this is the number for the humantime conversion e.g. 23300=>06:28:20 (seconds from midnight)
	bool GUI;					// if we want to have GUI
	bool PAX_GUI = false;		// if we want to display passenger info
	double times;
	int numTrackLines;
	int N_Routes;
	int num_OrderLists;
	int bufferTime;
	int recoveryTimePercentage;
	std::string InputMainFolder; // This is the Folder of all the input of EGTRAIN
	std::string OutputMainFolder;
	int TSM;													   // 1 if a TSM.xml file is shared to port 5555, 0 if not
	int RChoice;												   // 1 if a Routechoice.xml file is sent to port 5556, 0 if not
	bool nArgProvided = false;									   // true when -n was given on command line
	std::vector<std::string> Service_lines;						   // number of different lines for Timetable graph export
	std::map<std::string, std::vector<std::string>> Line_stations; // stations serving each service line

	//
	// Log parameters

	// log file path
	// fs::path logging_dir;
	// fs::path dir = "logging";
	// fs::path file = "Logfile.log";
	// fs::path full_path = dir / file;

	bool enabled_log = true; // if you want to disable logging
	bool log_to_file = true; // if you want log to pass to file
	/**********
	 * Netherlands Network


	double times = 8000;
	int numTrackLines = 268;
	int N_Routes = 74;
	int bufferTime = 0;
	int recoveryTimePercentage = 0;
	std::string InputMainFolder = "Input/Input_EGTRAIN_Netherlands";       //This is the Folder of all the input of EGTRAIN
	std::string OutputMainFolder = "Output/Output_EGTRAIN_Netherlands";
		*/
	/**********
	 * Paimpol Network
	 */
	// double times = 8000;
	// int numTrackLines = 6;
	// int N_Routes = 8;
	// int bufferTime = 0;
	//  recoveryTimePercentage = 0;

	/**********
	 * Copenhagen Network
	 */
	// double times = 8000;
	// int numTrackLines = 1;
	// int N_Routes = 1;
	// int bufferTime = 0;
	// int recoveryTimePercentage = 0;

	// std::string InputMainFolder = "Input/Input_EGTRAIN_Paimpol";
	// std::string OutputMainFolder = "Output/Output_EGTRAIN_Paimpol";
	// std::string InputMainFolder = "Input/Input_EGTRAIN_Copenhagen";
	// std::string OutputMainFolder = "Output/Output_EGTRAIN_Copenhagen";

	// fs::path logging_foder = "logging";
	// fs::path OutputDir = OutputMainFolder;
	// logging_dir = OutputDir / logging_foder;

	// Constructor

	InitialParameters(int case_study);
	void set_case(int case_study);
};

#endif
