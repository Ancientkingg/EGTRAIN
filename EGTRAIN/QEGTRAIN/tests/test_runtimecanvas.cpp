// SceneModel.h has a member named signals, so it comes before the Qt headers.
#include "scene/SceneModel.h"

#include "graphics/NetworkPresenter.h"

#include "app/GuiSimulationSnapshot.h"
#include "graphics/RuntimeGeometrySource.h"
#include "graphics/VisualPolish.h"
#include "graphics/items/HighlightEffect.h"
#include "graphics/items/NodeItem.h"
#include "graphics/items/PassengerItem.h"
#include "graphics/items/PlatformItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/TrainBadgeItem.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"
#include "simulation/Signalling.h"

#include <QApplication>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QLineF>
#include <QLoggingCategory>
#include <QMap>
#include <QObject>
#include <QPixmap>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

// One track line from 0 to 10 km with a station at each end, a platform at the station B, two block sections and a route
// over both. The counts of the run of the simulation stay 0, so the presenter can only draw what this source gives it. A test
// can give the middle of the line a second platform of the station B by naming it on the node there.
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
		m_platformTwo.ID = "P2";
		m_platformTwo.StationID = "B";
		m_platformTwo.Max_Passenger_Volume = 30;
	}

	bool passengers = false;

	int trackCount() const override { return static_cast<int>(m_tracks.size()); }
	BlockSet& track(int index) const override { return m_tracks[static_cast<std::size_t>(index)]; }
	int regionOfTrack(int index) const override { return m_tracks[static_cast<std::size_t>(index)].region; }
	int stationCount() const override { return static_cast<int>(m_stations.size()); }
	Stations& station(int index) const override { return m_stations[static_cast<std::size_t>(index)]; }
	int connectionCount() const override { return 0; }
	Connections& connection(int) const override { return m_noConnection; }
	int sectionCount() const override { return static_cast<int>(m_sections.size()); }
	const Section& section(int index) const override { return m_sections[static_cast<std::size_t>(index)]; }
	const Route* route(int index) const override { return index == 0 ? &m_route : nullptr; }
	const StationPlatform* platform(const std::string& stationId, const std::string& platformId) const override {
		for (const StationPlatform* candidate : {&m_platform, &m_platformTwo})
			if (stationId == candidate->StationID && platformId == candidate->ID)
				return candidate;
		return nullptr;
	}
	bool passengerGui() const override { return passengers; }

private:
	mutable std::vector<BlockSet> m_tracks;
	mutable std::vector<Stations> m_stations;
	mutable Connections m_noConnection;
	std::vector<Section> m_sections;
	Route m_route;
	StationPlatform m_platform;
	StationPlatform m_platformTwo;
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

// Records what apply() asks of its host, in order.
class RecordingHost final : public NetworkPresenter::FrameHost {
public:
	std::vector<std::string> calls;
	bool promoted = false;
	bool cameraMoves = false;

	bool isTrainOverlayPromoted(int) const override { return promoted; }
	void trainWillUpdate(int train) override { calls.push_back("willUpdate " + std::to_string(train)); }
	bool trainPlaced(int train, const QPointF&) override {
		calls.push_back("placed " + std::to_string(train));
		return cameraMoves;
	}
	void trainHidden(int train) override { calls.push_back("hidden " + std::to_string(train)); }
	bool trainAdded(int train, bool firstLiveTrain) override {
		calls.push_back("added " + std::to_string(train) + (firstLiveTrain ? " first" : ""));
		return cameraMoves;
	}
	void signalsApplied() override { calls.push_back("signals"); }
	void trainsApplied(bool anyAdded, bool cameraMoved) override {
		calls.push_back(std::string("trains") + (anyAdded ? " added" : "") + (cameraMoved ? " moved" : ""));
	}

	// The calls of one apply(), which are then forgotten.
	std::vector<std::string> take() {
		std::vector<std::string> taken;
		taken.swap(calls);
		return taken;
	}
};

using Calls = std::vector<std::string>;

// Counts how often the scene announces a change, which it does when an item asks to be painted again.
class SceneChanges {
public:
	explicit SceneChanges(QGraphicsScene& scene) {
		QObject::connect(&scene, &QGraphicsScene::changed, &m_context, [this]() { ++m_count; });
	}

	// The announcements since the last call, after the scene has delivered the changes that were waiting.
	int take() {
		for (int round = 0; round < 5; ++round)
			QApplication::processEvents();
		const int count = m_count;
		m_count = 0;
		return count;
	}

private:
	QObject m_context;
	int m_count = 0;
};

// The data role that marks the items that the scene holds at some moment. An item without the mark was added later.
constexpr int kSeen = 99;

static int markItems(QGraphicsScene& scene) {
	const QList<QGraphicsItem*> items = scene.items();
	for (QGraphicsItem* item : items)
		item->setData(kSeen, true);
	return items.size();
}

struct ItemChanges {
	int added = 0;
	int removed = 0;
};

// The items that the scene gained and lost since markItems returned marked.
static ItemChanges itemChanges(QGraphicsScene& scene, int marked) {
	ItemChanges changes;
	for (const QGraphicsItem* item : scene.items())
		if (!item->data(kSeen).isValid())
			++changes.added;
	changes.removed = marked - (scene.items().size() - changes.added);
	return changes;
}

// The passenger of each icon in the order in which the scene paints the icons, the last one on top.
static std::vector<std::string> paintedIcons(QGraphicsScene& scene) {
	std::vector<std::string> ids;
	for (QGraphicsItem* item : scene.items(Qt::AscendingOrder))
		if (const auto* icon = qgraphicsitem_cast<PassengerItem*>(item))
			ids.push_back(icon->passengerId);
	return ids;
}

