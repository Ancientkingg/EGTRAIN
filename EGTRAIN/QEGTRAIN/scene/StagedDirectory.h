#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

// A scene directory is filled in a private directory next to its destination and then renamed into place, so that a
// failure leaves the previous scene as it was. The functions here are shared by the scene writer and the legacy importer.

// A directory that is removed when the object goes out of scope. release() keeps it, for a caller that has renamed it.
struct StagedDirectory {
	std::filesystem::path path;

	StagedDirectory() = default;
	StagedDirectory(const StagedDirectory&) = delete;
	StagedDirectory& operator=(const StagedDirectory&) = delete;

	~StagedDirectory() {
		if (path.empty())
			return;
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
	}

	void release() { path.clear(); }
};

// Finds a name that is free in parent: the prefix, a time stamp and an attempt number. A name that is a dangling symlink
// is taken. Fails with file_exists when 100 names are taken, and with the error of the filesystem when it cannot tell.
inline std::error_code uniqueSiblingPath(const std::filesystem::path& parent, const std::string& prefix,
	std::filesystem::path& result) {
	std::error_code ec;
	for (unsigned int attempt = 0; attempt < 100; ++attempt) {
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path candidate = parent / (prefix + std::to_string(stamp) + "-" + std::to_string(attempt));
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

// Creates an empty directory in parent, named like uniqueSiblingPath names a path, and sets staged.path to it. With
// ownerOnly the directory is limited to its owner. The staged directory becomes the published scene directory, so its
// mode is part of the result: the writer asks for owner-only, the importer keeps the default mode.
inline std::error_code createStagedDirectory(const std::filesystem::path& parent, const std::string& prefix,
	bool ownerOnly, StagedDirectory& staged) {
	std::error_code ec;
	for (unsigned int attempt = 0; attempt < 100; ++attempt) {
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		const std::filesystem::path candidate = parent / (prefix + std::to_string(stamp) + "-" + std::to_string(attempt));
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

// The step of publishStagedDirectory that failed, or Published.
enum class StagedPublishStep {
	Published,
	MoveToBackup,
	Rename
};

// error is the error of the step that failed. restoreError is set when Rename failed and the old destination could not
// be moved back. removeBackupError is set when the directory was published and the backup could not be removed.
struct StagedPublishResult {
	StagedPublishStep step = StagedPublishStep::Published;
	std::error_code error{};
	std::error_code restoreError{};
	std::error_code removeBackupError{};
};

// Renames staged.path to destination. backup is empty when there is no destination, otherwise a free name from
// uniqueSiblingPath. A rename of a directory over an existing directory fails on Windows, which is why the old
// destination is moved aside first and moved back when the rename into place fails. When the step is MoveToBackup or
// Rename staged still owns its directory, so the caller can just return and let the destructor remove it.
inline StagedPublishResult publishStagedDirectory(StagedDirectory& staged, const std::filesystem::path& destination,
	const std::filesystem::path& backup) {
	StagedPublishResult result;
	if (!backup.empty()) {
		std::filesystem::rename(destination, backup, result.error);
		if (result.error) {
			result.step = StagedPublishStep::MoveToBackup;
			return result;
		}
	}
	std::filesystem::rename(staged.path, destination, result.error);
	if (result.error) {
		result.step = StagedPublishStep::Rename;
		if (!backup.empty())
			std::filesystem::rename(backup, destination, result.restoreError);
		return result;
	}
	staged.release();
	if (!backup.empty())
		std::filesystem::remove_all(backup, result.removeBackupError);
	return result;
}
