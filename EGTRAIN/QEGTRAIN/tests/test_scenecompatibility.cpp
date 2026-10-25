#include "scene/SceneBundle.h"
#include "scene/SceneCompatibility.h"
#include "scene/SceneDiagnostic.h"
#include "scene/SceneMigration.h"
#include "scene/SceneModel.h"
#include "scene/SceneValidator.h"
#include "scene/SceneWriter.h"
#include "io/third_party/miniz/miniz.h"

#include <QString>
#include <QTest>

#include <algorithm>
#include <chrono>
#include <climits>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
using json = nlohmann::json;

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

// A test-only schema step from 0 to the current number that sets schema_version in the staged scene.json.
static SceneMigrationRegistry schemaStepRegistry() {
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
	return registry;
}

// A test-only bundle step from the layout before the current one, which needs the legacy entry that writeBundleVersion adds.
static SceneMigrationRegistry bundleStepRegistry() {
	SceneMigrationRegistry bundleRegistry;
	bundleRegistry.addBundleStep(SceneMigrationStep(
		kCurrentSceneBundleVersion - 1, kCurrentSceneBundleVersion,
		[](const fs::path& staged, std::vector<SceneDiagnostic>&) {
			return fs::is_regular_file(staged / "legacy-data.json");
		},
		"test-only-bundle-step"));
	return bundleRegistry;
}

class TestSceneCompatibility : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void probesCurrentDirectory();
	void classifiesSchemaNumbers();
	void rejectsSchemaVersionOutsideIntRange_data();
	void rejectsSchemaVersionOutsideIntRange();
	void migratesDirectoryCopy();
	void appliesMigrationChain();
	void findsPathPastDeadEndStep();
	void probesCurrentBundle();
	void migratesOlderBundle();
	void migratesUnderUnicodeNames();
	void rejectsNewerAndOversizedBundles();
	void productionRegistryStartsEmpty();
	void refusesInvalidResult();
	void keepsServiceColourWarning();
	void refusesUnloadableSource();
	void refusesWriteIntoReadOnlyFolder();
	void namesRejectionReasons();

private:
	// The minimal fixture, which loads and does not pass validation.
	SceneLoadResult m_loaded;
	// The line fixture, which loads and passes validation.
	SceneLoadResult m_validLoaded;
};

void TestSceneCompatibility::initTestCase() {
	m_loaded = loadScene(EGTRAIN_TEST_SOURCE_DIR "/fixtures/scenes/minimal");
	QVERIFY2(!hasErrors(m_loaded.diagnostics), "fixture scene loads");
	m_validLoaded = loadScene(EGTRAIN_TEST_SOURCE_DIR "/fixtures/scenes/line");
	QVERIFY2(!hasErrors(m_validLoaded.diagnostics), "valid fixture scene loads");
	QVERIFY2(!hasErrors(validateScene(m_validLoaded.scene)), "valid fixture scene passes semantic validation");
}

void TestSceneCompatibility::probesCurrentDirectory() {
	TempDir temp;
	const fs::path source = temp.path / "source";
	QVERIFY2(saveScene(m_loaded.scene, source.string()).success(), "fixture scene saves");
	const SceneCompatibilityProbeResult current = probeSceneCompatibility(source.string());
	QVERIFY2(current.classification == SceneCompatibilityClass::Current, "current directory is current");
	// Saved-with provenance is reported.
	QCOMPARE(current.savedWithAppVersion, EGTRAIN_APP_VERSION);
	QVERIFY2(current.diagnostics.empty(), "a current directory gets no diagnostic");
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
	// Missing provenance remains valid.
	QVERIFY(withoutProvenance.classification == SceneCompatibilityClass::Current);
	QVERIFY(withoutProvenance.savedWithAppVersion.empty());
}

void TestSceneCompatibility::classifiesSchemaNumbers() {
	TempDir temp;
	const fs::path source = temp.path / "source";
	QVERIFY2(saveScene(m_loaded.scene, source.string()).success(), "fixture scene saves");
	QVERIFY2(writeManifest(source, 0), "older fixture writes");
	const SceneCompatibilityProbeResult older = probeSceneCompatibility(source.string());
	QVERIFY2(older.classification == SceneCompatibilityClass::OlderUnsupported, "empty production registry rejects older schema");
	QVERIFY2(!reasonOf(older, "scene.compatibility.older").empty(), "older schema is rejected with one reason");
	QVERIFY2(writeManifest(source, kCurrentSceneSchemaVersion + 1), "newer fixture writes");
	const SceneCompatibilityProbeResult newer = probeSceneCompatibility(source.string());
	QVERIFY2(newer.classification == SceneCompatibilityClass::Newer, "newer schema is reported");
	QVERIFY2(!reasonOf(newer, "scene.compatibility.newer").empty(), "newer schema is rejected with one reason");
	QVERIFY2(writeManifest(source, INT_MAX), "int limit schema fixture writes");
	QVERIFY2(probeSceneCompatibility(source.string()).classification == SceneCompatibilityClass::Newer,
		"schema_version at the int limit is still read");
	QVERIFY2(writeManifest(source, -1), "negative schema fixture writes");
	QVERIFY2(probeSceneCompatibility(source.string()).classification
			== SceneCompatibilityClass::OlderUnsupported,
		"negative schema_version is read as older");
	{
		std::ofstream output(source / "scene.json", std::ios::binary | std::ios::trunc);
		output << "not json\n";
	}
	const SceneCompatibilityProbeResult malformed = probeSceneCompatibility(source.string());
	QVERIFY2(malformed.classification == SceneCompatibilityClass::Malformed, "malformed manifest is reported");
}

