// The geo X of the routes of the committed scenes, as the blocking-time diagrams read it: every section has finite geo X
// at both ends, a route is reversed exactly when it runs against the geo X coordinate, its geo X keeps one direction,
// and the jumps of the position scale are those of the scenes. Only the infrastructure is built; nothing
// runs. Each scene prints its counts.
//
//   test_routegeo SCENES_DIR

#include "scene/SceneModel.h"
#include "simulation/Signalling.h"
#include "simulation/SimulationRun.h"
#include "diagrams/CompletedRunData.h"
#include "diagrams/PositionScale.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;
bool netherlandsChecked = false;
bool paimpolChecked = false;

void check(bool ok, const std::string& what) {
	if (ok)
		return;
	++failures;
	std::cerr << "failed: " << what << "\n";
}

// Keeps what the infrastructure builder prints out of the counts.
class CoutSilencer {
public:
	CoutSilencer() : m_saved(std::cout.rdbuf(m_sink.rdbuf())) {}
	~CoutSilencer() { std::cout.rdbuf(m_saved); }
	CoutSilencer(const CoutSilencer&) = delete;
	CoutSilencer& operator=(const CoutSilencer&) = delete;

private:
	std::ostringstream m_sink;
	std::streambuf* m_saved;
};

// Whether the geo X turns back between consecutive sections of a stretch.
bool turnsBack(const std::vector<BlockSectionExtent>& extents) {
	double direction = 0.0;
	for (const BlockSectionExtent& extent : extents) {
		const double step = extent.geoEndKm - extent.geoStartKm;
		if (!extent.geometryValid || std::abs(step) < 1e-9)
			continue;
		if (direction != 0.0 && (step > 0.0) != (direction > 0.0))
			return true;
		direction = step;
	}
	return false;
}

void checkScene(const fs::path& dir) {
	const std::string name = dir.filename().string();
	const SceneLoadResult loaded = loadScene(dir.string());
	check(!hasErrors(loaded.diagnostics), name + ": the scene loads");
	auto run = std::make_unique<SimulationRun>();
	bool built = false;
	{
		const CoutSilencer silencer;
		built = !hasErrors(buildInfrastructureAndSignallingFromScene(*run, loaded.scene));
	}
	check(built, name + ": the infrastructure builds");
	int reversed = 0, against = 0, jumps = 0, sections = 0;
	for (const Route& route : run->infra.routes) {
		const std::vector<BlockSectionInput> inputs = blockSectionInputsForRoute(route);
		check(!inputs.empty(), name + " " + route.ID + ": the route has sections");
		for (const BlockSectionInput& input : inputs) {
			check(std::isfinite(input.geoStartKm) && std::isfinite(input.geoEndKm),
				name + " " + route.ID + " " + input.id + ": finite geo X at both ends");
			check(input.geoStartKm != 0.0 || input.geoEndKm != 0.0, name + " " + route.ID + " " + input.id + ": geo X is set");
		}
		sections += static_cast<int>(inputs.size());
		const std::vector<BlockSectionExtent> extents = buildBlockSectionExtents(inputs);
		const PositionScale scale(extents);
		const auto& stretches = scale.stretches();
		const bool runsAgainst = !stretches.empty() && stretches.front().geoStartKm > stretches.back().geoEndKm;
		check(route.reversed_direction == runsAgainst, name + " " + route.ID + ": reversed exactly when it runs against the geo X coordinate");
		check(!turnsBack(extents), name + " " + route.ID + ": the geo X keeps its direction");
		reversed += route.reversed_direction;
		against += runsAgainst;
		jumps += static_cast<int>(scale.jumps().size());
		if (name == "Netherlands" && route.ID == "route0") {
			const auto& found = scale.jumps();
			check(found.size() == 1 && std::abs(found.front().geoBeforeKm - 18.298) < 5e-4
					&& std::abs(found.front().geoAfterKm - 30.569) < 5e-4,
				"Netherlands route0 has one jump, 18.298 km continues at 30.569 km");
			netherlandsChecked = true;
		}
		if (name == "Paimpol" && route.ID == "route1") {
			check(runsAgainst, "Paimpol route1 runs against the geo X coordinate");
			paimpolChecked = true;
		}
	}
	std::cout << name << ": " << run->infra.routes.size() << " routes, " << sections << " sections, " << reversed
			  << " reversed, " << against << " against the geo X coordinate, " << jumps << " jumps\n";
}

} // namespace

int main(int argc, char** argv) {
	if (argc != 2) {
		std::cerr << "usage: test_routegeo SCENES_DIR\n";
		return 2;
	}
	std::vector<fs::path> scenes;
	for (const fs::directory_entry& entry : fs::directory_iterator(argv[1]))
		if (fs::exists(entry.path() / "scene.json"))
			scenes.push_back(entry.path());
	std::sort(scenes.begin(), scenes.end());
	check(scenes.size() >= 6, "the committed scenes are found");
	for (const fs::path& scene : scenes)
		checkScene(scene);
	check(netherlandsChecked, "Netherlands route0 is checked");
	check(paimpolChecked, "Paimpol route1 is checked");
	if (failures)
		std::cerr << failures << " checks failed\n";
	return failures ? 1 : 0;
}