// The passenger of each icon in the order of the platforms of the presenter and then of the passengers, which is the
// order in which a rebuild of all the icons would add them.
static std::vector<std::string> expectedIcons(const NetworkPresenter& presenter) {
	std::vector<std::string> ids;
	for (const PlatformItem* platform : presenter.allPlatforms)
		for (const PassengerItem* icon : platform->passengerIcons)
			ids.push_back(icon->passengerId);
	return ids;
}

static std::vector<std::string> iconIds(const PlatformItem& platform) {
	std::vector<std::string> ids;
	for (const PassengerItem* icon : platform.passengerIcons)
		ids.push_back(icon->passengerId);
	return ids;
}

// Where the icon at place i of n sits on the platform: the passengers share the length of the platform, centred on its
// long axis.
static bool iconAt(const PlatformItem& platform, int i, int n, qreal iconSize) {
	const QRectF rect = platform.rect();
	const qreal spacing = rect.width() / n;
	const QPointF centre = platform.mapToScene(QPointF(rect.left() + spacing / 2 + spacing * i, rect.center().y()));
	const QPointF at = platform.passengerIcons[i]->pos();
	return near(at.x(), centre.x() - iconSize / 2) && near(at.y(), centre.y() - iconSize / 2);
}

// Whether the platform is parallel, within half a degree, to direction, a unit vector that points to the right, with its
// centre 1.15 times size from the node on the upper side of the track.
static bool platformAlong(const PlatformItem& platform, const Node& node, const QPointF& direction, qreal size) {
	const QRectF rect = platform.rect();
	const QLineF axis(platform.mapToScene(QPointF(rect.left(), rect.center().y())),
		platform.mapToScene(QPointF(rect.right(), rect.center().y())));
	const QPointF centre = platform.mapToScene(rect.center());
	const QPointF expected = QPointF(node.graphX, node.graphY) + QPointF(direction.y(), -direction.x()) * 1.15 * size;
	return std::fabs(axis.dx() * direction.y() - axis.dy() * direction.x()) < axis.length() * std::sin(qDegreesToRadians(0.5))
		&& near(centre.x(), expected.x()) && near(centre.y(), expected.y());
}

using Ids = std::vector<std::string>;

// Whether the presenter holds no item and points to none, the way a new presenter does.
static bool presenterEmpty(const NetworkPresenter& presenter) {
	return presenter.allTrains.isEmpty() && presenter.m_trainBadges.isEmpty() && presenter.m_prevTrainPositions.isEmpty()
		&& presenter.m_trainAnimations.isEmpty() && presenter.allSignals.isEmpty() && presenter.m_signalsByAheadId.empty()
		&& presenter.m_signalDecorations.isEmpty() && presenter.allArcs.isEmpty() && presenter.m_tracksBySectionId.empty()
		&& presenter.m_tracksByOccupiedArc.empty() && presenter.m_activeTrackItems.empty() && presenter.allPlatforms.isEmpty()
		&& presenter.m_stationOverlays.isEmpty() && presenter.m_stationDecorations.isEmpty() && presenter.m_stationLabels.isEmpty()
		&& presenter.m_stationPictures.isEmpty() && presenter.m_vcMessageItems.isEmpty() && !presenter.m_inspectedSignal
		&& !presenter.trainPaxItem && !presenter.trainPaxInfoItem && !presenter.paxIconItem && !presenter.paxIconInfoItem
		&& !presenter.effect;
}

