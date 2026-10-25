// The geo X of the routes of the committed scenes in EGTRAIN_SCENES_DIR, as the blocking-time diagrams read it: every
// section has finite geo X at both ends, a route is reversed exactly when it runs against the geo X coordinate, its geo X
// keeps one direction, and the jumps of the position scale are those of the scenes. Only the infrastructure is built;
// nothing runs. Each scene prints its counts.

#include "scene/SceneModel.h"
#include "simulation/Signalling.h"
#include "simulation/SimulationRun.h"
#include "diagrams/CompletedRunData.h"
#include "diagrams/PositionScale.h"

// The Qt headers come before the undef: Qt defines `signals`, and the scene model has a member of that name.
#include <QTest>
#ifdef signals
#undef signals
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

class TestRouteGeo : public QObject {
	Q_OBJECT

private slots:
	void findsCommittedScenes();
	void routesFollowGeoX_data();
	void routesFollowGeoX();
	void netherlandsRoute0HasOneJump();
	void paimpolRoute1RunsAgainstGeoX();
};

namespace {

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

// The folders of the committed scenes, sorted.
std::vector<fs::path> committedScenes() {
	std::vector<fs::path> scenes;
	for (const fs::directory_entry& entry : fs::directory_iterator(fs::path(EGTRAIN_SCENES_DIR)))
		if (fs::exists(entry.path() / "scene.json"))
			scenes.push_back(entry.path());
	std::sort(scenes.begin(), scenes.end());
	return scenes;
}

// Loads the scene of a folder and builds its infrastructure into the run. A failed check ends this function only, so the
// caller checks QTest::currentTestFailed() before it goes on.
void buildScene(const fs::path& dir, SimulationRun& run) {
	const std::string name = dir.filename().string();
	const SceneLoadResult loaded = loadScene(dir.string());
	QVERIFY2(!hasErrors(loaded.diagnostics), (name + ": the scene loads").c_str());
	bool built = false;
	{
		const CoutSilencer silencer;
		built = !hasErrors(buildInfrastructureAndSignallingFromScene(run, loaded.scene));
	}
	QVERIFY2(built, (name + ": the infrastructure builds").c_str());
}

// The route of a run with this ID, or null.
const Route* findRoute(const SimulationRun& run, const std::string& id) {
	for (const Route& route : run.infra.routes)
		if (route.ID == id)
			return &route;
	return nullptr;
}

// Whether a route runs against the geo X coordinate: its first stretch starts at a larger geo X than its last one ends.
bool runsAgainstGeoX(const PositionScale& scale) {
	const auto& stretches = scale.stretches();
	return !stretches.empty() && stretches.front().geoStartKm > stretches.back().geoEndKm;
}

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

} // namespace

void TestRouteGeo::findsCommittedScenes() {
	QVERIFY2(committedScenes().size() >= 6, "the committed scenes are found");
}

void TestRouteGeo::routesFollowGeoX_data() {
	QTest::addColumn<QString>("dir");
	for (const fs::path& scene : committedScenes())
		QTest::newRow(scene.filename().string().c_str()) << QString::fromStdString(scene.string());
}

void TestRouteGeo::routesFollowGeoX() {
	QFETCH(QString, dir);
	const fs::path path = dir.toStdString();
	const std::string name = path.filename().string();
	auto run = std::make_unique<SimulationRun>();
	buildScene(path, *run);
	if (QTest::currentTestFailed())
		return;
	int reversed = 0, against = 0, jumps = 0, sections = 0;
	for (const Route& route : run->infra.routes) {
		const std::vector<BlockSectionInput> inputs = blockSectionInputsForRoute(route);
		QVERIFY2(!inputs.empty(), (name + " " + route.ID + ": the route has sections").c_str());
		for (const BlockSectionInput& input : inputs) {
			const std::string section = name + " " + route.ID + " " + input.id;
			QVERIFY2(std::isfinite(input.geoStartKm), (section + ": finite geo X at both ends").c_str());
			QVERIFY2(std::isfinite(input.geoEndKm), (section + ": finite geo X at both ends").c_str());
			QVERIFY2(input.geoStartKm != 0.0 || input.geoEndKm != 0.0, (section + ": geo X is set").c_str());
		}
		sections += static_cast<int>(inputs.size());
		const std::vector<BlockSectionExtent> extents = buildBlockSectionExtents(inputs);
		const PositionScale scale(extents);
		const bool runsAgainst = runsAgainstGeoX(scale);
		QVERIFY2(route.reversed_direction == runsAgainst,
			(name + " " + route.ID + ": reversed exactly when it runs against the geo X coordinate").c_str());
		QVERIFY2(!turnsBack(extents), (name + " " + route.ID + ": the geo X keeps its direction").c_str());
		reversed += route.reversed_direction;
		against += runsAgainst;
		jumps += static_cast<int>(scale.jumps().size());
	}
	std::cout << name << ": " << run->infra.routes.size() << " routes, " << sections << " sections, " << reversed
			  << " reversed, " << against << " against the geo X coordinate, " << jumps << " jumps\n";
}

void TestRouteGeo::netherlandsRoute0HasOneJump() {
	auto run = std::make_unique<SimulationRun>();
	buildScene(fs::path(EGTRAIN_SCENES_DIR) / "Netherlands", *run);
	if (QTest::currentTestFailed())
		return;
	const Route* route = findRoute(*run, "route0");
	if (route == nullptr)
		QFAIL("Netherlands route0 is checked");
	const PositionScale scale(buildBlockSectionExtents(blockSectionInputsForRoute(*route)));
	const auto& found = scale.jumps();
	// Netherlands route0 has one jump, 18.298 km continues at 30.569 km.
	QCOMPARE(found.size(), std::size_t(1));
	QVERIFY(std::abs(found.front().geoBeforeKm - 18.298) < 5e-4);
	QVERIFY(std::abs(found.front().geoAfterKm - 30.569) < 5e-4);
}

void TestRouteGeo::paimpolRoute1RunsAgainstGeoX() {
	auto run = std::make_unique<SimulationRun>();
	buildScene(fs::path(EGTRAIN_SCENES_DIR) / "Paimpol", *run);
	if (QTest::currentTestFailed())
		return;
	const Route* route = findRoute(*run, "route1");
	if (route == nullptr)
		QFAIL("Paimpol route1 is checked");
	const PositionScale scale(buildBlockSectionExtents(blockSectionInputsForRoute(*route)));
	QVERIFY2(runsAgainstGeoX(scale), "Paimpol route1 runs against the geo X coordinate");
}

QTEST_APPLESS_MAIN(TestRouteGeo)
#include "test_routegeo.moc"