void TestSceneCompatibility::rejectsSchemaVersionOutsideIntRange_data() {
	// The schema_version as JSON text.
	QTest::addColumn<QString>("version");
	const long long wrapsToCurrent = (1LL << 32) + kCurrentSceneSchemaVersion;
	QTest::newRow("wraps to the current number") << QString::fromStdString(json(wrapsToCurrent).dump());
	QTest::newRow("int limit plus one") << QString::fromStdString(json(static_cast<long long>(INT_MAX) + 1).dump());
	QTest::newRow("fraction") << QString::fromStdString(json(1.5).dump());
	QTest::newRow("text") << QString::fromStdString(json("1").dump());
	QTest::newRow("unsigned 64-bit limit") << QString::fromStdString(json(18446744073709551615ULL).dump());
}

void TestSceneCompatibility::rejectsSchemaVersionOutsideIntRange() {
	QFETCH(QString, version);
	TempDir temp;
	const fs::path source = temp.path / "source";
	QVERIFY2(saveScene(m_loaded.scene, source.string()).success(), "fixture scene saves");
	QVERIFY2(writeManifest(source, json::parse(version.toStdString())), "out-of-range schema fixture writes");
	const SceneCompatibilityProbeResult badProbe = probeSceneCompatibility(source.string());
	// schema_version outside the int range or not an integer is rejected.
	QVERIFY(badProbe.classification == SceneCompatibilityClass::Malformed);
	QVERIFY(!badProbe.diagnostics.empty());
	QCOMPARE(badProbe.diagnostics.front().code, "scene.compatibility.schema");
	QCOMPARE(badProbe.diagnostics.front().file, "scene.json");
}

void TestSceneCompatibility::migratesDirectoryCopy() {
	TempDir temp;
	const fs::path migrationSource = temp.path / "migration-source";
	QVERIFY2(saveScene(m_validLoaded.scene, migrationSource.string()).success(), "migration source saves");
	QVERIFY2(writeManifest(migrationSource, 0), "test-only migration fixture writes");
	const std::string originalMigrationBytes = readSceneDirectorySnapshot(migrationSource.string()).bytes;
	const SceneMigrationRegistry registry = schemaStepRegistry();
	const SceneCompatibilityProbeResult migratable = probeSceneCompatibility(migrationSource.string(), registry);
	// Registered schema chain is migratable.
	QVERIFY(migratable.classification == SceneCompatibilityClass::OlderMigratable);
	QVERIFY(migratable.diagnostics.empty());
	const fs::path destination = temp.path / "upgraded";
	const SceneMigrationResult migrated = migrateSceneCopy(migrationSource.string(), destination.string(), registry);
	// Migration publishes a copy.
	QVERIFY(migrated.success());
	QVERIFY(fs::is_directory(destination));
	QVERIFY2(readSceneDirectorySnapshot(migrationSource.string()).bytes == originalMigrationBytes,
		"successful migration leaves source bytes unchanged");
	const SceneLoadResult reopened = loadScenePath(destination.string());
	// Upgraded directory copy reopens without errors.
	QVERIFY(!hasErrors(reopened.diagnostics));
	QVERIFY(!hasErrors(validateScene(reopened.scene)));
	SceneModel withoutServices = m_validLoaded.scene;
	withoutServices.services.clear();
	for (SceneScenario& scenario : withoutServices.scenarios) {
		scenario.entranceDelays.clear();
		scenario.incidents.clear();
	}
	withoutServices.passengers.clear();
	const fs::path withoutServicesSource = temp.path / "without-services-source";
	// Older scene without services writes.
	QVERIFY(saveScene(withoutServices, withoutServicesSource.string()).success());
	QVERIFY(writeManifest(withoutServicesSource, 0));
	const fs::path withoutServicesDestination = temp.path / "without-services-upgraded";
	const SceneMigrationResult withoutServicesUpgrade = migrateSceneCopy(withoutServicesSource.string(),
		withoutServicesDestination.string(), registry);
	// An older scene without services upgrades.
	QVERIFY(withoutServicesUpgrade.success());
	QVERIFY(fs::is_directory(withoutServicesDestination));
	const fs::path failedDestination = temp.path / "failed-upgrade";
	SceneMigrationRegistry failingRegistry;
	failingRegistry.addSchemaStep(SceneMigrationStep(0, kCurrentSceneSchemaVersion,
		[](const fs::path&, std::vector<SceneDiagnostic>&) { return false; }));
	const SceneMigrationResult failed = migrateSceneCopy(migrationSource.string(), failedDestination.string(), failingRegistry);
	// Failed migration leaves no destination.
	QVERIFY(!failed.success());
	QVERIFY(!failed.diagnostics.empty());
	QVERIFY(!fs::exists(failedDestination));
	QVERIFY2(readSceneDirectorySnapshot(migrationSource.string()).bytes == originalMigrationBytes,
		"failed migration leaves source bytes unchanged");
	const fs::path existingDestination = temp.path / "existing-upgrade";
	fs::create_directories(existingDestination);
	const fs::path existingMarker = existingDestination / "keep.txt";
	{
		std::ofstream marker(existingMarker);
		marker << "unchanged";
	}
	const SceneMigrationResult existing = migrateSceneCopy(migrationSource.string(), existingDestination.string(), registry);
	// Existing destination is rejected before publication.
	QVERIFY(!existing.success());
	QCOMPARE(readFileBytes(existingMarker), "unchanged");
}

