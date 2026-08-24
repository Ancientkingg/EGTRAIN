#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
using ArgumentChar = wchar_t;
using ArgumentString = std::wstring;
#define EGTRAIN_HELPER_MAIN wmain
#else
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
using ArgumentChar = char;
using ArgumentString = std::string;
#define EGTRAIN_HELPER_MAIN main
#endif

namespace {

struct Arguments {
	unsigned long long parentPid = 0;
	std::filesystem::path current;
	std::filesystem::path staged;
	std::filesystem::path backup;
	std::filesystem::path launch;
};

bool parseArguments(int argc, ArgumentChar** argv, Arguments& result) {
	for (int i = 1; i < argc; ++i) {
		const ArgumentString name(argv[i]);
		if (i + 1 >= argc)
			return false;
		const ArgumentString value(argv[++i]);
#if defined(_WIN32)
		if (name == L"--parent-pid") {
			wchar_t* end = nullptr;
			result.parentPid = std::wcstoull(value.c_str(), &end, 10);
#else
		if (name == "--parent-pid") {
			char* end = nullptr;
			result.parentPid = std::strtoull(value.c_str(), &end, 10);
#endif
			if (!end || *end != '\0')
				return false;
#if defined(_WIN32)
		} else if (name == L"--current") {
			result.current = value;
		} else if (name == L"--staged") {
			result.staged = value;
		} else if (name == L"--backup") {
			result.backup = value;
		} else if (name == L"--launch") {
#else
		} else if (name == "--current") {
			result.current = value;
		} else if (name == "--staged") {
			result.staged = value;
		} else if (name == "--backup") {
			result.backup = value;
		} else if (name == "--launch") {
#endif
			result.launch = value;
		} else {
			return false;
		}
	}
	return result.current.has_filename() && result.staged.has_filename()
		&& result.backup.has_filename() && result.launch.has_filename();
}

bool waitForParent(unsigned long long pid) {
	if (pid == 0)
		return true;
#if defined(_WIN32)
	HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
	if (!process)
		return GetLastError() == ERROR_INVALID_PARAMETER;
	const DWORD status = WaitForSingleObject(process, 5 * 60 * 1000);
	CloseHandle(process);
	return status == WAIT_OBJECT_0;
#else
	for (int attempt = 0; attempt < 6000; ++attempt) {
		if (kill(static_cast<pid_t>(pid), 0) == -1 && errno == ESRCH)
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	return false;
#endif
}

bool removePath(const std::filesystem::path& path) {
	std::error_code error;
	std::filesystem::remove_all(path, error);
	return !error;
}

bool movePath(const std::filesystem::path& from, const std::filesystem::path& to) {
	std::error_code error;
	std::filesystem::rename(from, to, error);
	return !error;
}

#if defined(_WIN32)
bool launch(const std::filesystem::path& executable) {
	const std::wstring path = executable.wstring();
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	if (!CreateProcessW(path.c_str(), nullptr, nullptr, nullptr, FALSE,
		CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, nullptr, nullptr, &startup, &process))
		return false;
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return true;
}
#else
bool launch(const std::filesystem::path& executable) {
	const pid_t child = fork();
	if (child < 0)
		return false;
	if (child == 0) {
		execl(executable.c_str(), executable.c_str(), static_cast<char*>(nullptr));
		_exit(127);
	}
	for (int attempt = 0; attempt < 20; ++attempt) {
		int status = 0;
		const pid_t result = waitpid(child, &status, WNOHANG);
		if (result == child)
			return WIFEXITED(status) && WEXITSTATUS(status) == 0;
		if (result < 0)
			return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	}
	return true;
}
#endif

bool transactionalInstall(const Arguments& arguments) {
	std::error_code error;
	if (!std::filesystem::exists(arguments.current, error)
		|| error || !std::filesystem::exists(arguments.staged, error) || error)
		return false;
	if (std::filesystem::exists(arguments.backup, error) || error)
		return false;

	if (!movePath(arguments.current, arguments.backup)) {
		launch(arguments.launch);
		return false;
	}
	if (!movePath(arguments.staged, arguments.current)) {
		movePath(arguments.backup, arguments.current);
		launch(arguments.launch);
		return false;
	}
	if (!launch(arguments.launch)) {
		removePath(arguments.current);
		if (movePath(arguments.backup, arguments.current))
			launch(arguments.launch);
		return false;
	}
	// Keep one recoverable installation until a later update proves this one usable.
	return true;
}

} // namespace

int EGTRAIN_HELPER_MAIN(int argc, ArgumentChar** argv) {
	Arguments arguments;
	if (!parseArguments(argc, argv, arguments)) {
		std::cerr << "usage: egtrain_update_helper --parent-pid PID --current PATH "
			"--staged PATH --backup PATH --launch PATH\n";
		return 2;
	}
	std::error_code workingDirectoryError;
	std::filesystem::current_path(arguments.staged.parent_path(), workingDirectoryError);
	if (workingDirectoryError)
		return 1;
	if (!waitForParent(arguments.parentPid))
		return 1;
	return transactionalInstall(arguments) ? 0 : 1;
}
