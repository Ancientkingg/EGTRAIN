#include "scene/SceneBundle.h"

#include "io/third_party/miniz/miniz.h"
#include "scene/SceneWriter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr mz_uint64 kMaxBundleFileSize = 32ULL * 1024ULL * 1024ULL;
constexpr mz_uint64 kMaxEntrySize = 16ULL * 1024ULL * 1024ULL;
constexpr mz_uint64 kMaxTotalSize = 64ULL * 1024ULL * 1024ULL;
constexpr mz_uint64 kMaxCompressionRatio = 1000;
constexpr mz_uint kMaxEntries = 16;

const std::array<const char*, 7> kRequiredEntries = {
	"scene.json", "infrastructure.json", "stations.json", "signalling.json",
	"rolling_stock.json", "services.json", "scenarios.json",
};

struct TempDirectory {
	fs::path path;

	~TempDirectory() {
		if (path.empty())
			return;
		std::error_code error;
		fs::remove_all(path, error);
	}

	void release() { path.clear(); }
};

struct ZipReader {
	mz_zip_archive zip{};
	bool initialized = false;

	~ZipReader() {
		if (initialized)
			mz_zip_reader_end(&zip);
	}
};

struct ArchiveEntry {
	mz_uint index = 0;
	std::string name;
	mz_zip_archive_file_stat stat{};
};

struct BundleManifest {
	int schemaVersion = 0;
	std::optional<int> bundleVersion;
	std::string savedWithAppVersion;
};

static void addDiagnostic(std::vector<SceneDiagnostic>& diagnostics, const std::string& code,
		const std::string& message, const std::string& file = "") {
	SceneDiagnostic diagnostic;
	diagnostic.severity = SceneSeverity::Error;
	diagnostic.code = code;
	diagnostic.message = message;
	diagnostic.file = file;
	diagnostics.push_back(diagnostic);
}

static std::string zipError(const mz_zip_archive& zip) {
	return mz_zip_get_error_string(mz_zip_peek_last_error(const_cast<mz_zip_archive*>(&zip)));
}

static std::string lowerAscii(const std::string& value) {
	std::string result = value;
	std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return result;
}

static bool unsafeArchiveName(const std::string& name, std::string& reason) {
	if (name.empty()) {
		reason = "empty entry name";
		return true;
	}
	if (name.find('\\') != std::string::npos) {
		reason = "backslash is not allowed in entry names";
		return true;
	}
	if (name.front() == '/') {
		reason = "absolute entry path is not allowed";
		return true;
	}
	if (name.size() >= 2 && std::isalpha(static_cast<unsigned char>(name[0])) && name[1] == ':') {
		reason = "drive-qualified entry path is not allowed";
		return true;
	}
	std::size_t begin = 0;
	while (begin <= name.size()) {
		const std::size_t end = name.find('/', begin);
		const std::string component = name.substr(begin,
				end == std::string::npos ? std::string::npos : end - begin);
		if (component == "." || component == "..") {
			reason = "dot or dot-dot path components are not allowed";
			return true;
		}
		if (end == std::string::npos)
			break;
		begin = end + 1;
	}
	return false;
}

static bool specialArchiveFile(const mz_zip_archive_file_stat& stat) {
	// Unix stores the file type in the high 16 bits of external attributes.
	const unsigned int creatorOs = stat.m_version_made_by >> 8;
	if (creatorOs == 3 || creatorOs == 19) {
		const unsigned int modeType = (stat.m_external_attr >> 16) & 0170000U;
		if (modeType != 0 && modeType != 0100000U)
			return true;
	}
	// FILE_ATTRIBUTE_REPARSE_POINT is the ZIP/DOS representation commonly used
	// for Windows links and other special filesystem objects.
	return (stat.m_external_attr & (0x008U | 0x010U | 0x040U | 0x400U)) != 0;
}