void TestSceneCompatibility::appliesMigrationChain() {
	TempDir temp;
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
	// Test-only migration chain applies 1 to 2 to 3.
	QVERIFY(applySceneMigrationChain(chainRegistry, SceneMigrationStepKind::Schema,
		1, 3, chainStage, chainDiagnostics));
	QCOMPARE(readFileBytes(chainStage / "chain.txt"), "123");
}

void TestSceneCompatibility::findsPathPastDeadEndStep() {
	SceneMigrationRegistry branchingRegistry;
	branchingRegistry.addSchemaStep(SceneMigrationStep(1, 2,
		[](const fs::path&, std::vector<SceneDiagnostic>&) { return true; }));
	branchingRegistry.addSchemaStep(SceneMigrationStep(1, 3,
		[](const fs::path&, std::vector<SceneDiagnostic>&) { return true; }));
	QVERIFY2(sceneMigrationPathAvailable(branchingRegistry, SceneMigrationStepKind::Schema, 1, 3),
		"a dead-end step does not hide a later valid migration path");
}

void TestSceneCompatibility::probesCurrentBundle() {
	TempDir temp;
	const fs::path bundle = temp.path / "current.egscene";
	SceneModel currentScene = m_loaded.scene;
	const SceneSaveResult bundleSaved = saveSceneBundle(currentScene, bundle.string());
	QVERIFY2(bundleSaved.success(), "current bundle saves");
	const SceneCompatibilityProbeResult bundled = probeSceneCompatibility(bundle.string());
	// Current bundle reports its actual version.
	QVERIFY(bundled.classification == SceneCompatibilityClass::Current);
	QVERIFY(bundled.bundleVersion);
	QCOMPARE(*bundled.bundleVersion, kCurrentSceneBundleVersion);
	const SceneLoadResult loadedBundle = loadScenePath(bundle.string());
	// Current bundle version propagates through the normal loader.
	QVERIFY(!hasErrors(loadedBundle.diagnostics));
	QVERIFY(loadedBundle.bundleVersion);
	QCOMPARE(*loadedBundle.bundleVersion, kCurrentSceneBundleVersion);
}

void TestSceneCompatibility::migratesOlderBundle() {
	TempDir temp;
	const fs::path olderBundle = temp.path / "older.egscene";
	const fs::path bundleSource = temp.path / "bundle-source";
	QVERIFY2(saveScene(m_validLoaded.scene, bundleSource.string()).success(), "bundle migration source saves");
	QVERIFY2(writeBundleVersion(olderBundle, bundleSource, kCurrentSceneBundleVersion - 1),
		"older bundle fixture writes");
	const SceneCompatibilityProbeResult olderBundleProbe = probeSceneCompatibility(olderBundle.string());
	// Older bundle is independent from current schema support.
	QVERIFY(olderBundleProbe.classification == SceneCompatibilityClass::OlderUnsupported);
	QCOMPARE(olderBundleProbe.schemaVersion, kCurrentSceneSchemaVersion);
	QVERIFY(olderBundleProbe.bundleVersion);
	QCOMPARE(*olderBundleProbe.bundleVersion, kCurrentSceneBundleVersion - 1);
	const std::string olderBundleReason = reasonOf(olderBundleProbe, "scene.compatibility.older");
	// Older bundle reason names both layout numbers.
	QVERIFY2(contains(olderBundleReason, "schema 1, bundle layout 0 (saved"), olderBundleReason.c_str());
	QVERIFY2(contains(olderBundleReason, "reads schema 1, bundle layout 1."), olderBundleReason.c_str());
	const SceneMigrationRegistry bundleRegistry = bundleStepRegistry();
	const SceneCompatibilityProbeResult migratableBundle = probeSceneCompatibility(olderBundle.string(), bundleRegistry);
	// Registered bundle step makes only the older bundle migratable.
	QVERIFY(migratableBundle.classification == SceneCompatibilityClass::OlderMigratable);
	QVERIFY(migratableBundle.diagnostics.empty());
	const std::string originalOlderBundleBytes = readFileBytes(olderBundle);
	const fs::path upgradedBundle = temp.path / "older-upgraded.egscene";
	const SceneMigrationResult migratedBundle = migrateSceneCopy(olderBundle.string(),
		upgradedBundle.string(), bundleRegistry);
	const SceneCompatibilityProbeResult upgradedBundleProbe = probeSceneCompatibility(upgradedBundle.string());
	// Bundle-only migration repackages a current bundle.
	QVERIFY(migratedBundle.success());
	QVERIFY(upgradedBundleProbe.classification == SceneCompatibilityClass::Current);
	QVERIFY(upgradedBundleProbe.bundleVersion);
	QCOMPARE(*upgradedBundleProbe.bundleVersion, kCurrentSceneBundleVersion);
	QVERIFY2(readFileBytes(olderBundle) == originalOlderBundleBytes,
		"bundle migration leaves source bytes unchanged");
	const SceneLoadResult reopenedBundle = loadScenePath(upgradedBundle.string());
	// Upgraded bundle copy reopens without errors.
	QVERIFY(!hasErrors(reopenedBundle.diagnostics));
	QVERIFY(!hasErrors(validateScene(reopenedBundle.scene)));
}

