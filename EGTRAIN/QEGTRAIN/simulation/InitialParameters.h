#ifndef INITIALPARAMETERS_H
#define INITIALPARAMETERS_H

#include <string>

class InitialParameters {
public:
	std::string name;
	int startingSimulationTime;
	bool GUI;
	bool PAX_GUI = false;
	double times;
	int numTrackLines;
	int N_Routes;
	int num_OrderLists;
	int bufferTime;
	int recoveryTimePercentage;
	// Explicit legacy readers may still receive a source root. Native runs leave it empty.
	std::string InputMainFolder;
	std::string OutputMainFolder;
	int TSM;
	int RChoice;
	bool nArgProvided = false;
	bool durationOverride = false;
	bool bufferTimeOverride = false;
	bool recoveryTimeOverride = false;
	bool enabled_log = true;
	bool log_to_file = true;

	explicit InitialParameters(int caseStudy);
	void set_case(int caseStudy);
};

#endif
