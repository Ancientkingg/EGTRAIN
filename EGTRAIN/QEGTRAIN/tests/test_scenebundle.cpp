#include "io/third_party/miniz/miniz.h"
#include "scene/SceneBundle.h"
#include "scene/SceneModel.h"
#include "scene/SceneWriter.h"
#include "scene/Utf8Path.h"

#include <QByteArray>
#include <QString>
#include <QTest>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;

struct TempDir {
	fs::path path;

	TempDir() {
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		path = fs::temp_directory_path() / ("scene_bundle_test_" + std::to_string(stamp));
		fs::create_directories(path);
	}

	~TempDir() {
		std::error_code error;
		fs::remove_all(path, error);
	}
};

static std::string readBytes(const fs::path& path) {
	std::ifstream input(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

static bool hasCode(const std::vector<SceneDiagnostic>& diagnostics, const char* code) {
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.code == code)
			return true;
	}
	return false;
}

static bool hasDiagnostic(const std::vector<SceneDiagnostic>& diagnostics,
	const char* code, const char* file) {
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.code == code && diagnostic.file == file)
			return true;
	}
	return false;
}

static void put16(std::string& output, unsigned int value) {
	output.push_back(static_cast<char>(value & 0xffU));
	output.push_back(static_cast<char>((value >> 8) & 0xffU));
}

static void put32(std::string& output, std::uint32_t value) {
	for (unsigned int shift = 0; shift < 32; shift += 8)
		output.push_back(static_cast<char>((value >> shift) & 0xffU));
}

struct RawEntry {
	std::string name;
	std::string data = "x";
	std::uint32_t reportedUncompressedSize = 0;
	std::uint32_t externalAttributes = 0;
	std::uint16_t versionMadeBy = 20;
	std::uint16_t method = 0;
};

static std::string rawArchive(const std::vector<RawEntry>& entries) {
	std::string output;
	std::vector<std::uint32_t> offsets;
	std::vector<std::uint32_t> crcs;
	for (const auto& entry : entries) {
		offsets.push_back(static_cast<std::uint32_t>(output.size()));
		crcs.push_back(static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT,
			reinterpret_cast<const unsigned char*>(entry.data.data()), entry.data.size())));
		put32(output, 0x04034b50U);
		put16(output, 20);
		put16(output, 0);
		put16(output, entry.method);
		put16(output, 0);
		put16(output, 0);
		put32(output, crcs.back());
		put32(output, static_cast<std::uint32_t>(entry.data.size()));
		put32(output, entry.reportedUncompressedSize == 0 ? static_cast<std::uint32_t>(entry.data.size()) : entry.reportedUncompressedSize);
		put16(output, static_cast<unsigned int>(entry.name.size()));
		put16(output, 0);
		output.append(entry.name);
		output.append(entry.data);
	}
	const std::uint32_t centralOffset = static_cast<std::uint32_t>(output.size());
	for (std::size_t index = 0; index < entries.size(); ++index) {
		const auto& entry = entries[index];
		put32(output, 0x02014b50U);
		put16(output, entry.versionMadeBy);
		put16(output, 20);
		put16(output, 0);
		put16(output, entry.method);
		put16(output, 0);
		put16(output, 0);
		put32(output, crcs[index]);
		put32(output, static_cast<std::uint32_t>(entry.data.size()));
		put32(output, entry.reportedUncompressedSize == 0 ? static_cast<std::uint32_t>(entry.data.size()) : entry.reportedUncompressedSize);
		put16(output, static_cast<unsigned int>(entry.name.size()));
		put16(output, 0);
		put16(output, 0);
		put16(output, 0);
		put16(output, 0);
		put32(output, entry.externalAttributes);
		put32(output, offsets[index]);
		output.append(entry.name);
	}
	const std::uint32_t centralSize = static_cast<std::uint32_t>(output.size()) - centralOffset;
	put32(output, 0x06054b50U);
	put16(output, 0);
	put16(output, 0);
	put16(output, static_cast<unsigned int>(entries.size()));
	put16(output, static_cast<unsigned int>(entries.size()));
	put32(output, centralSize);
	put32(output, centralOffset);
	put16(output, 0);
	return output;
}

static bool writeBytes(const fs::path& path, const std::string& bytes) {
	std::ofstream output(path, std::ios::binary);
	output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	return static_cast<bool>(output);
}

