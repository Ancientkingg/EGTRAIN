// Scenes written by older builds, kept as fixtures in tests/fixtures/scene-versions and listed in its manifest.json.
// Every fixture is a byte copy and is never edited or saved again.
#include "scene/SceneBundle.h"
#include "scene/SceneCompatibility.h"
#include "scene/SceneDiagnostic.h"
#include "scene/SceneMigration.h"
#include "scene/SceneModel.h"
#include "scene/SceneValidator.h"
#include "scene/Utf8Path.h"

#include <chrono>
#include <climits>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
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

static bool expect(bool condition, const std::string& message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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
	int schemaVersion = 0;
	std::optional<int> bundleVersion;
	std::string savedWithAppVersion;
	bool runnable = false;
	std::map<std::string, std::size_t> counts; // only when runnable
};

// Reads the keys of one manifest entry. A key that is missing or has the wrong type is a failed check that names the entry.
struct KeyReader {
	const json& object;
	std::string owner;
	bool ok = true;

	KeyReader(const json& entry, std::string name) : object(entry), owner(std::move(name)) {}

	void fail(const char* key, const char* problem) {
		ok = false;
		std::cerr << "failed: manifest entry " << owner << ": the key " << key << " " << problem << "\n";
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
static bool expectRefusedWithoutCopy(const std::string& name, const fs::path& source, const fs::path& destination) {
	std::string problem;
	const std::string before = sourceBytes(source, problem);
	bool ok = expect(problem.empty(), name + ": the source cannot be read before the copy upgrade: " + problem);
	const SceneMigrationResult result = migrateSceneCopy(source.u8string(), destination.u8string(), productionSceneMigrationRegistry());
	ok &= expect(!result.success(), name + ": migrateSceneCopy succeeded, expected a refusal");
	std::error_code ec;
	ok &= expect(!fs::exists(destination, ec), name + ": migrateSceneCopy created " + destination.filename().u8string() + ", expected nothing");
	const std::string after = sourceBytes(source, problem);
	ok &= expect(problem.empty() && after == before,
		name + ": the source has " + std::to_string(after.size()) + " bytes after the copy upgrade, expected the same " + std::to_string(before.size()) + " bytes as before");
	return ok;
}

// Reads manifest.json. Every entry that has all its keys is returned; the others are reported.
static bool readManifest(const fs::path& manifestPath, std::vector<Fixture>& fixtures, std::set<std::string>& listed) {
	json manifest;
	const std::string text = readFileBytes(manifestPath);
	if (text.empty())
		return expect(false, "manifest.json is missing or empty");
	try {
		manifest = json::parse(text);
	} catch (const json::exception& error) {
		return expect(false, std::string("manifest.json cannot be read: ") + error.what());
	}
	const auto entries = manifest.is_object() ? manifest.find("fixtures") : manifest.end();
	if (!manifest.is_object() || entries == manifest.end() || !entries->is_array() || entries->empty())
		return expect(false, "manifest.json must be an object with a non-empty array fixtures");
	bool ok = true;
	std::size_t index = 0;
	for (const json& entry : *entries) {
		std::string owner = "fixtures[" + std::to_string(index++) + "]";
		if (!entry.is_object()) {
			ok &= expect(false, "manifest entry " + owner + " is not an object");
			continue;
		}
		const auto path = entry.find("path");
		if (path != entry.end() && path->is_string())
			owner = path->get<std::string>();
		KeyReader reader(entry, owner);
		Fixture fixture;
		fixture.path = reader.text("path");
		reader.text("origin");
		fixture.className = reader.text("class");
		fixture.schemaVersion = reader.integer("schema_version", true).value_or(0);
		fixture.bundleVersion = reader.integer("bundle_version", false);
		fixture.savedWithAppVersion = reader.text("saved_with_app_version");
		fixture.runnable = reader.boolean("runnable");
		if (fixture.runnable)
			fixture.counts = reader.sizes("counts");
		ok &= reader.ok;
		if (!fixture.path.empty())
			ok &= expect(listed.insert(fixture.path).second, "manifest entry " + owner + " is listed twice");
		if (reader.ok)
			fixtures.push_back(fixture);
	}
	return ok;
}

// Every entry of the fixture directory is listed in the manifest, and every listed path exists.
static bool checkDirectoryAgrees(const fs::path& fixtureDir, const std::set<std::string>& listed) {
	bool ok = true;
	std::error_code ec;
	for (fs::directory_iterator it(fixtureDir, ec), end; !ec && it != end; it.increment(ec)) {
		const std::string name = it->path().filename().u8string();
		if (name == "manifest.json" || name.empty() || name.front() == '.')
			continue;
		ok &= expect(listed.count(name) == 1, "the fixture directory has " + name + ", which manifest.json does not list");
	}
	ok &= expect(!ec, "the fixture directory cannot be listed: " + ec.message());
	for (const std::string& name : listed) {
		ec.clear();
		ok &= expect(fs::exists(fixtureDir / pathFromUtf8(name), ec), "manifest.json lists " + name + ", which the fixture directory does not have");
	}
	return ok;
}

static bool checkRunnable(const Fixture& fixture, const fs::path& source, const std::optional<int>& probedBundleVersion) {
	const std::string& name = fixture.path;
	const SceneLoadResult loaded = loadScenePath(source.u8string());
	const bool loadFailed = hasErrors(loaded.diagnostics);
	std::vector<SceneDiagnostic> validation;
	if (!loadFailed)
		validation = validateRunnableScene(loaded.scene);
	const bool accepted = !loadFailed && !hasErrors(validation);
	if (!fixture.runnable)
		return expect(!accepted, name + ": loadScenePath and validateRunnableScene report no error, expected at least one");

	bool ok = expect(!loadFailed, name + ": loadScenePath reports an error (" + firstError(loaded.diagnostics) + "), expected none");
	ok &= expect(loaded.bundleVersion == probedBundleVersion,
		name + ": loadScenePath reports bundle_version " + describe(loaded.bundleVersion) + ", expected " + describe(probedBundleVersion));
	if (loadFailed)
		return ok;
	ok &= expect(!hasErrors(validation), name + ": validateRunnableScene reports an error (" + firstError(validation) + "), expected none");
	for (const auto& size : modelSizes(loaded.scene)) {
		const auto expected = fixture.counts.find(size.first);
		if (expected == fixture.counts.end()) {
			ok &= expect(false, name + ": counts has no " + size.first);
			continue;
		}
		ok &= expect(size.second == expected->second,
			name + ": the loaded model has " + std::to_string(size.second) + " " + size.first + ", expected " + std::to_string(expected->second));
	}
	return ok;
}

// Pins how one fixture is classified, loaded, validated and counted, and that a copy upgrade refuses it.
static bool checkFixture(const Fixture& fixture, const fs::path& fixtureDir, const fs::path& tempDir, std::size_t index) {
	const std::string& name = fixture.path;
	const fs::path source = fixtureDir / pathFromUtf8(name);
	const SceneCompatibilityProbeResult probe = probeSceneCompatibility(source.u8string());
	bool ok = expect(classLabel(probe.classification) == fixture.className,
		name + ": class is " + classLabel(probe.classification) + ", expected " + fixture.className + " (" + firstError(probe.diagnostics) + ")");
	ok &= expect(probe.schemaVersion == fixture.schemaVersion,
		name + ": schema_version is " + std::to_string(probe.schemaVersion) + ", expected " + std::to_string(fixture.schemaVersion));
	ok &= expect(probe.bundleVersion == fixture.bundleVersion,
		name + ": bundle_version is " + describe(probe.bundleVersion) + ", expected " + describe(fixture.bundleVersion));
	ok &= expect(probe.savedWithAppVersion == fixture.savedWithAppVersion,
		name + ": saved_with_app_version is \"" + probe.savedWithAppVersion + "\", expected \"" + fixture.savedWithAppVersion + "\"");

	if (fixture.className == "OlderMigratable" || probe.classification == SceneCompatibilityClass::OlderMigratable)
		return expect(false, name + ": a migration is registered for this fixture. Write the check of the migrated copy together with the migration;"
									" this test accepts the classes Current, OlderUnsupported, Newer and Malformed.");

	ok &= checkRunnable(fixture, source, probe.bundleVersion);

	std::error_code ec;
	const std::string extension = fs::is_directory(source, ec) ? "" : ".egscene";
	ok &= expectRefusedWithoutCopy(name, source, tempDir / ("upgrade-" + std::to_string(index) + extension));
	return ok;
}

// Copies the first runnable directory fixture twice, gives the copies a schema number that no build wrote and
// checks that the probe, the loader and the copy upgrade reject both. The stored fixture is never written.
static bool checkRejectedSchemaNumbers(const std::vector<Fixture>& fixtures, const fs::path& fixtureDir, const fs::path& tempDir) {
	const Fixture* origin = nullptr;
	for (const Fixture& fixture : fixtures) {
		std::error_code ec;
		if (fixture.runnable && fs::is_directory(fixtureDir / pathFromUtf8(fixture.path), ec)) {
			origin = &fixture;
			break;
		}
	}
	if (!origin)
		return expect(false, "manifest.json lists no runnable directory fixture to derive the rejected schema numbers from");

	const std::vector<std::pair<int, SceneCompatibilityClass>> cases = {
		{0, SceneCompatibilityClass::OlderUnsupported},
		{kCurrentSceneSchemaVersion + 1, SceneCompatibilityClass::Newer}};
	bool ok = true;
	for (const auto& rejected : cases) {
		const std::string name = origin->path + " with schema_version " + std::to_string(rejected.first);
		const fs::path copy = tempDir / ("rejected-" + std::to_string(rejected.first));
		std::error_code ec;
		fs::copy(fixtureDir / pathFromUtf8(origin->path), copy, fs::copy_options::recursive, ec);
		ok &= expect(!ec, name + ": the directory cannot be copied: " + ec.message());
		if (ec)
			continue;
		json manifest;
		try {
			manifest = json::parse(readFileBytes(copy / "scene.json"));
		} catch (const json::exception& error) {
			ok &= expect(false, name + ": scene.json cannot be read: " + error.what());
			continue;
		}
		manifest["schema_version"] = rejected.first;
		ok &= expect(writeFileBytes(copy / "scene.json", manifest.dump(4) + "\n"), name + ": scene.json cannot be written");

		const SceneCompatibilityClass found = probeSceneCompatibility(copy.u8string()).classification;
		ok &= expect(found == rejected.second, name + ": class is " + classLabel(found) + ", expected " + classLabel(rejected.second));
		const SceneLoadResult loaded = loadScenePath(copy.u8string());
		ok &= expect(hasErrorCode(loaded.diagnostics, "scene.version.unsupported"),
			name + ": loadScenePath reports " + firstError(loaded.diagnostics) + ", expected scene.version.unsupported");
		ok &= expectRefusedWithoutCopy(name, copy, tempDir / ("rejected-" + std::to_string(rejected.first) + "-upgrade"));
	}
	return ok;
}

// A number below the current one needs a fixture of that number and either a registered migration path or a fixture
// listed as OlderUnsupported. Each loop covers the numbers below the current one and does not run while the current
// number is 1.
static bool checkVersionNumbers(const char* kind, const char* constant, int current, SceneMigrationStepKind stepKind,
	const std::vector<std::pair<int, std::string>>& fixtureNumbers) {
	bool ok = true;
	const std::string rule = "See the version rule at the end of \"Compatibility boundary\" in docs/architecture/scene-model.md.";
	for (int number = 1; number < current; ++number) {
		const std::string label = std::string(kind) + " number " + std::to_string(number);
		bool hasFixture = false;
		bool listedUnsupported = false;
		for (const auto& fixture : fixtureNumbers) {
			if (fixture.first != number)
				continue;
			hasFixture = true;
			listedUnsupported = listedUnsupported || fixture.second == "OlderUnsupported";
		}
		ok &= expect(hasFixture,
			label + ", check (i): no fixture in manifest.json has it, and " + constant + " is " + std::to_string(current)
				+ ". Add a fixture written by a build with that number. " + rule);
		const bool registered = sceneMigrationPathAvailable(productionSceneMigrationRegistry(), stepKind, number, current);
		ok &= expect(registered || listedUnsupported,
			label + ", check (ii): no migration from " + std::to_string(number) + " to " + std::to_string(current)
				+ " is registered and no fixture with that number has the class OlderUnsupported. Register the migration, or set"
				+ " that class on the fixtures of that number to drop support on purpose. " + rule);
	}
	return ok;
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Usage: test_sceneversions <fixture-directory>\n";
		return 1;
	}
	const fs::path fixtureDir = pathFromUtf8(argv[1]);
	TempDir temp;
	std::vector<Fixture> fixtures;
	std::set<std::string> listed;
	bool ok = readManifest(fixtureDir / "manifest.json", fixtures, listed);
	if (listed.empty())
		return 1;

	ok &= checkDirectoryAgrees(fixtureDir, listed);

	std::size_t index = 0;
	for (const Fixture& fixture : fixtures)
		ok &= checkFixture(fixture, fixtureDir, temp.path, index++);

	ok &= checkRejectedSchemaNumbers(fixtures, fixtureDir, temp.path);

	std::vector<std::pair<int, std::string>> schemaNumbers;
	std::vector<std::pair<int, std::string>> bundleNumbers;
	for (const Fixture& fixture : fixtures) {
		schemaNumbers.emplace_back(fixture.schemaVersion, fixture.className);
		if (fixture.bundleVersion)
			bundleNumbers.emplace_back(*fixture.bundleVersion, fixture.className);
	}
	ok &= checkVersionNumbers("schema", "kCurrentSceneSchemaVersion", kCurrentSceneSchemaVersion, SceneMigrationStepKind::Schema, schemaNumbers);
	ok &= checkVersionNumbers("bundle", "kCurrentSceneBundleVersion", kCurrentSceneBundleVersion, SceneMigrationStepKind::Bundle, bundleNumbers);

	if (ok)
		std::cout << "all SceneVersions tests passed\n";
	return ok ? 0 : 1;
}