static bool createUniqueDirectory(const fs::path& parent, const std::string& prefix, fs::path& result) {
	std::error_code ec;
	for (unsigned int attempt = 0; attempt < 100; ++attempt) {
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		const fs::path candidate = parent / (prefix + std::to_string(stamp) + "-" + std::to_string(attempt));
		ec.clear();
		if (fs::create_directory(candidate, ec)) {
			ec.clear();
			fs::permissions(candidate, fs::perms::owner_all, fs::perm_options::replace, ec);
			if (ec) {
				std::error_code removeError;
				fs::remove_all(candidate, removeError);
				return false;
			}
			result = candidate;
			return true;
		}
		if (ec && ec != std::errc::file_exists)
			return false;
	}
	return false;
}

static bool makeTempDirectory(TempDirectory& temp, std::vector<SceneDiagnostic>& diagnostics) {
	std::error_code ec;
	const fs::path root = fs::temp_directory_path(ec);
	if (ec || root.empty() || !createUniqueDirectory(root, "egscene-", temp.path)) {
		addDiagnostic(diagnostics, "scene.bundle.temp", "Cannot create a private temporary directory");
		return false;
	}
	return true;
}

static bool readBoundedFile(const fs::path& path, std::string& contents,
		std::vector<SceneDiagnostic>& diagnostics, const std::string& file) {
	std::error_code ec;
	const std::uintmax_t size = fs::file_size(path, ec);
	if (ec) {
		addDiagnostic(diagnostics, "scene.bundle.file.read", "Cannot stat bundle entry", file);
		return false;
	}
	if (size > kMaxEntrySize) {
		addDiagnostic(diagnostics, "scene.bundle.entry.size", "Bundle entry exceeds the 16 MiB uncompressed limit", file);
		return false;
	}
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		addDiagnostic(diagnostics, "scene.bundle.file.read", "Cannot open bundle entry", file);
		return false;
	}
	contents.resize(static_cast<std::size_t>(size));
	if (size != 0) {
		input.read(contents.data(), static_cast<std::streamsize>(size));
		if (!input || static_cast<std::uintmax_t>(input.gcount()) != size) {
			addDiagnostic(diagnostics, "scene.bundle.file.read", "Cannot read bundle entry", file);
			return false;
		}
	}
	return true;
}

static bool readArchiveFile(const fs::path& path, std::string& contents,
		std::vector<SceneDiagnostic>& diagnostics) {
	std::error_code ec;
	const auto status = fs::symlink_status(path, ec);
	if (ec || status.type() != fs::file_type::regular) {
		addDiagnostic(diagnostics, "scene.bundle.file.missing", "Bundle file is missing or not a regular file",
				path.string());
		return false;
	}
	const std::uintmax_t size = fs::file_size(path, ec);
	if (ec) {
		addDiagnostic(diagnostics, "scene.bundle.file.read", "Cannot determine bundle size", path.string());
		return false;
	}
	if (size > kMaxBundleFileSize) {
		addDiagnostic(diagnostics, "scene.bundle.size", "Bundle file exceeds the 32 MiB compressed limit",
				path.string());
		return false;
	}
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		addDiagnostic(diagnostics, "scene.bundle.file.read", "Cannot open bundle file", path.string());
		return false;
	}
	contents.resize(static_cast<std::size_t>(size));
	if (size != 0) {
		input.read(contents.data(), static_cast<std::streamsize>(size));
		if (!input || static_cast<std::uintmax_t>(input.gcount()) != size) {
			addDiagnostic(diagnostics, "scene.bundle.file.read", "Cannot read bundle file", path.string());
			return false;
		}
	}
	return true;
}

static bool canonicalEntryName(const std::string& name);