struct ScopedTempEnvironment {
	std::array<const char*, 3> names{{"TMPDIR", "TEMP", "TMP"}};
	std::array<std::optional<std::string>, 3> previous;
	bool configured = true;

	explicit ScopedTempEnvironment(const fs::path& path) {
		for (std::size_t index = 0; index < names.size(); ++index) {
			if (const char* current = std::getenv(names[index]))
				previous[index] = current;
			configured = set(names[index], path) && configured;
		}
	}

	~ScopedTempEnvironment() {
		for (std::size_t index = 0; index < names.size(); ++index) {
			if (previous[index])
				set(names[index], previous[index]->c_str());
			else
				clear(names[index]);
		}
	}

	ScopedTempEnvironment(const ScopedTempEnvironment&) = delete;
	ScopedTempEnvironment& operator=(const ScopedTempEnvironment&) = delete;

private:
	static bool set(const char* name, const fs::path& value) {
#ifdef _WIN32
		std::wstring wideName;
		for (const char* character = name; *character != '\0'; ++character)
			wideName += static_cast<wchar_t>(*character);
		return _wputenv_s(wideName.c_str(), value.c_str()) == 0;
#else
		return setenv(name, value.c_str(), 1) == 0;
#endif
	}

	static bool set(const char* name, const char* value) {
#ifdef _WIN32
		return _putenv_s(name, value) == 0;
#else
		return setenv(name, value, 1) == 0;
#endif
	}

	static void clear(const char* name) {
#ifdef _WIN32
		_putenv_s(name, "");
#else
		unsetenv(name);
#endif
	}
};


// Japanese and Thai characters, as UTF-8. No Windows code page holds both.
static const std::string kUnicodeName = "scene-\xE6\x97\xA5\xE6\x9C\xAC-\xE0\xB8\x81";

static fs::path unicodePath(const fs::path& parent, const std::string& prefix = {}, const std::string& suffix = {}) {
	return parent / fs::u8path(prefix + kUnicodeName + suffix);
}


static bool sameCanonicalFiles(const SceneModel& first, const SceneModel& second, const fs::path& root) {
	const fs::path firstDir = root / "first";
	const fs::path secondDir = root / "second";
	const SceneSaveResult firstSave = saveScene(first, firstDir.string());
	const SceneSaveResult secondSave = saveScene(second, secondDir.string());
	if (!firstSave.success() || !secondSave.success())
		return false;
	for (const char* name : {"scene.json", "infrastructure.json", "stations.json", "signalling.json",
			 "rolling_stock.json", "services.json", "scenarios.json", "passengers.json", "views.json"}) {
		const fs::path firstPath = firstDir / name;
		const fs::path secondPath = secondDir / name;
		const bool firstExists = fs::exists(firstPath);
		const bool secondExists = fs::exists(secondPath);
		if (firstExists != secondExists)
			return false;
		if (!firstExists)
			continue;
		if (readBytes(firstPath) != readBytes(secondPath))
			return false;
	}
	return true;
}


class TestSceneBundle : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void extractionCleanup();
	void newSceneRoundTrip();
	void signallingAreaRoundTrip();
	void unicodePaths();
	void unreadablePaths();
	void serviceCategoryRoundTrip_data();
	void serviceCategoryRoundTrip();
	void serviceColourRoundTrip_data();
	void serviceColourRoundTrip();
	void rejectsBeforePrivateStaging();
	void retainsSnapshotsAndReplacesBundle();
	void keepsProtectedSectionBinding();
	void loadsBundleLikeDirectory();
	void unpacksBundle();
	void rejectsMalformedArchive_data();
	void rejectsMalformedArchive();

private:
	TempDir m_temp;
	SceneLoadResult m_source;
	// A bundle of the source scene, which the slots only read.
	fs::path m_bundle;
};

void TestSceneBundle::initTestCase() {
	m_source = loadScene(EGTRAIN_TEST_SOURCE_DIR "/fixtures/scenes/minimal");
	QVERIFY2(!hasErrors(m_source.diagnostics), "canonical directory loads");
	m_bundle = m_temp.path / "source.egscene";
	QVERIFY2(saveSceneBundle(m_source.scene, m_bundle.string()).success(), "canonical directory packs");
}

