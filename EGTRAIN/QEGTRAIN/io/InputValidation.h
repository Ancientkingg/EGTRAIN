#ifndef INPUTVALIDATION_H
#define INPUTVALIDATION_H

#include <string>

struct InputCheckResult {
    bool ok;
    std::string message;
};

// Check the required case study input files exist and are readable.
// inputMainFolder is the resolved absolute path to the case study Input dir.
InputCheckResult validateCaseStudyInput(const std::string& inputMainFolder);

#endif // INPUTVALIDATION_H