static bool inspectBundle(const fs::path& path, const std::string& archiveBytes, ZipReader& reader,
		std::vector<ArchiveEntry>& entries, BundleManifest& manifest,
		std::vector<SceneDiagnostic>& diagnostics) {
	const mz_uint64 archiveSize = archiveBytes.size();
	if (!mz_zip_reader_init_mem(&reader.zip, archiveBytes.data(), archiveBytes.size(), 0)) {
		addDiagnostic(diagnostics, "scene.bundle.archive", "Cannot open ZIP bundle: " + zipError(reader.zip),
				path.string());
		return false;
	}
	reader.initialized = true;
	const mz_uint fileCount = mz_zip_reader_get_num_files(&reader.zip);
	if (fileCount > kMaxEntries) {
		addDiagnostic(diagnostics, "scene.bundle.entries", "Bundle contains more than 16 entries", path.string());
		return false;
	}

	std::set<std::string> lowerNames;
	mz_uint64 totalSize = 0;
	for (mz_uint index = 0; index < fileCount; ++index) {
		mz_zip_archive_file_stat stat{};
		if (!mz_zip_reader_file_stat(&reader.zip, index, &stat)) {
			addDiagnostic(diagnostics, "scene.bundle.archive", "Cannot read ZIP entry metadata",
					"entry " + std::to_string(index));
			return false;
		}
		const mz_uint filenameSize = mz_zip_reader_get_filename(&reader.zip, index, nullptr, 0);
		if (filenameSize == 0 || filenameSize >= MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE) {
			addDiagnostic(diagnostics, "scene.bundle.path", "ZIP entry name is empty or too long",
					"entry " + std::to_string(index));
			return false;
		}
		std::vector<char> filename(filenameSize + 1, '\0');
		if (mz_zip_reader_get_filename(&reader.zip, index, filename.data(),
					static_cast<mz_uint>(filename.size())) == 0) {
			addDiagnostic(diagnostics, "scene.bundle.path", "Cannot read ZIP entry name",
					"entry " + std::to_string(index));
			return false;
		}
		const std::size_t nameLength = filenameSize - 1;
		if (std::strlen(filename.data()) != nameLength) {
			addDiagnostic(diagnostics, "scene.bundle.path", "ZIP entry name contains an embedded NUL", "entry " + std::to_string(index));
			return false;
		}
		const std::string name(filename.data(), nameLength);
		std::string pathReason;
		if (unsafeArchiveName(name, pathReason)) {
			addDiagnostic(diagnostics, "scene.bundle.path", "Unsafe ZIP entry path: " + pathReason, name);
			return false;
		}
		if (stat.m_is_directory) {
			addDiagnostic(diagnostics, "scene.bundle.entry", "Directory entries are not allowed in a bundle", name);
			return false;
		}
		if (stat.m_is_encrypted || (stat.m_bit_flag & 1U) != 0) {
			addDiagnostic(diagnostics, "scene.bundle.encryption", "Encrypted ZIP entries are not supported", name);
			return false;
		}
		if (!stat.m_is_supported || specialArchiveFile(stat)) {
			addDiagnostic(diagnostics, "scene.bundle.entry", "ZIP entry uses an unsupported or special file type", name);
			return false;
		}
		if (!lowerNames.insert(lowerAscii(name)).second) {
			addDiagnostic(diagnostics, "scene.bundle.duplicate", "Case-insensitive duplicate ZIP entry", name);
			return false;
		}
		if (stat.m_comp_size > archiveSize) {
			addDiagnostic(diagnostics, "scene.bundle.archive", "ZIP entry exceeds the archive size", name);
			return false;
		}
		if (stat.m_uncomp_size > kMaxEntrySize) {
			addDiagnostic(diagnostics, "scene.bundle.entry.size", "Bundle entry exceeds the 16 MiB uncompressed limit", name);
			return false;
		}
		if (stat.m_comp_size == 0 ? stat.m_uncomp_size != 0
				: stat.m_uncomp_size > stat.m_comp_size * kMaxCompressionRatio) {
			addDiagnostic(diagnostics, "scene.bundle.ratio", "Bundle entry exceeds the 1000:1 compression-ratio limit", name);
			return false;
		}
		if (totalSize > kMaxTotalSize - stat.m_uncomp_size) {
			addDiagnostic(diagnostics, "scene.bundle.total-size", "Bundle exceeds the 64 MiB total uncompressed limit", name);
			return false;
		}
		totalSize += stat.m_uncomp_size;
		entries.push_back({index, name, stat});
	}

	const auto manifestEntry = std::find_if(entries.begin(), entries.end(), [](const ArchiveEntry& entry) {
		return entry.name == "scene.json";
	});
	if (manifestEntry == entries.end()) {
		const auto unknown = std::find_if(entries.begin(), entries.end(), [](const ArchiveEntry& entry) {
			return !canonicalEntryName(entry.name);
		});
		if (unknown != entries.end()) {
			addDiagnostic(diagnostics, "scene.bundle.entry", "Unknown ZIP entry is not allowed in v1",
					unknown->name);
			return false;
		}
		addDiagnostic(diagnostics, "scene.bundle.required", "Required ZIP entry is missing", "scene.json");
		return false;
	}
	std::string manifestText(static_cast<std::size_t>(manifestEntry->stat.m_uncomp_size), '\0');
	if (!mz_zip_reader_extract_to_mem(&reader.zip, manifestEntry->index,
				manifestText.empty() ? nullptr : manifestText.data(), manifestText.size(), 0)) {
		const bool allRequired = std::all_of(kRequiredEntries.begin(), kRequiredEntries.end(),
				[&entries](const char* required) {
					return std::any_of(entries.begin(), entries.end(), [required](const ArchiveEntry& entry) {
						return entry.name == required;
					});
				});
		if (!allRequired)
			addDiagnostic(diagnostics, "scene.bundle.required", "Required ZIP entry is missing", "scene.json");
		else
			addDiagnostic(diagnostics, "scene.bundle.manifest", "Cannot extract scene.json manifest", "scene.json");
		return false;
	}
	try {
		const json manifestJson = json::parse(manifestText);
		if (!manifestJson.is_object() || !manifestJson.contains("format")
				|| !manifestJson["format"].is_string() || manifestJson["format"] != "egscene") {
			addDiagnostic(diagnostics, "scene.bundle.manifest", "scene.json format must be \"egscene\"",
					"scene.json");
			return false;
		}
		if (!manifestJson.contains("bundle_version") || !manifestJson["bundle_version"].is_number_integer()) {
			addDiagnostic(diagnostics, "scene.bundle.version", "scene.json bundle_version must be an integer", "scene.json");
			return false;
		}
		if (!manifestJson.contains("schema_version") || !manifestJson["schema_version"].is_number_integer()) {
			addDiagnostic(diagnostics, "scene.bundle.schema", "scene.json schema_version must be an integer", "scene.json");
			return false;
		}
		manifest.bundleVersion = manifestJson["bundle_version"].get<int>();
		manifest.schemaVersion = manifestJson["schema_version"].get<int>();
		if (manifestJson.contains("saved_with_app_version")) {
			if (!manifestJson["saved_with_app_version"].is_string()) {
				addDiagnostic(diagnostics, "scene.bundle.manifest",
						"scene.json saved_with_app_version must be a string", "scene.json");
				return false;
			}
			manifest.savedWithAppVersion = manifestJson["saved_with_app_version"].get<std::string>();
		}
	} catch (const json::exception& error) {
		const bool allRequired = std::all_of(kRequiredEntries.begin(), kRequiredEntries.end(),
				[&entries](const char* required) {
					return std::any_of(entries.begin(), entries.end(), [required](const ArchiveEntry& entry) {
						return entry.name == required;
					});
				});
		if (!allRequired) {
			const auto unknown = std::find_if(entries.begin(), entries.end(), [](const ArchiveEntry& entry) {
				return !canonicalEntryName(entry.name);
			});
			if (unknown != entries.end())
				addDiagnostic(diagnostics, "scene.bundle.entry", "Unknown ZIP entry is not allowed in v1", unknown->name);
			else
				addDiagnostic(diagnostics, "scene.bundle.required", "Required ZIP entry is missing", "scene.json");
		} else {
			addDiagnostic(diagnostics, "scene.bundle.manifest",
					std::string("Invalid scene.json manifest: ") + error.what(), "scene.json");
		}
		return false;
	}
	if (*manifest.bundleVersion == kCurrentSceneBundleVersion) {
		for (const auto& entry : entries) {
			if (entry.name != "passengers.json" && entry.name != "views.json"
					&& std::none_of(kRequiredEntries.begin(), kRequiredEntries.end(), [&entry](const char* required) {
						return entry.name == required;
					})) {
				addDiagnostic(diagnostics, "scene.bundle.entry", "Unknown ZIP entry is not allowed in v1", entry.name);
				return false;
			}
		}
		for (const char* required : kRequiredEntries) {
			if (std::none_of(entries.begin(), entries.end(), [required](const ArchiveEntry& entry) {
					return entry.name == required;
				})) {
				addDiagnostic(diagnostics, "scene.bundle.required", "Required ZIP entry is missing", required);
				return false;
			}
		}
	}
	if (!mz_zip_validate_archive(&reader.zip, 0)) {
		addDiagnostic(diagnostics, "scene.bundle.archive", "ZIP validation failed: " + zipError(reader.zip),
				path.string());
		return false;
	}
	return true;
}