void TestSceneBundle::extractionCleanup() {
	const SceneModel& source = m_source.scene;
	TempDir fixture;
	const fs::path validBundle = fixture.path / "valid.egscene";
	QVERIFY2(saveSceneBundle(source, validBundle.string()).success(), "cleanup success fixture packs");

	const fs::path canonical = fixture.path / "canonical";
	QVERIFY2(saveScene(source, canonical.string()).success(), "cleanup malformed fixture stages canonical files");
	std::vector<RawEntry> entries;
	for (const char* name : {"scene.json", "infrastructure.json", "stations.json", "signalling.json",
			 "rolling_stock.json", "services.json", "scenarios.json"}) {
		std::string data = readBytes(canonical / name);
		if (std::string(name) == "scene.json") {
			const std::size_t openingBrace = data.find('{');
			QVERIFY2(openingBrace != std::string::npos, "cleanup manifest fixture is JSON");
			data.insert(openingBrace + 1,
				"\n    \"format\": \"egscene\",\n    \"bundle_version\": 1,");
		} else if (std::string(name) == "infrastructure.json") {
			data = "not-json";
		}
		entries.push_back({name, std::move(data)});
	}
	const fs::path malformedBundle = fixture.path / "malformed-infrastructure.egscene";
	QVERIFY2(writeBytes(malformedBundle, rawArchive(entries)), "cleanup malformed archive fixture writes");

	const fs::path extractionRoot = fixture.path / "private-temp";
	QVERIFY2(fs::create_directory(extractionRoot), "private temporary root creates");
	ScopedTempEnvironment environment(extractionRoot);
	std::error_code error;
	const fs::path routedRoot = fs::temp_directory_path(error);
	const bool routed = environment.configured && !error
		&& fs::equivalent(routedRoot, extractionRoot, error) && !error;
	QVERIFY2(routed, "bundle extraction uses the private temporary root");

	const SceneLoadResult successful = loadSceneBundle(validBundle.string());
	QVERIFY2(!hasErrors(successful.diagnostics), "cleanup fixture bundle loads successfully");
	error.clear();
	// Successful bundle load removes private extraction state.
	QVERIFY(fs::is_empty(extractionRoot, error));
	QVERIFY(!error);

	const SceneLoadResult failed = loadSceneBundle(malformedBundle.string());
	// Malformed canonical entry reaches parser and names infrastructure.json.
	QVERIFY(hasErrors(failed.diagnostics));
	QVERIFY(hasDiagnostic(failed.diagnostics, "scene.json.parse", "infrastructure.json"));
	error.clear();
	// Failed bundle load removes private extraction state.
	QVERIFY(fs::is_empty(extractionRoot, error));
	QVERIFY(!error);
}

