#include "simulation/InitialParameters.h"
#include "util/Log.h"

InitialParameters::InitialParameters(int caseStudy)
	: startingSimulationTime(0), GUI(true), times(0), numTrackLines(0), N_Routes(0),
	  num_OrderLists(0), bufferTime(0), recoveryTimePercentage(0), TSM(0), RChoice(0) {
	if (caseStudy > 0)
		set_case(caseStudy);
}

void InitialParameters::set_case(int caseStudy) {
	static const char* names[] = {
		"", "Netherlands", "Paimpol", "Copenhagen", "Milano_Brescia", "Assignment_Gvc_Gdg_Ut"};
	if (caseStudy < 1 || caseStudy >= static_cast<int>(sizeof(names) / sizeof(names[0]))) {
		name.clear();
		EG_LOG(SimSetup, Warning) << "No case selected";
		return;
	}
	name = names[caseStudy];
	EG_LOG(SimSetup, Info) << "\nSelected Case study: " << caseStudy << '\n';
}
