#include "simulation/InitialParameters.h"

#include <iostream>

InitialParameters::InitialParameters(int caseStudy)
	: startingSimulationTime(0), GUI(true), times(0), numTrackLines(0), N_Routes(0),
	  num_OrderLists(0), bufferTime(0), recoveryTimePercentage(0), TSM(0), RChoice(0) {
	if (caseStudy > 0)
		set_case(caseStudy);
}

void InitialParameters::set_case(int caseStudy) {
	static const char* names[] = {
		"", "Netherlands", "Paimpol", "Copenhagen", "Milano_Brescia", "Assignment_Gvc_Gdg_Ut", "Lebanon"
	};
	if (caseStudy < 1 || caseStudy >= static_cast<int>(sizeof(names) / sizeof(names[0]))) {
		name.clear();
		std::cout << "No case selected";
		return;
	}
	name = names[caseStudy];
	std::cout << "\nSelected Case study: " << caseStudy << '\n';
}
