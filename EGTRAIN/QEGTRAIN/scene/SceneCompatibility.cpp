#include "scene/SceneCompatibility.h"

#include "scene/SceneBundle.h"
#include "scene/SceneMigration.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr std::uintmax_t kMaxSceneManifestSize = 16ULL * 1024ULL * 1024ULL;

void addDiagnostic(std::vector<SceneDiagnostic>& diagnostics, const std::string& code,
		const std::string& message, const std::string& file = {}) {
	SceneDiagnostic diagnostic;
	diagnostic.severity = SceneSeverity::Error;
	diagnostic.code = code;
	diagnostic.message = message;
	diagnostic.file = file;
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
	if ((oldSchema && !sceneMigrationPathAvailable(registry, SceneMigrationStepKind::Schema,
				result.schemaVersion, kCurrentSceneSchemaVersion))
			|| (oldBundle && !sceneMigrationPathAvailable(registry, SceneMigrationStepKind::Bundle,
				*result.bundleVersion, kCurrentSceneBundleVersion)))
		result.classification = SceneCompatibilityClass::OlderUnsupported;
}

SceneCompatibilityProbeResult probeDirectory(const std::string& path,
		const SceneMigrationRegistry& registry) {
	SceneCompatibilityProbeResult result;
	result.sourceKind = SceneSourceKind::Directory;
	std::error_code ec;
	const fs::path directory(path);
	if (!fs::is_directory(directory, ec) || ec) {
		addDiagnostic(result.diagnostics, "scene.compatibility.directory",
				"Scene directory is missing or unreadable", path);
		return result;
	}
	std::string manifestText;
	if (!readManifest(directory / "scene.json", manifestText, result.diagnostics))
		return result;
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
		result.schemaVersion = manifest["schema_version"].get<int>();
		if (manifest.contains("saved_with_app_version")) {
			if (!manifest["saved_with_app_version"].is_string()) {
				addDiagnostic(result.diagnostics, "scene.compatibility.provenance",
						"saved_with_app_version must be a string", "scene.json");
				return result;
			}
			result.savedWithAppVersion = manifest["saved_with_app_version"].get<std::string>();
		}
	} catch (const json::exception& error) {
		addDiagnostic(result.diagnostics, "scene.compatibility.manifest",
				std::string("Invalid scene.json: ") + error.what(), "scene.json");
		return result;
	}
	result.classification = classify(result.schemaVersion, result.bundleVersion, result.sourceKind);
	classifyWithRegistry(result, registry);
	return result;
}

} // namespace

SceneCompatibilityProbeResult probeSceneCompatibility(const std::string& path) {
	return probeSceneCompatibility(path, productionSceneMigrationRegistry());
}

SceneCompatibilityProbeResult probeSceneCompatibility(const std::string& path,
		const SceneMigrationRegistry& registry) {
	std::error_code ec;
	const fs::path scenePath(path);
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
	return result;
}