static bool extractEntries(const ZipReader& reader, const std::vector<ArchiveEntry>& entries,
		const fs::path& destination, std::vector<SceneDiagnostic>& diagnostics) {
	for (const auto& entry : entries) {
		const fs::path output = destination / entry.name;
		std::error_code ec;
		if (!fs::create_directories(output.parent_path(), ec) && ec) {
			addDiagnostic(diagnostics, "scene.bundle.extract",
				"Cannot create ZIP entry parent directory: " + ec.message(), entry.name);
			return false;
		}
		if (!mz_zip_reader_extract_to_file(const_cast<mz_zip_archive*>(&reader.zip), entry.index,
				output.string().c_str(), 0)) {
			addDiagnostic(diagnostics, "scene.bundle.extract", "Cannot extract ZIP entry: " + zipError(reader.zip),
					entry.name);
			return false;
		}
	}
	return true;
}

static bool canonicalEntryName(const std::string& name) {
	return name == "passengers.json" || name == "views.json"
			|| std::any_of(kRequiredEntries.begin(), kRequiredEntries.end(), [&name](const char* required) {
				return name == required;
			});
}

static bool publishPath(const fs::path& staging, const fs::path& target,
		std::vector<SceneDiagnostic>& diagnostics, const std::string& operation,
		bool replaceRegularFile) {
	const fs::path parent = target.parent_path().empty() ? fs::path(".") : target.parent_path();
	std::error_code ec;
	if (!fs::exists(parent, ec) || ec) {
		addDiagnostic(diagnostics, "scene.bundle.publish", "Destination parent does not exist for " + operation,
				target.string());
		return false;
	}
	const auto targetStatus = fs::symlink_status(target, ec);
	if (ec && ec != std::errc::no_such_file_or_directory) {
		addDiagnostic(diagnostics, "scene.bundle.publish", "Cannot inspect destination for " + operation,
				target.string());
		return false;
	}
	const bool hadTarget = !ec && targetStatus.type() != fs::file_type::not_found;
	if (hadTarget && (!replaceRegularFile || targetStatus.type() != fs::file_type::regular)) {
		addDiagnostic(diagnostics, "scene.bundle.publish", "Destination already exists for " + operation,
				target.string());
		return false;
	}

	ec.clear();
#ifdef _WIN32
	const DWORD flags = MOVEFILE_WRITE_THROUGH | (replaceRegularFile ? MOVEFILE_REPLACE_EXISTING : 0);
	if (!MoveFileExW(staging.c_str(), target.c_str(), flags))
		ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
	fs::rename(staging, target, ec);
#endif
	if (ec) {
		addDiagnostic(diagnostics, "scene.bundle.publish", "Cannot publish " + operation + ": " + ec.message(),
				target.string());
		return false;
	}
	return true;
}

} // namespace

