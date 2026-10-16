// SceneModel.h has a member named signals, so it comes before the Qt headers.
#include "scene/SceneModel.h"

#include "graphics/NetworkPresenter.h"

#include "app/GuiSimulationSnapshot.h"
#include "graphics/RuntimeGeometrySource.h"
#include "graphics/items/PlatformItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"
#include "simulation/Signalling.h"

#include <QApplication>
#include <QGraphicsScene>
#include <QLoggingCategory>

#include <cmath>
#include <iostream>
#include <vector>

Logger owl;

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

// One track line from 0 to 10 km with a station at each end, a platform at the station B, two block sections and a route
// over both. The numbers of the simulation globals stay 0, so the presenter can only draw what this source gives it.
class FakeGeometry final : public RuntimeGeometrySource {
public:
	FakeGeometry() : m_tracks(1), m_stations(2), m_sections(2) {
		BlockSet& track = m_tracks[0];
		track.len = 2;
		track.numNodes = 3;
		track.region = 0;
		track.hasGraphLayout = false;
		track.member.resize(2);
		track.member[0].startNode.X = 0.0;
		track.member[0].endNode.X = 5.0;
		track.member[1].startNode.X = 5.0;
		track.member[1].endNode.X = 10.0;
		track.member[1].endNode.stationName = "B";
		track.member[1].endNode.stationPlatformId = "P1";

		m_stations[0].stationName = "A";
		m_stations[0].X = 0.0;
		m_stations[0].Y = 0.0;
		m_stations[1].stationName = "B";
		m_stations[1].X = 10.0;
		m_stations[1].Y = 0.0;

		m_sections[0].ID = "s0";
		m_sections[0].trackLineId = 0;
		m_sections[0].start_node.X = 0.0;
		m_sections[0].end_node.X = 5.0;
		m_sections[0].length = 5000.0;
		m_sections[1].ID = "s1";
		m_sections[1].trackLineId = 0;
		m_sections[1].start_node.X = 5.0;
		m_sections[1].end_node.X = 10.0;
		m_sections[1].length = 5000.0;
		m_sections[0].total_arcs = 0;
		m_sections[1].total_arcs = 0;

		m_route.N_Block_Sections = 2;
		m_route.sequence_of_block_sections = m_sections;

		m_platform.ID = "P1";
		m_platform.StationID = "B";
		m_platform.Max_Passenger_Volume = 40;
		m_platform.Current_N_Passengers = 7;
	}

	bool passengers = false;

	int trackCount() const override { return static_cast<int>(m_tracks.size()); }
	BlockSet& track(int index) const override { return m_tracks[static_cast<std::size_t>(index)]; }
	int stationCount() const override { return static_cast<int>(m_stations.size()); }
	Stations& station(int index) const override { return m_stations[static_cast<std::size_t>(index)]; }
	int connectionCount() const override { return 0; }
	Connections& connection(int) const override { return m_noConnection; }
	int sectionCount() const override { return static_cast<int>(m_sections.size()); }
	const Section& section(int index) const override { return m_sections[static_cast<std::size_t>(index)]; }
	const Route* route(int index) const override { return index == 0 ? &m_route : nullptr; }
	const StationPlatform* platform(const std::string& stationId, const std::string& platformId) const override {
		return stationId == m_platform.StationID && platformId == m_platform.ID ? &m_platform : nullptr;
	}
	bool passengerGui() const override { return passengers; }

private:
	mutable std::vector<BlockSet> m_tracks;
	mutable std::vector<Stations> m_stations;
	mutable Connections m_noConnection;
	std::vector<Section> m_sections;
	Route m_route;
	StationPlatform m_platform;
};

// A train of one wagon that is 100 m long, with its head at 7.5 km and its tail at 7.4 km.
static GuiTrainState trainAt(double headKm) {
	GuiTrainState train;
	train.index = 0;
	train.id = 1.0;
	train.description = "T1";
	train.routeIndex = 0;
	train.wagonCount = 0;
	train.length = 100.0;
	train.routeAxisPosition = headKm * 1000.0;
	train.wagonHeadPositions = {headKm};
	train.wagonTailPositions = {headKm - 0.1};
	return train;
}

