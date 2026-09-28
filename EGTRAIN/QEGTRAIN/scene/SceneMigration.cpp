#include "scene/SceneMigration.h"

#include "scene/SceneBundle.h"
#include "scene/SceneWriter.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;
namespace {

void addDiagnostic(std::vector<SceneDiagnostic>& diagnostics, const std::string& code,
		const std::string& message, const std::string& file = {}) {
	SceneDiagnostic diagnostic;
	diagnostic.severity = SceneSeverity::Error;
	diagnostic.code = code;
	diagnostic.message = message;
	diagnostic.file = file;
	diagnostics.push_back(std::move(diagnostic));
}

const std::vector<SceneMigrationStep>& stepsFor(const SceneMigrationRegistry& registry,
		SceneMigrationStepKind kind) {
	return kind == SceneMigrationStepKind::Schema ? registry.schemaSteps() : registry.bundleSteps();
}

bool appendPath(const std::vector<SceneMigrationStep>& steps, int current, int target,
		std::vector<const SceneMigrationStep*>& path) {
	if (current == target)
		return true;
	for (const auto& step : steps) {
		if (step.fromVersion != current || step.toVersion > target || !step.transform)
			continue;
		path.push_back(&step);
		if (appendPath(steps, step.toVersion, target, path))
			return true;
		path.pop_back();
	}
	return false;
}

std::vector<const SceneMigrationStep*> findPath(const SceneMigrationRegistry& registry,
		SceneMigrationStepKind kind, int fromVersion, int toVersion) {
	const auto& steps = stepsFor(registry, kind);
	if (fromVersion >= toVersion)
		return {};
	std::vector<const SceneMigrationStep*> result;
	return appendPath(steps, fromVersion, toVersion, result) ? result
			: std::vector<const SceneMigrationStep*>{};
}

bool isSafeDirectory(const fs::path& path) {
	std::error_code ec;
	const auto status = fs::symlink_status(path, ec);
	return !ec && status.type() == fs::file_type::directory;
}

bool copyDirectoryTree(const fs::path& source, const fs::path& destination,
		std::vector<SceneDiagnostic>& diagnostics) {
	if (!isSafeDirectory(source) || !isSafeDirectory(destination)) {
		addDiagnostic(diagnostics, "scene.migration.source", "Migration source and staging must be directories",
				source.string());
		return false;
	}
	std::error_code ec;
	for (fs::recursive_directory_iterator iterator(source, fs::directory_options::skip_permission_denied, ec),
			end; iterator != end; iterator.increment(ec)) {
		if (ec) {
			addDiagnostic(diagnostics, "scene.migration.source", "Cannot traverse scene source: " + ec.message(),
					source.string());
			return false;
		}
		const fs::path current = iterator->path();
		const auto status = fs::symlink_status(current, ec);
		if (ec || status.type() == fs::file_type::symlink
				|| (status.type() != fs::file_type::directory
					&& status.type() != fs::file_type::regular)) {
			addDiagnostic(diagnostics, "scene.migration.source",
					"Migration source contains a symlink or special file", current.string());
			return false;
		}
		const fs::path relative = fs::relative(current, source, ec);
		if (ec) {
			addDiagnostic(diagnostics, "scene.migration.source", "Cannot resolve scene source path",
					current.string());
			return false;
		}
		const fs::path target = destination / relative;
		if (status.type() == fs::file_type::directory) {
			fs::create_directories(target, ec);
		} else {
			fs::create_directories(target.parent_path(), ec);
			if (!ec)
				fs::copy_file(current, target, fs::copy_options::overwrite_existing, ec);
		}
		if (ec) {
			addDiagnostic(diagnostics, "scene.migration.source", "Cannot stage scene source: " + ec.message(),
					current.string());
			return false;
		}
	}
	return true;
}

bool createPrivateDirectory(const fs::path& parent, const std::string& prefix, fs::path& result) {
	std::error_code ec;
	for (unsigned int attempt = 0; attempt < 100; ++attempt) {
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		const fs::path candidate = parent / (prefix + std::to_string(stamp) + "-"
				+ std::to_string(attempt));
		if (fs::create_directory(candidate, ec)) {
			fs::permissions(candidate, fs::perms::owner_all, fs::perm_options::replace, ec);
			if (ec) {
				std::error_code cleanup;
				fs::remove_all(candidate, cleanup);
				return false;
			}
			result = candidate;
			return true;
		}
		if (ec && ec != std::errc::file_exists)
			return false;
		ec.clear();
	}
	return false;
}

bool validDestination(const fs::path& destination, const fs::path& source,
		std::vector<SceneDiagnostic>& diagnostics) {
	if (destination.empty() || destination.filename().empty()
			|| destination.filename() == "." || destination.filename() == "..") {
		addDiagnostic(diagnostics, "scene.migration.destination", "Migration destination must be a named path",
				destination.string());
		return false;
	}
	const fs::path parent = destination.parent_path().empty() ? fs::path(".") : destination.parent_path();
	std::error_code ec;
	const auto parentStatus = fs::symlink_status(parent, ec);
	if (ec || parentStatus.type() != fs::file_type::directory) {
		addDiagnostic(diagnostics, "scene.migration.destination",
				"Migration destination parent must be an existing directory", parent.string());
		return false;
	}
	std::error_code sourceEc;
	std::error_code destinationEc;
	const fs::path canonicalSource = fs::weakly_canonical(source, sourceEc);
	const fs::path canonicalDestination = fs::weakly_canonical(destination, destinationEc);
	if (sourceEc || destinationEc) {
		addDiagnostic(diagnostics, "scene.migration.destination",
				"Cannot resolve migration source or destination", destination.string());
		return false;
	}
	if (canonicalSource == canonicalDestination) {
		addDiagnostic(diagnostics, "scene.migration.destination",
				"Migration source and destination must differ", destination.string());
		return false;
	}
	const auto pathMismatch = std::mismatch(canonicalSource.begin(), canonicalSource.end(),
			canonicalDestination.begin(), canonicalDestination.end());
	if (fs::is_directory(source) && pathMismatch.first == canonicalSource.end()) {
		addDiagnostic(diagnostics, "scene.migration.destination",
				"Migration destination must not be inside the source scene", destination.string());
		return false;
	}
	const auto status = fs::symlink_status(destination, ec);
	if (!ec && status.type() != fs::file_type::not_found) {
		addDiagnostic(diagnostics, "scene.migration.destination",
				"Migration destination already exists or is unsafe", destination.string());
		return false;
	}
	if (ec != std::errc::no_such_file_or_directory && ec) {
		addDiagnostic(diagnostics, "scene.migration.destination",
				"Cannot inspect migration destination: " + ec.message(), destination.string());
		return false;
	}
	return true;
}

} // namespace