SceneBundleProbeResult probeSceneBundle(const std::string& bundlePath) {
	SceneBundleProbeResult result;
	std::string archiveBytes;
	if (!readArchiveFile(fs::path(bundlePath), archiveBytes, result.diagnostics))
		return result;
	ZipReader reader;
	std::vector<ArchiveEntry> entries;
	BundleManifest manifest;
	if (!inspectBundle(fs::path(bundlePath), archiveBytes, reader, entries, manifest,
			result.diagnostics))
		return result;
	result.schemaVersion = manifest.schemaVersion;
	result.bundleVersion = manifest.bundleVersion;
	result.savedWithAppVersion = manifest.savedWithAppVersion;
	result.structurallyValid = true;
	return result;
}

SceneSaveResult extractSceneBundleForMigration(const std::string& bundlePath,
		const std::string& destinationDirectory) {
	SceneSaveResult result;
	const fs::path destination(destinationDirectory);
	std::error_code ec;
	if (destination.empty() || !fs::is_directory(destination, ec) || ec
			|| fs::is_symlink(destination, ec)) {
		addDiagnostic(result.diagnostics, "scene.bundle.publish",
				"Migration extraction destination must be a private directory",
				destinationDirectory);
		return result;
	}
	std::string archiveBytes;
	if (!readArchiveFile(fs::path(bundlePath), archiveBytes, result.diagnostics))
		return result;
	ZipReader reader;
	std::vector<ArchiveEntry> entries;
	BundleManifest manifest;
	if (!inspectBundle(fs::path(bundlePath), archiveBytes, reader, entries, manifest,
			result.diagnostics))
		return result;
	if (manifest.bundleVersion && *manifest.bundleVersion > kCurrentSceneBundleVersion) {
		addDiagnostic(result.diagnostics, "scene.bundle.version",
				"Newer bundle layouts are not extracted", "scene.json");
		return result;
	}
	// Older layouts are eligible for an explicitly registered migration step.
	// Their unknown-but-safe entries are part of that step's input; current v1
	// has already passed its exact allowlist checks in inspectBundle().
	if (!extractEntries(reader, entries, destination, result.diagnostics))
		return result;
	result.wroteAll = !hasErrors(result.diagnostics);
	return result;
}