// Sources and copies with Unicode names are probed and migrated under those names.
void TestSceneCompatibility::migratesUnderUnicodeNames() {
	TempDir temp;
	const SceneMigrationRegistry registry = schemaStepRegistry();
	const SceneMigrationRegistry bundleRegistry = bundleStepRegistry();
	const fs::path bundleSource = temp.path / "bundle-source";
	QVERIFY2(saveScene(m_validLoaded.scene, bundleSource.string()).success(), "bundle migration source saves");
	const fs::path unicodeRoot = temp.path / fs::u8path(kUnicodeName);
	const fs::path unicodeSource = unicodeRoot / fs::u8path(kUnicodeName + "-source");
	// Directory with a Unicode name saves as an older scene.
	QVERIFY(saveScene(m_validLoaded.scene, unicodeSource.u8string()).success());
	QVERIFY(writeManifest(unicodeSource, 0));
	QVERIFY2(probeSceneCompatibility(unicodeSource.u8string(), registry).classification
			== SceneCompatibilityClass::OlderMigratable,
		"directory with a Unicode name is probed");
	const fs::path unicodeUpgraded = unicodeRoot / fs::u8path(kUnicodeName + "-upgraded");
	// Directory with a Unicode name migrates to a copy with a Unicode name.
	QVERIFY(migrateSceneCopy(unicodeSource.u8string(), unicodeUpgraded.u8string(), registry).success());
	QVERIFY(fs::is_regular_file(unicodeUpgraded / "scene.json"));
	const fs::path unicodeOlderBundle = unicodeRoot / fs::u8path(kUnicodeName + "-older.egscene");
	QVERIFY2(writeBundleVersion(unicodeOlderBundle, bundleSource, kCurrentSceneBundleVersion - 1),
		"older bundle with a Unicode name writes");
	const fs::path unicodeUpgradedBundle = unicodeRoot / fs::u8path(kUnicodeName + "-upgraded.egscene");
	// Bundle with a Unicode name migrates to a bundle with a Unicode name.
	QVERIFY(migrateSceneCopy(unicodeOlderBundle.u8string(), unicodeUpgradedBundle.u8string(), bundleRegistry).success());
	QVERIFY(probeSceneCompatibility(unicodeUpgradedBundle.u8string()).classification
		== SceneCompatibilityClass::Current);
}

void TestSceneCompatibility::rejectsNewerAndOversizedBundles() {
	TempDir temp;
	const fs::path newerBundle = temp.path / "newer.egscene";
	QVERIFY2(writeNewerBundle(newerBundle), "newer bundle fixture writes");
	const SceneCompatibilityProbeResult newerBundleProbe = probeSceneCompatibility(newerBundle.string());
	QVERIFY2(newerBundleProbe.classification == SceneCompatibilityClass::Newer,
		"newer bundle layout is classified without extraction");
	const std::string newerBundleReason = reasonOf(newerBundleProbe, "scene.compatibility.newer");
	QVERIFY2(contains(newerBundleReason, "schema 1, bundle layout " + std::to_string(kCurrentSceneBundleVersion + 1)),
		("newer bundle reason names both layout numbers: " + newerBundleReason).c_str());
	const fs::path hostileNewerBundle = temp.path / "hostile-newer.egscene";
	QVERIFY2(writeNewerBundle(hostileNewerBundle, "../future.json"), "hostile newer bundle fixture writes");
	const SceneCompatibilityProbeResult hostileProbe = probeSceneCompatibility(hostileNewerBundle.string());
	QVERIFY2(hostileProbe.classification == SceneCompatibilityClass::Malformed,
		"newer bundle keeps generic ZIP path safety");
	const fs::path bundleSource = temp.path / "bundle-source";
	QVERIFY2(saveScene(m_validLoaded.scene, bundleSource.string()).success(), "bundle migration source saves");
	const fs::path hugeBundle = temp.path / "huge-version.egscene";
	QVERIFY2(writeBundleVersion(hugeBundle, bundleSource, (1LL << 32) + kCurrentSceneBundleVersion),
		"oversized bundle version fixture writes");
	const SceneCompatibilityProbeResult hugeBundleProbe = probeSceneCompatibility(hugeBundle.string());
	// Oversized bundle_version is rejected.
	QVERIFY(hugeBundleProbe.classification == SceneCompatibilityClass::Malformed);
	QVERIFY(!hugeBundleProbe.diagnostics.empty());
	QCOMPARE(hugeBundleProbe.diagnostics.front().code, "scene.bundle.version");
	QVERIFY2(writeBundleVersion(hugeBundle, bundleSource, kCurrentSceneBundleVersion,
				 (1LL << 32) + kCurrentSceneSchemaVersion),
		"oversized bundle schema fixture writes");
	const SceneCompatibilityProbeResult hugeSchemaProbe = probeSceneCompatibility(hugeBundle.string());
	// Oversized bundle schema_version is rejected.
	QVERIFY(hugeSchemaProbe.classification == SceneCompatibilityClass::Malformed);
	QVERIFY(!hugeSchemaProbe.diagnostics.empty());
	QCOMPARE(hugeSchemaProbe.diagnostics.front().code, "scene.bundle.schema");
}

