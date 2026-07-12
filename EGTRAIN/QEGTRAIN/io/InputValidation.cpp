#include "io/InputValidation.h"
#include <fstream>
#include <vector>

static bool fileReadable(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

InputCheckResult validateCaseStudyInput(const std::string& inputMainFolder) {
    // files the startup path reads from the case study input folder.
    // Routes/List_of_Blocks_IDs.txt is not required: printAllBlocksId writes it
    // as a route-authoring aid and nothing reads it back.
    const std::vector<std::string> required = {
        "/TrackLines/Connections.txt",
        "/TrackLines/Stations.txt",
        "/trainNames.txt",
    };
    for (const auto& rel : required) {
        std::string full = inputMainFolder + rel;
        if (!fileReadable(full)) {
            return InputCheckResult{false, std::string("Cannot read required input file: ") + full};
        }
    }
    return InputCheckResult{true, "ok"};
}
