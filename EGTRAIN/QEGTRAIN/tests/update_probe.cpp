// Stand-in for an installed application in the update helper test. It reads
// behavior.txt from its own directory (key=value lines) and acts on it:
//   label=NAME   name written to the log
//   log=PATH     file that receives "NAME started" when the probe starts
//   mode=exit    exit with code=N (default 0)
//   mode=sleep   wait ms=N, write "NAME done", exit 0
//   mode=crash   write through a null pointer (Windows only)
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

std::filesystem::path ownDirectory(const char* argv0) {
#if defined(_WIN32)
	(void)argv0;
	wchar_t buffer[32768];
	const DWORD length = GetModuleFileNameW(nullptr, buffer, 32768);
	if (length == 0 || length >= 32768)
		return {};
	return std::filesystem::path(buffer).parent_path();
#else
	return std::filesystem::absolute(std::filesystem::path(argv0)).parent_path();
#endif
}

std::map<std::string, std::string> readBehavior(const std::filesystem::path& file) {
	std::map<std::string, std::string> values;
	std::ifstream input(file);
	std::string line;
	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		const std::size_t separator = line.find('=');
		if (separator != std::string::npos)
			values[line.substr(0, separator)] = line.substr(separator + 1);
	}
	return values;
}

void appendLog(const std::string& log, const std::string& text) {
	if (log.empty())
		return;
	std::ofstream output(log, std::ios::app);
	output << text << "\n";
}

} // namespace

int main(int, char** argv) {
	const std::filesystem::path directory = ownDirectory(argv[0]);
	if (directory.empty())
		return 70;
	std::map<std::string, std::string> behavior = readBehavior(directory / "behavior.txt");
	const std::string label = behavior["label"];
	const std::string log = behavior["log"];
	const std::string mode = behavior["mode"];
	appendLog(log, label + " started");
	if (mode == "exit")
		return behavior["code"].empty() ? 0 : std::stoi(behavior["code"]);
	if (mode == "sleep") {
		std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(behavior["ms"])));
		appendLog(log, label + " done");
		return 0;
	}
#if defined(_WIN32)
	if (mode == "crash") {
		volatile int* nothing = nullptr;
		*nothing = 1;
	}
#endif
	return 64;
}