void TestSceneCompatibility::productionRegistryStartsEmpty() {
	// Production migration registry starts empty.
	QVERIFY(productionSceneMigrationRegistry().schemaSteps().empty());
	QVERIFY(productionSceneMigrationRegistry().bundleSteps().empty());
}

// Every stopped upgrade below runs into a folder of its own that must stay empty.
void TestSceneCompatibility::refusesInvalidResult() {
	const std::string invalidErrorCode = firstErrorCode(validateScene(m_loaded.scene));
	QVERIFY2(!invalidErrorCode.empty(), "minimal fixture scene has a validation error");
	TempDir temp;
	const SceneMigrationRegistry registry = schemaStepRegistry();
	const SceneMigrationRegistry bundleRegistry = bundleStepRegistry();

	const fs::path invalidFolder = temp.path / "invalid-result";
	fs::create_directories(invalidFolder);
	const fs::path invalidSource = temp.path / "invalid-source";
	QVERIFY2(saveScene(m_loaded.scene, invalidSource.string()).success(), "invalid model saves");
	QVERIFY2(writeManifest(invalidSource, 0), "invalid model fixture writes");
	const std::string originalInvalidBytes = readSceneDirectorySnapshot(invalidSource.string()).bytes;
	const fs::path invalidDestination = invalidFolder / "upgraded";
	const SceneMigrationResult invalidUpgrade = migrateSceneCopy(invalidSource.string(), invalidDestination.string(), registry);
	// Upgrade of an invalid result reports the validation error.
	QVERIFY(!invalidUpgrade.success());
	QVERIFY(hasDiagnostic(invalidUpgrade.diagnostics, invalidErrorCode, SceneSeverity::Error));
	QVERIFY2(leavesNoCopy(invalidDestination), "upgrade of an invalid result leaves no copy");
	QVERIFY2(readSceneDirectorySnapshot(invalidSource.string()).bytes == originalInvalidBytes,
		"upgrade of an invalid result leaves source bytes unchanged");

	const fs::path invalidBundleFolder = temp.path / "invalid-bundle-result";
	fs::create_directories(invalidBundleFolder);
	const fs::path invalidBundleSource = temp.path / "invalid-bundle-source";
	QVERIFY2(saveScene(m_loaded.scene, invalidBundleSource.string()).success(), "invalid bundle model saves");
	const fs::path invalidBundle = temp.path / "invalid.egscene";
	QVERIFY2(writeBundleVersion(invalidBundle, invalidBundleSource, kCurrentSceneBundleVersion - 1),
		"invalid older bundle fixture writes");
	const std::string originalInvalidBundleBytes = readFileBytes(invalidBundle);
	const fs::path invalidBundleDestination = invalidBundleFolder / "upgraded.egscene";
	const SceneMigrationResult invalidBundleUpgrade = migrateSceneCopy(invalidBundle.string(),
		invalidBundleDestination.string(), bundleRegistry);
	// Bundle upgrade of an invalid result reports the validation error.
	QVERIFY(!invalidBundleUpgrade.success());
	QVERIFY(hasDiagnostic(invalidBundleUpgrade.diagnostics, invalidErrorCode, SceneSeverity::Error));
	QVERIFY2(leavesNoCopy(invalidBundleDestination), "bundle upgrade of an invalid result leaves no copy");
	QVERIFY2(readFileBytes(invalidBundle) == originalInvalidBundleBytes,
		"bundle upgrade of an invalid result leaves source bytes unchanged");
}