void SceneMigrationRegistry::addSchemaStep(SceneMigrationStep step) {
	if (step.toVersion > step.fromVersion && step.transform)
		m_schemaSteps.push_back(std::move(step));
}

void SceneMigrationRegistry::addBundleStep(SceneMigrationStep step) {
	if (step.toVersion > step.fromVersion && step.transform)
		m_bundleSteps.push_back(std::move(step));
}

const SceneMigrationRegistry& productionSceneMigrationRegistry() {
	static const SceneMigrationRegistry registry;
	return registry;
}

bool sceneMigrationPathAvailable(const SceneMigrationRegistry& registry,
		SceneMigrationStepKind kind, int fromVersion, int toVersion) {
	return fromVersion < toVersion && !findPath(registry, kind, fromVersion, toVersion).empty();
}

bool sceneMigrationAvailable(const SceneMigrationRegistry& registry,
		const SceneCompatibilityProbeResult& probe) {
	if (probe.classification == SceneCompatibilityClass::Newer
			|| probe.classification == SceneCompatibilityClass::Malformed
			|| probe.classification == SceneCompatibilityClass::Current)
		return false;
	const bool schemaOk = probe.schemaVersion >= kCurrentSceneSchemaVersion
			|| sceneMigrationPathAvailable(registry, SceneMigrationStepKind::Schema,
					probe.schemaVersion, kCurrentSceneSchemaVersion);
	const bool bundleOk = probe.sourceKind != SceneSourceKind::Bundle || !probe.bundleVersion
			|| *probe.bundleVersion >= kCurrentSceneBundleVersion
			|| sceneMigrationPathAvailable(registry, SceneMigrationStepKind::Bundle,
					*probe.bundleVersion, kCurrentSceneBundleVersion);
	return schemaOk && bundleOk;
}

bool SceneMigrationResult::success() const {
	return migrated && !hasErrors(diagnostics);
}