SceneLoadResult loadSceneBundle(const std::string& bundlePath) {
	SceneLoadResult result;
	std::string archiveBytes;
	if (!readArchiveFile(fs::path(bundlePath), archiveBytes, result.diagnostics))
		return result;
	ZipReader reader;
	std::vector<ArchiveEntry> entries;
	BundleManifest manifest;
	if (!inspectBundle(fs::path(bundlePath), archiveBytes, reader, entries, manifest, result.diagnostics))
		return result;
	result.bundleVersion = manifest.bundleVersion;
	if (manifest.bundleVersion != kCurrentSceneBundleVersion) {
		addDiagnostic(result.diagnostics, "scene.bundle.version",
				"Unsupported bundle_version; only the current bundle layout can be loaded", "scene.json");
		return result;
	}
	if (manifest.schemaVersion != kCurrentSceneSchemaVersion) {
		addDiagnostic(result.diagnostics, "scene.bundle.schema",
				"Unsupported schema_version; only the current scene schema can be loaded", "scene.json");
		return result;
	}

	TempDirectory temp;
	if (!makeTempDirectory(temp, result.diagnostics))
		return result;
	if (!extractEntries(reader, entries, temp.path, result.diagnostics))
		return result;
	SceneLoadResult loaded = loadScene(temp.path.string());
	loaded.inputSnapshot = std::move(archiveBytes);
	loaded.bundleVersion = manifest.bundleVersion;
	return loaded;
}

SceneLoadResult loadScenePath(const std::string& path) {
	const fs::path scenePath(path);
	std::error_code ec;
	if (fs::is_directory(scenePath, ec))
		return loadScene(path);
	ec.clear();
	if (!fs::exists(scenePath, ec) && lowerAscii(scenePath.extension().string()) != ".egscene")
		return loadScene(path);
	return loadSceneBundle(path);
}

