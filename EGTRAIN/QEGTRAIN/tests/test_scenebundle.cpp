#include "io/third_party/miniz/miniz.h"
#include "scene/SceneBundle.h"
#include "scene/SceneModel.h"
#include "scene/SceneWriter.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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
		put16(output, entry.method);
		put16(output, 0);
		put16(output, 0);
		put32(output, crcs.back());
		put32(output, static_cast<std::uint32_t>(entry.data.size()));
		put32(output, entry.reportedUncompressedSize == 0 ? static_cast<std::uint32_t>(entry.data.size())
				: entry.reportedUncompressedSize);
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
		put32(output, entry.reportedUncompressedSize == 0 ? static_cast<std::uint32_t>(entry.data.size())
				: entry.reportedUncompressedSize);
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

static bool sameCanonicalFiles(const SceneModel& first, const SceneModel& second, const fs::path& root) {
	const fs::path firstDir = root / "first";
	const fs::path secondDir = root / "second";
	const SceneSaveResult firstSave = saveScene(first, firstDir.string());
	const SceneSaveResult secondSave = saveScene(second, secondDir.string());
	if (!firstSave.success() || !secondSave.success())
		return false;
	for (const char* name : {"scene.json", "infrastructure.json", "stations.json", "signalling.json",
			"rolling_stock.json", "services.json", "scenarios.json", "passengers.json"}) {
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

static bool testNewSceneRoundTrip(const fs::path& root) {
	const SceneModel expected = makeNewSceneModel();
	const fs::path folder = root / "new-scene";
	const fs::path bundle = root / "new-scene.egscene";
	bool ok = true;
	ok &= expect(saveScene(expected, folder.string()).success(), "new scene saves as canonical folder");
	ok &= expect(saveSceneBundle(expected, bundle.string()).success(), "new scene saves as canonical bundle");
	const SceneLoadResult folderLoad = loadScene(folder.string());
	const SceneLoadResult bundleLoad = loadScenePath(bundle.string());
	ok &= expect(!hasErrors(folderLoad.diagnostics), "new scene folder has no structural diagnostics");
	ok &= expect(!hasErrors(bundleLoad.diagnostics), "new scene bundle has no structural diagnostics");
	ok &= expect(sameCanonicalFiles(expected, folderLoad.scene, root / "folder-semantics"),
			"new scene folder preserves canonical case data");
	ok &= expect(sameCanonicalFiles(expected, bundleLoad.scene, root / "bundle-semantics"),
			"new scene bundle preserves canonical case data");
	for (const char* name : {"scene.json", "infrastructure.json", "stations.json", "signalling.json",
			"rolling_stock.json", "services.json", "scenarios.json"})
		ok &= expect(fs::is_regular_file(folder / name), "new scene folder remains canonical");
	ok &= expect(fs::is_regular_file(bundle) && bundle.extension() == ".egscene",
			"new scene bundle remains an egscene container");
	return ok;
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Usage: test_scenebundle <scene-directory>\n";
		return 1;
	}

	bool ok = true;
	TempDir temp;
	ok &= testNewSceneRoundTrip(temp.path);
	const SceneLoadResult source = loadScene(argv[1]);
	ok &= expect(!hasErrors(source.diagnostics), "canonical directory loads");
	const fs::path firstBundle = temp.path / "first.egscene";
	const fs::path secondBundle = temp.path / "second.egscene";
	const SceneSaveResult firstSave = saveSceneBundle(source.scene, firstBundle.string());
	ok &= expect(firstSave.success(), "canonical directory packs");
	const SceneSaveResult secondSave = saveSceneBundle(source.scene, secondBundle.string());
	ok &= expect(secondSave.success(), "canonical directory packs twice");
	ok &= expect(readBytes(firstBundle) == readBytes(secondBundle), "bundle bytes are deterministic");
	const SceneSaveResult replacementSave = saveSceneBundle(source.scene, firstBundle.string());
	ok &= expect(replacementSave.success() && readBytes(firstBundle) == readBytes(secondBundle),
			"existing bundle is atomically replaced");
	const std::string originalBundleBytes = readBytes(firstBundle);
	SceneModel editedScene = source.scene;
	editedScene.description += " (edited)";
	const fs::path saveAsBundle = temp.path / "save-as.egscene";
	const SceneSaveResult saveAsResult = saveSceneBundle(editedScene, saveAsBundle.string());
	ok &= expect(saveAsResult.success()
			&& readBytes(firstBundle) == originalBundleBytes
			&& readBytes(saveAsBundle) != originalBundleBytes,
			"Save As writes a second bundle without changing the original bytes");

	const SceneLoadResult bundled = loadScenePath(firstBundle.string());
	ok &= expect(!hasErrors(bundled.diagnostics), "bundle loads through shared path dispatch");
	ok &= expect(sameCanonicalFiles(source.scene, bundled.scene, temp.path / "model-compare"),
			"bundle model matches directory model");

	const fs::path unpacked = temp.path / "unpacked";
	const SceneSaveResult unpackResult = unpackSceneBundle(firstBundle.string(), unpacked.string());
	ok &= expect(unpackResult.success(), "bundle unpacks atomically");
	for (const char* name : {"scene.json", "infrastructure.json", "stations.json", "signalling.json",
			"rolling_stock.json", "services.json", "scenarios.json"})
		ok &= expect(fs::is_regular_file(unpacked / name), "unpacked canonical entry exists");
	ok &= expect(!fs::exists(unpacked / "views.json") && !fs::exists(unpacked / "legacy"),
			"bundle excludes views and legacy outputs");
	const SceneLoadResult unpackedLoad = loadScene(unpacked.string());
	ok &= expect(!hasErrors(unpackedLoad.diagnostics)
			&& sameCanonicalFiles(source.scene, unpackedLoad.scene, temp.path / "unpacked-compare"),
			"unpacked scene reloads");
	const fs::path existingDestination = temp.path / "existing-destination";
	fs::create_directory(existingDestination);
	ok &= expect(writeBytes(existingDestination / "keep.txt", "keep"), "existing destination fixture writes");
	const SceneSaveResult existingResult = unpackSceneBundle(firstBundle.string(), existingDestination.string());
	ok &= expect(!existingResult.success() && hasCode(existingResult.diagnostics, "scene.bundle.publish")
			&& readBytes(existingDestination / "keep.txt") == "keep",
			"unpack refuses and preserves an existing destination");

	const struct MalformedCase {
		const char* name;
		const char* diagnosticCode;
		std::string bytes;
	} malformed[] = {
		{"traversal", "scene.bundle.path", rawArchive({{"../../outside", "x"}})},
		{"absolute", "scene.bundle.path", rawArchive({{"/outside", "x"}})},
		{"symlink", "scene.bundle.entry", rawArchive({{"scene.json", "x", 0, 0120000U << 16, 0x0314}})},
		{"macOS symlink", "scene.bundle.entry", rawArchive({{"scene.json", "x", 0, 0120000U << 16, 0x1314}})},
		{"case-insensitive duplicate", "scene.bundle.duplicate", rawArchive({{"scene.json", "x"}, {"SCENE.JSON", "x"}})},
		{"entry limit", "scene.bundle.entries", rawArchive(std::vector<RawEntry>(17, {"x", "x", 0, 0, 20}))},
		{"entry size limit", "scene.bundle.entry.size", rawArchive({{"scene.json", "x", 16U * 1024U * 1024U + 1U, 0, 20, 8}})},
		{"ratio limit", "scene.bundle.ratio", rawArchive({{"scene.json", "x", 1001U, 0, 20, 8}})},
		{"unknown entry", "scene.bundle.entry", rawArchive({{"views.json", "x"}})},
		{"missing required entry", "scene.bundle.required", rawArchive({{"scene.json", "x"}})},
	};
	for (const auto& test : malformed) {
		const fs::path malformedPath = temp.path / (std::string(test.name) + ".egscene");
		ok &= expect(writeBytes(malformedPath, test.bytes), "malformed archive fixture writes");
		const SceneLoadResult rejected = loadSceneBundle(malformedPath.string());
		ok &= expect(hasErrors(rejected.diagnostics) && hasCode(rejected.diagnostics, test.diagnosticCode), test.name);
	}
	std::string truncated = readBytes(firstBundle);
	truncated.resize(truncated.size() - 4);
	const fs::path truncatedPath = temp.path / "truncated.egscene";
	ok &= expect(writeBytes(truncatedPath, truncated), "truncated archive fixture writes");
	const SceneLoadResult truncatedResult = loadSceneBundle(truncatedPath.string());
	ok &= expect(hasErrors(truncatedResult.diagnostics) && hasCode(truncatedResult.diagnostics, "scene.bundle.archive"),
			"truncated archive");

	if (!ok)
		return 1;
	std::cout << "all SceneBundle tests passed\n";
	return 0;
}
