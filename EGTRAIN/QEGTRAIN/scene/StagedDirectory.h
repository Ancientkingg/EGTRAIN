#pragma once

#include "scene/Utf8Path.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <system_error>

// File helpers of the scene code. A scene directory or a bundle is filled in a private directory next to its destination,
// flushed to disk and then renamed into place, so that a failure leaves the previous scene as it was. A bundle and a
// migration are unpacked into a private temporary directory. The flushes, publishStagedDirectory and writeSceneFiles are
// defined in SceneWriter.cpp, so that the system headers of the flush stay out of this header.

struct SceneModel;
struct SceneSaveResult;

/// Reads the whole file at path into content, byte for byte. Returns false when the file cannot be opened.
inline bool readFileBytes(const std::filesystem::path& path, std::string& content) {
	std::ifstream input(path, std::ios::binary);
	if (!input)
		return false;
	content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
	return true;
}

/// The absolute, normalised form of path, with symlinks resolved as far as the path exists, so that two spellings of one
/// location compare equal. A path that cannot be resolved is returned normalised.
inline std::filesystem::path comparablePath(const std::filesystem::path& path) {
	std::error_code ec;
	std::filesystem::path resolved = std::filesystem::weakly_canonical(path, ec);
	if (!ec)
		return resolved;
	resolved = std::filesystem::absolute(path, ec);
	return ec ? path.lexically_normal() : resolved.lexically_normal();
}

/// True when child is parent or lies inside it, compared element by element. Pass both through comparablePath first.
inline bool containsPath(const std::filesystem::path& parent, const std::filesystem::path& child) {
	auto parentIt = parent.begin();
	auto childIt = child.begin();
	for (; parentIt != parent.end() && childIt != child.end(); ++parentIt, ++childIt) {
		if (*parentIt != *childIt)
			return false;
	}
	return parentIt == parent.end();
}

/// A directory that is removed when the object goes out of scope. release() keeps it, for a caller that has renamed it.
struct StagedDirectory {
	std::filesystem::path path; ///< The directory; empty when there is none or after release().

	StagedDirectory() = default;
	StagedDirectory(const StagedDirectory&) = delete;
	StagedDirectory& operator=(const StagedDirectory&) = delete;

	~StagedDirectory() {
		if (path.empty())
			return;
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
	}

	void release() { path.clear(); } ///< Keeps the directory: the destructor no longer removes it.
};

/// Finds a name that is free in parent: the prefix (UTF-8), a time stamp and an attempt number. A name that is a dangling
/// symlink is taken. Fails with file_exists when 100 names are taken, and with the error of the filesystem when it cannot
/// tell.
inline std::error_code uniqueSiblingPath(const std::filesystem::path& parent, const std::string& prefix,
	std::filesystem::path& result) {
	std::error_code ec;
	for (unsigned int attempt = 0; attempt < 100; ++attempt) {
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path candidate = parent / pathFromUtf8(prefix + std::to_string(stamp) + "-" + std::to_string(attempt));
		ec.clear();
		const auto status = std::filesystem::symlink_status(candidate, ec);
		if (!ec && status.type() == std::filesystem::file_type::not_found) {
			result = candidate;
			return std::error_code();
		}
		if (ec == std::errc::no_such_file_or_directory) {
			result = candidate;
			return std::error_code();
		}
		if (ec)
			return ec;
	}
	return std::make_error_code(std::errc::file_exists);
}