SceneSaveResult saveSceneBundle(const SceneModel& scene, const std::string& bundlePath) {
	SceneSaveResult result;
	if (scene.schemaVersion != kCurrentSceneSchemaVersion) {
		addDiagnostic(result.diagnostics, "scene.bundle.schema",
				"Bundle writer requires the current schema_version", "scene.json");
		return result;
	}
	const fs::path target(bundlePath);
	if (target.empty() || target.filename().empty()) {
		addDiagnostic(result.diagnostics, "scene.bundle.write", "Bundle output path is empty");
		return result;
	}

	TempDirectory temp;
	if (!makeTempDirectory(temp, result.diagnostics))
		return result;
	SceneSaveResult directoryResult = saveScene(scene, temp.path.string());
	result.diagnostics = directoryResult.diagnostics;
	if (!directoryResult.success())
		return result;

	std::string manifestText;
	if (!readBoundedFile(temp.path / "scene.json", manifestText, result.diagnostics, "scene.json"))
		return result;
	try {
		json manifest = json::parse(manifestText);
		if (!manifest.is_object()) {
			addDiagnostic(result.diagnostics, "scene.bundle.manifest", "scene.json root is not an object", "scene.json");
			return result;
		}
		manifest["format"] = "egscene";
		manifest["bundle_version"] = kCurrentSceneBundleVersion;
		manifestText = manifest.dump(4) + "\n";
	} catch (const json::exception& error) {
		addDiagnostic(result.diagnostics, "scene.bundle.manifest", std::string("Cannot update scene.json manifest: ") + error.what(),
				"scene.json");
		return result;
	}
	{
		std::ofstream output(temp.path / "scene.json", std::ios::binary | std::ios::trunc);
		if (output)
			output.write(manifestText.data(), static_cast<std::streamsize>(manifestText.size()));
		output.close();
		if (!output) {
			addDiagnostic(result.diagnostics, "scene.bundle.write", "Cannot write scene.json manifest", "scene.json");
			return result;
		}
	}

	std::vector<std::string> names;
	for (const char* required : kRequiredEntries)
		names.emplace_back(required);
	std::error_code ec;
	if (fs::is_regular_file(temp.path / "passengers.json", ec))
		names.emplace_back("passengers.json");
	ec.clear();
	if (fs::is_regular_file(temp.path / "views.json", ec))
		names.emplace_back("views.json");
	std::sort(names.begin(), names.end());

	fs::path parent = target.parent_path().empty() ? fs::path(".") : target.parent_path();
	fs::create_directories(parent, ec);
	if (ec) {
		addDiagnostic(result.diagnostics, "scene.bundle.write", "Cannot create bundle output directory: " + ec.message(),
				target.string());
		return result;
	}
	const auto targetStatus = fs::symlink_status(target, ec);
	if (!ec && targetStatus.type() == fs::file_type::directory) {
		addDiagnostic(result.diagnostics, "scene.bundle.write", "Bundle output path is a directory", target.string());
		return result;
	}

	mz_uint64 totalSize = 0;
	for (const auto& name : names) {
		ec.clear();
		const std::uintmax_t size = fs::file_size(temp.path / name, ec);
		if (ec) {
			addDiagnostic(result.diagnostics, "scene.bundle.file.read", "Cannot stat bundle entry", name);
			return result;
		}
		if (size > kMaxEntrySize) {
			addDiagnostic(result.diagnostics, "scene.bundle.entry.size", "Bundle entry exceeds the 16 MiB uncompressed limit", name);
			return result;
		}
		if (totalSize > kMaxTotalSize - size) {
			addDiagnostic(result.diagnostics, "scene.bundle.total-size", "Generated bundle exceeds the 64 MiB total limit", name);
			return result;
		}
		totalSize += size;
	}

	TempDirectory stagingContainer;
	if (!createUniqueDirectory(parent, target.filename().string() + ".staging-", stagingContainer.path)) {
		addDiagnostic(result.diagnostics, "scene.bundle.write", "Cannot create a private bundle staging directory",
				target.string());
		return result;
	}
	const fs::path staging = stagingContainer.path / "bundle.egscene";
	mz_zip_archive writer{};
	bool writerInitialized = mz_zip_writer_init_file(&writer, staging.string().c_str(), 0) != 0;
	if (!writerInitialized) {
		addDiagnostic(result.diagnostics, "scene.bundle.write", "Cannot create ZIP bundle: " + zipError(writer), target.string());
		return result;
	}
	bool archiveOk = true;
	for (const auto& name : names) {
		if (!mz_zip_writer_add_file(&writer, name.c_str(), (temp.path / name).string().c_str(),
				nullptr, 0, MZ_BEST_COMPRESSION)) {
			addDiagnostic(result.diagnostics, "scene.bundle.write", "Cannot add ZIP entry: " + zipError(writer), name);
			archiveOk = false;
			break;
		}
	}
	if (archiveOk && !mz_zip_writer_finalize_archive(&writer)) {
		addDiagnostic(result.diagnostics, "scene.bundle.write", "Cannot finalize ZIP bundle: " + zipError(writer), target.string());
		archiveOk = false;
	}
	if (!mz_zip_writer_end(&writer)) {
		addDiagnostic(result.diagnostics, "scene.bundle.write", "Cannot close ZIP bundle", target.string());
		archiveOk = false;
	}
	if (!archiveOk) {
		return result;
	}

	std::string archiveBytes;
	if (!readArchiveFile(staging, archiveBytes, result.diagnostics))
		return result;
	{
		ZipReader checkReader;
		std::vector<ArchiveEntry> checkEntries;
		BundleManifest checkManifest;
		if (!inspectBundle(staging, archiveBytes, checkReader, checkEntries, checkManifest, result.diagnostics))
			return result;
	}
	if (!publishPath(staging, target, result.diagnostics, "bundle", true))
		return result;
	result.wroteAll = !hasErrors(result.diagnostics);
	if (result.wroteAll)
		result.inputSnapshot = std::move(archiveBytes);
	return result;
}

