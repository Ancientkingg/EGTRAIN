#include "scene/SceneBundle.h"
#include "scene/SceneCompatibility.h"
#include "scene/SceneDiagnostic.h"
#include "scene/SceneMigration.h"
#include "scene/SceneModel.h"
#include "scene/SceneValidator.h"
#include "scene/SceneWriter.h"
#include "io/third_party/miniz/miniz.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>

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
		path = fs::temp_directory_path() / ("scene_compatibility_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		fs::create_directories(path);
	}
	~TempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

// Japanese and Thai characters, as UTF-8. No Windows code page holds both.
static const std::string kUnicodeName = "scene-\xE6\x97\xA5\xE6\x9C\xAC-\xE0\xB8\x81";

static bool writeManifest(const fs::path& path, const json& schemaVersion) {
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
	if (!mz_zip_writer_init_file(&writer, path.u8string().c_str(), 0))
		return false;
	const std::string manifest = json({{"format", "egscene"},
										  {"bundle_version", kCurrentSceneBundleVersion + 1},
										  {"schema_version", kCurrentSceneSchemaVersion}})
									 .dump();
	const char* futureName = unsafeName ? unsafeName : "future-layout.json";
	bool ok = mz_zip_writer_add_mem(&writer, "scene.json", manifest.data(), manifest.size(), MZ_BEST_COMPRESSION)
		&& mz_zip_writer_add_mem(&writer, futureName, "future", 6, MZ_BEST_COMPRESSION)
		&& mz_zip_writer_finalize_archive(&writer);
	ok = mz_zip_writer_end(&writer) && ok;
	return ok;
}

static bool writeBundleVersion(const fs::path& path, const fs::path& source, const json& bundleVersion,
	const json& schemaVersion = json()) {
	mz_zip_archive writer{};
	if (!mz_zip_writer_init_file(&writer, path.u8string().c_str(), 0))
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
				if (!schemaVersion.is_null())
					manifest["schema_version"] = schemaVersion;
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

static bool hasDiagnostic(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code,
	SceneSeverity severity) {
	return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const SceneDiagnostic& diagnostic) {
		return diagnostic.code == code && diagnostic.severity == severity;
	});
}

static std::string firstErrorCode(const std::vector<SceneDiagnostic>& diagnostics) {
	for (const SceneDiagnostic& diagnostic : diagnostics) {
		if (diagnostic.severity == SceneSeverity::Error)
			return diagnostic.code;
	}
	return {};
}

// True when nothing was created at the destination or next to it in its folder.
static bool leavesNoCopy(const fs::path& destination) {
	std::error_code ec;
	const bool exists = fs::exists(destination, ec);
	fs::directory_iterator folder(destination.parent_path(), ec);
	return !exists && !ec && folder == fs::directory_iterator();
}

// Gives a directory its write permission back, so that the temporary directory can be removed.
struct RestorePermissions {
	fs::path path;
	~RestorePermissions() {
		std::error_code ec;
		fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace, ec);
	}
};

// Sets the keys of scene.json; a null value removes the key.
static bool changeManifest(const fs::path& scene, const json& changes) {
	json manifest;
	{
		std::ifstream input(scene / "scene.json");
		input >> manifest;
	}
	for (const auto& change : changes.items()) {
		if (change.value().is_null())
			manifest.erase(change.key());
		else
			manifest[change.key()] = change.value();
	}
	std::ofstream output(scene / "scene.json", std::ios::binary | std::ios::trunc);
	output << manifest.dump(4) << "\n";
	return static_cast<bool>(output);
}

static bool writeText(const fs::path& path, const std::string& contents) {
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output << contents;
	return static_cast<bool>(output);
}

static bool contains(const std::string& text, const std::string& part) {
	return text.find(part) != std::string::npos;
}

// The one diagnostic of a rejected scene as the user reads it, or an empty text when the probe has not exactly one.
static std::string reasonOf(const SceneCompatibilityProbeResult& probe, const char* code) {
	if (probe.diagnostics.size() != 1 || probe.diagnostics.front().code != code || probe.diagnostics.front().file != "scene.json")
		return {};
	return toDisplayText(probe.diagnostics.front());
}