void TestSceneBundle::newSceneRoundTrip() {
	SceneModel expected = makeNewSceneModel();
	expected.tracks.push_back({"bundle-track"});
	expected.trackViews.push_back({"bundle-track", 3, 0});
	SceneStation station;
	station.id = "bundle-station";
	station.name = "Bundle station";
	ScenePlatform explicitPlatform;
	explicitPlatform.id = "bundle-platform-explicit";
	explicitPlatform.hasLength = true;
	explicitPlatform.lengthM = 135.0;
	explicitPlatform.hasWidth = true;
	explicitPlatform.widthM = 3.25;
	station.platforms.push_back(explicitPlatform);
	station.platforms.push_back({"bundle-platform-default", {}});
	expected.stations.push_back(station);
	SceneStationView stationView;
	stationView.stationId = "bundle-station";
	stationView.latitude = 1.5;
	stationView.longitude = 2.5;
	stationView.regions = {{0, 4.5}};
	expected.stationViews.push_back(stationView);
	TempDir temp;
	const fs::path& root = temp.path;
	const fs::path folder = root / "new-scene";
	const fs::path bundle = root / "new-scene.egscene";
	QVERIFY2(saveScene(expected, folder.string()).success(), "new scene saves as canonical folder");
	QVERIFY2(saveSceneBundle(expected, bundle.string()).success(), "new scene saves as canonical bundle");
	const SceneLoadResult folderLoad = loadScene(folder.string());
	const SceneLoadResult bundleLoad = loadScenePath(bundle.string());
	QVERIFY2(!hasErrors(folderLoad.diagnostics), "new scene folder has no structural diagnostics");
	QVERIFY2(!hasErrors(bundleLoad.diagnostics), "new scene bundle has no structural diagnostics");
	// Bundle preserves authored display layout.
	QCOMPARE(bundleLoad.scene.trackViews.size(), std::size_t(1));
	QCOMPARE(bundleLoad.scene.stationViews.size(), std::size_t(1));
	// Bundle preserves explicit platform geometry and absent defaults.
	QCOMPARE(folderLoad.scene.stations.size(), std::size_t(1));
	QVERIFY(folderLoad.scene.stations[0].platforms[0].hasLength);
	QCOMPARE_EQ(folderLoad.scene.stations[0].platforms[0].lengthM, 135.0);
	QVERIFY(folderLoad.scene.stations[0].platforms[0].hasWidth);
	QCOMPARE_EQ(folderLoad.scene.stations[0].platforms[0].widthM, 3.25);
	QVERIFY(!folderLoad.scene.stations[0].platforms[1].hasLength);
	QCOMPARE_EQ(folderLoad.scene.stations[0].platforms[1].lengthM, 100.0);
	QVERIFY(!folderLoad.scene.stations[0].platforms[1].hasWidth);
	QCOMPARE_EQ(folderLoad.scene.stations[0].platforms[1].widthM, 2.5);
	QVERIFY2(sameCanonicalFiles(expected, folderLoad.scene, root / "folder-semantics"),
		"new scene folder preserves canonical case data");
	QVERIFY2(sameCanonicalFiles(expected, bundleLoad.scene, root / "bundle-semantics"),
		"new scene bundle preserves canonical case data");
	for (const char* name : {"scene.json", "infrastructure.json", "stations.json", "signalling.json",
			 "rolling_stock.json", "services.json", "scenarios.json"})
		QVERIFY2(fs::is_regular_file(folder / name), "new scene folder remains canonical");
	// New scene bundle remains an egscene container.
	QVERIFY(fs::is_regular_file(bundle));
	QVERIFY(bundle.extension() == ".egscene");
}

// Every level from 0 to 5, with and without a track, in file order.
void TestSceneBundle::signallingAreaRoundTrip() {
	SceneModel expected = makeNewSceneModel();
	expected.tracks = {{"area-track-a"}, {"area-track-b"}};
	for (int level = 0; level <= 5; ++level) {
		SceneSignallingArea area;
		area.id = "area-" + std::to_string(level);
		area.startKm = 1.5 * level;
		area.endKm = 1.5 * level + 1.25;
		area.level = level;
		if (level % 3 == 1)
			area.trackId = "area-track-a";
		else if (level % 3 == 2)
			area.trackId = "area-track-b";
		expected.signallingAreas.push_back(area);
	}
	TempDir temp;
	const fs::path bundle = temp.path / "areas.egscene";
	QVERIFY2(saveSceneBundle(expected, bundle.string()).success(), "scene with areas saves as bundle");
	const SceneLoadResult loaded = loadScenePath(bundle.string());
	QVERIFY2(!hasErrors(loaded.diagnostics), "bundle with areas has no structural diagnostics");
	// Bundle keeps every signalling area.
	QCOMPARE(loaded.scene.signallingAreas.size(), expected.signallingAreas.size());
	for (std::size_t index = 0; index < std::min(loaded.scene.signallingAreas.size(),
									expected.signallingAreas.size());
		++index) {
		const SceneSignallingArea& want = expected.signallingAreas[index];
		const SceneSignallingArea& got = loaded.scene.signallingAreas[index];
		// Bundle keeps area id, range, level and track exactly.
		QCOMPARE(got.id, want.id);
		QCOMPARE_EQ(got.startKm, want.startKm);
		QCOMPARE_EQ(got.endKm, want.endKm);
		QCOMPARE(got.level, want.level);
		QCOMPARE(got.trackId, want.trackId);
	}
}