int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	// The entity icons are resources of the application, which this test does not have.
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
		ok &= expect(presenter.allPlatforms.size() == 1, "the run draws the platform with or without passengers");
		PlatformItem* const platform = presenter.allPlatforms.value(0);
		// The counter and the details of the platform come only with the passengers.
		ok &= expect(platform
				&& (source->passengers
						? platform->stationId == "B" && platform->platformId == "P1" && platform->maxVolume == 40 && platform->textIcon
						: platform->stationId.empty() && platform->maxVolume == 0 && !platform->textIcon),
			"the platform takes its details from the source and has a counter only in a run with passengers");
		ok &= expect(platform && platform->rotation() == 0
				&& platformAlong(*platform, source->track(0).member[1].endNode, QPointF(1, 0),
					presenter.station_node_size * presenter.presentationScale()),
			"the platform of a level track is level, above its node");
		// The bar shows with the stations in the near look; its counter waits for the zoom.
		const auto counterShown = [platform]() { return platform && platform->textIcon && platform->textIcon->isVisible(); };
		ok &= expect(platform && !platform->isVisible() && !counterShown(), "the platform and its counter are hidden in the far look");
		presenter.setStationNearLook(true);
		ok &= expect(platform && platform->isVisible() && !counterShown(), "the platform shows in the near look");
		presenter.m_stationLayerVisible = false;
		presenter.setStationNearLook(true);
		ok &= expect(platform && !platform->isVisible(), "the platform hides with the stations");
		presenter.m_stationLayerVisible = true;

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
			ok &= expect(scene.items().size() == drawnItems + 1, "the passengers add only the counter of the platform");
		presenter.clear();
		ok &= expect(scene.items().isEmpty() && presenter.allArcs.isEmpty() && presenter.allSignals.isEmpty()
				&& presenter.m_signalsByAheadId.empty() && presenter.allTrains.isEmpty() && presenter.m_trainBadges.isEmpty()
				&& presenter.allPlatforms.isEmpty() && presenter.m_stationOverlays.isEmpty(),
			"clear leaves nothing of the run");
	}

	// A virtual station gets no platform bar, as it gets no overlay.
	{
		Node& middle = source->track(0).member[0].endNode;
		middle.stationName = "virtualLink";
		middle.stationPlatformId = "P2";
		presenter.showRuntime(model);
		ok &= expect(presenter.allPlatforms.size() == 1 && presenter.allPlatforms.value(0)->node == &source->track(0).member[1].endNode,
			"a virtual station gets no platform bar");
		presenter.clear();
		middle.stationName.clear();
		middle.stationPlatformId.clear();
	}

	// apply() shows the steps of a run on the items that showRuntime drew.
	{
		source->passengers = true;
		presenter.showRuntime(model);
		RecordingHost host;
		presenter.setFrameHost(&host);
		SignalItem* const head = presenter.m_signalsByAheadId["s0"].front();
		TrackLineItem* const track = presenter.m_tracksByOccupiedArc.at({0, 0.0});
		PlatformItem* const platform = presenter.allPlatforms.value(0);
		const auto platformText = [platform]() { return platform->textIcon->toPlainText(); };

		GuiSimulationSnapshot snapshot;
		snapshot.timestep = 10;
		snapshot.trains = {trainAt(7.5)};
		snapshot.trains[0].occupiedArcs = {GuiOccupiedArc{0, 0.0}};
		snapshot.signalStates = {GuiSignalState{"s0", 75, head->reversedDirection, 3, false}};
		snapshot.platforms = {GuiPlatformState{"B", "P1", 40, {"p1", "p2"}}};

		presenter.apply(snapshot, NetworkPresenter::Playback::Live);
		ok &= expect(host.take() == Calls({"signals", "willUpdate 0", "added 0 first", "trains added"}),
			"a live step reports the signals, then each train, then the end of the trains to the host");
		ok &= expect(presenter.allTrains.size() == 1 && bodyAt(presenter, 7.5), "a train of the step that is not on the canvas is added");
		ok &= expect(head->aspectCode() == 75, "the head of a signal shows the state of its section");
		ok &= expect(presenter.allSignals.size() == 2 && presenter.allSignals[0] != presenter.allSignals[1]
				&& (presenter.allSignals[0] == head ? presenter.allSignals[1] : presenter.allSignals[0])->aspectCode() == kGuiSignalUnavailable,
			"a head without a state shows nothing");
		ok &= expect(track->operationalState() == TrackOperationalState::Occupied, "the track of an occupied arc shows as occupied");
		ok &= expect(platform->passengerIcons.size() == 2 && platformText() == "Pax on platform: 2\nMax pax volume: 40",
			"the platform shows its passengers");
		// The passengers on the platform show only where its bar shows, with the passenger layer.
		const auto iconsShown = [platform](bool shown) {
			return std::all_of(platform->passengerIcons.cbegin(), platform->passengerIcons.cend(),
				[shown](const PassengerItem* icon) { return icon->isVisible() == shown; });
		};
		ok &= expect(!platform->isVisible() && iconsShown(false), "the passengers on a hidden platform bar are hidden");
		presenter.setStationNearLook(true);
		ok &= expect(platform->isVisible() && iconsShown(true), "they show with the bar in the near look");
		// The details of a selected passenger go with its icon, also when the next step paints them again.
		PassengerItem* const selected = platform->passengerIcons[0];
		presenter.paintPassengerInfoIcon(selected, passengerStateIn(&snapshot, selected->passengerId), false);
		ok &= expect(presenter.paxIconInfoItem && presenter.paxIconInfoItem->isVisible(), "the details of a selected passenger show");
		presenter.setStationNearLook(false);
		presenter.updatePaxIconInfo(&snapshot, NetworkPresenter::Playback::Live);
		ok &= expect(presenter.paxIconInfoItem && !presenter.paxIconInfoItem->isVisible(),
			"they hide with the icon in the far look");
		presenter.setStationNearLook(true);
		ok &= expect(presenter.paxIconInfoItem->isVisible(), "they show again with the icon");
		presenter.removePaxInfoIcon();
		presenter.paxIconItem = nullptr;
		presenter.m_passengerLayerVisible = false;
		presenter.applyPlatformVisibility();
		ok &= expect(platform->isVisible() && iconsShown(false), "the passenger layer hides them and not the bar");
		presenter.m_passengerLayerVisible = true;
		presenter.m_stationLayerVisible = false;
		presenter.applyPlatformVisibility();
		ok &= expect(!platform->isVisible() && iconsShown(false), "the station layer hides the bar and its passengers");
		presenter.m_stationLayerVisible = true;
		presenter.setStationNearLook(false);

		snapshot.timestep = 11;
		snapshot.trains = {trainAt(8.0)};
		snapshot.signalStates.clear();
		snapshot.platforms[0].passengerIds = {"p1"};
		snapshot.virtualCouplingMessages = {GuiVirtualCouplingState{11, "T1", "message"}};
		host.promoted = true;
		host.cameraMoves = true;
		presenter.apply(snapshot, NetworkPresenter::Playback::Live);
		ok &= expect(host.take() == Calls({"signals", "willUpdate 0", "placed 0", "trains moved"}),
			"a train on the canvas is placed, and the host hears whether the camera moved");
		ok &= expect(bodyAt(presenter, 8.0), "the body follows the train");
		ok &= expect(head->aspectCode() == kGuiSignalUnavailable, "a head keeps no earlier aspect");
		ok &= expect(track->operationalState() == TrackOperationalState::Free, "a track that is no longer occupied is free again");
		ok &= expect(platform->passengerIcons.size() == 1 && platformText() == "Pax on platform: 1\nMax pax volume: 40",
			"the platform shows the passengers that are left");
		ok &= expect(presenter.m_vcMessageItems.contains(0) && presenter.m_vcMessageItems[0]->isVisible(),
			"the message of a virtual coupling shows above the train");
		// It shows by itself, so it stays under the station names, and over the train and its speed.
		ok &= expect(presenter.m_vcMessageItems.contains(0) && presenter.m_trainBadges.value(0)
				&& presenter.m_vcMessageItems[0]->zValue() < StationNameItem::NameZValue
				&& presenter.m_vcMessageItems[0]->zValue() > presenter.m_trainBadges.value(0)->zValue()
				&& presenter.m_trainBadges.value(0)->zValue() > presenter.allTrains[0]->zValue(),
			"the message of a virtual coupling is stacked over the train and its speed and under the station names");
		ok &= expect(presenter.m_trainBadges.value(0) && presenter.m_trainBadges.value(0)->isPromoted(),
			"the host decides whether the badge of a train is promoted");
		ok &= expect(!presenter.m_trainAnimations.isEmpty(), "a live train glides to its new place");

		snapshot.timestep = 12;
		snapshot.trains = {trainAt(8.5)};
		snapshot.virtualCouplingMessages.clear();
		presenter.apply(snapshot, NetworkPresenter::Playback::Replay);
		ok &= expect(host.take() == Calls({"signals", "willUpdate 0", "placed 0", "trains moved"}), "a replay step reports the same calls");
		ok &= expect(presenter.m_trainAnimations.isEmpty() && bodyAt(presenter, 8.5), "a replay step places the train without gliding");
		ok &= expect(presenter.m_vcMessageItems.contains(0) && !presenter.m_vcMessageItems[0]->isVisible(),
			"a message that the step does not carry is hidden");

		snapshot.trains[0].outOfSimulation = true;
		presenter.apply(snapshot, NetworkPresenter::Playback::Live);
		ok &= expect(host.take() == Calls({"signals", "willUpdate 0", "hidden 0", "trains"}), "a train that has left is hidden");
		ok &= expect(!presenter.allTrains[0]->isVisible(), "the item of a train that has left is not shown");

		snapshot.trains = {trainAt(7.5)};
		presenter.setFrameHost(nullptr);
		presenter.apply(snapshot, NetworkPresenter::Playback::Live);
		ok &= expect(host.take().empty() && presenter.allTrains[0]->isVisible() && bodyAt(presenter, 7.5),
			"a presenter without a host still shows the step");

		presenter.clear();
		ok &= expect(presenter.m_trainAnimations.isEmpty() && scene.items().isEmpty(), "clear stops the movement of the trains");
	}

	// A step that changes nothing leaves the canvas as it is, and a step that changes a little touches only what it changes.
	{
		source->passengers = true;
		// a second platform, at the middle of the line
		source->track(0).member[0].endNode.stationName = "B";
		source->track(0).member[0].endNode.stationPlatformId = "P2";
		presenter.showRuntime(model);
		// The platforms and their passengers show in the near look.
		presenter.setStationNearLook(true);
		presenter.pax_pixmap_scaled = QPixmap(10, 10);
		const qreal iconSize = presenter.pax_pixmap_scaled.width() * presenter.presentationScale();
		ok &= expect(presenter.allPlatforms.size() == 2, "the run draws both platforms");

		GuiSimulationSnapshot snapshot;
		snapshot.timestep = 10;
		snapshot.trains = {trainAt(7.5)};
		snapshot.trains[0].occupiedArcs = {GuiOccupiedArc{0, 0.0}};
		SignalItem* const head = presenter.m_signalsByAheadId["s0"].front();
		snapshot.signalStates = {GuiSignalState{"s0", 75, head->reversedDirection, 3, false}};
		// listed in the opposite order of the platforms on the canvas
		snapshot.platforms = {GuiPlatformState{"B", "P1", 40, {"p1", "p2", "p3"}}, GuiPlatformState{"B", "P2", 30, {"q1", "q2"}}};
		PlatformItem* const endPlatform = presenter.allPlatforms.value(1);
		PlatformItem* const middlePlatform = presenter.allPlatforms.value(0);
		ok &= expect(endPlatform->platformId == "P1" && middlePlatform->platformId == "P2", "the platforms are in the order of the line");

		SceneChanges changes(scene);
		presenter.apply(snapshot, NetworkPresenter::Playback::Live);
		ok &= expect(changes.take() > 0, "a step that adds items is announced by the scene");
		ok &= expect(iconIds(*endPlatform) == Ids({"p1", "p2", "p3"}) && iconIds(*middlePlatform) == Ids({"q1", "q2"}),
			"each platform shows an icon for each of its passengers");
		ok &= expect(paintedIcons(scene) == Ids({"q1", "q2", "p1", "p2", "p3"}) && paintedIcons(scene) == expectedIcons(presenter),
			"the icons are painted in the order of the platforms, whatever the order of the snapshot");

		// the same step again
		int marked = markItems(scene);
		presenter.apply(snapshot, NetworkPresenter::Playback::Live);
		ItemChanges itemsNow = itemChanges(scene, marked);
		ok &= expect(itemsNow.added == 0 && itemsNow.removed == 0, "a step that changes nothing adds and removes no item");
		ok &= expect(changes.take() == 0, "a step that changes nothing asks for no item to be painted again");

		// a passenger boards: only its icon goes and the others close up
		PassengerItem* const firstIcon = endPlatform->passengerIcons[0];
		PassengerItem* const lastIcon = endPlatform->passengerIcons[2];
		presenter.paxIconItem = firstIcon;
		snapshot.timestep = 11;
		snapshot.platforms[0].passengerIds = {"p1", "p3"};
		marked = markItems(scene);
		presenter.apply(snapshot, NetworkPresenter::Playback::Live);
		itemsNow = itemChanges(scene, marked);
		ok &= expect(itemsNow.added == 0 && itemsNow.removed == 1, "a passenger who boards takes one icon away and adds none");
		ok &= expect(endPlatform->passengerIcons.size() == 2 && endPlatform->passengerIcons[0] == firstIcon
				&& endPlatform->passengerIcons[1] == lastIcon,
			"the icons of the other passengers stay");
		ok &= expect(iconAt(*endPlatform, 0, 2, iconSize) && iconAt(*endPlatform, 1, 2, iconSize),
			"the icons that stay share the width of the platform again");
		ok &= expect(presenter.paxIconItem == firstIcon, "the selected icon stays selected while its passenger is there");
		ok &= expect(changes.take() > 0, "a change is announced");

		// passengers arrive, one of them between the two
		snapshot.timestep = 12;
		snapshot.platforms[0].passengerIds = {"p1", "p4", "p3", "p5"};
		marked = markItems(scene);
		presenter.apply(snapshot, NetworkPresenter::Playback::Live);
		itemsNow = itemChanges(scene, marked);
		ok &= expect(itemsNow.added == 2 && itemsNow.removed == 0, "passengers who arrive add one icon each");
		ok &= expect(iconIds(*endPlatform) == Ids({"p1", "p4", "p3", "p5"}) && endPlatform->passengerIcons[0] == firstIcon
				&& endPlatform->passengerIcons[2] == lastIcon,
			"the icons of the passengers who were there stay");
		ok &= expect(paintedIcons(scene) == Ids({"q1", "q2", "p1", "p4", "p3", "p5"}) && paintedIcons(scene) == expectedIcons(presenter),
			"icons that are added are painted in the place of their passenger");
		for (int i = 0; i < 4; ++i)
			ok &= expect(iconAt(*endPlatform, i, 4, iconSize), "the icons share the width of the platform");

		// the passengers swap places
		snapshot.timestep = 13;
		snapshot.platforms[0].passengerIds = {"p5", "p3", "p1", "p4"};
		marked = markItems(scene);
		presenter.apply(snapshot, NetworkPresenter::Playback::Live);
		itemsNow = itemChanges(scene, marked);
		ok &= expect(itemsNow.added == 0 && itemsNow.removed == 0, "passengers who swap places keep their icons");
		ok &= expect(iconIds(*endPlatform) == Ids({"p5", "p3", "p1", "p4"}) && endPlatform->passengerIcons[2] == firstIcon
				&& paintedIcons(scene) == Ids({"q1", "q2", "p5", "p3", "p1", "p4"}),
			"the icons are painted in the new order");
		ok &= expect(iconAt(*endPlatform, 0, 4, iconSize) && iconAt(*endPlatform, 2, 4, iconSize), "the icons move to their new places");

		// the platform that is not in the snapshot keeps what it shows
		snapshot.timestep = 14;
		snapshot.platforms.pop_back();
		presenter.apply(snapshot, NetworkPresenter::Playback::Live);
		ok &= expect(iconIds(*middlePlatform) == Ids({"q1", "q2"}), "a platform that the snapshot does not list keeps its icons");

		// nothing of the passengers is drawn while their layer is hidden, and the layer shows the step that is current when it is shown
		const QString middleText = middlePlatform->textIcon->toPlainText();
		const QString endText = endPlatform->textIcon->toPlainText();
		presenter.m_passengerLayerVisible = false;
		snapshot.timestep = 15;
		snapshot.platforms = {GuiPlatformState{"B", "P1", 40, {"p9"}}, GuiPlatformState{"B", "P2", 30, {}}};
		marked = markItems(scene);
		presenter.apply(snapshot, NetworkPresenter::Playback::Live);
		itemsNow = itemChanges(scene, marked);
		ok &= expect(itemsNow.added == 0 && itemsNow.removed == 0 && endPlatform->textIcon->toPlainText() == endText
				&& middlePlatform->textIcon->toPlainText() == middleText,
			"a hidden passenger layer leaves the platforms as they are");
		ok &= expect(iconIds(*endPlatform).size() == 4 && iconIds(*middlePlatform).size() == 2, "a hidden layer keeps the icons of the earlier step");
		presenter.m_passengerLayerVisible = true;
		presenter.updatePaxIconInfo(&snapshot, NetworkPresenter::Playback::Live);
		ok &= expect(iconIds(*endPlatform) == Ids({"p9"}) && middlePlatform->passengerIcons.isEmpty(),
			"the layer that is shown again has the icons of the current step");
		ok &= expect(endPlatform->textIcon->toPlainText() == "Pax on platform: 1\nMax pax volume: 40"
				&& middlePlatform->textIcon->toPlainText() == "Pax on platform: 0\nMax pax volume: 30",
			"the layer that is shown again has the counters of the current step");
		ok &= expect(presenter.paxIconItem == nullptr, "the selected icon is forgotten when its passenger has left");
		ok &= expect(paintedIcons(scene) == Ids({"p9"}), "no icon of a passenger who left is left on the canvas");
		marked = markItems(scene);
		presenter.apply(snapshot, NetworkPresenter::Playback::Replay);
		itemsNow = itemChanges(scene, marked);
		ok &= expect(itemsNow.added == 0 && itemsNow.removed == 0, "a platform that is up to date is not drawn again");
		presenter.clear();
		ok &= expect(scene.items().isEmpty(), "clear leaves nothing of the run");
	}

	// The trains of a snapshot are found by their index, whatever the order of the snapshot.
	{
		source->passengers = false;
		source->track(0).member[0].endNode.stationName.clear();
		source->track(0).member[0].endNode.stationPlatformId.clear();
		presenter.showRuntime(model);
		const auto train = [](int index, double headKm) {
			GuiTrainState state = trainAt(headKm);
			state.index = index;
			return state;
		};
		GuiSimulationSnapshot snapshot;
		snapshot.timestep = 10;
		snapshot.trains = {train(3, 2.5), train(1, 5.5), train(2, 7.5)};
		presenter.apply(snapshot, NetworkPresenter::Playback::Live);
		ok &= expect(presenter.allTrains.size() == 3, "each train of the snapshot is added");

		snapshot.timestep = 11;
		snapshot.trains = {train(2, 8.5), train(3, 3.5), train(1, 6.5)};
		presenter.apply(snapshot, NetworkPresenter::Playback::Live);
		const auto headOf = [&presenter](int index) {
			for (const TrainItemGroup* group : presenter.allTrains)
				if (group->index == index)
					return group->trainPolygonItemList->at(0)->polygon()[0].x();
			return -1.0;
		};
		ok &= expect(presenter.allTrains.size() == 3 && near(headOf(3), 3500.0) && near(headOf(1), 6500.0) && near(headOf(2), 8500.0),
			"each train of the snapshot is moved and none is added twice");
		presenter.clear();
	}

	// The canvas is built, filled with the steps of a run and of its replay, with a selection on it, and cleared again and
	// again. Nothing of an earlier round is left, and the next round draws what the first one drew.
	{
		presenter.pax_pixmap_scaled = QPixmap(10, 10);
		ok &= expect(presenterEmpty(presenter) && scene.items().isEmpty(), "the canvas is empty before the first round");
		int itemsAtEnd[2] = {0, 0};
		for (int round = 0; round < 20; ++round) {
			source->passengers = round % 2 == 1;
			presenter.showRuntime(model);
			SignalItem* const head = presenter.m_signalsByAheadId["s0"].front();

			GuiSimulationSnapshot snapshot;
			snapshot.timestep = 10;
			snapshot.trains = {trainAt(7.5)};
			snapshot.trains[0].occupiedArcs = {GuiOccupiedArc{0, 0.0}};
			snapshot.signalStates = {GuiSignalState{"s0", 75, head->reversedDirection, 3, false}};
			snapshot.sectionStates = {GuiSectionState{"s1", true, true}};
			if (source->passengers) {
				snapshot.platforms = {GuiPlatformState{"B", "P1", 40, {"p1", "p2", "p3"}}};
				snapshot.passengers = {GuiPassengerState{"p1", "waiting", "P1", "T1", "A"}};
			}
			presenter.apply(snapshot, NetworkPresenter::Playback::Live);

			// a selection of a train, a signal and a passenger
			TrainItemGroup* const train = presenter.allTrains.value(0);
			presenter.paintTrainPassengerInfo(train);
			presenter.m_inspectedSignal = head;
			presenter.effect = new HighlightEffect(Qt::blue, 1);
			train->setGraphicsEffect(presenter.effect);
			if (source->passengers) {
				PassengerItem* const icon = presenter.allPlatforms.value(0)->passengerIcons.value(0);
				presenter.paintPassengerInfoIcon(icon, passengerStateIn(&snapshot, icon->passengerId), false);
			}

			// the train runs on, passengers board and arrive, and a message of a virtual coupling comes and goes
			for (int step = 0; step < 4; ++step) {
				snapshot.timestep = 11 + step;
				snapshot.trains = {trainAt(8.0 + step * 0.5)};
				snapshot.trains[0].occupiedArcs = {GuiOccupiedArc{0, 0.0}};
				snapshot.virtualCouplingMessages = step == 1 ? std::vector<GuiVirtualCouplingState>{{12, "T1", "message"}}
															 : std::vector<GuiVirtualCouplingState>{};
				if (source->passengers)
					snapshot.platforms[0].passengerIds = step % 2 == 0 ? Ids({"p1", "p3", "p4"}) : Ids({"p1", "p5"});
				presenter.apply(snapshot, NetworkPresenter::Playback::Live);
			}
			snapshot.trains[0].outOfSimulation = true;
			presenter.apply(snapshot, NetworkPresenter::Playback::Live);

			// the replay of the run, forward and back
			presenter.stopTrainAnimations();
			presenter.m_prevTrainPositions.clear();
			for (const int time : {20, 15, 25, 10}) {
				snapshot.timestep = time;
				snapshot.trains = {trainAt(7.5 + (time - 10) * 0.1)};
				snapshot.trains[0].occupiedArcs = {GuiOccupiedArc{0, 0.0}};
				if (source->passengers)
					snapshot.platforms[0].passengerIds = time % 2 == 0 ? Ids({"p1", "p2"}) : Ids({"p2", "p1", "p6"});
				presenter.apply(snapshot, NetworkPresenter::Playback::Replay);
			}
			ok &= expect(presenter.allTrains.size() == 1 && presenter.m_trainBadges.size() == 1,
				"a round holds one train and one badge, whatever the earlier rounds drew");
			ok &= expect(presenter.trainPaxInfoItem && presenter.m_inspectedSignal && presenter.effect
					&& (!source->passengers || presenter.paxIconInfoItem),
				"the selection is on the canvas before it is cleared");
			itemsAtEnd[round % 2] = round < 2 ? scene.items().size() : itemsAtEnd[round % 2];
			ok &= expect(scene.items().size() == itemsAtEnd[round % 2], "a round draws as many items as the first round of its kind");

			presenter.clear();
			QApplication::processEvents();
			ok &= expect(presenterEmpty(presenter) && scene.items().isEmpty(), "clear leaves no item, entry or pointer of the round");
		}
	}

	// The heads of a run name their track by its id in the scene and their sections by the labels of the scene of the
	// run, in the tooltip and in Find. The runtime number of the track and the section ids stay on the heads, and the ids
	// find a head too.
	{
		QGraphicsScene labelScene;
		NetworkPresenter labelPresenter(&labelScene, nullptr);
		auto geometry = std::make_unique<FakeGeometry>();
		geometry->track(0).sceneTrackId = "T";
		labelPresenter.setGeometrySource(std::move(geometry));
		SceneModel labelled;
		labelled.tracks = {{"T"}};
		labelled.nodes = {{"T.a", "T", 0.0, 0.0}, {"T.b", "T", 5.0, 0.0}, {"T.c", "T", 10.0, 0.0}};
		labelled.arcs = {{"T.arc.1", "T", "T.a", "T.b", 0.0, 0.0, 12.0}, {"T.arc.2", "T", "T.b", "T.c", 0.0, 0.0, 12.0}};
		labelled.blocks = {{"s0", "T", 5.0}, {"s1", "T", 5.0}};
		labelPresenter.showRuntime(labelled);
		const SignalItem* forward = nullptr;
		for (const SignalItem* head : labelPresenter.allSignals)
			if (!head->reversedDirection)
				forward = head;
		ok &= expect(labelPresenter.sceneTrackId(0) == "T" && labelPresenter.sceneTrackId(1).empty(),
			"the presenter gives the id in the scene of a track line of the run");
		ok &= expect(forward && forward->trackID == 0 && forward->sceneTrackId == "T" && forward->sectionAheadId == "s1"
				&& forward->sectionAheadLabel == "T 5.000 to 10.000 km" && forward->sectionBehindId == "s0"
				&& forward->sectionBehindLabel == "T 0.000 to 5.000 km",
			"a head of a run keeps its track number and section ids and gets the track id and the section labels");
		ok &= expect(forward && forward->inspectionIdentity() == "Track T at 5 km; ahead T 5.000 to 10.000 km; behind T 0.000 to 5.000 km",
			"the tooltip of a head of a run names the track by its id and the sections by their labels");
		const QList<NetworkPresenter::FindTarget> targets = labelPresenter.findTargets();
		const auto target = [&targets](const QString& label) {
			const auto found = std::find_if(targets.cbegin(), targets.cend(),
				[&label](const NetworkPresenter::FindTarget& candidate) { return candidate.label == label; });
			return found == targets.cend() ? nullptr : &*found;
		};
		const NetworkPresenter::FindTarget* signalTarget =
			target("Signal: Track T at 5 km; ahead T 5.000 to 10.000 km; behind T 0.000 to 5.000 km, right");
		ok &= expect(signalTarget && signalTarget->item == forward && signalTarget->keys == "s1 s0",
			"Find names a head by its labels and keeps its section ids as keys");
		ok &= expect(target("Station: B (track T)") != nullptr, "Find names the track of a station node by its id");
		labelPresenter.clear();
	}

	// A run draws a track whose view sets double_switch as the original drew a double switch: the middle arc and no node
	// dot. Without that view the track of four nodes keeps its three arcs and the dot at its end.
	{
		QGraphicsScene switchScene;
		NetworkPresenter switchPresenter(&switchScene, nullptr);
		auto geometry = std::make_unique<FakeGeometry>();
		BlockSet& track = geometry->track(0);
		track.sceneTrackId = "B0";
		track.len = 3;
		track.numNodes = 4;
		track.member.resize(3);
		track.member[1].endNode.X = 7.5;
		track.member[1].endNode.stationName.clear();
		track.member[1].endNode.stationPlatformId.clear();
		track.member[2].startNode.X = 7.5;
		track.member[2].endNode.X = 10.0;
		switchPresenter.setGeometrySource(std::move(geometry));
		const auto dots = [&switchScene]() {
			int count = 0;
			for (QGraphicsItem* item : switchScene.items()) {
				const auto* node = qgraphicsitem_cast<NodeItem*>(item);
				if (node && node->rect().width() > 0.0)
					++count;
			}
			return count;
		};
		SceneModel switchModel;
		switchPresenter.showRuntime(switchModel);
		ok &= expect(switchPresenter.allArcs.size() == 3 && dots() == 1,
			"a run draws the three arcs of a track of four nodes and the dot at its end");
		switchPresenter.clear();
		switchModel.trackViews = {{"B0", 0, 0, true, true}};
		switchPresenter.showRuntime(switchModel);
		ok &= expect(switchPresenter.allArcs.size() == 1 && switchPresenter.allArcs[0]->arc == &track.member[1] && dots() == 0,
			"a run draws the middle arc of a double switch and no node dot");
		switchPresenter.clear();
	}

	// With the station B lower down the line runs diagonally. A platform lies along it on its upper side, while its counter
	// stays level above it and its passengers share its length on its long axis.
	{
		source->passengers = true;
		source->station(1).Y = 5.0;
		source->track(0).member[0].endNode.stationName = "B";
		source->track(0).member[0].endNode.stationPlatformId = "P2";
		presenter.showRuntime(model);
		// The platforms and their passengers show in the near look.
		presenter.setStationNearLook(true);
		const Arc& lastArc = source->track(0).member[1];
		const QLineF unit = QLineF(lastArc.startNode.graphX, lastArc.startNode.graphY, lastArc.endNode.graphX, lastArc.endNode.graphY)
								.unitVector();
		const QPointF direction(unit.dx(), unit.dy());
		const qreal size = presenter.station_node_size * presenter.presentationScale();
		PlatformItem* const middlePlatform = presenter.allPlatforms.value(0);
		PlatformItem* const endPlatform = presenter.allPlatforms.value(1);
		ok &= expect(direction.y() > 0.1 && middlePlatform && endPlatform
				&& platformAlong(*middlePlatform, lastArc.startNode, direction, size)
				&& platformAlong(*endPlatform, lastArc.endNode, direction, size),
			"a platform lies along its diagonal track, on the upper side");

		GuiSimulationSnapshot snapshot;
		snapshot.timestep = 10;
		snapshot.platforms = {GuiPlatformState{"B", "P1", 40, {"p1", "p2", "p3"}}, GuiPlatformState{"B", "P2", 30, {"q1", "q2"}}};
		presenter.apply(snapshot, NetworkPresenter::Playback::Live);
		const qreal iconSize = presenter.pax_pixmap_scaled.width() * presenter.presentationScale();
		bool upright = middlePlatform && endPlatform && middlePlatform->passengerIcons.size() == 2
			&& endPlatform->passengerIcons.size() == 3;
		for (const PlatformItem* platform : {middlePlatform, endPlatform}) {
			if (!upright)
				break;
			const QRectF text = platform->textIcon->sceneBoundingRect();
			upright = platform->textIcon->rotation() == 0 && text.bottom() <= platform->sceneBoundingRect().top() + 1e-6
				&& near(text.center().x(), platform->sceneBoundingRect().center().x());
			for (int i = 0; i < platform->passengerIcons.size(); ++i)
				upright = upright && platform->passengerIcons[i]->rotation() == 0
					&& iconAt(*platform, i, static_cast<int>(platform->passengerIcons.size()), iconSize);
		}
		ok &= expect(upright, "the counter and the passenger icons of a turned platform stay level, the icons on its long axis");
		presenter.clear();
	}

	// Without the geometry of its stations a track line is drawn one track separation higher for each level.
	{
		BlockSet& track = source->track(0);
		track.hasGraphLayout = true;
		track.graphID = 1;
		track.region = 1; // no station is in this region
		presenter.showRuntime(model);
		bool above = presenter.allArcs.size() == 2;
		for (const TrackLineItem* arc : presenter.allArcs)
			above = above && near(arc->line().y1(), -presenter.track_separation) && near(arc->line().y2(), -presenter.track_separation);
		ok &= expect(above, "a track line on level 1 without station geometry is drawn above level 0");
		presenter.clear();
	}

	// With the preview of the scene a run puts the pictures of the stations on a higher schematic level where the
	// preview put them.
	{
		SceneModel levels;
		levels.tracks = {{"low"}, {"high"}};
		levels.nodes = {{"low.a", "low", 0.0, 0.0}, {"low.b", "low", 10.0, 0.0}, {"high.a", "high", 0.0, 0.0},
			{"high.b", "high", 10.0, 0.0}};
		levels.arcs = {{"low.arc", "low", "low.a", "low.b", 0.0, 0.0, 12.0},
			{"high.arc", "high", "high.a", "high.b", 0.0, 0.0, 12.0}};
		levels.trackViews = {{"low", 0, 0}, {"high", 2, 0}};
		levels.stationViews = {{"A", 1.0, 0.0, {{0, 0.0}}, {}}, {"Koge", 1.0, 0.1, {{0, 10.0}}, {}}};
		for (const auto& stationData : {
				 std::pair<const char*, const char*>{"A", "high.a"}, std::pair<const char*, const char*>{"Koge", "high.b"}}) {
			SceneStation station;
			station.id = station.name = stationData.first;
			station.platforms.push_back({std::string(stationData.first) + ".platform", {stationData.second}});
			levels.stations.push_back(std::move(station));
		}
		QGraphicsScene levelScene;
		NetworkPresenter levelPresenter(&levelScene, nullptr);
		auto geometry = std::make_unique<FakeGeometry>();
		geometry->station(1).stationName = "Koge";
		levelPresenter.setGeometrySource(std::move(geometry));
		// The bottom centre of the picture of each station, by name.
		const auto pictureAnchors = [&levelPresenter]() {
			QMap<QString, QPointF> anchors;
			for (StationOverlayItem* overlay : levelPresenter.m_stationOverlays)
				if (const QGraphicsPixmapItem* picture = levelPresenter.m_stationPictures.value(overlay, nullptr))
					anchors.insert(overlay->stationName(),
						picture->mapToScene(QPointF(levelPresenter.station_size / 2.0, levelPresenter.station_size)));
			return anchors;
		};
		levelPresenter.showPreview(levels);
		const QMap<QString, QPointF> preview = pictureAnchors();
		levelPresenter.clear();
		levelPresenter.showRuntime(levels);
		const QMap<QString, QPointF> run = pictureAnchors();
		bool same = preview.size() == 2 && run.size() == 2;
		for (auto anchor = preview.cbegin(); anchor != preview.cend(); ++anchor)
			same = same && run.contains(anchor.key()) && QLineF(run.value(anchor.key()), anchor.value()).length() < 1e-6;
		ok &= expect(same, "a run with the preview puts the station pictures of a higher schematic level where the preview put them");
	}

	if (!ok)
		return 1;

	std::cout << "all runtime canvas tests passed\n";
	return 0;
}