/// Creates an empty directory in parent, named like uniqueSiblingPath names a path, and sets staged.path to it. With
/// ownerOnly the directory is limited to its owner. The staged directory becomes the published directory, so its mode is
/// part of the result: a save asks for owner-only, an import and an export keep the default mode.
inline std::error_code createStagedDirectory(const std::filesystem::path& parent, const std::string& prefix,
	bool ownerOnly, StagedDirectory& staged) {
	std::error_code ec;
	for (unsigned int attempt = 0; attempt < 100; ++attempt) {
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path candidate = parent / pathFromUtf8(prefix + std::to_string(stamp) + "-" + std::to_string(attempt));
		ec.clear();
		if (std::filesystem::create_directory(candidate, ec)) {
			if (ownerOnly) {
				ec.clear();
				std::filesystem::permissions(candidate, std::filesystem::perms::owner_all,
					std::filesystem::perm_options::replace, ec);
				if (ec) {
					std::error_code cleanupError;
					std::filesystem::remove_all(candidate, cleanupError);
					return ec;
				}
			}
			staged.path = candidate;
			return std::error_code();
		}
		if (ec && ec != std::errc::file_exists)
			return ec;
	}
	return std::make_error_code(std::errc::file_exists);
}

/// Called in place of each flush to disk, with the path and whether it is a directory: the files and folders of a staged
/// scene save, of a staged export or of an unpacked bundle, the archive of a bundle save, the temporary file of a
/// standalone scenario, and the folders whose entries changed once these have been moved into place. A test counts the
/// flushes with it or makes one fail by returning an error. It is empty in a normal run.
inline std::function<std::error_code(const std::filesystem::path& path, bool directory)>& syncToDiskHook() {
	static std::function<std::error_code(const std::filesystem::path& path, bool directory)> hook;
	return hook;
}

/// Flushes the file or the directory at path from the caches of the system to the disk: fsync on macOS and Linux,
/// FlushFileBuffers on Windows, or syncToDiskHook() when it is set. Windows flushes no directory, and a file with the
/// read-only attribute is left as it is there. A directory that cannot be opened for reading or whose file system cannot
/// flush one counts as flushed. Returns the error of the system.
std::error_code syncToDisk(const std::filesystem::path& path, bool directory);

/// Flushes every file and folder in directory, and directory itself, to disk with syncToDisk, each folder after what it
/// holds. Links are not followed. On an error failed is the path that could not be flushed or listed.
std::error_code syncDirectoryTree(const std::filesystem::path& directory, std::filesystem::path& failed);

/// Writes the scene files of scene into directory, replacing files of the same names, and loads the directory back: the
/// step of saveScene inside its staging directory, without the flush to disk and the rename into place. The bundle writer
/// packs its archive from such a private temporary directory and removes it afterwards. On success inputSnapshot holds the
/// snapshot of the files loaded back.
SceneSaveResult writeSceneFiles(const SceneModel& scene, const std::filesystem::path& directory);

/// The step of publishStagedDirectory that failed, or Published.
enum class StagedPublishStep {
	Published,	  ///< The directory was renamed into place.
	MoveToBackup, ///< Moving the old destination to the backup name failed.
	Rename		  ///< Renaming the staged directory to the destination failed.
};

/// The outcome of publishStagedDirectory.
struct StagedPublishResult {
	StagedPublishStep step = StagedPublishStep::Published; ///< The step that failed, or Published.
	std::error_code error{};							   ///< The error of that step.
	std::error_code restoreError{};						   ///< Set when Rename failed and the old destination could not be moved back.
	std::error_code removeBackupError{};				   ///< Set when the directory was published and the backup could not be removed.
	/// Set when the directory was published and the rename could not be flushed to disk; the backup is then kept.
	std::error_code syncError{};
};

/// Renames staged.path to destination. backup is empty when there is no destination, otherwise a free name from
/// uniqueSiblingPath. A rename of a directory over an existing directory fails on Windows, which is why the old
/// destination is moved aside first and moved back when the rename into place fails. When the step is MoveToBackup or
/// Rename staged still owns its directory, so the caller can just return and let the destructor remove it. The rename
/// into place is on disk before the backup is removed: on macOS and Linux the parent folder of destination is flushed
/// after it, and on Windows it is written through. When that flush fails the backup stays and syncError says why.
StagedPublishResult publishStagedDirectory(StagedDirectory& staged, const std::filesystem::path& destination,
	const std::filesystem::path& backup);