// Every path crosses the scene API as UTF-8 text, in the file name, in the folders above it and in the temporary folder.
void TestSceneBundle::unicodePaths() {
	const SceneModel& scene = m_source.scene;
	TempDir temp;
	const fs::path root = temp.path / "unicode";
	// A UTF-8 name keeps its characters in a path.
	QCOMPARE(pathFromUtf8(kUnicodeName).u8string(), kUnicodeName);
	QVERIFY2(!pathFromUtf8("caf\xE9").empty(), "a name that is not UTF-8 still gives a path");
	const fs::path folder = unicodePath(root);
	const fs::path bundle = unicodePath(folder, "", ".egscene");
	const std::string bundleText = bundle.u8string();
	// A bundle saves to a path with Unicode names.
	QVERIFY(saveSceneBundle(scene, bundleText).success());
	QVERIFY(fs::is_regular_file(bundle));
	QVERIFY2(probeSceneBundle(bundleText).structurallyValid, "a bundle at a path with Unicode names is probed");
	const SceneLoadResult loaded = loadScenePath(bundleText);
	// A bundle at a path with Unicode names loads.
	QVERIFY(!hasErrors(loaded.diagnostics));
	QVERIFY(loaded.inputSnapshot == readBytes(bundle));

	const fs::path missing = unicodePath(folder, "missing-", ".egscene");
	const SceneLoadResult absent = loadScenePath(missing.u8string());
	QVERIFY2(hasDiagnostic(absent.diagnostics, "scene.bundle.file.missing", missing.u8string().c_str()),
		"a diagnostic gives a path with Unicode names as UTF-8");

	const fs::path unpacked = unicodePath(folder, "unpacked-");
	// A bundle unpacks to a directory with a Unicode name.
	QVERIFY(unpackSceneBundle(bundleText, unpacked.u8string()).success());
	QVERIFY(fs::is_regular_file(unpacked / "scene.json"));
	// A scene directory with a Unicode name loads and is read as a snapshot.
	QVERIFY(!hasErrors(loadScene(unpacked.u8string()).diagnostics));
	QVERIFY(readSceneDirectorySnapshot(unpacked.u8string()).reason.empty());

	const fs::path directory = unicodePath(folder, "directory-");
	// A scene directory saves to a path with Unicode names.
	QVERIFY(saveScene(scene, directory.u8string()).success());
	QVERIFY(fs::is_regular_file(directory / "scene.json"));

	const fs::path temporaryRoot = unicodePath(root, "temporary-");
	QVERIFY2(fs::create_directories(temporaryRoot), "temporary folder with a Unicode name creates");
	{
		ScopedTempEnvironment environment(temporaryRoot);
		std::error_code error;
		// The temporary folder has a Unicode name.
		QVERIFY(environment.configured);
		QVERIFY(fs::equivalent(fs::temp_directory_path(error), temporaryRoot, error));
		QVERIFY(!error);
		const fs::path viaTemporary = unicodePath(folder, "via-temporary-", ".egscene");
		// A bundle saves and loads while the temporary folder has a Unicode name.
		QVERIFY(saveSceneBundle(scene, viaTemporary.u8string()).success());
		QVERIFY(!hasErrors(loadScenePath(viaTemporary.u8string()).diagnostics));
		error.clear();
		// The work in a temporary folder with a Unicode name leaves nothing behind.
		QVERIFY(fs::is_empty(temporaryRoot, error));
		QVERIFY(!error);
	}
}

// A scene path that the file system refuses gives an error and does not throw.
void TestSceneBundle::unreadablePaths() {
	TempDir temp;
	const fs::path& root = temp.path;
	const SceneLoadResult tooLong = loadScenePath((root / std::string(300, 'x') / "scene").u8string());
	QVERIFY2(hasCode(tooLong.diagnostics, "scene.dir.unreadable") || hasCode(tooLong.diagnostics, "scene.dir.missing"),
		"a path with a 300-character name gives an error");
#ifndef _WIN32
	// A folder without permissions does not stop root, so the case skips itself then. The permissions are given back
	// before the checks, so that the temporary folder can be removed after a failure.
	const fs::path closed = root / "closed";
	fs::create_directories(closed / "scene");
	fs::permissions(closed, fs::perms::none, fs::perm_options::replace);
	const bool closedFolderRefused = geteuid() == 0 || hasCode(loadScenePath((closed / "scene").u8string()).diagnostics, "scene.dir.unreadable");
	fs::permissions(closed, fs::perms::owner_all, fs::perm_options::replace);
	// A scene folder that cannot be read under a folder that can.
	fs::permissions(closed / "scene", fs::perms::none, fs::perm_options::replace);
	const bool closedSceneRefused = geteuid() == 0 || hasErrors(loadScenePath((closed / "scene").u8string()).diagnostics);
	fs::permissions(closed / "scene", fs::perms::owner_all, fs::perm_options::replace);
	QVERIFY2(closedFolderRefused, "a scene under a folder that cannot be read gives scene.dir.unreadable");
	QVERIFY2(closedSceneRefused, "a scene folder that cannot be read gives an error");
#endif
}

