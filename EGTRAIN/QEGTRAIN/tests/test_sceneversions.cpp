// Scenes written by older builds, kept as fixtures in tests/fixtures/scene-versions and listed in its manifest.json.
// Every fixture is a byte copy and is never edited or saved again.
#include "scene/SceneBundle.h"
#include "scene/SceneCompatibility.h"
#include "scene/SceneDiagnostic.h"
#include "scene/SceneMigration.h"
#include "scene/SceneModel.h"
#include "scene/SceneValidator.h"
#include "scene/Utf8Path.h"

#include <QString>
#include <QTest>

#include <chrono>
#include <climits>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

static const fs::path kFixtureDir = pathFromUtf8(EGTRAIN_TEST_SOURCE_DIR "/fixtures/scene-versions");

struct TempDir {
	fs::path path;
	TempDir() {
		path = fs::temp_directory_path() / ("scene_versions_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		fs::create_directories(path);
	}
	~TempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

// One entry of manifest.json.
struct Fixture {
	std::string path;
	std::string className;
	std::string reason; // the diagnostic code of the probe; only for a scene that is rejected
	int schemaVersion = 0;
	std::optional<int> bundleVersion;
	std::string savedWithAppVersion;
	bool runnable = false;
	std::map<std::string, std::size_t> counts; // only when runnable
};

// Reads the keys of one manifest entry. A key that is missing or has the wrong type is a problem that names the entry.
struct KeyReader {
	const json& object;
	std::string owner;
	std::vector<std::string>& problems;
	bool ok = true;

	KeyReader(const json& entry, std::string name, std::vector<std::string>& found) : object(entry), owner(std::move(name)), problems(found) {}

	void fail(const char* key, const char* problem) {
		ok = false;
		problems.push_back("manifest entry " + owner + ": the key " + key + " " + problem);
	}

	std::string text(const char* key) {
		const auto found = object.find(key);
		if (found == object.end() || !found->is_string()) {
			fail(key, "is missing or is not a string");
			return {};
		}
		return found->get<std::string>();
	}

	std::optional<int> integer(const char* key, bool required) {
		const auto found = object.find(key);
		if (found == object.end()) {
			if (required)
				fail(key, "is missing");
			return std::nullopt;
		}
		int value = 0;
		if (!readJsonInt(*found, INT_MIN, INT_MAX, value)) {
			fail(key, "is not an integer");
			return std::nullopt;
		}
		return value;
	}

	bool boolean(const char* key) {
		const auto found = object.find(key);
		if (found == object.end() || !found->is_boolean()) {
			fail(key, "is missing or is not true or false");
			return false;
		}
		return found->get<bool>();
	}

	std::map<std::string, std::size_t> sizes(const char* key) {
		std::map<std::string, std::size_t> result;
		const auto found = object.find(key);
		if (found == object.end() || !found->is_object()) {
			fail(key, "is missing or is not an object");
			return result;
		}
		for (const auto& item : found->items()) {
			if (!item.value().is_number_unsigned()) {
				fail(key, "has a value that is not a count");
				continue;
			}
			result[item.key()] = item.value().get<std::size_t>();
		}
		return result;
	}
};

static std::string readFileBytes(const fs::path& path) {
	std::ifstream input(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

static bool writeFileBytes(const fs::path& path, const std::string& contents) {
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output << contents;
	return static_cast<bool>(output);
}

static std::string classLabel(SceneCompatibilityClass value) {
	switch (value) {
		case SceneCompatibilityClass::Current:
			return "Current";
		case SceneCompatibilityClass::OlderMigratable:
			return "OlderMigratable";
		case SceneCompatibilityClass::OlderUnsupported:
			return "OlderUnsupported";
		case SceneCompatibilityClass::Newer:
			return "Newer";
		case SceneCompatibilityClass::Malformed:
			return "Malformed";
	}
	return "unknown";
}

static std::string describe(const std::optional<int>& value) {
	return value ? std::to_string(*value) : "none";
}

static bool hasErrorCode(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code) {
	for (const SceneDiagnostic& diagnostic : diagnostics) {
		if (diagnostic.severity == SceneSeverity::Error && diagnostic.code == code)
			return true;
	}
	return false;
}

static std::string firstError(const std::vector<SceneDiagnostic>& diagnostics) {
	for (const SceneDiagnostic& diagnostic : diagnostics) {
		if (diagnostic.severity == SceneSeverity::Error)
			return diagnostic.code + " in " + diagnostic.file;
	}
	return "none";
}

// The sizes of the vectors of the loaded model that the manifest counts.
static std::vector<std::pair<std::string, std::size_t>> modelSizes(const SceneModel& scene) {
	return {{"nodes", scene.nodes.size()}, {"arcs", scene.arcs.size()}, {"blocks", scene.blocks.size()},
		{"stations", scene.stations.size()}, {"routes", scene.routes.size()},
		{"trainUnits", scene.trainUnits.size()}, {"compositions", scene.compositions.size()},
		{"services", scene.services.size()}, {"scenarios", scene.scenarios.size()},
		{"passengers", scene.passengers.size()}};
}

// The bytes of a scene source, to compare the source with itself. A directory is read as the loader reads it.
// The problem is empty when the source was read and has bytes.
static std::string sourceBytes(const fs::path& source, std::string& problem) {
	std::error_code ec;
	std::string bytes;
	problem.clear();
	if (fs::is_directory(source, ec)) {
		const SceneInputSnapshot snapshot = readSceneDirectorySnapshot(source.u8string());
		problem = snapshot.reason;
		bytes = snapshot.bytes;
	} else {
		bytes = readFileBytes(source);
	}
	if (problem.empty() && bytes.empty())
		problem = "the source has no bytes";
	return bytes;
}

// The copy upgrade refuses the scene, writes nothing at the destination and leaves the source unchanged.
static void expectRefusedWithoutCopy(const std::string& name, const fs::path& source, const fs::path& destination) {
	std::string problem;
	const std::string before = sourceBytes(source, problem);
	QVERIFY2(problem.empty(), (name + ": the source cannot be read before the copy upgrade: " + problem).c_str());
	const SceneMigrationResult result = migrateSceneCopy(source.u8string(), destination.u8string(), productionSceneMigrationRegistry());
	QVERIFY2(!result.success(), (name + ": migrateSceneCopy succeeded, expected a refusal").c_str());
	std::error_code ec;
	QVERIFY2(!fs::exists(destination, ec), (name + ": migrateSceneCopy created " + destination.filename().u8string() + ", expected nothing").c_str());
	const std::string after = sourceBytes(source, problem);
	const std::string unchanged = name + ": the source has " + std::to_string(after.size()) + " bytes after the copy upgrade, expected the same "
		+ std::to_string(before.size()) + " bytes as before";
	QVERIFY2(problem.empty(), unchanged.c_str());
	QVERIFY2(after == before, unchanged.c_str());
}

// Reads manifest.json. Every entry that has all its keys is returned; what is wrong with the others is added to the
// problems. The result is empty when the manifest was read, else why it was not.
static std::string readManifest(const fs::path& manifestPath, std::vector<Fixture>& fixtures, std::set<std::string>& listed,
	std::vector<std::string>& problems) {
	json manifest;
	const std::string text = readFileBytes(manifestPath);
	if (text.empty())
		return "manifest.json is missing or empty";
	try {
		manifest = json::parse(text);
	} catch (const json::exception& error) {
		return std::string("manifest.json cannot be read: ") + error.what();
	}
	const auto entries = manifest.is_object() ? manifest.find("fixtures") : manifest.end();
	if (!manifest.is_object() || entries == manifest.end() || !entries->is_array() || entries->empty())
		return "manifest.json must be an object with a non-empty array fixtures";
	std::size_t index = 0;
	for (const json& entry : *entries) {
		std::string owner = "fixtures[" + std::to_string(index++) + "]";
		if (!entry.is_object()) {
			problems.push_back("manifest entry " + owner + " is not an object");
			continue;
		}
		const auto path = entry.find("path");
		if (path != entry.end() && path->is_string())
			owner = path->get<std::string>();
		KeyReader reader(entry, owner, problems);
		Fixture fixture;
		fixture.path = reader.text("path");
		reader.text("origin");
		fixture.className = reader.text("class");
		if (fixture.className == "OlderUnsupported" || fixture.className == "Newer")
			fixture.reason = reader.text("reason");
		fixture.schemaVersion = reader.integer("schema_version", true).value_or(0);
		fixture.bundleVersion = reader.integer("bundle_version", false);
		fixture.savedWithAppVersion = reader.text("saved_with_app_version");
		fixture.runnable = reader.boolean("runnable");
		if (fixture.runnable)
			fixture.counts = reader.sizes("counts");
		if (!fixture.path.empty() && !listed.insert(fixture.path).second)
			problems.push_back("manifest entry " + owner + " is listed twice");
		if (reader.ok)
			fixtures.push_back(fixture);
	}
	return {};
}

// A rejected scene gets one diagnostic with the code that the manifest lists, and any other scene gets none.
static void checkRejectionReason(const Fixture& fixture, const SceneCompatibilityProbeResult& probe) {
	const std::string& name = fixture.path;
	if (fixture.reason.empty()) {
		QVERIFY2(probe.diagnostics.empty(), (name + ": the probe reports " + firstError(probe.diagnostics) + ", expected nothing").c_str());
		return;
	}
	const std::string oneReason = name + ": the probe reports " + std::to_string(probe.diagnostics.size()) + " diagnostics, the first one "
		+ firstError(probe.diagnostics) + ", expected one with the code " + fixture.reason;
	QVERIFY2(probe.diagnostics.size() == 1, oneReason.c_str());
	QVERIFY2(probe.diagnostics.front().code == fixture.reason, oneReason.c_str());
	const std::string text = toDisplayText(probe.diagnostics.front());
	QVERIFY2(text.find("schema " + std::to_string(fixture.schemaVersion)) != std::string::npos,
		(name + ": the reason \"" + text + "\" does not name the schema number").c_str());
	if (fixture.reason == "scene.compatibility.draft") {
		const std::string wayOut = name + ": the reason \"" + text + "\" does not name the date and the way out";
		QVERIFY2(text.find("2026-08-08") != std::string::npos, wayOut.c_str());
		QVERIFY2(text.find("Import Legacy Case") != std::string::npos, wayOut.c_str());
	}
}

static void checkRunnable(const Fixture& fixture, const fs::path& source, const std::optional<int>& probedBundleVersion) {
	const std::string& name = fixture.path;
	const SceneLoadResult loaded = loadScenePath(source.u8string());
	const bool loadFailed = hasErrors(loaded.diagnostics);
	std::vector<SceneDiagnostic> validation;
	if (!loadFailed)
		validation = validateRunnableScene(loaded.scene);
	const bool accepted = !loadFailed && !hasErrors(validation);
	if (!fixture.runnable) {
		QVERIFY2(!accepted, (name + ": loadScenePath and validateRunnableScene report no error, expected at least one").c_str());
		return;
	}

	QVERIFY2(!loadFailed, (name + ": loadScenePath reports an error (" + firstError(loaded.diagnostics) + "), expected none").c_str());
	QVERIFY2(loaded.bundleVersion == probedBundleVersion,
		(name + ": loadScenePath reports bundle_version " + describe(loaded.bundleVersion) + ", expected " + describe(probedBundleVersion)).c_str());
	QVERIFY2(!hasErrors(validation), (name + ": validateRunnableScene reports an error (" + firstError(validation) + "), expected none").c_str());
	for (const auto& size : modelSizes(loaded.scene)) {
		const auto expected = fixture.counts.find(size.first);
		QVERIFY2(expected != fixture.counts.end(), (name + ": counts has no " + size.first).c_str());
		QVERIFY2(size.second == expected->second,
			(name + ": the loaded model has " + std::to_string(size.second) + " " + size.first + ", expected " + std::to_string(expected->second)).c_str());
	}
}

// A number below the current one needs a fixture of that number and either a registered migration path or every fixture
// of that number listed as OlderUnsupported. Each loop covers the numbers below the current one and does not run while
// the current number is 1.
static void checkVersionNumbers(const char* kind, const char* constant, int current, SceneMigrationStepKind stepKind,
	const std::vector<std::pair<int, std::string>>& fixtureNumbers) {
	const std::string rule = "See the version rule at the end of \"Compatibility boundary\" in docs/architecture/scene-model.md.";
	for (int number = 1; number < current; ++number) {
		const std::string label = std::string(kind) + " number " + std::to_string(number);
		bool hasFixture = false;
		bool allUnsupported = true;
		for (const auto& fixture : fixtureNumbers) {
			if (fixture.first != number)
				continue;
			hasFixture = true;
			allUnsupported = allUnsupported && fixture.second == "OlderUnsupported";
		}
		QVERIFY2(hasFixture,
			(label + ", check (i): no fixture in manifest.json has it, and " + constant + " is " + std::to_string(current)
				+ ". Add a fixture written by a build with that number. " + rule)
				.c_str());
		const bool registered = sceneMigrationPathAvailable(productionSceneMigrationRegistry(), stepKind, number, current);
		QVERIFY2(registered || (hasFixture && allUnsupported),
			(label + ", check (ii): no migration from " + std::to_string(number) + " to " + std::to_string(current)
				+ " is registered and not every fixture with that number has the class OlderUnsupported. Register the migration, or set"
				+ " that class on the fixtures of that number to drop support on purpose. " + rule)
				.c_str());
	}
}

class TestSceneVersions : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void manifestEntriesAreComplete();
	void directoryAgreesWithManifest();
	void fixtureMatchesManifest_data();
	void fixtureMatchesManifest();
	void rejectsSchemaNumbersNoBuildWrote_data();
	void rejectsSchemaNumbersNoBuildWrote();
	void schemaNumbersHaveFixtures();
	void bundleNumbersHaveFixtures();

private:
	TempDir m_temp;
	std::vector<Fixture> m_fixtures;
	std::set<std::string> m_listed;
	std::vector<std::string> m_manifestProblems;
};

void TestSceneVersions::initTestCase() {
	const std::string unread = readManifest(kFixtureDir / "manifest.json", m_fixtures, m_listed, m_manifestProblems);
	QVERIFY2(unread.empty(), unread.c_str());
	std::string problems;
	for (const std::string& problem : m_manifestProblems)
		problems += "\n" + problem;
	QVERIFY2(!m_listed.empty(), ("manifest.json lists no fixture path" + problems).c_str());
}

void TestSceneVersions::manifestEntriesAreComplete() {
	std::string problems;
	for (const std::string& problem : m_manifestProblems)
		problems += "\n" + problem;
	QVERIFY2(m_manifestProblems.empty(), ("manifest.json has entries that cannot be read:" + problems).c_str());
}

// Every entry of the fixture directory is listed in the manifest, and every listed path exists.
void TestSceneVersions::directoryAgreesWithManifest() {
	std::error_code ec;
	for (fs::directory_iterator it(kFixtureDir, ec), end; !ec && it != end; it.increment(ec)) {
		const std::string name = it->path().filename().u8string();
		if (name == "manifest.json" || name.empty() || name.front() == '.')
			continue;
		QVERIFY2(m_listed.count(name) == 1, ("the fixture directory has " + name + ", which manifest.json does not list").c_str());
	}
	QVERIFY2(!ec, ("the fixture directory cannot be listed: " + ec.message()).c_str());
	for (const std::string& name : m_listed) {
		ec.clear();
		QVERIFY2(fs::exists(kFixtureDir / pathFromUtf8(name), ec), ("manifest.json lists " + name + ", which the fixture directory does not have").c_str());
	}
}

void TestSceneVersions::fixtureMatchesManifest_data() {
	QTest::addColumn<int>("index");
	for (std::size_t index = 0; index < m_fixtures.size(); ++index)
		QTest::newRow(m_fixtures[index].path.c_str()) << static_cast<int>(index);
}

// Pins how one fixture is classified, loaded, validated and counted, and that a copy upgrade refuses it.
void TestSceneVersions::fixtureMatchesManifest() {
	QFETCH(int, index);
	const Fixture& fixture = m_fixtures[static_cast<std::size_t>(index)];
	const std::string& name = fixture.path;
	const fs::path source = kFixtureDir / pathFromUtf8(name);
	const SceneCompatibilityProbeResult probe = probeSceneCompatibility(source.u8string());
	QVERIFY2(classLabel(probe.classification) == fixture.className,
		(name + ": class is " + classLabel(probe.classification) + ", expected " + fixture.className + " (" + firstError(probe.diagnostics) + ")").c_str());
	QVERIFY2(probe.schemaVersion == fixture.schemaVersion,
		(name + ": schema_version is " + std::to_string(probe.schemaVersion) + ", expected " + std::to_string(fixture.schemaVersion)).c_str());
	QVERIFY2(probe.bundleVersion == fixture.bundleVersion,
		(name + ": bundle_version is " + describe(probe.bundleVersion) + ", expected " + describe(fixture.bundleVersion)).c_str());
	QVERIFY2(probe.savedWithAppVersion == fixture.savedWithAppVersion,
		(name + ": saved_with_app_version is \"" + probe.savedWithAppVersion + "\", expected \"" + fixture.savedWithAppVersion + "\"").c_str());

	if (fixture.className == "OlderMigratable" || probe.classification == SceneCompatibilityClass::OlderMigratable)
		QFAIL((name + ": a migration is registered for this fixture. Write the check of the migrated copy together with the migration;"
					  " this test accepts the classes Current, OlderUnsupported, Newer and Malformed.")
				.c_str());

	checkRejectionReason(fixture, probe);
	if (QTest::currentTestFailed())
		return;
	checkRunnable(fixture, source, probe.bundleVersion);
	if (QTest::currentTestFailed())
		return;

	std::error_code ec;
	const std::string extension = fs::is_directory(source, ec) ? "" : ".egscene";
	expectRefusedWithoutCopy(name, source, m_temp.path / ("upgrade-" + std::to_string(index) + extension));
}

void TestSceneVersions::rejectsSchemaNumbersNoBuildWrote_data() {
	QTest::addColumn<int>("schema");
	QTest::addColumn<int>("expected");
	QTest::addColumn<QString>("code");
	QTest::newRow("older") << 0 << static_cast<int>(SceneCompatibilityClass::OlderUnsupported) << "scene.compatibility.older";
	QTest::newRow("newer") << kCurrentSceneSchemaVersion + 1 << static_cast<int>(SceneCompatibilityClass::Newer) << "scene.compatibility.newer";
}

// Copies the first runnable directory fixture, gives the copy a schema number that no build wrote and checks that the
// probe, the loader and the copy upgrade reject it. The stored fixture is never written.
void TestSceneVersions::rejectsSchemaNumbersNoBuildWrote() {
	QFETCH(int, schema);
	QFETCH(int, expected);
	QFETCH(QString, code);
	const SceneCompatibilityClass expectedClass = static_cast<SceneCompatibilityClass>(expected);
	const Fixture* origin = nullptr;
	for (const Fixture& fixture : m_fixtures) {
		std::error_code ec;
		if (fixture.runnable && fs::is_directory(kFixtureDir / pathFromUtf8(fixture.path), ec)) {
			origin = &fixture;
			break;
		}
	}
	if (origin == nullptr)
		QFAIL("manifest.json lists no runnable directory fixture to derive the rejected schema numbers from");

	const std::string name = origin->path + " with schema_version " + std::to_string(schema);
	const fs::path copy = m_temp.path / ("rejected-" + std::to_string(schema));
	std::error_code ec;
	fs::copy(kFixtureDir / pathFromUtf8(origin->path), copy, fs::copy_options::recursive, ec);
	QVERIFY2(!ec, (name + ": the directory cannot be copied: " + ec.message()).c_str());
	json manifest;
	try {
		manifest = json::parse(readFileBytes(copy / "scene.json"));
	} catch (const json::exception& error) {
		QFAIL((name + ": scene.json cannot be read: " + error.what()).c_str());
	}
	manifest["schema_version"] = schema;
	QVERIFY2(writeFileBytes(copy / "scene.json", manifest.dump(4) + "\n"), (name + ": scene.json cannot be written").c_str());

	const SceneCompatibilityProbeResult probe = probeSceneCompatibility(copy.u8string());
	QVERIFY2(probe.classification == expectedClass,
		(name + ": class is " + classLabel(probe.classification) + ", expected " + classLabel(expectedClass)).c_str());
	const std::string reason = probe.diagnostics.empty() ? std::string() : toDisplayText(probe.diagnostics.front());
	const std::string bothNumbers = name + ": the reason is \"" + reason + "\", expected one with " + code.toStdString() + " that names both schema numbers";
	QVERIFY2(probe.diagnostics.size() == 1, bothNumbers.c_str());
	QVERIFY2(reason.find(code.toStdString()) != std::string::npos, bothNumbers.c_str());
	QVERIFY2(reason.find("schema " + std::to_string(schema)) != std::string::npos, bothNumbers.c_str());
	QVERIFY2(reason.find("schema " + std::to_string(kCurrentSceneSchemaVersion)) != std::string::npos, bothNumbers.c_str());
	const SceneLoadResult loaded = loadScenePath(copy.u8string());
	QVERIFY2(hasErrorCode(loaded.diagnostics, "scene.version.unsupported"),
		(name + ": loadScenePath reports " + firstError(loaded.diagnostics) + ", expected scene.version.unsupported").c_str());
	expectRefusedWithoutCopy(name, copy, m_temp.path / ("rejected-" + std::to_string(schema) + "-upgrade"));
}

void TestSceneVersions::schemaNumbersHaveFixtures() {
	std::vector<std::pair<int, std::string>> schemaNumbers;
	for (const Fixture& fixture : m_fixtures)
		schemaNumbers.emplace_back(fixture.schemaVersion, fixture.className);
	checkVersionNumbers("schema", "kCurrentSceneSchemaVersion", kCurrentSceneSchemaVersion, SceneMigrationStepKind::Schema, schemaNumbers);
}

void TestSceneVersions::bundleNumbersHaveFixtures() {
	std::vector<std::pair<int, std::string>> bundleNumbers;
	for (const Fixture& fixture : m_fixtures) {
		if (fixture.bundleVersion)
			bundleNumbers.emplace_back(*fixture.bundleVersion, fixture.className);
	}
	checkVersionNumbers("bundle", "kCurrentSceneBundleVersion", kCurrentSceneBundleVersion, SceneMigrationStepKind::Bundle, bundleNumbers);
}

QTEST_APPLESS_MAIN(TestSceneVersions)
#include "test_sceneversions.moc"
