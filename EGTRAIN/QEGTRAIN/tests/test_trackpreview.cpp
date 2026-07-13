#include "scene/TrackPreview.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

struct TempDir {
	fs::path path;

	TempDir() {
		path = fs::temp_directory_path()
			   / ("track_preview_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		fs::create_directories(path);
	}

	~TempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

static void writeFile(const fs::path& path, const std::string& text) {
	fs::create_directories(path.parent_path());
	std::ofstream(path) << text;
}

int main() {
	bool ok = true;

	{
		TempDir scene;
		const fs::path tracks = scene.path / "legacy" / "TrackLines";
		writeFile(tracks / "B10" / "NodiCumPari.txt", "1 10 0\n2 20 1\n");
		writeFile(tracks / "B2" / "NodiCumPari.txt", "bad row\n1 0 0\n2 5 0\n");
		writeFile(tracks / "B3" / "NodiCumPari.txt", "1 3 0\n");
		writeFile(tracks / "Bbad" / "NodiCumPari.txt", "1 0 0\n2 1 0\n");
		writeFile(tracks / "Connections.txt", "2 4.5 10 10.5\ninvalid\n");

		const auto result = loadTrackPreview(scene.path.string());
		ok &= expect(result.lines.size() == 2, "keeps only tracks with at least two valid points");
		ok &= expect(result.lines[0].id == 2 && result.lines[1].id == 10, "sorts B directories numerically");
		ok &= expect(result.lines[0].points.size() == 2, "skips malformed node rows");
		ok &= expect(result.connections.size() == 1, "parses valid connections and skips malformed rows");
		ok &= expect(result.connections[0].firstTrackId == 2 && result.connections[0].secondTrackId == 10,
					 "preserves connection track ids");
		ok &= expect(!result.warnings.empty(), "reports non-blocking diagnostics for malformed input");
	}

	{
		TempDir scene;
		const fs::path tracks = scene.path / "legacy" / "Tracklines";
		writeFile(tracks / "B0" / "NodiCumPari.txt", "1 0 0\n2 1 0\n");
		ok &= expect(loadTrackPreview(scene.path.string()).lines.size() == 1,
					 "accepts legacy Tracklines spelling");
	}

	{
		TempDir scene;
		const auto result = loadTrackPreview(scene.path.string());
		ok &= expect(result.lines.empty(), "missing preview directory returns no lines");
		ok &= expect(!result.warnings.empty(), "missing preview directory reports a warning");
	}

	return ok ? 0 : 1;
}