void TestSceneBundle::serviceCategoryRoundTrip_data() {
	QTest::addColumn<QString>("category");
	for (const char* category : {"", "Intercity", "Regional", "High speed/international",
			 "Freight", "Metro/urban", "Suburban", " Regional heritage / custom "})
		QTest::newRow(*category ? category : "missing") << QString::fromUtf8(category);
}

void TestSceneBundle::serviceCategoryRoundTrip() {
	QFETCH(QString, category);
	if (m_source.scene.services.empty())
		QSKIP("the fixture scene has no service");
	SceneModel categoryScene = m_source.scene;
	categoryScene.services.front().category = category.toStdString();
	TempDir temp;
	const fs::path categoryFolder = temp.path / "category";
	const fs::path categoryBundle = temp.path / "category.egscene";
	// Category fixtures save as folder and bundle.
	QVERIFY(saveScene(categoryScene, categoryFolder.string()).success());
	QVERIFY(saveSceneBundle(categoryScene, categoryBundle.string()).success());
	for (const fs::path& path : {categoryFolder, categoryBundle}) {
		const SceneLoadResult loaded = loadScenePath(path.string());
		// Missing, preset and unknown category values round-trip exactly.
		QVERIFY(!hasErrors(loaded.diagnostics));
		QVERIFY(!loaded.scene.services.empty());
		QCOMPARE(loaded.scene.services.front().category, category.toStdString());
	}
}

void TestSceneBundle::serviceColourRoundTrip_data() {
	QTest::addColumn<QString>("color");
	for (const char* color : {"", "#3c8dd2", "#3C8DD2", "not-a-colour"})
		QTest::newRow(*color ? color : "missing") << QString::fromUtf8(color);
}

void TestSceneBundle::serviceColourRoundTrip() {
	QFETCH(QString, color);
	if (m_source.scene.services.empty())
		QSKIP("the fixture scene has no service");
	SceneModel colorScene = m_source.scene;
	colorScene.services.front().category.clear();
	colorScene.services.front().visualizationColor = color.toStdString();
	TempDir temp;
	const fs::path colorFolder = temp.path / "color";
	const fs::path colorBundle = temp.path / "color.egscene";
	// Colour fixtures save as folder and bundle.
	QVERIFY(saveScene(colorScene, colorFolder.string()).success());
	QVERIFY(saveSceneBundle(colorScene, colorBundle.string()).success());
	for (const fs::path& path : {colorFolder, colorBundle}) {
		const SceneLoadResult loaded = loadScenePath(path.string());
		// Missing, valid and invalid service colours round-trip exactly.
		QVERIFY(!hasErrors(loaded.diagnostics));
		QVERIFY(!loaded.scene.services.empty());
		QCOMPARE(loaded.scene.services.front().visualizationColor, color.toStdString());
	}
}

void TestSceneBundle::rejectsBeforePrivateStaging() {
	TempDir temp;
	const fs::path firstBundle = temp.path / "first.egscene";
	SceneModel unsupported = m_source.scene;
	++unsupported.schemaVersion;
	QVERIFY2(!saveSceneBundle(unsupported, firstBundle.string()).writeAttempted,
		"unsupported schema rejects before private staging");
	QVERIFY2(!saveSceneBundle(m_source.scene, "").writeAttempted,
		"empty bundle path rejects before private staging");
#ifndef _WIN32
	const char* previousTemp = std::getenv("TMPDIR");
	const std::optional<std::string> savedTemp = previousTemp
		? std::optional<std::string>(previousTemp)
		: std::nullopt;
	const std::string missingTemp = (temp.path / "missing-temporary-root").string();
	const bool changedTemp = setenv("TMPDIR", missingTemp.c_str(), 1) == 0;
	const SceneSaveResult missingTempResult = saveSceneBundle(m_source.scene, firstBundle.string());
	if (savedTemp) setenv("TMPDIR", savedTemp->c_str(), 1);
	else unsetenv("TMPDIR");
	// Temporary-root lookup failure does not claim a staging write attempt.
	QVERIFY(changedTemp);
	QVERIFY(!missingTempResult.success());
	QVERIFY(!missingTempResult.writeAttempted);
#endif
	const SceneSaveResult stagingFailure = saveSceneBundle(m_source.scene, temp.path.string());
	// Directory target failure retains nested private staging attempt.
	QVERIFY(!stagingFailure.success());
	QVERIFY(stagingFailure.writeAttempted);
}