void TestSceneCompatibility::keepsServiceColourWarning() {
	const std::string colorCode = "scene.service.color.invalid";
	QVERIFY2(!m_validLoaded.scene.services.empty(), "valid fixture scene has a service");
	TempDir temp;
	const SceneMigrationRegistry registry = schemaStepRegistry();
	SceneModel coloredScene = m_validLoaded.scene;
	const std::string coloredId = coloredScene.services.front().id;
	coloredScene.services.front().visualizationColor = "red";
	const std::vector<SceneDiagnostic> coloredDiagnostics = validateScene(coloredScene);
	QVERIFY2(!hasDiagnostic(validateScene(m_validLoaded.scene), colorCode, SceneSeverity::Warning),
		"valid fixture scene has no service colour warning");
	// An invalid service colour is only a warning.
	QVERIFY(hasDiagnostic(coloredDiagnostics, colorCode, SceneSeverity::Warning));
	QVERIFY(!hasErrors(coloredDiagnostics));
	const fs::path coloredSource = temp.path / "colored-source";
	QVERIFY2(saveScene(coloredScene, coloredSource.string()).success(), "colored model saves");
	QVERIFY2(writeManifest(coloredSource, 0), "colored model fixture writes");
	const fs::path coloredDestination = temp.path / "colored-upgraded";
	const SceneMigrationResult coloredUpgrade = migrateSceneCopy(coloredSource.string(), coloredDestination.string(), registry);
	// A warning does not stop the upgrade.
	QVERIFY(coloredUpgrade.success());
	QVERIFY(fs::is_directory(coloredDestination));
	QVERIFY2(hasDiagnostic(coloredUpgrade.diagnostics, colorCode, SceneSeverity::Warning),
		"the warning stays in the upgrade diagnostics");
	const SceneLoadResult reopenedColored = loadScenePath(coloredDestination.string());
	const auto coloredService = std::find_if(reopenedColored.scene.services.begin(),
		reopenedColored.scene.services.end(), [&](const SceneService& service) { return service.id == coloredId; });
	// The upgraded copy keeps the service colour.
	QVERIFY(coloredService != reopenedColored.scene.services.end());
	QCOMPARE(coloredService->visualizationColor, "red");
}

void TestSceneCompatibility::refusesUnloadableSource() {
	TempDir temp;
	const SceneMigrationRegistry registry = schemaStepRegistry();
	const fs::path brokenFolder = temp.path / "broken-input";
	fs::create_directories(brokenFolder);
	const fs::path brokenSource = temp.path / "broken-source";
	QVERIFY2(saveScene(m_loaded.scene, brokenSource.string()).success(), "broken input source saves");
	{
		std::ofstream brokenServices(brokenSource / "services.json", std::ios::binary | std::ios::trunc);
		brokenServices << "not json\n";
	}
	// The source is the invalid minimal model, so validation of what the loader returns reports an error.
	const std::string unloadedValidationCode = firstErrorCode(validateScene(loadScene(brokenSource.string()).scene));
	QVERIFY2(writeManifest(brokenSource, 0), "broken input fixture writes");
	const std::string originalBrokenBytes = readSceneDirectorySnapshot(brokenSource.string()).bytes;
	QVERIFY2(!originalBrokenBytes.empty(), "broken input snapshot is read");
	const fs::path brokenDestination = brokenFolder / "upgraded";
	const SceneMigrationResult brokenUpgrade = migrateSceneCopy(brokenSource.string(), brokenDestination.string(), registry);
	// Upgrade of an unloadable source reports the loader error.
	QVERIFY(!brokenUpgrade.success());
	QVERIFY(hasDiagnostic(brokenUpgrade.diagnostics, "scene.json.parse", SceneSeverity::Error));
	// A model whose load failed is not validated.
	QVERIFY(!unloadedValidationCode.empty());
	QVERIFY(!hasDiagnostic(brokenUpgrade.diagnostics, unloadedValidationCode, SceneSeverity::Error));
	QVERIFY2(leavesNoCopy(brokenDestination), "upgrade of an unloadable source leaves no copy");
	QVERIFY2(readSceneDirectorySnapshot(brokenSource.string()).bytes == originalBrokenBytes,
		"upgrade of an unloadable source leaves source bytes unchanged");
}

void TestSceneCompatibility::refusesWriteIntoReadOnlyFolder() {
	TempDir temp;
	const SceneMigrationRegistry registry = schemaStepRegistry();
	const SceneMigrationRegistry bundleRegistry = bundleStepRegistry();
	const fs::path migrationSource = temp.path / "migration-source";
	QVERIFY2(saveScene(m_validLoaded.scene, migrationSource.string()).success(), "migration source saves");
	QVERIFY2(writeManifest(migrationSource, 0), "test-only migration fixture writes");
	const std::string originalMigrationBytes = readSceneDirectorySnapshot(migrationSource.string()).bytes;
	const fs::path olderBundle = temp.path / "older.egscene";
	const fs::path bundleSource = temp.path / "bundle-source";
	QVERIFY2(saveScene(m_validLoaded.scene, bundleSource.string()).success(), "bundle migration source saves");
	QVERIFY2(writeBundleVersion(olderBundle, bundleSource, kCurrentSceneBundleVersion - 1),
		"older bundle fixture writes");
	const std::string originalOlderBundleBytes = readFileBytes(olderBundle);

	const fs::path readOnlyFolder = temp.path / "read-only";
	fs::create_directories(readOnlyFolder);
	RestorePermissions restoreReadOnly{readOnlyFolder};
	std::error_code permissionError;
	fs::permissions(readOnlyFolder, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace, permissionError);
	QVERIFY2(!permissionError, "destination folder becomes read-only");
	const fs::path writeProbe = readOnlyFolder / "probe";
	std::error_code probeError;
	if (fs::create_directory(writeProbe, probeError)) {
		fs::remove(writeProbe, probeError);
		QSKIP("a new directory can be created in a read-only directory");
	}
	const fs::path refusedDestination = readOnlyFolder / "upgraded";
	const SceneMigrationResult refused = migrateSceneCopy(migrationSource.string(), refusedDestination.string(), registry);
	// Refused write is reported by the scene writer.
	QVERIFY(!refused.success());
	QVERIFY(hasDiagnostic(refused.diagnostics, "scene.save.write", SceneSeverity::Error));
	QVERIFY2(leavesNoCopy(refusedDestination), "refused write leaves no copy");
	QVERIFY2(readSceneDirectorySnapshot(migrationSource.string()).bytes == originalMigrationBytes,
		"refused write leaves source bytes unchanged");
	const fs::path refusedBundleDestination = readOnlyFolder / "upgraded.egscene";
	const SceneMigrationResult refusedBundle = migrateSceneCopy(olderBundle.string(), refusedBundleDestination.string(), bundleRegistry);
	// Refused bundle write is reported by the bundle writer.
	QVERIFY(!refusedBundle.success());
	QVERIFY(hasDiagnostic(refusedBundle.diagnostics, "scene.bundle.write", SceneSeverity::Error));
	QVERIFY2(leavesNoCopy(refusedBundleDestination), "refused bundle write leaves no copy");
	QVERIFY2(readFileBytes(olderBundle) == originalOlderBundleBytes,
		"refused bundle write leaves source bytes unchanged");
}

