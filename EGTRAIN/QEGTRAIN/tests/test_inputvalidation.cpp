#include "io/InputValidation.h"

#include <filesystem>
#include <fstream>
#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

int main() {
	InputCheckResult r = validateCaseStudyInput("/no/such/folder/xyz");

	bool ok = true;
	ok &= expect(!r.ok, "missing folder is invalid");
	ok &= expect(!r.message.empty(), "missing folder reports message");

	const auto root = std::filesystem::temp_directory_path() / "egtrain-input-validation";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root / "TrackLines");
	std::ofstream(root / "TrackLines/Connections.txt");
	std::ofstream(root / "TrackLines/Stations.txt");

	r = validateCaseStudyInput(root.string());
	ok &= expect(!r.ok && r.message.find("trainNames.txt") != std::string::npos, "missing train list names its path");

	std::ofstream(root / "trainNames.txt");
	r = validateCaseStudyInput(root.string());
	ok &= expect(r.ok, "complete startup file set is valid");

	// exported scenes carry no List_of_Blocks_IDs.txt; the validator must not demand one
	std::filesystem::create_directories(root / "Routes");
	std::ofstream(root / "Routes/Route0.txt");
	r = validateCaseStudyInput(root.string());
	ok &= expect(r.ok, "scene export layout without route list is valid");
	std::filesystem::remove_all(root);

	if (!ok)
		return 1;

	std::cout << "all InputValidation tests passed\n";
	return 0;
}