// The probe names why it rejects a scene, and says nothing about a scene that it accepts or that a migration can convert.
static bool checkRejectionReasons(const SceneModel& model, const fs::path& folder) {
	bool ok = true;
	const fs::path scene = folder / "reasons";
	ok &= expect(saveScene(model, scene.string()).success(), "reason fixture saves");
	const SceneCompatibilityProbeResult current = probeSceneCompatibility(scene.string());
	ok &= expect(current.classification == SceneCompatibilityClass::Current && current.diagnostics.empty(),
		"a current scene gets no diagnostic");

	ok &= expect(changeManifest(scene, {{"schema_version", 0}, {"saved_with_app_version", "0.9"}}), "older reason fixture writes");
	const SceneCompatibilityProbeResult older = probeSceneCompatibility(scene.string());
	const std::string olderReason = reasonOf(older, "scene.compatibility.older");
	ok &= expect(older.classification == SceneCompatibilityClass::OlderUnsupported && !olderReason.empty()
			&& contains(olderReason, "schema 0 (saved with EGTRAIN 0.9)") && contains(olderReason, "saved by an older version of EGTRAIN") && contains(olderReason, "reads schema 1.")
			&& contains(olderReason, "Import Legacy Case") && !contains(olderReason, "bundle layout"),
		"an older schema is rejected with its number, the saved-with version and the way out");
	const fs::path refusedCopy = folder / "refused-copy";
	const SceneMigrationResult refused = migrateSceneCopy(scene.string(), refusedCopy.string(), productionSceneMigrationRegistry());
	ok &= expect(!refused.success() && hasDiagnostic(refused.diagnostics, "scene.compatibility.older", SceneSeverity::Error)
			&& !fs::exists(refusedCopy),
		"the copy upgrade of an older scene reports the reason and writes nothing");

	ok &= expect(changeManifest(scene, {{"schema_version", kCurrentSceneSchemaVersion + 1}, {"saved_with_app_version", nullptr}}),
		"newer reason fixture writes");
	const SceneCompatibilityProbeResult newer = probeSceneCompatibility(scene.string());
	const std::string newerReason = reasonOf(newer, "scene.compatibility.newer");
	ok &= expect(newer.classification == SceneCompatibilityClass::Newer && !newerReason.empty()
			&& contains(newerReason, "schema " + std::to_string(kCurrentSceneSchemaVersion + 1) + " (saved-with version not recorded)")
			&& contains(newerReason, "saved by a newer version of EGTRAIN") && contains(newerReason, "Check for Updates"),
		"a newer schema is rejected with its number and the way out");

	// A long saved_with_app_version is cut before a character that the cut would split.
	const std::string longVersion = std::string(63, 'a') + "\xE6\x97\xA5" + std::string(10, 'b');
	ok &= expect(changeManifest(scene, {{"saved_with_app_version", longVersion}}), "long version fixture writes");
	const std::string cutReason = reasonOf(probeSceneCompatibility(scene.string()), "scene.compatibility.newer");
	ok &= expect(contains(cutReason, "saved with EGTRAIN " + std::string(63, 'a') + "...)") && !contains(cutReason, "bbb"),
		"a long saved-with version is cut at a character boundary");

	// The draft layout: a legacy_source and an infrastructure.json without nodes and arcs.
	const json emptyInfrastructure = {{"arcs", json::array()}, {"nodes", json::array()}};
	ok &= expect(changeManifest(scene, {{"schema_version", kCurrentSceneSchemaVersion}, {"saved_with_app_version", nullptr}, {"legacy_source", "Input/Input_EGTRAIN_Paimpol"}}),
		"draft reason fixture writes");
	const SceneCompatibilityProbeResult onlyLegacySource = probeSceneCompatibility(scene.string());
	ok &= expect(onlyLegacySource.classification == SceneCompatibilityClass::Current && onlyLegacySource.diagnostics.empty(),
		"legacy_source alone does not mark the draft layout");
	ok &= expect(writeText(scene / "infrastructure.json", emptyInfrastructure.dump()), "empty infrastructure writes");
	const SceneCompatibilityProbeResult draft = probeSceneCompatibility(scene.string());
	const std::string draftReason = reasonOf(draft, "scene.compatibility.draft");
	ok &= expect(draft.classification == SceneCompatibilityClass::OlderUnsupported && !draftReason.empty()
			&& contains(draftReason, "draft layout that EGTRAIN saved before 2026-08-08 (schema 1, saved-with version not recorded)")
			&& contains(draftReason, "not supported") && contains(draftReason, "File > Import Legacy Case..."),
		"legacy_source with an empty infrastructure is the draft layout");
	ok &= expect(writeText(scene / "infrastructure.json", "{}"), "infrastructure without lists writes");
	ok &= expect(probeSceneCompatibility(scene.string()).classification == SceneCompatibilityClass::OlderUnsupported,
		"an infrastructure without the lists has no nodes and no arcs");
	ok &= expect(writeText(scene / "infrastructure.json", R"({"arcs": [], "nodes": [1]})"), "infrastructure with a node writes");
	ok &= expect(probeSceneCompatibility(scene.string()).classification == SceneCompatibilityClass::Current,
		"legacy_source with a node is not the draft layout");
	ok &= expect(writeText(scene / "infrastructure.json", R"({"arcs": [1], "nodes": []})"), "infrastructure with an arc writes");
	ok &= expect(probeSceneCompatibility(scene.string()).classification == SceneCompatibilityClass::Current,
		"legacy_source with an arc is not the draft layout");
	ok &= expect(writeText(scene / "infrastructure.json", "not json"), "damaged infrastructure writes");
	ok &= expect(probeSceneCompatibility(scene.string()).classification == SceneCompatibilityClass::Current,
		"an unreadable infrastructure is left to the loader");
	std::error_code ec;
	fs::remove(scene / "infrastructure.json", ec);
	ok &= expect(probeSceneCompatibility(scene.string()).classification == SceneCompatibilityClass::Current,
		"a missing infrastructure is left to the loader");
	ok &= expect(writeText(scene / "infrastructure.json", emptyInfrastructure.dump()), "empty infrastructure writes again");
	ok &= expect(changeManifest(scene, {{"legacy_source", nullptr}}), "legacy_source is removed");
	ok &= expect(probeSceneCompatibility(scene.string()).classification == SceneCompatibilityClass::Current,
		"an empty infrastructure alone does not mark the draft layout");

	// A schema number that is rejected on its own is named by the number. A migration does not convert a draft.
	ok &= expect(changeManifest(scene, {{"legacy_source", "Input/Input_EGTRAIN_Paimpol"}, {"schema_version", kCurrentSceneSchemaVersion + 1}}),
		"newer draft fixture writes");
	const SceneCompatibilityProbeResult newerDraft = probeSceneCompatibility(scene.string());
	ok &= expect(newerDraft.classification == SceneCompatibilityClass::Newer && !reasonOf(newerDraft, "scene.compatibility.newer").empty(),
		"a newer draft is rejected for its number");
	ok &= expect(changeManifest(scene, {{"schema_version", 0}}), "migratable draft fixture writes");
	SceneMigrationRegistry registry;
	registry.addSchemaStep(SceneMigrationStep(
		0, kCurrentSceneSchemaVersion, [](const fs::path&, std::vector<SceneDiagnostic>&) { return true; }, "test-only-step-zero-to-one"));
	const SceneCompatibilityProbeResult migratableDraft = probeSceneCompatibility(scene.string(), registry);
	ok &= expect(migratableDraft.classification == SceneCompatibilityClass::OlderUnsupported
			&& !reasonOf(migratableDraft, "scene.compatibility.draft").empty(),
		"a draft is not offered a copy upgrade");
	ok &= expect(changeManifest(scene, {{"legacy_source", nullptr}}), "migratable fixture writes");
	const SceneCompatibilityProbeResult migratable = probeSceneCompatibility(scene.string(), registry);
	ok &= expect(migratable.classification == SceneCompatibilityClass::OlderMigratable && migratable.diagnostics.empty(),
		"a scene that a migration converts gets no diagnostic");
	return ok;
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
	const SceneLoadResult validLoaded = loadScene((fs::path(argv[1]).parent_path() / "line").string());
	ok &= expect(!hasErrors(validLoaded.diagnostics), "valid fixture scene loads");
	ok &= expect(!hasErrors(validateScene(validLoaded.scene)), "valid fixture scene passes semantic validation");
	ok &= expect(saveScene(loaded.scene, source.string()).success(), "fixture scene saves");
	const SceneCompatibilityProbeResult current = probeSceneCompatibility(source.string());
	ok &= expect(current.classification == SceneCompatibilityClass::Current,
		"current directory is current");
	ok &= expect(current.savedWithAppVersion == EGTRAIN_APP_VERSION,
		"saved-with provenance is reported");
	ok &= expect(current.diagnostics.empty(), "a current directory gets no diagnostic");
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
			&& withoutProvenance.savedWithAppVersion.empty(),
		"missing provenance remains valid");
	ok &= expect(writeManifest(source, 0), "older fixture writes");
	const SceneCompatibilityProbeResult older = probeSceneCompatibility(source.string());
	ok &= expect(older.classification == SceneCompatibilityClass::OlderUnsupported,
		"empty production registry rejects older schema");
	ok &= expect(!reasonOf(older, "scene.compatibility.older").empty(), "older schema is rejected with one reason");
	ok &= expect(writeManifest(source, kCurrentSceneSchemaVersion + 1), "newer fixture writes");
	const SceneCompatibilityProbeResult newer = probeSceneCompatibility(source.string());
	ok &= expect(newer.classification == SceneCompatibilityClass::Newer, "newer schema is reported");
	ok &= expect(!reasonOf(newer, "scene.compatibility.newer").empty(), "newer schema is rejected with one reason");
	const long long wrapsToCurrent = (1LL << 32) + kCurrentSceneSchemaVersion;
	for (const json& badVersion : {json(wrapsToCurrent), json(static_cast<long long>(INT_MAX) + 1),
			 json(1.5), json("1"), json(18446744073709551615ULL)}) {
		ok &= expect(writeManifest(source, badVersion), "out-of-range schema fixture writes");
		const SceneCompatibilityProbeResult badProbe = probeSceneCompatibility(source.string());
		ok &= expect(badProbe.classification == SceneCompatibilityClass::Malformed
				&& !badProbe.diagnostics.empty()
				&& badProbe.diagnostics.front().code == "scene.compatibility.schema"
				&& badProbe.diagnostics.front().file == "scene.json",
			"schema_version outside the int range or not an integer is rejected");
	}
	ok &= expect(writeManifest(source, INT_MAX), "int limit schema fixture writes");
	ok &= expect(probeSceneCompatibility(source.string()).classification == SceneCompatibilityClass::Newer,
		"schema_version at the int limit is still read");
	ok &= expect(writeManifest(source, -1), "negative schema fixture writes");
	ok &= expect(probeSceneCompatibility(source.string()).classification
			== SceneCompatibilityClass::OlderUnsupported,
		"negative schema_version is read as older");
	{
		std::ofstream output(source / "scene.json", std::ios::binary | std::ios::trunc);
		output << "not json\n";
	}
	const SceneCompatibilityProbeResult malformed = probeSceneCompatibility(source.string());
	ok &= expect(malformed.classification == SceneCompatibilityClass::Malformed,
		"malformed manifest is reported");

	const fs::path migrationSource = temp.path / "migration-source";
	ok &= expect(saveScene(validLoaded.scene, migrationSource.string()).success(), "migration source saves");
	ok &= expect(writeManifest(migrationSource, 0), "test-only migration fixture writes");
	const std::string originalMigrationBytes = readSceneDirectorySnapshot(migrationSource.string()).bytes;
	SceneMigrationRegistry registry;
	registry.addSchemaStep(SceneMigrationStep(0, kCurrentSceneSchemaVersion, [](const fs::path& staged, std::vector<SceneDiagnostic>& diagnostics) {
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
			return static_cast<bool>(output); }, "test-only-step-zero-to-one"));
	const SceneCompatibilityProbeResult migratable = probeSceneCompatibility(migrationSource.string(), registry);
	ok &= expect(migratable.classification == SceneCompatibilityClass::OlderMigratable && migratable.diagnostics.empty(),
		"registered schema chain is migratable");
	const fs::path destination = temp.path / "upgraded";
	const SceneMigrationResult migrated = migrateSceneCopy(migrationSource.string(), destination.string(), registry);
	ok &= expect(migrated.success() && fs::is_directory(destination), "migration publishes a copy");
	ok &= expect(readSceneDirectorySnapshot(migrationSource.string()).bytes == originalMigrationBytes,
		"successful migration leaves source bytes unchanged");
	const SceneLoadResult reopened = loadScenePath(destination.string());
	ok &= expect(!hasErrors(reopened.diagnostics) && !hasErrors(validateScene(reopened.scene)),
		"upgraded directory copy reopens without errors");
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
	chainRegistry.addSchemaStep(SceneMigrationStep(1, 2, [](const fs::path& staged, std::vector<SceneDiagnostic>&) {
			std::ofstream output(staged / "chain.txt", std::ios::app);
			output << "2";
			return static_cast<bool>(output); }, "test-only-chain-1-to-2"));
	chainRegistry.addSchemaStep(SceneMigrationStep(2, 3, [](const fs::path& staged, std::vector<SceneDiagnostic>&) {
			std::ofstream output(staged / "chain.txt", std::ios::app);
			output << "3";
			return static_cast<bool>(output); }, "test-only-chain-2-to-3"));
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
	ok &= expect(saveScene(validLoaded.scene, bundleSource.string()).success(), "bundle migration source saves");
	ok &= expect(writeBundleVersion(olderBundle, bundleSource, kCurrentSceneBundleVersion - 1),
		"older bundle fixture writes");
	const SceneCompatibilityProbeResult olderBundleProbe = probeSceneCompatibility(olderBundle.string());
	ok &= expect(olderBundleProbe.classification == SceneCompatibilityClass::OlderUnsupported
			&& olderBundleProbe.schemaVersion == kCurrentSceneSchemaVersion
			&& olderBundleProbe.bundleVersion && *olderBundleProbe.bundleVersion == kCurrentSceneBundleVersion - 1,
		"older bundle is independent from current schema support");
	const std::string olderBundleReason = reasonOf(olderBundleProbe, "scene.compatibility.older");
	ok &= expect(contains(olderBundleReason, "schema 1, bundle layout 0 (saved") && contains(olderBundleReason, "reads schema 1, bundle layout 1."),
		"older bundle reason names both layout numbers");
	SceneMigrationRegistry bundleRegistry;
	bundleRegistry.addBundleStep(SceneMigrationStep(
		kCurrentSceneBundleVersion - 1, kCurrentSceneBundleVersion,
		[](const fs::path& staged, std::vector<SceneDiagnostic>&) {
			return fs::is_regular_file(staged / "legacy-data.json");
		},
		"test-only-bundle-step"));
	const SceneCompatibilityProbeResult migratableBundle = probeSceneCompatibility(olderBundle.string(), bundleRegistry);
	ok &= expect(migratableBundle.classification == SceneCompatibilityClass::OlderMigratable && migratableBundle.diagnostics.empty(),
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
	const SceneLoadResult reopenedBundle = loadScenePath(upgradedBundle.string());
	ok &= expect(!hasErrors(reopenedBundle.diagnostics) && !hasErrors(validateScene(reopenedBundle.scene)),
		"upgraded bundle copy reopens without errors");

	// Sources and copies with Unicode names are probed and migrated under those names.
	const fs::path unicodeRoot = temp.path / fs::u8path(kUnicodeName);
	const fs::path unicodeSource = unicodeRoot / fs::u8path(kUnicodeName + "-source");
	ok &= expect(saveScene(validLoaded.scene, unicodeSource.u8string()).success() && writeManifest(unicodeSource, 0),
		"directory with a Unicode name saves as an older scene");
	ok &= expect(probeSceneCompatibility(unicodeSource.u8string(), registry).classification
			== SceneCompatibilityClass::OlderMigratable,
		"directory with a Unicode name is probed");
	const fs::path unicodeUpgraded = unicodeRoot / fs::u8path(kUnicodeName + "-upgraded");
	ok &= expect(migrateSceneCopy(unicodeSource.u8string(), unicodeUpgraded.u8string(), registry).success()
			&& fs::is_regular_file(unicodeUpgraded / "scene.json"),
		"directory with a Unicode name migrates to a copy with a Unicode name");
	const fs::path unicodeOlderBundle = unicodeRoot / fs::u8path(kUnicodeName + "-older.egscene");
	ok &= expect(writeBundleVersion(unicodeOlderBundle, bundleSource, kCurrentSceneBundleVersion - 1),
		"older bundle with a Unicode name writes");
	const fs::path unicodeUpgradedBundle = unicodeRoot / fs::u8path(kUnicodeName + "-upgraded.egscene");
	ok &= expect(migrateSceneCopy(unicodeOlderBundle.u8string(), unicodeUpgradedBundle.u8string(), bundleRegistry).success()
			&& probeSceneCompatibility(unicodeUpgradedBundle.u8string()).classification
				== SceneCompatibilityClass::Current,
		"bundle with a Unicode name migrates to a bundle with a Unicode name");
	const fs::path newerBundle = temp.path / "newer.egscene";
	ok &= expect(writeNewerBundle(newerBundle), "newer bundle fixture writes");
	const SceneCompatibilityProbeResult newerBundleProbe = probeSceneCompatibility(newerBundle.string());
	ok &= expect(newerBundleProbe.classification == SceneCompatibilityClass::Newer,
		"newer bundle layout is classified without extraction");
	ok &= expect(contains(reasonOf(newerBundleProbe, "scene.compatibility.newer"), "schema 1, bundle layout " + std::to_string(kCurrentSceneBundleVersion + 1)),
		"newer bundle reason names both layout numbers");
	const fs::path hostileNewerBundle = temp.path / "hostile-newer.egscene";
	ok &= expect(writeNewerBundle(hostileNewerBundle, "../future.json"), "hostile newer bundle fixture writes");
	const SceneCompatibilityProbeResult hostileProbe = probeSceneCompatibility(hostileNewerBundle.string());
	ok &= expect(hostileProbe.classification == SceneCompatibilityClass::Malformed,
		"newer bundle keeps generic ZIP path safety");
	const fs::path hugeBundle = temp.path / "huge-version.egscene";
	ok &= expect(writeBundleVersion(hugeBundle, bundleSource, (1LL << 32) + kCurrentSceneBundleVersion),
		"oversized bundle version fixture writes");
	const SceneCompatibilityProbeResult hugeBundleProbe = probeSceneCompatibility(hugeBundle.string());
	ok &= expect(hugeBundleProbe.classification == SceneCompatibilityClass::Malformed
			&& !hugeBundleProbe.diagnostics.empty()
			&& hugeBundleProbe.diagnostics.front().code == "scene.bundle.version",
		"oversized bundle_version is rejected");
	ok &= expect(writeBundleVersion(hugeBundle, bundleSource, kCurrentSceneBundleVersion,
					 (1LL << 32) + kCurrentSceneSchemaVersion),
		"oversized bundle schema fixture writes");
	const SceneCompatibilityProbeResult hugeSchemaProbe = probeSceneCompatibility(hugeBundle.string());
	ok &= expect(hugeSchemaProbe.classification == SceneCompatibilityClass::Malformed
			&& !hugeSchemaProbe.diagnostics.empty()
			&& hugeSchemaProbe.diagnostics.front().code == "scene.bundle.schema",
		"oversized bundle schema_version is rejected");
	ok &= expect(productionSceneMigrationRegistry().schemaSteps().empty()
			&& productionSceneMigrationRegistry().bundleSteps().empty(),
		"production migration registry starts empty");

	// Every stopped upgrade below runs into a folder of its own that must stay empty.
	const std::string invalidErrorCode = firstErrorCode(validateScene(loaded.scene));
	ok &= expect(!invalidErrorCode.empty(), "minimal fixture scene has a validation error");

	const fs::path invalidFolder = temp.path / "invalid-result";
	fs::create_directories(invalidFolder);
	const fs::path invalidSource = temp.path / "invalid-source";
	ok &= expect(saveScene(loaded.scene, invalidSource.string()).success(), "invalid model saves");
	ok &= expect(writeManifest(invalidSource, 0), "invalid model fixture writes");
	const std::string originalInvalidBytes = readSceneDirectorySnapshot(invalidSource.string()).bytes;
	const fs::path invalidDestination = invalidFolder / "upgraded";
	const SceneMigrationResult invalidUpgrade = migrateSceneCopy(invalidSource.string(), invalidDestination.string(), registry);
	ok &= expect(!invalidUpgrade.success() && hasDiagnostic(invalidUpgrade.diagnostics, invalidErrorCode, SceneSeverity::Error),
		"upgrade of an invalid result reports the validation error");
	ok &= expect(leavesNoCopy(invalidDestination), "upgrade of an invalid result leaves no copy");
	ok &= expect(readSceneDirectorySnapshot(invalidSource.string()).bytes == originalInvalidBytes,
		"upgrade of an invalid result leaves source bytes unchanged");

	const fs::path invalidBundleFolder = temp.path / "invalid-bundle-result";
	fs::create_directories(invalidBundleFolder);
	const fs::path invalidBundleSource = temp.path / "invalid-bundle-source";
	ok &= expect(saveScene(loaded.scene, invalidBundleSource.string()).success(), "invalid bundle model saves");
	const fs::path invalidBundle = temp.path / "invalid.egscene";
	ok &= expect(writeBundleVersion(invalidBundle, invalidBundleSource, kCurrentSceneBundleVersion - 1),
		"invalid older bundle fixture writes");
	const std::string originalInvalidBundleBytes = readFileBytes(invalidBundle);
	const fs::path invalidBundleDestination = invalidBundleFolder / "upgraded.egscene";
	const SceneMigrationResult invalidBundleUpgrade = migrateSceneCopy(invalidBundle.string(),
		invalidBundleDestination.string(), bundleRegistry);
	ok &= expect(!invalidBundleUpgrade.success()
			&& hasDiagnostic(invalidBundleUpgrade.diagnostics, invalidErrorCode, SceneSeverity::Error),
		"bundle upgrade of an invalid result reports the validation error");
	ok &= expect(leavesNoCopy(invalidBundleDestination), "bundle upgrade of an invalid result leaves no copy");
	ok &= expect(readFileBytes(invalidBundle) == originalInvalidBundleBytes,
		"bundle upgrade of an invalid result leaves source bytes unchanged");

	const std::string colorCode = "scene.service.color.invalid";
	const bool hasService = expect(!validLoaded.scene.services.empty(), "valid fixture scene has a service");
	ok &= hasService;
	if (hasService) {
		SceneModel coloredScene = validLoaded.scene;
		const std::string coloredId = coloredScene.services.front().id;
		coloredScene.services.front().visualizationColor = "red";
		const std::vector<SceneDiagnostic> coloredDiagnostics = validateScene(coloredScene);
		ok &= expect(!hasDiagnostic(validateScene(validLoaded.scene), colorCode, SceneSeverity::Warning),
			"valid fixture scene has no service colour warning");
		ok &= expect(hasDiagnostic(coloredDiagnostics, colorCode, SceneSeverity::Warning) && !hasErrors(coloredDiagnostics),
			"an invalid service colour is only a warning");
		const fs::path coloredSource = temp.path / "colored-source";
		ok &= expect(saveScene(coloredScene, coloredSource.string()).success(), "colored model saves");
		ok &= expect(writeManifest(coloredSource, 0), "colored model fixture writes");
		const fs::path coloredDestination = temp.path / "colored-upgraded";
		const SceneMigrationResult coloredUpgrade = migrateSceneCopy(coloredSource.string(), coloredDestination.string(), registry);
		ok &= expect(coloredUpgrade.success() && fs::is_directory(coloredDestination),
			"a warning does not stop the upgrade");
		ok &= expect(hasDiagnostic(coloredUpgrade.diagnostics, colorCode, SceneSeverity::Warning),
			"the warning stays in the upgrade diagnostics");
		const SceneLoadResult reopenedColored = loadScenePath(coloredDestination.string());
		const auto coloredService = std::find_if(reopenedColored.scene.services.begin(),
			reopenedColored.scene.services.end(), [&](const SceneService& service) { return service.id == coloredId; });
		ok &= expect(coloredService != reopenedColored.scene.services.end() && coloredService->visualizationColor == "red",
			"the upgraded copy keeps the service colour");
	}

	const fs::path brokenFolder = temp.path / "broken-input";
	fs::create_directories(brokenFolder);
	const fs::path brokenSource = temp.path / "broken-source";
	ok &= expect(saveScene(validLoaded.scene, brokenSource.string()).success(), "broken input source saves");
	{
		std::ofstream brokenServices(brokenSource / "services.json", std::ios::binary | std::ios::trunc);
		brokenServices << "not json\n";
	}
	ok &= expect(writeManifest(brokenSource, 0), "broken input fixture writes");
	const std::string originalBrokenBytes = readSceneDirectorySnapshot(brokenSource.string()).bytes;
	ok &= expect(!originalBrokenBytes.empty(), "broken input snapshot is read");
	const fs::path brokenDestination = brokenFolder / "upgraded";
	const SceneMigrationResult brokenUpgrade = migrateSceneCopy(brokenSource.string(), brokenDestination.string(), registry);
	ok &= expect(!brokenUpgrade.success() && hasDiagnostic(brokenUpgrade.diagnostics, "scene.json.parse", SceneSeverity::Error),
		"upgrade of an unloadable source reports the loader error");
	ok &= expect(!hasDiagnostic(brokenUpgrade.diagnostics, "scene.services.none", SceneSeverity::Error),
		"a model whose load failed is not validated");
	ok &= expect(leavesNoCopy(brokenDestination), "upgrade of an unloadable source leaves no copy");
	ok &= expect(readSceneDirectorySnapshot(brokenSource.string()).bytes == originalBrokenBytes,
		"upgrade of an unloadable source leaves source bytes unchanged");

	const fs::path readOnlyFolder = temp.path / "read-only";
	fs::create_directories(readOnlyFolder);
	RestorePermissions restoreReadOnly{readOnlyFolder};
	std::error_code permissionError;
	fs::permissions(readOnlyFolder, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace, permissionError);
	ok &= expect(!permissionError, "destination folder becomes read-only");
	const fs::path writeProbe = readOnlyFolder / "probe";
	std::error_code probeError;
	if (fs::create_directory(writeProbe, probeError)) {
		fs::remove(writeProbe, probeError);
		std::cerr << "skipped: refused-write case, a new directory can be created in a read-only directory\n";
	} else {
		std::cout << "refused-write case runs\n";
		const fs::path refusedDestination = readOnlyFolder / "upgraded";
		const SceneMigrationResult refused = migrateSceneCopy(migrationSource.string(), refusedDestination.string(), registry);
		ok &= expect(!refused.success() && hasDiagnostic(refused.diagnostics, "scene.save.write", SceneSeverity::Error),
			"refused write is reported by the scene writer");
		ok &= expect(leavesNoCopy(refusedDestination), "refused write leaves no copy");
		ok &= expect(readSceneDirectorySnapshot(migrationSource.string()).bytes == originalMigrationBytes,
			"refused write leaves source bytes unchanged");
		const fs::path refusedBundleDestination = readOnlyFolder / "upgraded.egscene";
		const SceneMigrationResult refusedBundle = migrateSceneCopy(olderBundle.string(), refusedBundleDestination.string(), bundleRegistry);
		ok &= expect(!refusedBundle.success() && hasDiagnostic(refusedBundle.diagnostics, "scene.bundle.write", SceneSeverity::Error),
			"refused bundle write is reported by the bundle writer");
		ok &= expect(leavesNoCopy(refusedBundleDestination), "refused bundle write leaves no copy");
		ok &= expect(readFileBytes(olderBundle) == originalOlderBundleBytes,
			"refused bundle write leaves source bytes unchanged");
	}

	ok &= checkRejectionReasons(validLoaded.scene, temp.path);

	if (!ok)
		return 1;
	std::cout << "all SceneCompatibility tests passed\n";
	return 0;
}