// The probe names why it rejects a scene, and says nothing about a scene that it accepts or that a migration can convert.
void TestSceneCompatibility::namesRejectionReasons() {
	TempDir temp;
	const fs::path& folder = temp.path;
	const fs::path scene = folder / "reasons";
	QVERIFY2(saveScene(m_validLoaded.scene, scene.string()).success(), "reason fixture saves");
	const SceneCompatibilityProbeResult current = probeSceneCompatibility(scene.string());
	// A current scene gets no diagnostic.
	QVERIFY(current.classification == SceneCompatibilityClass::Current);
	QVERIFY(current.diagnostics.empty());

	QVERIFY2(changeManifest(scene, {{"schema_version", 0}, {"saved_with_app_version", "0.9"}}), "older reason fixture writes");
	const SceneCompatibilityProbeResult older = probeSceneCompatibility(scene.string());
	const std::string olderReason = reasonOf(older, "scene.compatibility.older");
	// An older schema is rejected with its number, the saved-with version and the way out.
	QVERIFY(older.classification == SceneCompatibilityClass::OlderUnsupported);
	QVERIFY(!olderReason.empty());
	QVERIFY2(contains(olderReason, "schema 0 (saved with EGTRAIN 0.9)"), olderReason.c_str());
	QVERIFY2(contains(olderReason, "saved by an older version of EGTRAIN"), olderReason.c_str());
	QVERIFY2(contains(olderReason, "reads schema 1."), olderReason.c_str());
	QVERIFY2(contains(olderReason, "Import Legacy Case"), olderReason.c_str());
	QVERIFY2(!contains(olderReason, "bundle layout"), olderReason.c_str());
	const fs::path refusedCopy = folder / "refused-copy";
	const SceneMigrationResult refused = migrateSceneCopy(scene.string(), refusedCopy.string(), productionSceneMigrationRegistry());
	// The copy upgrade of an older scene reports the reason and writes nothing.
	QVERIFY(!refused.success());
	QVERIFY(hasDiagnostic(refused.diagnostics, "scene.compatibility.older", SceneSeverity::Error));
	QVERIFY(!fs::exists(refusedCopy));

	QVERIFY2(changeManifest(scene, {{"schema_version", kCurrentSceneSchemaVersion + 1}, {"saved_with_app_version", nullptr}}),
		"newer reason fixture writes");
	const SceneCompatibilityProbeResult newer = probeSceneCompatibility(scene.string());
	const std::string newerReason = reasonOf(newer, "scene.compatibility.newer");
	// A newer schema is rejected with its number and the way out.
	QVERIFY(newer.classification == SceneCompatibilityClass::Newer);
	QVERIFY(!newerReason.empty());
	QVERIFY2(contains(newerReason, "schema " + std::to_string(kCurrentSceneSchemaVersion + 1) + " (saved-with version not recorded)"),
		newerReason.c_str());
	QVERIFY2(contains(newerReason, "saved by a newer version of EGTRAIN"), newerReason.c_str());
	QVERIFY2(contains(newerReason, "Check for Updates"), newerReason.c_str());

	// A long saved_with_app_version is cut before a character that the cut would split.
	const std::string longVersion = std::string(63, 'a') + "\xE6\x97\xA5" + std::string(10, 'b');
	QVERIFY2(changeManifest(scene, {{"saved_with_app_version", longVersion}}), "long version fixture writes");
	const std::string cutReason = reasonOf(probeSceneCompatibility(scene.string()), "scene.compatibility.newer");
	// A long saved-with version is cut at a character boundary.
	QVERIFY2(contains(cutReason, "saved with EGTRAIN " + std::string(63, 'a') + "...)"), cutReason.c_str());
	QVERIFY2(!contains(cutReason, "bbb"), cutReason.c_str());

	// The draft layout: a legacy_source and an infrastructure.json without nodes and arcs.
	const json emptyInfrastructure = {{"arcs", json::array()}, {"nodes", json::array()}};
	QVERIFY2(changeManifest(scene, {{"schema_version", kCurrentSceneSchemaVersion}, {"saved_with_app_version", nullptr}, {"legacy_source", "Input/Input_EGTRAIN_Paimpol"}}),
		"draft reason fixture writes");
	const SceneCompatibilityProbeResult onlyLegacySource = probeSceneCompatibility(scene.string());
	// legacy_source alone does not mark the draft layout.
	QVERIFY(onlyLegacySource.classification == SceneCompatibilityClass::Current);
	QVERIFY(onlyLegacySource.diagnostics.empty());
	QVERIFY2(writeText(scene / "infrastructure.json", emptyInfrastructure.dump()), "empty infrastructure writes");
	const SceneCompatibilityProbeResult draft = probeSceneCompatibility(scene.string());
	const std::string draftReason = reasonOf(draft, "scene.compatibility.draft");
	// legacy_source with an empty infrastructure is the draft layout.
	QVERIFY(draft.classification == SceneCompatibilityClass::OlderUnsupported);
	QVERIFY(!draftReason.empty());
	QVERIFY2(contains(draftReason, "draft layout that EGTRAIN saved before 2026-08-08 (schema 1, saved-with version not recorded)"),
		draftReason.c_str());
	QVERIFY2(contains(draftReason, "not supported"), draftReason.c_str());
	QVERIFY2(contains(draftReason, "File > Import Legacy Case..."), draftReason.c_str());
	QVERIFY2(writeText(scene / "infrastructure.json", "{}"), "infrastructure without lists writes");
	QVERIFY2(probeSceneCompatibility(scene.string()).classification == SceneCompatibilityClass::OlderUnsupported,
		"an infrastructure without the lists has no nodes and no arcs");
	QVERIFY2(writeText(scene / "infrastructure.json", R"({"arcs": [], "nodes": [1]})"), "infrastructure with a node writes");
	QVERIFY2(probeSceneCompatibility(scene.string()).classification == SceneCompatibilityClass::Current,
		"legacy_source with a node is not the draft layout");
	QVERIFY2(writeText(scene / "infrastructure.json", R"({"arcs": [1], "nodes": []})"), "infrastructure with an arc writes");
	QVERIFY2(probeSceneCompatibility(scene.string()).classification == SceneCompatibilityClass::Current,
		"legacy_source with an arc is not the draft layout");
	QVERIFY2(writeText(scene / "infrastructure.json", "not json"), "damaged infrastructure writes");
	QVERIFY2(probeSceneCompatibility(scene.string()).classification == SceneCompatibilityClass::Current,
		"an unreadable infrastructure is left to the loader");
	std::error_code ec;
	fs::remove(scene / "infrastructure.json", ec);
	QVERIFY2(probeSceneCompatibility(scene.string()).classification == SceneCompatibilityClass::Current,
		"a missing infrastructure is left to the loader");
	QVERIFY2(writeText(scene / "infrastructure.json", emptyInfrastructure.dump()), "empty infrastructure writes again");
	QVERIFY2(changeManifest(scene, {{"legacy_source", nullptr}}), "legacy_source is removed");
	QVERIFY2(probeSceneCompatibility(scene.string()).classification == SceneCompatibilityClass::Current,
		"an empty infrastructure alone does not mark the draft layout");

	// A schema number that is rejected on its own is named by the number. A migration does not convert a draft.
	QVERIFY2(changeManifest(scene, {{"legacy_source", "Input/Input_EGTRAIN_Paimpol"}, {"schema_version", kCurrentSceneSchemaVersion + 1}}),
		"newer draft fixture writes");
	const SceneCompatibilityProbeResult newerDraft = probeSceneCompatibility(scene.string());
	// A newer draft is rejected for its number.
	QVERIFY(newerDraft.classification == SceneCompatibilityClass::Newer);
	QVERIFY(!reasonOf(newerDraft, "scene.compatibility.newer").empty());
	QVERIFY2(changeManifest(scene, {{"schema_version", 0}}), "migratable draft fixture writes");
	SceneMigrationRegistry registry;
	registry.addSchemaStep(SceneMigrationStep(
		0, kCurrentSceneSchemaVersion, [](const fs::path&, std::vector<SceneDiagnostic>&) { return true; }, "test-only-step-zero-to-one"));
	const SceneCompatibilityProbeResult migratableDraft = probeSceneCompatibility(scene.string(), registry);
	// A draft is not offered a copy upgrade.
	QVERIFY(migratableDraft.classification == SceneCompatibilityClass::OlderUnsupported);
	QVERIFY(!reasonOf(migratableDraft, "scene.compatibility.draft").empty());
	QVERIFY2(changeManifest(scene, {{"legacy_source", nullptr}}), "migratable fixture writes");
	const SceneCompatibilityProbeResult migratable = probeSceneCompatibility(scene.string(), registry);
	// A scene that a migration converts gets no diagnostic.
	QVERIFY(migratable.classification == SceneCompatibilityClass::OlderMigratable);
	QVERIFY(migratable.diagnostics.empty());
}

QTEST_APPLESS_MAIN(TestSceneCompatibility)
#include "test_scenecompatibility.moc"
