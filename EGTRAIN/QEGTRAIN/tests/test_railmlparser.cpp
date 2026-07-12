#include "simulation/InitialParameters.h"

#include <iostream>
#include <sstream>
#include <string>

InitialParameters initial_variables(0);

void read_rttp_train_view(std::string rttp);

int main() {
	std::ostringstream output;
	std::ostringstream errors;
	auto* oldOutput = std::cout.rdbuf(output.rdbuf());
	auto* oldErrors = std::cerr.rdbuf(errors.rdbuf());

	read_rttp_train_view(
		"<rTTP><rTTPTrainView><rTTPForSingleTrain trainID=\"T42\">"
		"<tDSectionOccupation tDSectionID=\"S7\" trainID=\"T42\" occupationStart=\"10\" routeId=\"R1\"/>"
		"</rTTPForSingleTrain></rTTPTrainView></rTTP>");
	const bool parsedPayload = output.str().find("Train T42") != std::string::npos && output.str().find("in Section S7") != std::string::npos;

	errors.str("");
	errors.clear();
	read_rttp_train_view("<rTTP><unclosed>");
	std::cout.rdbuf(oldOutput);
	std::cerr.rdbuf(oldErrors);
	if (!parsedPayload || errors.str().find("RTTP XML parse error") == std::string::npos)
		return 1;

	return 0;
}
