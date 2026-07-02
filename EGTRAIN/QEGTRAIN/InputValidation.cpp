#include "InputValidation.h"
#include <fstream>
#include <vector>

static bool fileReadable(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

InputCheckResult validateCaseStudyInput(const std::string& inputMainFolder) {
    // required files relative to the case study input folder (confirmed in EGTRAIN.cpp lines 79-80)
    const std::vector<std::string> required = {
        "/TrackLines/Connections.txt",
        "/TrackLines/Stations.txt",
    };
    for (const auto& rel : required) {
        std::string full = inputMainFolder + rel;
        if (!fileReadable(full)) {
            return InputCheckResult{false, std::string("Cannot read required input file: ") + full};
        }
    }
    return InputCheckResult{true, "ok"};
}
