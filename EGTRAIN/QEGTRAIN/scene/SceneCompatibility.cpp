#include "scene/SceneCompatibility.h"

#include "scene/SceneBundle.h"
#include "scene/SceneMigration.h"
#include "scene/Utf8Path.h"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr std::uintmax_t kMaxSceneManifestSize = 16ULL * 1024ULL * 1024ULL;
// A saved_with_app_version longer than this is cut when a message quotes it.
constexpr std::size_t kMaxQuotedVersionLength = 64;

void addDiagnostic(std::vector<SceneDiagnostic>& diagnostics, const std::string& code,
	const std::string& message, const std::string& file = {}, const std::string& suggestedFix = {}) {
	SceneDiagnostic diagnostic;
	diagnostic.severity = SceneSeverity::Error;
	diagnostic.code = code;
	diagnostic.message = message;
	diagnostic.file = file;
	diagnostic.suggestedFix = suggestedFix;
	diagnostics.push_back(std::move(diagnostic));
}

bool readManifest(const fs::path& path, std::string& contents,
	std::vector<SceneDiagnostic>& diagnostics) {
	std::error_code ec;
	const auto status = fs::symlink_status(path, ec);
	if (ec || status.type() != fs::file_type::regular) {
		addDiagnostic(diagnostics, "scene.compatibility.manifest",
			"scene.json is missing or not a regular file", "scene.json");
		return false;
	}
	const std::uintmax_t size = fs::file_size(path, ec);
	if (ec || size > kMaxSceneManifestSize) {
		addDiagnostic(diagnostics, "scene.compatibility.manifest",
			"scene.json exceeds the bounded manifest size", "scene.json");
		return false;
	}
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		addDiagnostic(diagnostics, "scene.compatibility.manifest",
			"scene.json cannot be opened", "scene.json");
		return false;
	}
	contents.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
	if (!input && !input.eof()) {
		addDiagnostic(diagnostics, "scene.compatibility.manifest",
			"scene.json cannot be read", "scene.json");
		return false;
	}
	return true;
}

// True when the list is missing or empty.
bool hasNoItems(const json& object, const char* key) {
	const auto found = object.find(key);
	return found == object.end() || (found->is_array() && found->empty());
}

// The draft layout, saved by the builds before 2026-08-08, has a legacy_source in scene.json. Its
// infrastructure.json has no nodes and no arcs: the railway was read from a legacy text tree. Neither
// sign alone marks the layout. A file that cannot be read is left to the loader.
bool isDraftLayout(const fs::path& directory, const json& manifest) {
	if (!manifest.contains("legacy_source"))
		return false;
	const fs::path path = directory / "infrastructure.json";
	std::error_code ec;
	const auto status = fs::symlink_status(path, ec);
	if (ec || status.type() != fs::file_type::regular)
		return false;
	const std::uintmax_t size = fs::file_size(path, ec);
	if (ec || size > kMaxSceneManifestSize)
		return false;
	std::ifstream input(path, std::ios::binary);
	const json infrastructure = json::parse(input, nullptr, false);
	return infrastructure.is_object() && hasNoItems(infrastructure, "nodes") && hasNoItems(infrastructure, "arcs");
}

SceneCompatibilityClass classify(int schemaVersion, const std::optional<int>& bundleVersion,
	SceneSourceKind sourceKind) {
	if (schemaVersion > kCurrentSceneSchemaVersion
		|| (sourceKind == SceneSourceKind::Bundle && bundleVersion
			&& *bundleVersion > kCurrentSceneBundleVersion))
		return SceneCompatibilityClass::Newer;
	const bool oldSchema = schemaVersion < kCurrentSceneSchemaVersion;
	const bool oldBundle = sourceKind == SceneSourceKind::Bundle && bundleVersion
		&& *bundleVersion < kCurrentSceneBundleVersion;
	if (!oldSchema && !oldBundle)
		return SceneCompatibilityClass::Current;
	return SceneCompatibilityClass::OlderMigratable;
}

void classifyWithRegistry(SceneCompatibilityProbeResult& result,
	const SceneMigrationRegistry& registry) {
	if (result.classification == SceneCompatibilityClass::Malformed)
		return;
	const bool oldSchema = result.schemaVersion < kCurrentSceneSchemaVersion;
	const bool oldBundle = result.sourceKind == SceneSourceKind::Bundle
		&& result.bundleVersion && *result.bundleVersion < kCurrentSceneBundleVersion;
	if ((oldSchema && !sceneMigrationPathAvailable(registry, SceneMigrationStepKind::Schema, result.schemaVersion, kCurrentSceneSchemaVersion))
		|| (oldBundle && !sceneMigrationPathAvailable(registry, SceneMigrationStepKind::Bundle, *result.bundleVersion, kCurrentSceneBundleVersion)))
		result.classification = SceneCompatibilityClass::OlderUnsupported;
}

// "schema 1" or, for a bundle, "schema 1, bundle layout 1".
std::string versionNumbers(int schemaVersion, const std::optional<int>& bundleVersion) {
	std::string text = "schema " + std::to_string(schemaVersion);
	if (bundleVersion)
		text += ", bundle layout " + std::to_string(*bundleVersion);
	return text;
}

std::string savedWithText(const std::string& version) {
	if (version.empty())
		return "saved-with version not recorded";
	if (version.size() <= kMaxQuotedVersionLength)
		return "saved with EGTRAIN " + version;
	std::size_t length = kMaxQuotedVersionLength;
	while (length > 0 && (static_cast<unsigned char>(version[length]) & 0xC0) == 0x80)
		--length;
	return "saved with EGTRAIN " + version.substr(0, length) + "...";
}

