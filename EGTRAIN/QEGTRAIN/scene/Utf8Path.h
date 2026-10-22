#pragma once

#include <filesystem>
#include <string>
#include <system_error>

// A file name crosses the scene API as a UTF-8 std::string. std::filesystem::path reads a narrow string in the system
// code page on Windows, so a name with a character outside that page is lost. The scene layer builds a path from such
// a string with pathFromUtf8 and turns a path back into a string with u8string(), never with string().

/// The path for a UTF-8 name. Text that cannot be converted as UTF-8 is read in the system code page, as path(text) does.
inline std::filesystem::path pathFromUtf8(const std::string& utf8) {
	try {
		return std::filesystem::u8path(utf8);
	} catch (const std::system_error&) {
		return std::filesystem::path(utf8);
	}
}