void TestSceneBundle::retainsSnapshotsAndReplacesBundle() {
	const SceneModel& source = m_source.scene;
	TempDir temp;
	const fs::path firstBundle = temp.path / "first.egscene";
	const fs::path secondBundle = temp.path / "second.egscene";
	const SceneSaveResult firstSave = saveSceneBundle(source, firstBundle.string());
	QVERIFY2(firstSave.writeAttempted, "bundle success retains private staging attempt");
	QVERIFY2(firstSave.success(), "canonical directory packs");
	const std::string firstBundleBytes = readBytes(firstBundle);
	// Successful bundle save retains the exact staged archive bytes.
	QVERIFY(!firstSave.inputSnapshot.empty());
	QVERIFY(firstSave.inputSnapshot == firstBundleBytes);
	const SceneLoadResult firstLoad = loadScenePath(firstBundle.string());
	// Bundle load retains the exact archive bytes it parsed.
	QVERIFY(!hasErrors(firstLoad.diagnostics));
	QVERIFY(firstLoad.inputSnapshot == firstBundleBytes);
	QVERIFY2(writeBytes(firstBundle, "external replacement"), "external bundle replacement writes");
	// External bundle replacement does not mutate retained snapshots.
	QVERIFY(firstSave.inputSnapshot == firstBundleBytes);
	QVERIFY(firstLoad.inputSnapshot == firstBundleBytes);
	QVERIFY(readBytes(firstBundle) != firstBundleBytes);
	QVERIFY2(saveSceneBundle(source, firstBundle.string()).success(), "bundle replacement restores the external test fixture");
	const SceneSaveResult secondSave = saveSceneBundle(source, secondBundle.string());
	QVERIFY2(secondSave.success(), "canonical directory packs twice");
	QVERIFY2(readBytes(firstBundle) == readBytes(secondBundle), "bundle bytes are deterministic");
	const SceneSaveResult replacementSave = saveSceneBundle(source, firstBundle.string());
	// Existing bundle is atomically replaced.
	QVERIFY(replacementSave.success());
	QVERIFY(readBytes(firstBundle) == readBytes(secondBundle));
	const std::string originalBundleBytes = readBytes(firstBundle);
	SceneModel editedScene = source;
	editedScene.description += " (edited)";
	const fs::path saveAsBundle = temp.path / "save-as.egscene";
	const SceneSaveResult saveAsResult = saveSceneBundle(editedScene, saveAsBundle.string());
	// Save As writes a second bundle without changing the original bytes.
	QVERIFY(saveAsResult.success());
	QVERIFY(readBytes(firstBundle) == originalBundleBytes);
	QVERIFY(readBytes(saveAsBundle) != originalBundleBytes);
}

void TestSceneBundle::keepsProtectedSectionBinding() {
	SceneModel boundScene = m_source.scene;
	if (!sceneSignals(boundScene).empty())
		sceneSignals(boundScene).front().protectedSection = "@sig.section@";
	TempDir temp;
	const fs::path boundBundle = temp.path / "bound.egscene";
	QVERIFY2(saveSceneBundle(boundScene, boundBundle.string()).success(),
		"bundle with a protected-section binding saves");
	const SceneLoadResult boundLoad = loadScenePath(boundBundle.string());
	// Bundle preserves a present protected-section binding.
	QVERIFY(!hasErrors(boundLoad.diagnostics));
	QVERIFY(!sceneSignals(boundLoad.scene).empty());
	QCOMPARE(sceneSignals(boundLoad.scene).front().protectedSection, "@sig.section@");
}

void TestSceneBundle::loadsBundleLikeDirectory() {
	const SceneLoadResult bundled = loadScenePath(m_bundle.string());
	QVERIFY2(!hasErrors(bundled.diagnostics), "bundle loads through shared path dispatch");
	// ID-only signals remain unbound after bundle round-trip.
	QVERIFY(!sceneSignals(bundled.scene).empty());
	QVERIFY(sceneSignals(bundled.scene).front().protectedSection.empty());
	TempDir temp;
	QVERIFY2(sameCanonicalFiles(m_source.scene, bundled.scene, temp.path / "model-compare"),
		"bundle model matches directory model");
}

