#ifndef RUNOUTPUTFILE_H
#define RUNOUTPUTFILE_H

#include "scene/Utf8Path.h"

#include <cerrno>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <system_error>

/// \file
/// The files that a run writes to its output folder. Each writer opens its file with openRunOutputFile and ends it with
/// closeRunOutputFile, so a file that cannot be written throws "Cannot write <path>: <reason>", which ends the run as failed
/// (DispatchController::runSimulation). A path is UTF-8, as the output folder of the settings of a run is.

/// The exception for a run output file that cannot be written: "Cannot write <path>: <reason>". The reason is the text of the
/// errno value error, or fallback when error is 0.
inline std::runtime_error runOutputWriteError(const std::string& path, int error, const char* fallback) {
	return std::runtime_error("Cannot write " + path + ": " + (error != 0 ? std::generic_category().message(error) : std::string(fallback)));
}

/// Opens the file at the UTF-8 path for writing with mode and std::ios::out; the default replaces the file and writes text.
/// Throws runOutputWriteError when the file cannot be opened.
inline std::ofstream openRunOutputFile(const std::string& path, std::ios::openmode mode = std::ios::out) {
	errno = 0;
	std::ofstream file(pathFromUtf8(path), mode | std::ios::out);
	if (!file.is_open())
		throw runOutputWriteError(path, errno, "the file cannot be opened");
	return file;
}

/// Closes a file that openRunOutputFile opened for path. Throws runOutputWriteError when a write or the close failed.
inline void closeRunOutputFile(std::ofstream& file, const std::string& path) {
	if (!file.fail())
		errno = 0;
	file.close();
	if (file.fail())
		throw runOutputWriteError(path, errno, "a write failed");
}

#endif // RUNOUTPUTFILE_H