// Names why a scene of another version or layout is rejected. A scene that the registry can migrate gets
// no diagnostic: the copy upgrade reports the diagnostics of its probe as errors.
void explainRejection(SceneCompatibilityProbeResult& result, bool draftLayout) {
	// No migration is written for the draft layout, so it is never offered a copy upgrade.
	const bool draft = draftLayout
		&& (result.classification == SceneCompatibilityClass::Current || result.classification == SceneCompatibilityClass::OlderMigratable);
	if (!draft && !isRejectedForVersion(result))
		return;
	const std::string found = versionNumbers(result.schemaVersion, result.bundleVersion);
	const std::string saved = savedWithText(result.savedWithAppVersion);
	const std::string reads = "This version of EGTRAIN reads "
		+ versionNumbers(kCurrentSceneSchemaVersion,
			result.sourceKind == SceneSourceKind::Bundle ? std::optional<int>(kCurrentSceneBundleVersion) : std::nullopt);
	if (draft) {
		result.classification = SceneCompatibilityClass::OlderUnsupported;
		addDiagnostic(result.diagnostics, "scene.compatibility.draft",
			"This scene uses the draft layout that EGTRAIN saved before 2026-08-08 (" + found + ", " + saved
				+ "). That layout is not supported.",
			"scene.json", "Import the original legacy case again with File > Load Legacy Case...");
	} else if (result.classification == SceneCompatibilityClass::Newer) {
		addDiagnostic(result.diagnostics, "scene.compatibility.newer",
			"This scene uses " + found + " (" + saved + "). " + reads + " and does not open newer scenes.",
			"scene.json", "Update EGTRAIN with Help > Check for Updates...");
	} else {
		addDiagnostic(result.diagnostics, "scene.compatibility.older",
			"This scene uses " + found + " (" + saved + "). " + reads + " and cannot convert older scenes.",
			"scene.json",
			"Open a scene saved by this version of EGTRAIN, or import the original legacy case again with File > Load Legacy Case...");
	}
}

SceneCompatibilityProbeResult probeDirectory(const std::string& path,
	const SceneMigrationRegistry& registry) {
	SceneCompatibilityProbeResult result;
	result.sourceKind = SceneSourceKind::Directory;
	std::error_code ec;
	const fs::path directory = pathFromUtf8(path);
	if (!fs::is_directory(directory, ec) || ec) {
		addDiagnostic(result.diagnostics, "scene.compatibility.directory",
			"Scene directory is missing or unreadable", path);
		return result;
	}
	std::string manifestText;
	if (!readManifest(directory / "scene.json", manifestText, result.diagnostics))
		return result;
	bool draftLayout = false;
	try {
		const json manifest = json::parse(manifestText);
		if (!manifest.is_object()) {
			addDiagnostic(result.diagnostics, "scene.compatibility.manifest",
				"scene.json root must be an object", "scene.json");
			return result;
		}
		if (!manifest.contains("schema_version") || !manifest["schema_version"].is_number_integer()) {
			addDiagnostic(result.diagnostics, "scene.compatibility.schema",
				"scene.json schema_version must be an integer", "scene.json");
			return result;
		}
		if (!readJsonInt(manifest["schema_version"], INT_MIN, INT_MAX, result.schemaVersion)) {
			addDiagnostic(result.diagnostics, "scene.compatibility.schema",
				"scene.json schema_version is outside the supported integer range", "scene.json");
			return result;
		}
		if (manifest.contains("saved_with_app_version")) {
			if (!manifest["saved_with_app_version"].is_string()) {
				addDiagnostic(result.diagnostics, "scene.compatibility.provenance",
					"saved_with_app_version must be a string", "scene.json");
				return result;
			}
			result.savedWithAppVersion = manifest["saved_with_app_version"].get<std::string>();
		}
		draftLayout = isDraftLayout(directory, manifest);
	} catch (const json::exception& error) {
		addDiagnostic(result.diagnostics, "scene.compatibility.manifest",
			std::string("Invalid scene.json: ") + error.what(), "scene.json");
		return result;
	}
	result.classification = classify(result.schemaVersion, result.bundleVersion, result.sourceKind);
	classifyWithRegistry(result, registry);
	explainRejection(result, draftLayout);
	return result;
}

} // namespace

SceneCompatibilityProbeResult probeSceneCompatibility(const std::string& path) {
	return probeSceneCompatibility(path, productionSceneMigrationRegistry());
}

SceneCompatibilityProbeResult probeSceneCompatibility(const std::string& path,
	const SceneMigrationRegistry& registry) {
	std::error_code ec;
	const fs::path scenePath = pathFromUtf8(path);
	if (fs::is_directory(scenePath, ec))
		return probeDirectory(path, registry);

	SceneCompatibilityProbeResult result;
	result.sourceKind = SceneSourceKind::Bundle;
	const SceneBundleProbeResult bundle = probeSceneBundle(path);
	result.schemaVersion = bundle.schemaVersion;
	result.bundleVersion = bundle.bundleVersion;
	result.savedWithAppVersion = bundle.savedWithAppVersion;
	result.diagnostics = bundle.diagnostics;
	if (!bundle.structurallyValid)
		return result;
	result.classification = classify(result.schemaVersion, result.bundleVersion, result.sourceKind);
	classifyWithRegistry(result, registry);
	explainRejection(result, false);
	return result;
}