void TestSceneBundle::unpacksBundle() {
	TempDir temp;
	const fs::path unpacked = temp.path / "unpacked";
	const SceneSaveResult unpackResult = unpackSceneBundle(m_bundle.string(), unpacked.string());
	QVERIFY2(unpackResult.success(), "bundle unpacks atomically");
	for (const char* name : {"scene.json", "infrastructure.json", "stations.json", "signalling.json",
			 "rolling_stock.json", "services.json", "scenarios.json"})
		QVERIFY2(fs::is_regular_file(unpacked / name), "unpacked canonical entry exists");
	// Bundle omits absent views and legacy outputs.
	QVERIFY(!fs::exists(unpacked / "views.json"));
	QVERIFY(!fs::exists(unpacked / "legacy"));
	const SceneLoadResult unpackedLoad = loadScene(unpacked.string());
	// Unpacked scene reloads.
	QVERIFY(!hasErrors(unpackedLoad.diagnostics));
	QVERIFY(sameCanonicalFiles(m_source.scene, unpackedLoad.scene, temp.path / "unpacked-compare"));
	const fs::path existingDestination = temp.path / "existing-destination";
	fs::create_directory(existingDestination);
	QVERIFY2(writeBytes(existingDestination / "keep.txt", "keep"), "existing destination fixture writes");
	const SceneSaveResult existingResult = unpackSceneBundle(m_bundle.string(), existingDestination.string());
	// Unpack refuses and preserves an existing destination.
	QVERIFY(!existingResult.success());
	QVERIFY(hasCode(existingResult.diagnostics, "scene.bundle.publish"));
	QCOMPARE(readBytes(existingDestination / "keep.txt"), "keep");
}

void TestSceneBundle::rejectsMalformedArchive_data() {
	QTest::addColumn<QString>("diagnosticCode");
	QTest::addColumn<QByteArray>("bytes");
	const auto row = [](const char* name, const char* diagnosticCode, const std::string& bytes) {
		QTest::newRow(name) << QString(diagnosticCode) << QByteArray::fromStdString(bytes);
	};
	row("traversal", "scene.bundle.path", rawArchive({{"../../outside", "x"}}));
	row("absolute", "scene.bundle.path", rawArchive({{"/outside", "x"}}));
	row("symlink", "scene.bundle.entry", rawArchive({{"scene.json", "x", 0, 0120000U << 16, 0x0314}}));
	row("macOS symlink", "scene.bundle.entry", rawArchive({{"scene.json", "x", 0, 0120000U << 16, 0x1314}}));
	row("case-insensitive duplicate", "scene.bundle.duplicate", rawArchive({{"scene.json", "x"}, {"SCENE.JSON", "x"}}));
	row("entry limit", "scene.bundle.entries", rawArchive(std::vector<RawEntry>(17, {"x", "x", 0, 0, 20})));
	row("entry size limit", "scene.bundle.entry.size", rawArchive({{"scene.json", "x", 16U * 1024U * 1024U + 1U, 0, 20, 8}}));
	row("ratio limit", "scene.bundle.ratio", rawArchive({{"scene.json", "x", 1001U, 0, 20, 8}}));
	row("unknown entry", "scene.bundle.entry", rawArchive({{"assets.json", "x"}}));
	row("missing required entry", "scene.bundle.required", rawArchive({{"scene.json", "x"}}));
	std::string truncated = readBytes(m_bundle);
	truncated.resize(truncated.size() - 4);
	row("truncated archive", "scene.bundle.archive", truncated);
}

void TestSceneBundle::rejectsMalformedArchive() {
	QFETCH(QString, diagnosticCode);
	QFETCH(QByteArray, bytes);
	TempDir temp;
	const fs::path malformedPath = temp.path / (std::string(QTest::currentDataTag()) + ".egscene");
	QVERIFY2(writeBytes(malformedPath, bytes.toStdString()), "malformed archive fixture writes");
	const SceneLoadResult rejected = loadSceneBundle(malformedPath.string());
	// The archive is rejected with the diagnostic code of the row.
	QVERIFY(hasErrors(rejected.diagnostics));
	QVERIFY(hasCode(rejected.diagnostics, diagnosticCode.toStdString().c_str()));
}

QTEST_APPLESS_MAIN(TestSceneBundle)
#include "test_scenebundle.moc"