bool applySceneMigrationChain(const SceneMigrationRegistry& registry,
		SceneMigrationStepKind kind, int fromVersion, int toVersion,
		const fs::path& stagedScene, std::vector<SceneDiagnostic>& diagnostics) {
	if (fromVersion == toVersion)
		return true;
	if (fromVersion > toVersion) {
		addDiagnostic(diagnostics, "scene.migration.version",
			"Migration steps cannot downgrade a scene version", stagedScene.string());
		return false;
	}
	const std::vector<const SceneMigrationStep*> chain = findPath(registry, kind,
			fromVersion, toVersion);
	if (chain.empty()) {
		addDiagnostic(diagnostics, "scene.migration.unavailable",
			"No registered migration path reaches the current scene format", stagedScene.string());
		return false;
	}
	for (const SceneMigrationStep* step : chain) {
		const std::size_t diagnosticCount = diagnostics.size();
		if (!step->transform(stagedScene, diagnostics)) {
			if (diagnostics.size() == diagnosticCount)
				addDiagnostic(diagnostics, "scene.migration.step",
					"Registered migration step failed" + (step->id.empty() ? std::string() : ": " + step->id),
					stagedScene.string());
			return false;
		}
	}
	return true;
}

SceneMigrationResult migrateSceneCopy(const std::string& sourcePath,
		const std::string& destinationPath, const SceneMigrationRegistry& registry) {
	SceneMigrationResult result;
	const fs::path source(sourcePath);
	const fs::path destination(destinationPath);
	std::error_code ec;
	const auto sourceStatus = fs::symlink_status(source, ec);
	if (ec || (sourceStatus.type() != fs::file_type::directory
			&& sourceStatus.type() != fs::file_type::regular)) {
		addDiagnostic(result.diagnostics, "scene.migration.source",
				"Migration source is missing, invalid, or a symlink", sourcePath);
		return result;
	}
	if (!validDestination(destination, source, result.diagnostics))
		return result;
	const SceneCompatibilityProbeResult probe = probeSceneCompatibility(sourcePath, registry);
	result.diagnostics.insert(result.diagnostics.end(), probe.diagnostics.begin(), probe.diagnostics.end());
	if (probe.classification != SceneCompatibilityClass::OlderMigratable) {
		addDiagnostic(result.diagnostics, "scene.migration.unavailable",
				"No registered migration path reaches the current scene format", sourcePath);
		return result;
	}

	const fs::path tempParent = fs::temp_directory_path(ec);
	if (ec || tempParent.empty()) {
		addDiagnostic(result.diagnostics, "scene.migration.staging",
				"Cannot locate a temporary directory for migration");
		return result;
	}
	fs::path staging;
	if (!createPrivateDirectory(tempParent, "egscene-migrate-", staging)) {
		addDiagnostic(result.diagnostics, "scene.migration.staging",
				"Cannot create a private migration staging directory");
		return result;
	}
	struct Cleanup {
		fs::path path;
		~Cleanup() {
			if (!path.empty()) {
				std::error_code ec;
				fs::remove_all(path, ec);
			}
		}
	} cleanup{staging};

	bool staged = false;
	if (probe.sourceKind == SceneSourceKind::Bundle) {
		const SceneSaveResult extracted = extractSceneBundleForMigration(sourcePath, staging.string());
		result.diagnostics.insert(result.diagnostics.end(), extracted.diagnostics.begin(), extracted.diagnostics.end());
		staged = extracted.success();
	} else {
		staged = copyDirectoryTree(source, staging, result.diagnostics);
	}
	if (!staged)
		return result;

	if (!applySceneMigrationChain(registry, SceneMigrationStepKind::Schema,
			probe.schemaVersion, kCurrentSceneSchemaVersion, staging, result.diagnostics))
		return result;
	if (probe.sourceKind == SceneSourceKind::Bundle && probe.bundleVersion
				&& *probe.bundleVersion < kCurrentSceneBundleVersion) {
		if (!applySceneMigrationChain(registry, SceneMigrationStepKind::Bundle,
				*probe.bundleVersion, kCurrentSceneBundleVersion, staging, result.diagnostics))
			return result;
	}

	const SceneLoadResult loaded = loadScene(staging.string());
	result.diagnostics.insert(result.diagnostics.end(), loaded.diagnostics.begin(), loaded.diagnostics.end());
	if (hasErrors(loaded.diagnostics))
		return result;
	std::string extension = destination.extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
		return static_cast<char>(std::tolower(value));
	});
	const bool destinationBundle = extension == ".egscene";
	const SceneSaveResult saved = destinationBundle
			? saveSceneBundle(loaded.scene, destination.string())
			: saveScene(loaded.scene, destination.string());
	result.diagnostics.insert(result.diagnostics.end(), saved.diagnostics.begin(), saved.diagnostics.end());
	if (!saved.success())
		return result;
	result.migrated = true;
	return result;
}
