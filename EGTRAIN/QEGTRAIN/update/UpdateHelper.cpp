#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

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

// How long a launched application is watched for an early failure.
#if defined(_WIN32)
constexpr unsigned kDefaultObserveMs = 3000;
#else
constexpr unsigned kDefaultObserveMs = 500;
#endif
constexpr unsigned long kMaxObserveMs = 10 * 60 * 1000;
// Name prefix of the folder that SelfUpdater stages an update in; it holds the staged installation and this helper.
constexpr char kStagingPrefix[] = ".qegtrain-update-";

struct Arguments {
	unsigned long long parentPid = 0;
	unsigned observeMs = kDefaultObserveMs;
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
#if defined(_WIN32)
		} else if (name == L"--observe-ms") {
			wchar_t* end = nullptr;
			const unsigned long milliseconds = std::wcstoul(value.c_str(), &end, 10);
#else
		} else if (name == "--observe-ms") {
			char* end = nullptr;
			const unsigned long milliseconds = std::strtoul(value.c_str(), &end, 10);
#endif
			if (value.empty() || !end || *end != '\0' || milliseconds > kMaxObserveMs)
				return false;
			result.observeMs = static_cast<unsigned>(milliseconds);
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

// Starts the application in the directory of its executable and watches it for observeMs.
// A process that exits with a non-zero code inside the window failed to start. One that
// exits with code 0 or is still running when the window ends started. observeMs == 0 does not watch.
#if defined(_WIN32)
bool launch(const std::filesystem::path& executable, unsigned observeMs) {
	const std::wstring path = executable.wstring();
	const std::wstring directory = executable.parent_path().wstring();
	const wchar_t* workingDirectory = directory.empty() ? nullptr : directory.c_str();
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	// The child inherits the error mode. Without these flags a missing DLL or a crash
	// can leave a modal dialog open and the process alive, which would look like a good start.
	const UINT previousMode = SetErrorMode(0);
	SetErrorMode(previousMode | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	const BOOL created = CreateProcessW(path.c_str(), nullptr, nullptr, nullptr, FALSE,
		CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, nullptr, workingDirectory, &startup, &process);
	SetErrorMode(previousMode);
	if (!created)
		return false;
	CloseHandle(process.hThread);
	bool started = true;
	if (observeMs > 0) {
		const DWORD status = WaitForSingleObject(process.hProcess, observeMs);
		if (status == WAIT_OBJECT_0) {
			DWORD exitCode = 1;
			started = GetExitCodeProcess(process.hProcess, &exitCode) && exitCode == 0;
		} else if (status != WAIT_TIMEOUT) {
			started = false;
		}
	}
	CloseHandle(process.hProcess);
	return started;
}
#else
bool launch(const std::filesystem::path& executable, unsigned observeMs) {
	const std::filesystem::path directory = executable.parent_path();
	const pid_t child = fork();
	if (child < 0)
		return false;
	if (child == 0) {
		if (!directory.empty() && chdir(directory.c_str()) != 0)
			_exit(127);
		execl(executable.c_str(), executable.c_str(), static_cast<char*>(nullptr));
		_exit(127);
	}
	// The window is measured on the clock: a sleep can take much longer than asked on a busy machine.
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(observeMs);
	while (std::chrono::steady_clock::now() < deadline) {
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

// Renames a path, retrying while a scanner or a dying process still holds a file in it.
bool movePathWithRetries(const std::filesystem::path& from, const std::filesystem::path& to) {
	for (int attempt = 0; attempt < 15; ++attempt) {
		if (attempt > 0)
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		if (movePath(from, to))
			return true;
	}
	return false;
}

// Removes the staging folder of a successful update. On Windows the running helper cannot be
// deleted and keeps its folder; the application removes that folder later.
void removeStaging(const std::filesystem::path& staging) {
	if (staging.filename().native().rfind(std::filesystem::path(kStagingPrefix).native(), 0) != 0)
		return;
#if defined(_WIN32)
	wchar_t buffer[32768];
	const DWORD length = GetModuleFileNameW(nullptr, buffer, 32768);
	if (length == 0 || length >= 32768)
		return;
	const std::filesystem::path self(buffer);
	std::vector<std::filesystem::path> entries;
	std::error_code error;
	std::filesystem::directory_iterator iterator(staging, error);
	for (; !error && iterator != std::filesystem::directory_iterator(); iterator.increment(error))
		entries.push_back(iterator->path());
	for (const std::filesystem::path& entry : entries)
		if (!std::filesystem::equivalent(entry, self, error))
			removePath(entry);
#else
	removePath(staging);
#endif
}

// Replaces the installation that failed to start with the backup. The failed one is
// renamed aside first, so a file that is still locked cannot leave a half-deleted
// installation. If the backup cannot be renamed back, the failed one is put back.
void restoreBackup(const Arguments& arguments) {
	std::filesystem::path failed = arguments.backup;
	failed += ".failed";
	removePath(failed);
	if (!movePathWithRetries(arguments.current, failed))
		return;
	if (movePathWithRetries(arguments.backup, arguments.current)) {
		removePath(failed);
		launch(arguments.launch, 0);
	} else {
		movePathWithRetries(failed, arguments.current);
	}
}

bool transactionalInstall(const Arguments& arguments) {
	std::error_code error;
	if (!std::filesystem::exists(arguments.current, error)
		|| error || !std::filesystem::exists(arguments.staged, error) || error)
		return false;
	if (std::filesystem::exists(arguments.backup, error) || error)
		return false;

	if (!movePath(arguments.current, arguments.backup)) {
		launch(arguments.launch, 0);
		return false;
	}
	if (!movePath(arguments.staged, arguments.current)) {
		movePath(arguments.backup, arguments.current);
		launch(arguments.launch, 0);
		return false;
	}
	if (!launch(arguments.launch, arguments.observeMs)) {
		restoreBackup(arguments);
		return false;
	}
	removeStaging(arguments.staged.parent_path());
	// Keep one recoverable installation until a later update proves this one usable.
	return true;
}

} // namespace

int EGTRAIN_HELPER_MAIN(int argc, ArgumentChar** argv) {
	Arguments arguments;
	if (!parseArguments(argc, argv, arguments)) {
		std::cerr << "usage: egtrain_update_helper --parent-pid PID --current PATH "
					 "--staged PATH --backup PATH --launch PATH [--observe-ms MILLISECONDS]\n";
		return 2;
	}
	std::error_code workingDirectoryError;
	std::filesystem::current_path(arguments.current.parent_path(), workingDirectoryError);
	if (workingDirectoryError)
		return 1;
	if (!waitForParent(arguments.parentPid))
		return 1;
	return transactionalInstall(arguments) ? 0 : 1;
}
