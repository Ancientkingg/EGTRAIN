#include "scene/SceneBundle.h"
#include "scene/SceneCompatibility.h"
#include "scene/SceneMigration.h"
#include "scene/SceneModel.h"
#include "scene/SceneWriter.h"
#include "io/third_party/miniz/miniz.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

struct TempDir {
	fs::path path;
	TempDir() {
		path = fs::temp_directory_path() / ("scene_compatibility_test_"
				+ std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		fs::create_directories(path);
	}
	~TempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

static bool writeManifest(const fs::path& path, int schemaVersion) {
	std::ifstream input(path / "scene.json");
	json manifest;
	input >> manifest;
	manifest["schema_version"] = schemaVersion;
	std::ofstream output(path / "scene.json", std::ios::binary | std::ios::trunc);
	output << manifest.dump(4) << "\n";
	return static_cast<bool>(output);
}

static bool writeNewerBundle(const fs::path& path, const char* unsafeName = nullptr) {
	mz_zip_archive writer{};
	if (!mz_zip_writer_init_file(&writer, path.string().c_str(), 0))
		return false;
	const std::string manifest = json({{"format", "egscene"},
		{"bundle_version", kCurrentSceneBundleVersion + 1},
		{"schema_version", kCurrentSceneSchemaVersion}}).dump();
	const char* futureName = unsafeName ? unsafeName : "future-layout.json";
	bool ok = mz_zip_writer_add_mem(&writer, "scene.json", manifest.data(), manifest.size(), MZ_BEST_COMPRESSION)
			&& mz_zip_writer_add_mem(&writer, futureName, "future", 6, MZ_BEST_COMPRESSION)
			&& mz_zip_writer_finalize_archive(&writer);
	ok = mz_zip_writer_end(&writer) && ok;
	return ok;
}

static bool writeBundleVersion(const fs::path& path, const fs::path& source, int bundleVersion) {
	mz_zip_archive writer{};
	if (!mz_zip_writer_init_file(&writer, path.string().c_str(), 0))
		return false;
	bool ok = true;
	for (const char* name : {"scene.json", "infrastructure.json", "stations.json",
			"signalling.json", "rolling_stock.json", "services.json", "scenarios.json"}) {
		std::ifstream input(source / name, std::ios::binary);
		std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		if (!input)
			ok = false;
		if (ok && std::string(name) == "scene.json") {
			try {
				json manifest = json::parse(contents);
				manifest["format"] = "egscene";
				manifest["bundle_version"] = bundleVersion;
				contents = manifest.dump(4) + "\n";
			} catch (const json::exception&) {
				ok = false;
			}
		}
		if (ok && !mz_zip_writer_add_mem(&writer, name, contents.data(), contents.size(), MZ_BEST_COMPRESSION))
			ok = false;
		if (!ok)
			break;
	}
	if (ok)
		ok = mz_zip_writer_add_mem(&writer, "legacy-data.json", "legacy", 6, MZ_BEST_COMPRESSION) != 0;
	if (ok)
		ok = mz_zip_writer_finalize_archive(&writer) != 0;
	ok = mz_zip_writer_end(&writer) && ok;
	return ok;
}

static std::string readFileBytes(const fs::path& path) {
	std::ifstream input(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Usage: test_scenecompatibility <scene-directory>\n";
		return 1;
	}
	TempDir temp;
	bool ok = true;
	const fs::path source = temp.path / "source";
	const SceneLoadResult loaded = loadScene(argv[1]);
	ok &= expect(!hasErrors(loaded.diagnostics), "fixture scene loads");
	ok &= expect(saveScene(loaded.scene, source.string()).success(), "fixture scene saves");
	const SceneCompatibilityProbeResult current = probeSceneCompatibility(source.string());
	ok &= expect(current.classification == SceneCompatibilityClass::Current,
			"current directory is current");
	ok &= expect(current.savedWithAppVersion == EGTRAIN_APP_VERSION,
			"saved-with provenance is reported");
	json manifest;
	{
		std::ifstream input(source / "scene.json");
		input >> manifest;
	}
	manifest.erase("saved_with_app_version");
	{
		std::ofstream output(source / "scene.json", std::ios::binary | std::ios::trunc);
		output << manifest.dump(4) << "\n";
	}
	const SceneCompatibilityProbeResult withoutProvenance = probeSceneCompatibility(source.string());
	ok &= expect(withoutProvenance.classification == SceneCompatibilityClass::Current
			&& withoutProvenance.savedWithAppVersion.empty(), "missing provenance remains valid");
	ok &= expect(writeManifest(source, 0), "older fixture writes");
	const SceneCompatibilityProbeResult older = probeSceneCompatibility(source.string());
	ok &= expect(older.classification == SceneCompatibilityClass::OlderUnsupported,
			"empty production registry rejects older schema");
	ok &= expect(writeManifest(source, kCurrentSceneSchemaVersion + 1), "newer fixture writes");
	const SceneCompatibilityProbeResult newer = probeSceneCompatibility(source.string());
	ok &= expect(newer.classification == SceneCompatibilityClass::Newer, "newer schema is reported");
	{
		std::ofstream output(source / "scene.json", std::ios::binary | std::ios::trunc);
		output << "not json\n";
	}
	const SceneCompatibilityProbeResult malformed = probeSceneCompatibility(source.string());
	ok &= expect(malformed.classification == SceneCompatibilityClass::Malformed,
			"malformed manifest is reported");

	const fs::path migrationSource = temp.path / "migration-source";
	ok &= expect(saveScene(loaded.scene, migrationSource.string()).success(), "migration source saves");
	ok &= expect(writeManifest(migrationSource, 0), "test-only migration fixture writes");
	const std::string originalMigrationBytes = readSceneDirectorySnapshot(migrationSource.string()).bytes;
	SceneMigrationRegistry registry;
	registry.addSchemaStep(SceneMigrationStep(0, kCurrentSceneSchemaVersion,
		[](const fs::path& staged, std::vector<SceneDiagnostic>& diagnostics) {
			std::ifstream input(staged / "scene.json");
			json value;
			try {
				input >> value;
			} catch (const json::exception& error) {
				SceneDiagnostic diagnostic;
				diagnostic.severity = SceneSeverity::Error;
				diagnostic.code = "test.migration.parse";
				diagnostic.message = error.what();
				diagnostics.push_back(diagnostic);
				return false;
			}
			value["schema_version"] = kCurrentSceneSchemaVersion;
			std::ofstream output(staged / "scene.json", std::ios::binary | std::ios::trunc);
			output << value.dump(4) << "\n";
			return static_cast<bool>(output);
		}, "test-only-step-zero-to-one"));
	const SceneCompatibilityProbeResult migratable = probeSceneCompatibility(migrationSource.string(), registry);
	ok &= expect(migratable.classification == SceneCompatibilityClass::OlderMigratable,
			"registered schema chain is migratable");
	const fs::path destination = temp.path / "upgraded";
	const SceneMigrationResult migrated = migrateSceneCopy(migrationSource.string(), destination.string(), registry);
	ok &= expect(migrated.success() && fs::is_directory(destination), "migration publishes a copy");
	ok &= expect(readSceneDirectorySnapshot(migrationSource.string()).bytes == originalMigrationBytes,
			"successful migration leaves source bytes unchanged");
	const fs::path failedDestination = temp.path / "failed-upgrade";
	SceneMigrationRegistry failingRegistry;
	failingRegistry.addSchemaStep(SceneMigrationStep(0, kCurrentSceneSchemaVersion,
		[](const fs::path&, std::vector<SceneDiagnostic>&) { return false; }));
	const SceneMigrationResult failed = migrateSceneCopy(migrationSource.string(), failedDestination.string(), failingRegistry);
	ok &= expect(!failed.success() && !failed.diagnostics.empty() && !fs::exists(failedDestination),
			"failed migration leaves no destination");
	ok &= expect(readSceneDirectorySnapshot(migrationSource.string()).bytes == originalMigrationBytes,
			"failed migration leaves source bytes unchanged");
	const fs::path existingDestination = temp.path / "existing-upgrade";
	fs::create_directories(existingDestination);
	const fs::path existingMarker = existingDestination / "keep.txt";
	{
		std::ofstream marker(existingMarker);
		marker << "unchanged";
	}
	const SceneMigrationResult existing = migrateSceneCopy(migrationSource.string(), existingDestination.string(), registry);
	ok &= expect(!existing.success() && readFileBytes(existingMarker) == "unchanged",
		"existing destination is rejected before publication");

	const fs::path chainStage = temp.path / "chain-stage";
	fs::create_directories(chainStage);
	{
		std::ofstream marker(chainStage / "chain.txt");
		marker << "1";
	}
	SceneMigrationRegistry chainRegistry;
	chainRegistry.addSchemaStep(SceneMigrationStep(1, 2,
		[](const fs::path& staged, std::vector<SceneDiagnostic>&) {
			std::ofstream output(staged / "chain.txt", std::ios::app);
			output << "2";
			return static_cast<bool>(output);
		}, "test-only-chain-1-to-2"));
	chainRegistry.addSchemaStep(SceneMigrationStep(2, 3,
		[](const fs::path& staged, std::vector<SceneDiagnostic>&) {
			std::ofstream output(staged / "chain.txt", std::ios::app);
			output << "3";
			return static_cast<bool>(output);
		}, "test-only-chain-2-to-3"));
	std::vector<SceneDiagnostic> chainDiagnostics;
	ok &= expect(applySceneMigrationChain(chainRegistry, SceneMigrationStepKind::Schema,
			1, 3, chainStage, chainDiagnostics)
			&& readFileBytes(chainStage / "chain.txt") == "123",
		"test-only migration chain applies 1 to 2 to 3");
	SceneMigrationRegistry branchingRegistry;
	branchingRegistry.addSchemaStep(SceneMigrationStep(1, 2,
		[](const fs::path&, std::vector<SceneDiagnostic>&) { return true; }));
	branchingRegistry.addSchemaStep(SceneMigrationStep(1, 3,
		[](const fs::path&, std::vector<SceneDiagnostic>&) { return true; }));
	ok &= expect(sceneMigrationPathAvailable(branchingRegistry, SceneMigrationStepKind::Schema, 1, 3),
		"a dead-end step does not hide a later valid migration path");

	const fs::path bundle = temp.path / "current.egscene";
	SceneModel currentScene = loaded.scene;
	const SceneSaveResult bundleSaved = saveSceneBundle(currentScene, bundle.string());
	ok &= expect(bundleSaved.success(), "current bundle saves");
	const SceneCompatibilityProbeResult bundled = probeSceneCompatibility(bundle.string());
	ok &= expect(bundled.classification == SceneCompatibilityClass::Current
			&& bundled.bundleVersion && *bundled.bundleVersion == kCurrentSceneBundleVersion,
			"current bundle reports its actual version");
	const SceneLoadResult loadedBundle = loadScenePath(bundle.string());
	ok &= expect(!hasErrors(loadedBundle.diagnostics) && loadedBundle.bundleVersion
			&& *loadedBundle.bundleVersion == kCurrentSceneBundleVersion,
			"current bundle version propagates through the normal loader");
	const fs::path olderBundle = temp.path / "older.egscene";
	const fs::path bundleSource = temp.path / "bundle-source";
	ok &= expect(saveScene(loaded.scene, bundleSource.string()).success(), "bundle migration source saves");
	ok &= expect(writeBundleVersion(olderBundle, bundleSource, kCurrentSceneBundleVersion - 1),
		"older bundle fixture writes");
	const SceneCompatibilityProbeResult olderBundleProbe = probeSceneCompatibility(olderBundle.string());
	ok &= expect(olderBundleProbe.classification == SceneCompatibilityClass::OlderUnsupported
			&& olderBundleProbe.schemaVersion == kCurrentSceneSchemaVersion
			&& olderBundleProbe.bundleVersion && *olderBundleProbe.bundleVersion == kCurrentSceneBundleVersion - 1,
			"older bundle is independent from current schema support");
	SceneMigrationRegistry bundleRegistry;
	bundleRegistry.addBundleStep(SceneMigrationStep(kCurrentSceneBundleVersion - 1,
		kCurrentSceneBundleVersion,
		[](const fs::path& staged, std::vector<SceneDiagnostic>&) {
			return fs::is_regular_file(staged / "legacy-data.json");
		},
		"test-only-bundle-step"));
	const SceneCompatibilityProbeResult migratableBundle = probeSceneCompatibility(olderBundle.string(), bundleRegistry);
	ok &= expect(migratableBundle.classification == SceneCompatibilityClass::OlderMigratable,
		"registered bundle step makes only the older bundle migratable");
	const std::string originalOlderBundleBytes = readFileBytes(olderBundle);
	const fs::path upgradedBundle = temp.path / "older-upgraded.egscene";
	const SceneMigrationResult migratedBundle = migrateSceneCopy(olderBundle.string(),
		upgradedBundle.string(), bundleRegistry);
	const SceneCompatibilityProbeResult upgradedBundleProbe = probeSceneCompatibility(upgradedBundle.string());
	ok &= expect(migratedBundle.success() && upgradedBundleProbe.classification == SceneCompatibilityClass::Current
			&& upgradedBundleProbe.bundleVersion && *upgradedBundleProbe.bundleVersion == kCurrentSceneBundleVersion,
			"bundle-only migration repackages a current bundle");
	ok &= expect(readFileBytes(olderBundle) == originalOlderBundleBytes,
			"bundle migration leaves source bytes unchanged");
	const fs::path newerBundle = temp.path / "newer.egscene";
	ok &= expect(writeNewerBundle(newerBundle), "newer bundle fixture writes");
	const SceneCompatibilityProbeResult newerBundleProbe = probeSceneCompatibility(newerBundle.string());
	ok &= expect(newerBundleProbe.classification == SceneCompatibilityClass::Newer,
			"newer bundle layout is classified without extraction");
	const fs::path hostileNewerBundle = temp.path / "hostile-newer.egscene";
	ok &= expect(writeNewerBundle(hostileNewerBundle, "../future.json"), "hostile newer bundle fixture writes");
	const SceneCompatibilityProbeResult hostileProbe = probeSceneCompatibility(hostileNewerBundle.string());
	ok &= expect(hostileProbe.classification == SceneCompatibilityClass::Malformed,
			"newer bundle keeps generic ZIP path safety");
	ok &= expect(productionSceneMigrationRegistry().schemaSteps().empty()
			&& productionSceneMigrationRegistry().bundleSteps().empty(),
			"production migration registry starts empty");

	if (!ok)
		return 1;
	std::cout << "all SceneCompatibility tests passed\n";
	return 0;
}