SceneSaveResult unpackSceneBundle(const std::string& bundlePath, const std::string& destinationDirectory) {
	SceneSaveResult result;
	const fs::path destination(destinationDirectory);
	const std::string destinationName = destination.filename().string();
	if (destination.empty() || destinationName.empty() || destinationName == "." || destinationName == "..") {
		addDiagnostic(result.diagnostics, "scene.bundle.publish", "Extraction destination must be a named directory",
				destinationDirectory);
		return result;
	}
	std::error_code equivalenceError;
	if (fs::exists(destination, equivalenceError) && !equivalenceError
			&& fs::equivalent(fs::path(bundlePath), destination, equivalenceError) && !equivalenceError) {
		addDiagnostic(result.diagnostics, "scene.bundle.publish", "Extraction destination must differ from the input bundle",
				destinationDirectory);
		return result;
	}

	ZipReader reader;
	std::string archiveBytes;
	if (!readArchiveFile(fs::path(bundlePath), archiveBytes, result.diagnostics))
		return result;
	std::vector<ArchiveEntry> entries;
	BundleManifest manifest;
	if (!inspectBundle(fs::path(bundlePath), archiveBytes, reader, entries, manifest, result.diagnostics))
		return result;
	if (manifest.bundleVersion != kCurrentSceneBundleVersion
			|| manifest.schemaVersion != kCurrentSceneSchemaVersion) {
		addDiagnostic(result.diagnostics, "scene.bundle.version",
				"Only the current scene bundle can be unpacked", "scene.json");
		return result;
	}
	const fs::path parent = destination.parent_path().empty() ? fs::path(".") : destination.parent_path();
	std::error_code ec;
	fs::create_directories(parent, ec);
	if (ec) {
		addDiagnostic(result.diagnostics, "scene.bundle.publish", "Cannot create extraction parent: " + ec.message(),
				parent.string());
		return result;
	}

	TempDirectory staging;
	if (!createUniqueDirectory(parent, destination.filename().string() + ".staging-", staging.path)) {
		addDiagnostic(result.diagnostics, "scene.bundle.publish", "Cannot create extraction staging directory",
				destinationDirectory);
		return result;
	}
	if (!extractEntries(reader, entries, staging.path, result.diagnostics))
		return result;
	const SceneLoadResult loaded = loadScene(staging.path.string());
	result.diagnostics.insert(result.diagnostics.end(), loaded.diagnostics.begin(), loaded.diagnostics.end());
	if (hasErrors(loaded.diagnostics))
		return result;
	if (!publishPath(staging.path, destination, result.diagnostics, "extracted scene", false))
		return result;
	staging.release();
	result.wroteAll = !hasErrors(result.diagnostics);
	return result;
}