static bool near(double left, double right) {
	return std::fabs(left - right) < 1e-6;
}

// The body of the train is the rectangle of 100 m along the track and twice 0.1 of the track separation across it.
static bool bodyAt(const NetworkPresenter& presenter, double headKm) {
	if (presenter.allTrains.size() != 1 || !presenter.allTrains[0]->trainPolygonItemList)
		return false;
	const QPolygonF polygon = presenter.allTrains[0]->trainPolygonItemList->at(0)->polygon();
	const double head = headKm * 1000.0;
	const double tail = head - 100.0;
	const double half = 0.10 * presenter.track_separation;
	return polygon.size() == 4 && near(polygon[0].x(), head) && near(polygon[0].y(), half) && near(polygon[1].x(), tail)
		&& near(polygon[1].y(), half) && near(polygon[2].x(), tail) && near(polygon[2].y(), -half)
		&& near(polygon[3].x(), head) && near(polygon[3].y(), -half);
}

int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	// The station icons are resources of the application, which this test does not have.
	QLoggingCategory::setFilterRules(QStringLiteral("qt.svg.warning=false"));
	bool ok = true;

	const SceneModel model;
	QGraphicsScene scene;
	NetworkPresenter presenter(&scene, nullptr);

	// Without a source nothing of a run is drawn.
	presenter.showRuntime(model);
	ok &= expect(scene.items().isEmpty(), "a presenter without a source draws nothing");

	auto fake = std::make_unique<FakeGeometry>();
	FakeGeometry* const source = fake.get();
	presenter.setGeometrySource(std::move(fake));

	int drawnItems = 0;
	for (int cycle = 0; cycle < 3; ++cycle) {
		source->passengers = cycle > 0;
		presenter.showRuntime(model);
		ok &= expect(presenter.allArcs.size() == 2 && presenter.m_stationOverlays.size() == 2,
			"the run draws both arcs and both stations of the source");
		ok &= expect(presenter.allSignals.size() == 2 && presenter.m_signalDecorations.size() == 6,
			"the run draws one signal with a head for each direction at the border of the block sections");
		ok &= expect(presenter.m_signalsByAheadId.size() == 2 && presenter.m_signalsByAheadId.count("s0") == 1
				&& presenter.m_signalsByAheadId.count("s1") == 1,
			"the signals are indexed by the section ahead of them");
		ok &= expect(presenter.allPlatforms.size() == (cycle > 0 ? 1 : 0), "the platform shows only with the passengers");
		if (cycle > 0) {
			const PlatformItem* platform = presenter.allPlatforms.value(0);
			ok &= expect(platform && platform->stationId == "B" && platform->platformId == "P1" && platform->maxVolume == 40,
				"the platform takes its details from the source");
		}

		presenter.paintTrain(trainAt(7.5), model, false);
		ok &= expect(presenter.allTrains.size() == 1 && presenter.m_trainBadges.contains(0), "the train and its badge are added");
		ok &= expect(bodyAt(presenter, 7.5), "the body of the train lies on the track line of the source");
		ok &= expect(presenter.getTrainPolygonItemList(presenter.allTrains[0]->trainPolygonItemList, trainAt(8.0))
				&& bodyAt(presenter, 8.0),
			"the body follows the train");
		QPolygonF none;
		GuiTrainState lost = trainAt(7.5);
		lost.routeIndex = 5;
		presenter.getTrainPolygon(&none, 0, lost);
		ok &= expect(none.isEmpty(), "a train on a route that the source does not have has no shape");

		if (cycle == 0)
			drawnItems = scene.items().size();
		else
			ok &= expect(scene.items().size() > drawnItems, "the platform adds items");
		presenter.clear();
		ok &= expect(scene.items().isEmpty() && presenter.allArcs.isEmpty() && presenter.allSignals.isEmpty()
				&& presenter.m_signalsByAheadId.empty() && presenter.allTrains.isEmpty() && presenter.m_trainBadges.isEmpty()
				&& presenter.allPlatforms.isEmpty() && presenter.m_stationOverlays.isEmpty(),
			"clear leaves nothing of the run");
	}

	if (!ok)
		return 1;

	std::cout << "all runtime canvas tests passed\n";
	return 0;
}
