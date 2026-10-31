// SceneModel.h has a member named signals, so it comes before the Qt headers.
#include "scene/SceneModel.h"

#include "graphics/NetworkPresenter.h"
#include "tests/NetworkPresenterTestAccess.h"

#include "simulation/GuiSimulationSnapshot.h"
#include "graphics/FollowCamera.h"
#include "graphics/NetworkView.h"
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
#include "scene/TrackPreview.h"
#include "simulation/Signalling.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QLineF>
#include <QLoggingCategory>
#include <QMap>
#include <QObject>
#include <QPixmap>
#include <QTest>
#include <QTimer>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

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
		m_sections[0].GeoXBegNode = 0.0;
		m_sections[1].GeoXBegNode = 5000.0;

		m_route.N_Block_Sections = 2;
		m_route.sequence_of_block_sections = m_sections;
		m_undrawnRoute = m_route;
		for (Section& section : m_undrawnRoute.sequence_of_block_sections)
			section.trackLineId = 2;

		m_platform.ID = "P1";
		m_platform.StationID = "B";
		m_platform.Max_Passenger_Volume = 40;
		m_platform.Current_N_Passengers = 7;
		m_platformTwo.ID = "P2";
		m_platformTwo.StationID = "B";
		m_platformTwo.Max_Passenger_Volume = 30;
	}

	bool passengers = false;

	// Makes the line two parallel tracks, the scene tracks T and U, with a crossover from T at 4 km to U at 6 km, and the
	// route one from the start of T to the end of U over it, in the sections that the simulation makes of it: on T up to
	// 3 km, the section derived at the switch from 3 to 7 km, and on U. The positions along the route are its X.
	void makeCrossover() {
		makeTwoTracks();
		m_sections.resize(3);
		const auto section = [this](std::size_t index, const char* id, int track, double fromKm, double toKm) {
			Section& made = m_sections[index];
			made.ID = id;
			made.trackLineId = track;
			made.start_node.X = fromKm;
			made.end_node.X = toKm;
			made.length = (toKm - fromKm) * 1000.0;
			made.total_arcs = 0;
			made.GeoXBegNode = fromKm * 1000.0;
		};
		section(0, "@1-s0@", 0, 0.0, 3.0);
		section(1, "@1-s0@-4.000/@2-u1@-6.000", -1, 3.0, 7.0);
		section(2, "@2-u1@", 1, 7.0, 10.0);
		m_route.N_Block_Sections = 3;
		m_route.sequence_of_block_sections = m_sections;
	}

	// Makes the line two parallel tracks, the scene tracks T and U, and puts the second section of the route on U, so
	// that the route runs on T up to 5 km and on U after it. The positions along the route are its X.
	void makeJoin() {
		makeTwoTracks();
		m_sections[1].trackLineId = 1;
		m_route.sequence_of_block_sections = m_sections;
	}

	// Gives the crossover of makeCrossover() its connection, from T at 4 km to U at 6 km.
	void addCrossoverConnection() {
		m_connections.resize(1);
		m_connections[0].ID = "c";
		m_connections[0].idFirstTrackLine = 0;
		m_connections[0].idSecondTrackLine = 1;
		m_connections[0].xFirstNode = 4.0;
		m_connections[0].xSecondNode = 6.0;
	}

	int trackCount() const override { return static_cast<int>(m_tracks.size()); }
	const BlockSet& track(int index) const override { return m_tracks[static_cast<std::size_t>(index)]; }
	// The track line with this index, for a test to set up.
	BlockSet& track(int index) { return m_tracks[static_cast<std::size_t>(index)]; }
	int regionOfTrack(int index) const override { return m_tracks[static_cast<std::size_t>(index)].region; }
	int stationCount() const override { return static_cast<int>(m_stations.size()); }
	const Stations& station(int index) const override { return m_stations[static_cast<std::size_t>(index)]; }
	// The station with this index, for a test to set up.
	Stations& station(int index) { return m_stations[static_cast<std::size_t>(index)]; }
	int connectionCount() const override { return static_cast<int>(m_connections.size()); }
	const Connections& connection(int index) const override { return m_connections[static_cast<std::size_t>(index)]; }
	int sectionCount() const override { return static_cast<int>(m_sections.size()); }
	const Section& section(int index) const override { return m_sections[static_cast<std::size_t>(index)]; }
	// Route 1 runs over the same sections as route 0, so that a train can change its route where it is. Route 2 runs over
	// the same places on a track that the source does not have, so a train on it is not drawn.
	const Route* route(int index) const override {
		if (index == 2)
			return &m_undrawnRoute;
		return index == 0 || index == 1 ? &m_route : nullptr;
	}
	const StationPlatform* platform(const std::string& stationId, const std::string& platformId) const override {
		for (const StationPlatform* candidate : {&m_platform, &m_platformTwo})
			if (stationId == candidate->StationID && platformId == candidate->ID)
				return candidate;
		return nullptr;
	}
	bool passengerGui() const override { return passengers; }

private:
	// Makes the line two parallel tracks, the scene tracks T and U, with the station B at the end of T only.
	void makeTwoTracks() {
		m_tracks.resize(2);
		m_tracks[0].sceneTrackId = "T";
		m_tracks[1] = m_tracks[0];
		m_tracks[1].sceneTrackId = "U";
		m_tracks[1].member[1].endNode.stationName.clear();
		m_tracks[1].member[1].endNode.stationPlatformId.clear();
	}

	std::vector<BlockSet> m_tracks;
	std::vector<Stations> m_stations;
	std::vector<Connections> m_connections;
	std::vector<Section> m_sections;
	Route m_route;
	Route m_undrawnRoute;
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
	if (presenter.runTrains().size() != 1 || !presenter.runTrains()[0]->trainPolygonItemList)
		return false;
	const QPolygonF polygon = presenter.runTrains()[0]->trainPolygonItemList->at(0)->polygon();
	const double head = headKm * 1000.0;
	const double tail = head - 100.0;
	const double half = 0.10 * presenter.track_separation;
	return polygon.size() == 4 && near(polygon[0].x(), head) && near(polygon[0].y(), half) && near(polygon[1].x(), tail)
		&& near(polygon[1].y(), half) && near(polygon[2].x(), tail) && near(polygon[2].y(), -half)
		&& near(polygon[3].x(), head) && near(polygon[3].y(), -half);
}

// The middle of the first body of a train as it is drawn, in scene coordinates.
static QPointF drawnMiddle(const TrainItemGroup& train) {
	const TrainBodyItem* body = train.trainPolygonItemList->at(0);
	return body->mapToScene(body->polygon().boundingRect().center());
}

// A train of wagons of 100 m each with its head at headKm, on a route whose positions are its X.
static GuiTrainState trainOfWagons(int wagons, double headKm) {
	GuiTrainState train = trainAt(headKm);
	train.wagonCount = wagons - 1;
	train.length = 100.0 * wagons;
	train.wagonHeadPositions.clear();
	train.wagonTailPositions.clear();
	for (int wagon = 0; wagon < wagons; ++wagon) {
		train.wagonHeadPositions.push_back(headKm - 0.1 * wagon);
		train.wagonTailPositions.push_back(headKm - 0.1 * (wagon + 1));
	}
	return train;
}

// The middle of the front of the first body of a train, where its head is drawn, in scene coordinates.
static QPointF drawnFront(const TrainItemGroup& train) {
	const TrainBodyItem* body = train.trainPolygonItemList->at(0);
	const QPolygonF polygon = body->polygon();
	return polygon.isEmpty() ? QPointF() : body->mapToScene((polygon.first() + polygon.last()) / 2);
}

// The middle of the back of the last body of a train, where its tail is drawn, in scene coordinates.
static QPointF drawnBack(const TrainItemGroup& train) {
	const TrainBodyItem* body = train.trainPolygonItemList->last();
	const QPolygonF polygon = body->polygon();
	const int corners = static_cast<int>(polygon.size());
	return corners < 2 ? QPointF() : body->mapToScene((polygon[corners / 2 - 1] + polygon[corners / 2]) / 2);
}

// The drawn line of a track of the preview, segment by segment.
static std::vector<QLineF> segmentsOf(const TrackPreviewLine& line) {
	std::vector<QLineF> segments;
	for (std::size_t i = 1; i < line.points.size(); ++i)
		segments.emplace_back(line.points[i - 1].x, line.points[i - 1].y + line.displayOffset, line.points[i].x,
			line.points[i].y + line.displayOffset);
	return segments;
}

// The point of the drawn line of a track of the preview at X in kilometres.
static QPointF linePointAt(const TrackPreviewLine& line, double xKm) {
	TrackPreviewPoint point;
	trackPreviewPointAtX(line, xKm, point);
	return QPointF(point.x, point.y + line.displayOffset);
}

// The distance from a point to the nearest of the segments of a line.
static qreal distanceTo(const std::vector<QLineF>& segments, const QPointF& point) {
	qreal nearest = std::numeric_limits<qreal>::max();
	for (const QLineF& segment : segments) {
		const QPointF along = segment.p2() - segment.p1();
		const qreal squared = QPointF::dotProduct(along, along);
		const qreal t = squared > 0.0 ? std::clamp(QPointF::dotProduct(point - segment.p1(), along) / squared, 0.0, 1.0) : 0.0;
		nearest = std::min(nearest, QLineF(point, segment.p1() + along * t).length());
	}
	return nearest;
}

// The distance from the line to the corner of a body of the train that is furthest from it; the largest distance when
// a body has no shape.
static qreal farthestCorner(const TrainItemGroup& train, const std::vector<QLineF>& segments) {
	qreal farthest = 0.0;
	for (const TrainBodyItem* body : *train.trainPolygonItemList) {
		if (body->polygon().isEmpty())
			return std::numeric_limits<qreal>::max();
		for (const QPointF& corner : body->polygon())
			farthest = std::max(farthest, distanceTo(segments, body->mapToScene(corner)));
	}
	return farthest;
}

// The centre of the middle line of the drawn bodies of a train, each piece weighted by its length, in scene coordinates.
// The first half of the corners of a body runs along one side from the front and the second half back along the other.
static QPointF drawnCentre(const TrainItemGroup& train) {
	QPointF sum;
	qreal drawn = 0.0;
	for (const TrainBodyItem* body : *train.trainPolygonItemList) {
		const QPolygonF polygon = body->mapToScene(body->polygon());
		const int corners = static_cast<int>(polygon.size());
		for (int i = 1; i < corners / 2; ++i) {
			const QPointF from = (polygon[i - 1] + polygon[corners - i]) / 2.0;
			const QPointF to = (polygon[i] + polygon[corners - 1 - i]) / 2.0;
			const qreal length = QLineF(from, to).length();
			sum += (from + to) * (length / 2.0);
			drawn += length;
		}
	}
	return drawn > 0.0 ? sum / drawn : QPointF();
}

// Records what apply() asks of its host, in order, and apart from that where the steps of a glide draw a train.
class RecordingHost final : public NetworkPresenter::FrameHost {
public:
	std::vector<std::string> calls;
	std::vector<QPointF> moves;
	QPointF placedAt;
	bool promoted = false;
	bool cameraMoves = false;

	bool isTrainOverlayPromoted(int) const override { return promoted; }
	void trainWillUpdate(int train) override { calls.push_back("willUpdate " + std::to_string(train)); }
	bool trainPlaced(int train, const QPointF& center) override {
		calls.push_back("placed " + std::to_string(train));
		placedAt = center;
		return cameraMoves;
	}
	void trainMoved(int, const QPointF& center) override { moves.push_back(center); }
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

// Follows train 0 with a camera, as the window does: the camera goes to the train as it is drawn at every update and at
// every step of its motion.
class FollowingHost final : public NetworkPresenter::FrameHost {
public:
	explicit FollowingHost(FollowCamera& camera) : m_camera(camera) {}

	bool isTrainOverlayPromoted(int train) const override { return train == 0; }
	void trainWillUpdate(int) override {}
	bool trainPlaced(int train, const QPointF& center) override { return follow(train, center, false); }
	void trainMoved(int train, const QPointF& center) override { follow(train, center, false); }
	void trainHidden(int) override {}
	bool trainAdded(int train, bool) override { return follow(train, QPointF(), true); }
	void signalsApplied() override {}
	void trainsApplied(bool, bool) override {}

	NetworkPresenter* presenter = nullptr;

private:
	bool follow(int train, const QPointF& center, bool snap) {
		if (train != 0 || !presenter)
			return false;
		m_camera.follow(snap ? presenter->trainCenter(train) : center, snap);
		return true;
	}

	FollowCamera& m_camera;
};

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
	return static_cast<int>(items.size());
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
	changes.removed = marked - (static_cast<int>(scene.items().size()) - changes.added);
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
	for (const PlatformItem* platform : PresenterAccess::allPlatforms(presenter))
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

// The line the presenter draws for the arc, from its start node to its end node; an empty line when it draws none.
static QLineF drawnArc(NetworkPresenter& presenter, const Arc& arc) {
	for (const TrackLineItem* item : PresenterAccess::allArcs(presenter))
		if (item->arc == &arc)
			return item->line();
	return {};
}

// Whether the platform is parallel, within half a degree, to direction, a unit vector that points to the right, with its
// centre 1.15 times size from node, the point where the node is drawn, on the upper side of the track.
static bool platformAlong(const PlatformItem& platform, const QPointF& node, const QPointF& direction, qreal size) {
	const QRectF rect = platform.rect();
	const QLineF axis(platform.mapToScene(QPointF(rect.left(), rect.center().y())),
		platform.mapToScene(QPointF(rect.right(), rect.center().y())));
	const QPointF centre = platform.mapToScene(rect.center());
	const QPointF expected = node + QPointF(direction.y(), -direction.x()) * 1.15 * size;
	return std::fabs(axis.dx() * direction.y() - axis.dy() * direction.x()) < axis.length() * std::sin(qDegreesToRadians(0.5))
		&& near(centre.x(), expected.x()) && near(centre.y(), expected.y());
}

// The fields of the source that hold a place on the canvas: the levels of the tracks, the canvas points of their nodes,
// the places, regions and shifts of the stations and the canvas ends of the connections, a map as its keys and values.
static std::vector<double> canvasFields(const RuntimeGeometrySource& source) {
	std::vector<double> fields;
	const auto addMap = [&fields](const std::map<int, double>& values) {
		fields.push_back(static_cast<double>(values.size()));
		for (const auto& [key, value] : values) {
			fields.push_back(key);
			fields.push_back(value);
		}
	};
	for (int track = 0; track < source.trackCount(); ++track) {
		fields.push_back(source.track(track).graphID);
		for (const Arc& arc : source.track(track).member)
			fields.insert(fields.end(), {arc.startNode.graphX, arc.startNode.graphY, arc.endNode.graphX, arc.endNode.graphY});
	}
	for (int index = 0; index < source.stationCount(); ++index) {
		const Stations& station = source.station(index);
		fields.insert(fields.end(), {station.graphX, station.graphY, static_cast<double>(station.regions.size())});
		fields.insert(fields.end(), station.regions.begin(), station.regions.end());
		for (const std::map<int, double>* values :
			{&station.regionX, &station.shiftX, &station.shiftY, &station.signalDeltaX, &station.signalDeltaY})
			addMap(*values);
	}
	for (int index = 0; index < source.connectionCount(); ++index) {
		const Connections& connection = source.connection(index);
		fields.insert(fields.end(), {connection.graphXFirstNode, connection.graphYFirstNode, connection.graphXSecondNode, connection.graphYSecondNode});
	}
	return fields;
}

using Ids = std::vector<std::string>;

// Whether the presenter holds no item and points to none, the way a new presenter does.
static bool presenterEmpty(const NetworkPresenter& presenter) {
	return presenter.runTrains().isEmpty() && PresenterAccess::trainBadges(presenter).isEmpty() && !presenter.trainsMoving()
		&& presenter.runSignals().isEmpty() && PresenterAccess::signalsByAheadId(presenter).empty() && PresenterAccess::signalDecorations(presenter).isEmpty()
		&& PresenterAccess::allArcs(presenter).isEmpty() && PresenterAccess::tracksBySectionId(presenter).empty()
		&& PresenterAccess::tracksByOccupiedArc(presenter).empty() && PresenterAccess::activeTrackItems(presenter).empty() && PresenterAccess::allPlatforms(presenter).isEmpty()
		&& presenter.stationOverlays().isEmpty() && PresenterAccess::stationDecorations(presenter).isEmpty() && PresenterAccess::stationLabels(presenter).isEmpty()
		&& PresenterAccess::stationPictures(presenter).isEmpty() && PresenterAccess::vcMessageItems(presenter).isEmpty() && !PresenterAccess::inspectedSignal(presenter)
		&& !presenter.selectedTrain() && !PresenterAccess::trainPaxInfoItem(presenter) && !PresenterAccess::paxIconItem(presenter) && !PresenterAccess::paxIconInfoItem(presenter)
		&& !presenter.hasHighlight();
}

// A presenter that draws the run of a FakeGeometry on a scene of its own.
struct RunFixture {
	const SceneModel model;
	QGraphicsScene scene;
	NetworkPresenter presenter{&scene, nullptr};
	FakeGeometry* source = nullptr;

	RunFixture() {
		auto fake = std::make_unique<FakeGeometry>();
		source = fake.get();
		presenter.setGeometrySource(std::move(fake));
	}
};

// The motion of a live train across updates, on the clock of the test, which runs the steps by hand. The track of this
// source runs along x in metres.
struct MotionFixture : RunFixture {
	qint64 now = 0;
	GuiSimulationSnapshot snapshot;

	MotionFixture() {
		presenter.setClock([this]() { return now; });
		presenter.showRuntime(model);
		snapshot.timestep = 0;
	}

	~MotionFixture() {
		presenter.setClock({});
		presenter.clear();
	}

	// Delivers the train with its head at headKm. The tail of a train that is still entering its route has no position,
	// as in a run.
	void update(double headKm) {
		GuiTrainState state = trainAt(headKm);
		if (headKm < 0.1)
			state.wagonTailPositions = {-1.0};
		snapshot.timestep += 1;
		snapshot.trains = {state};
		presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	}

	// The x of the head of the train as it is drawn; not a number when its body has no shape.
	double drawnHead() const {
		const TrainBodyItem* body = presenter.runTrains()[0]->trainPolygonItemList->at(0);
		const QPolygonF polygon = body->polygon();
		return polygon.isEmpty() ? std::numeric_limits<double>::quiet_NaN() : body->mapToScene(polygon.first()).x();
	}
};

class TestRuntimeCanvas : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void drawsNothingWithoutSource();
	void drawsGeometryOfSource();
	void virtualStationGetsNoPlatformBar();
	void applyShowsStepsOfRun();
	void trainOnUndrawnTrackShowsNoPassengers();
	void passengersOnBoardKeepTheirGroup();
	void headTooltipFollowsAspect();
	void stepTouchesOnlyWhatItChanges();
	void trainGlidesAlongBentLine();
	void trainRunsOverCrossover();
	void trainCentreStepsOverJoin();
	void drawsOnlyTrainsNearVisibleScene();
	void enteringTrainGlidesOnward();
	void glideStartsAtSectionEnd();
	void steadyRhythmGivesEvenSpeed();
	void irregularGapsNeverMoveTrainBack();
	void longGapDoesNotSlowGlides();
	void updateLeavesGlidingTrainInPlace();
	void updatesDoNotHoldOffSteps();
	void unevenUpdatesGiveEvenSpeed();
	void stoppingTrainReachesItsPlace();
	void newRouteOrDirectionPlacesTrainAtOnce();
	void followedTrainMovesViewEvenly();
	void findsTrainsByIndex();
	void roundsLeaveNothingBehind();
	void headsUseSceneLabels();
	void doubleSwitchDrawsMiddleArc();
	void platformLiesAlongDiagonalTrack();
	void levelWithoutStationGeometryIsDrawnHigher();
	void runPutsPicturesWherePreviewDoes();
	void drawingLeavesSourceUnchanged();
};

void TestRuntimeCanvas::initTestCase() {
	// The entity icons are resources of the application, which this test does not have.
	QLoggingCategory::setFilterRules(QStringLiteral("qt.svg.warning=false"));
}

// Without a source nothing of a run is drawn.
void TestRuntimeCanvas::drawsNothingWithoutSource() {
	const SceneModel model;
	QGraphicsScene scene;
	NetworkPresenter presenter(&scene, nullptr);
	presenter.showRuntime(model);
	QVERIFY2(scene.items().isEmpty(), "a presenter without a source draws nothing");
}

void TestRuntimeCanvas::drawsGeometryOfSource() {
	RunFixture f;
	const SceneModel& model = f.model;
	QGraphicsScene& scene = f.scene;
	NetworkPresenter& presenter = f.presenter;
	FakeGeometry* const source = f.source;
	qsizetype drawnItems = 0;
	for (int cycle = 0; cycle < 3; ++cycle) {
		source->passengers = cycle > 0;
		presenter.showRuntime(model);
		// The run draws both arcs and both stations of the source.
		QCOMPARE(PresenterAccess::allArcs(presenter).size(), qsizetype(2));
		QCOMPARE(presenter.stationOverlays().size(), qsizetype(2));
		// The run draws one signal with a head for each direction at the border of the block sections.
		QCOMPARE(presenter.runSignals().size(), qsizetype(2));
		QCOMPARE(PresenterAccess::signalDecorations(presenter).size(), qsizetype(6));
		// The signals are indexed by the section ahead of them.
		QCOMPARE(PresenterAccess::signalsByAheadId(presenter).size(), std::size_t(2));
		QCOMPARE(PresenterAccess::signalsByAheadId(presenter).count("s0"), std::size_t(1));
		QCOMPARE(PresenterAccess::signalsByAheadId(presenter).count("s1"), std::size_t(1));
		// The run draws the platform with or without passengers.
		QCOMPARE(PresenterAccess::allPlatforms(presenter).size(), qsizetype(1));
		PlatformItem* const platform = PresenterAccess::allPlatforms(presenter).value(0);
		// The counter and the details of the platform come only with the passengers. The platform takes its details from
		// the source and has a counter only in a run with passengers.
		if (platform == nullptr)
			QFAIL("the run draws no platform");
		if (source->passengers) {
			QCOMPARE(platform->stationId, "B");
			QCOMPARE(platform->platformId, "P1");
			QCOMPARE(platform->maxVolume, 40);
			QVERIFY(platform->textIcon);
		} else {
			QVERIFY(platform->stationId.empty());
			QCOMPARE(platform->maxVolume, 0);
			QVERIFY(!platform->textIcon);
		}
		// The platform of a level track is level, above its node.
		QCOMPARE_EQ(platform->rotation(), 0.0);
		QVERIFY(platformAlong(*platform, drawnArc(presenter, source->track(0).member[1]).p2(), QPointF(1, 0),
			presenter.station_node_size * presenter.presentationScale()));
		// The bar shows with the stations in the near look; its counter waits for the zoom.
		const auto counterShown = [platform]() { return platform && platform->textIcon && platform->textIcon->isVisible(); };
		// The platform and its counter are hidden in the far look.
		QVERIFY(!platform->isVisible());
		QVERIFY(!counterShown());
		PresenterAccess::setStationNearLook(presenter, true);
		// The platform shows in the near look.
		QVERIFY(platform->isVisible());
		QVERIFY(!counterShown());
		PresenterAccess::stationLayerVisible(presenter) = false;
		PresenterAccess::setStationNearLook(presenter, true);
		QVERIFY2(!platform->isVisible(), "the platform hides with the stations");
		PresenterAccess::stationLayerVisible(presenter) = true;

		presenter.paintTrain(trainAt(7.5), model, false);
		// The train and its badge are added.
		QCOMPARE(presenter.runTrains().size(), qsizetype(1));
		QVERIFY(PresenterAccess::trainBadges(presenter).contains(0));
		QVERIFY2(bodyAt(presenter, 7.5), "the body of the train lies on the track line of the source");
		// The body follows the train.
		QVERIFY(presenter.getTrainPolygonItemList(presenter.runTrains()[0]->trainPolygonItemList, trainAt(8.0)));
		QVERIFY(bodyAt(presenter, 8.0));
		QPolygonF none;
		GuiTrainState lost = trainAt(7.5);
		lost.routeIndex = 5;
		presenter.getTrainPolygon(&none, 0, lost);
		QVERIFY2(none.isEmpty(), "a train on a route that the source does not have has no shape");

		if (cycle == 0) {
			drawnItems = scene.items().size();
		} else {
			// The passengers add only the counter of the platform.
			QCOMPARE(scene.items().size(), drawnItems + 1);
		}
		presenter.clear();
		// Clear leaves nothing of the run.
		QVERIFY(scene.items().isEmpty());
		QVERIFY(PresenterAccess::allArcs(presenter).isEmpty());
		QVERIFY(presenter.runSignals().isEmpty());
		QVERIFY(PresenterAccess::signalsByAheadId(presenter).empty());
		QVERIFY(presenter.runTrains().isEmpty());
		QVERIFY(PresenterAccess::trainBadges(presenter).isEmpty());
		QVERIFY(PresenterAccess::allPlatforms(presenter).isEmpty());
		QVERIFY(presenter.stationOverlays().isEmpty());
	}
}

// A virtual station gets no platform bar, as it gets no overlay.
void TestRuntimeCanvas::virtualStationGetsNoPlatformBar() {
	RunFixture f;
	NetworkPresenter& presenter = f.presenter;
	FakeGeometry* const source = f.source;
	source->passengers = true;
	Node& middle = source->track(0).member[0].endNode;
	middle.stationName = "virtualLink";
	middle.stationPlatformId = "P2";
	presenter.showRuntime(f.model);
	// A virtual station gets no platform bar.
	QCOMPARE(PresenterAccess::allPlatforms(presenter).size(), qsizetype(1));
	QCOMPARE(PresenterAccess::allPlatforms(presenter).value(0)->node, &source->track(0).member[1].endNode);
	presenter.clear();
}

// apply() shows the steps of a run on the items that showRuntime drew, on the clock of the test.
void TestRuntimeCanvas::applyShowsStepsOfRun() {
	RunFixture f;
	QGraphicsScene& scene = f.scene;
	NetworkPresenter& presenter = f.presenter;
	f.source->passengers = true;
	presenter.showRuntime(f.model);
	RecordingHost host;
	presenter.setFrameHost(&host);
	qint64 now = 0;
	presenter.setClock([&now]() { return now; });
	SignalItem* const head = PresenterAccess::signalsByAheadId(presenter)["s0"].front();
	TrackLineItem* const track = PresenterAccess::tracksByOccupiedArc(presenter).at({0, 0.0});
	PlatformItem* const platform = PresenterAccess::allPlatforms(presenter).value(0);
	const auto platformText = [platform]() { return platform->textIcon->toPlainText(); };

	GuiSimulationSnapshot snapshot;
	snapshot.timestep = 10;
	snapshot.trains = {trainAt(7.5)};
	snapshot.trains[0].occupiedArcs = {GuiOccupiedArc{0, 0.0}};
	snapshot.signalStates = {GuiSignalState{"s0", 75, head->reversedDirection, 3, false}};
	snapshot.platforms = {GuiPlatformState{"B", "P1", 40, {"p1", "p2"}}};

	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	// A live step reports the signals, then each train, then the end of the trains to the host.
	QCOMPARE(host.take(), Calls({"signals", "willUpdate 0", "added 0 first", "trains added"}));
	// A train of the step that is not on the canvas is added.
	QCOMPARE(presenter.runTrains().size(), qsizetype(1));
	QVERIFY(bodyAt(presenter, 7.5));
	// The head of a signal shows the state of its section.
	QCOMPARE(head->aspectCode(), 75);
	// A head without a state shows nothing.
	QCOMPARE(presenter.runSignals().size(), qsizetype(2));
	QCOMPARE_NE(presenter.runSignals()[0], presenter.runSignals()[1]);
	QCOMPARE((presenter.runSignals()[0] == head ? presenter.runSignals()[1] : presenter.runSignals()[0])->aspectCode(),
		kGuiSignalUnavailable);
	// The track of an occupied arc shows as occupied.
	QCOMPARE(track->operationalState(), TrackOperationalState::Occupied);
	// The platform shows its passengers.
	QCOMPARE(platform->passengerIcons.size(), qsizetype(2));
	QCOMPARE(platformText(), "2 of 40 passengers");
	// The passengers on the platform show only where its bar shows, with the passenger layer.
	const auto iconsShown = [platform](bool shown) {
		return std::all_of(platform->passengerIcons.cbegin(), platform->passengerIcons.cend(),
			[shown](const PassengerItem* icon) { return icon->isVisible() == shown; });
	};
	// The passengers on a hidden platform bar are hidden.
	QVERIFY(!platform->isVisible());
	QVERIFY(iconsShown(false));
	PresenterAccess::setStationNearLook(presenter, true);
	// They show with the bar in the near look.
	QVERIFY(platform->isVisible());
	QVERIFY(iconsShown(true));
	// The details of a selected passenger go with its icon, also when the next step paints them again.
	PassengerItem* const selected = platform->passengerIcons[0];
	presenter.paintPassengerInfoIcon(selected, passengerStateIn(&snapshot, selected->passengerId), false);
	// The details of a selected passenger show.
	if (PresenterAccess::paxIconInfoItem(presenter) == nullptr)
		QFAIL("the details of the selected passenger are not drawn");
	QVERIFY(PresenterAccess::paxIconInfoItem(presenter)->isVisible());
	PresenterAccess::paxIconInfoItem(presenter)->setData(0, QStringLiteral("kept"));
	PresenterAccess::setStationNearLook(presenter, false);
	presenter.updatePaxIconInfo(&snapshot, NetworkPresenter::Playback::Live);
	// They hide with the icon in the far look. Painting the details again keeps their group.
	if (PresenterAccess::paxIconInfoItem(presenter) == nullptr)
		QFAIL("the details of the selected passenger are gone after the step");
	QVERIFY(!PresenterAccess::paxIconInfoItem(presenter)->isVisible());
	QCOMPARE(PresenterAccess::paxIconInfoItem(presenter)->data(0), QVariant(QStringLiteral("kept")));
	PresenterAccess::setStationNearLook(presenter, true);
	QVERIFY2(PresenterAccess::paxIconInfoItem(presenter)->isVisible(), "they show again with the icon");
	presenter.removePaxInfoIcon();
	presenter.clearSelectedPassengerIcon();
	PresenterAccess::passengerLayerVisible(presenter) = false;
	PresenterAccess::applyPlatformVisibility(presenter);
	// The passenger layer hides them and not the bar.
	QVERIFY(platform->isVisible());
	QVERIFY(iconsShown(false));
	PresenterAccess::passengerLayerVisible(presenter) = true;
	PresenterAccess::stationLayerVisible(presenter) = false;
	PresenterAccess::applyPlatformVisibility(presenter);
	// The station layer hides the bar and its passengers.
	QVERIFY(!platform->isVisible());
	QVERIFY(iconsShown(false));
	PresenterAccess::stationLayerVisible(presenter) = true;
	PresenterAccess::setStationNearLook(presenter, false);

	snapshot.timestep = 11;
	snapshot.trains = {trainAt(8.0)};
	snapshot.signalStates.clear();
	snapshot.platforms[0].passengerIds = {"p1"};
	snapshot.virtualCouplingMessages = {GuiVirtualCouplingState{11, "T1", "message"}};
	host.promoted = true;
	host.cameraMoves = true;
	now = 100;
	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	// A train on the canvas is placed, and the host hears whether the camera moved.
	QCOMPARE(host.take(), Calls({"signals", "willUpdate 0", "placed 0", "trains moved"}));
	// A head keeps no earlier aspect.
	QCOMPARE(head->aspectCode(), kGuiSignalUnavailable);
	// A track that is no longer occupied is free again.
	QCOMPARE(track->operationalState(), TrackOperationalState::Free);
	// The platform shows the passengers that are left.
	QCOMPARE(platform->passengerIcons.size(), qsizetype(1));
	QCOMPARE(platformText(), "1 of 40 passengers");
	// The message of a virtual coupling shows above the train.
	QVERIFY(PresenterAccess::vcMessageItems(presenter).contains(0));
	QVERIFY(PresenterAccess::vcMessageItems(presenter)[0]->isVisible());
	// It shows by itself, so it stays under the station names, and over the train and its speed. The message of a virtual
	// coupling is stacked over the train and its speed and under the station names.
	QVERIFY(PresenterAccess::vcMessageItems(presenter).contains(0));
	if (PresenterAccess::trainBadges(presenter).value(0) == nullptr)
		QFAIL("the train has no badge");
	QCOMPARE_LT(PresenterAccess::vcMessageItems(presenter)[0]->zValue(), StationNameItem::NameZValue);
	QCOMPARE_GT(PresenterAccess::vcMessageItems(presenter)[0]->zValue(), PresenterAccess::trainBadges(presenter).value(0)->zValue());
	QCOMPARE_GT(PresenterAccess::trainBadges(presenter).value(0)->zValue(), presenter.runTrains()[0]->zValue());
	// The host decides whether the badge of a train is promoted.
	QVERIFY(PresenterAccess::trainBadges(presenter).value(0)->isPromoted());
	// A live train glides to its new place, from where it was drawn.
	QVERIFY(presenter.trainMoving(0));
	QVERIFY(bodyAt(presenter, 7.5));
	// The badge goes to the centre of the train and the callouts to its front, as it is drawn at each step.
	presenter.paintTrainPassengerInfo(presenter.runTrains()[0]);
	const auto onTrain = [&presenter](double headKm) {
		const auto at = [](const QGraphicsItem* item, const QPointF& point) {
			return item && QLineF(item->pos(), point).length() < 1e-6;
		};
		const QPointF front(headKm * 1000.0, 0.0);
		return bodyAt(presenter, headKm) && at(PresenterAccess::trainBadges(presenter).value(0), presenter.runTrains()[0]->sceneBoundingRect().center())
			&& at(PresenterAccess::vcMessageItems(presenter).value(0), front) && at(PresenterAccess::trainPaxInfoItem(presenter), front);
	};
	bool withTrain = onTrain(7.5);
	// half way through the glide of 500 ms, five times the interval between the two updates
	now += 250;
	presenter.stepTrains();
	withTrain &= onTrain(7.75) && presenter.trainMoving(0);
	now += 250;
	presenter.stepTrains();
	withTrain &= onTrain(8.0) && !presenter.trainMoving(0);
	QVERIFY2(withTrain, "the badge, the message of a virtual coupling and the passengers on board move with the train");
	presenter.removeTrainPaxInfoIcon();
	presenter.clearSelectedTrain();

	snapshot.timestep = 12;
	snapshot.trains = {trainAt(8.5)};
	snapshot.virtualCouplingMessages.clear();
	presenter.apply(snapshot, NetworkPresenter::Playback::Replay);
	// A replay step reports the same calls.
	QCOMPARE(host.take(), Calls({"signals", "willUpdate 0", "placed 0", "trains moved"}));
	// A replay step places the train without gliding.
	QVERIFY(!presenter.trainsMoving());
	QVERIFY(bodyAt(presenter, 8.5));
	// A message that the step does not carry is hidden.
	QVERIFY(PresenterAccess::vcMessageItems(presenter).contains(0));
	QVERIFY(!PresenterAccess::vcMessageItems(presenter)[0]->isVisible());

	snapshot.trains[0].outOfSimulation = true;
	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	// A train that has left is hidden.
	QCOMPARE(host.take(), Calls({"signals", "willUpdate 0", "hidden 0", "trains"}));
	QVERIFY2(!presenter.runTrains()[0]->isVisible(), "the item of a train that has left is not shown");

	snapshot.trains = {trainAt(7.5)};
	presenter.setFrameHost(nullptr);
	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	// A presenter without a host still shows the step.
	QVERIFY(host.take().empty());
	QVERIFY(presenter.runTrains()[0]->isVisible());
	QVERIFY(bodyAt(presenter, 7.5));

	snapshot.trains = {trainAt(8.0)};
	now += 100;
	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	QVERIFY2(presenter.trainsMoving(), "the train glides on to its next place");
	presenter.clear();
	// Clear stops the motion of the trains.
	QVERIFY(!presenter.trainsMoving());
	QVERIFY(scene.items().isEmpty());
	presenter.setClock({});
}

// A selected train on a track that is not drawn shows no passengers on board, and stays selected.
void TestRuntimeCanvas::trainOnUndrawnTrackShowsNoPassengers() {
	RunFixture f;
	NetworkPresenter& presenter = f.presenter;
	f.source->passengers = true;
	presenter.showRuntime(f.model);
	GuiSimulationSnapshot snapshot;
	snapshot.timestep = 10;
	snapshot.trains = {trainAt(7.5)};
	presenter.apply(snapshot, NetworkPresenter::Playback::Replay);
	TrainItemGroup* const train = presenter.runTrains().value(0);
	if (train == nullptr)
		QFAIL("the step draws no train");
	presenter.paintTrainPassengerInfo(train);
	// A selected train shows its passengers on board.
	QCOMPARE(presenter.selectedTrain(), train);
	QVERIFY(PresenterAccess::trainPaxInfoItem(presenter));
	snapshot.timestep = 11;
	snapshot.trains[0].routeIndex = 2;
	presenter.apply(snapshot, NetworkPresenter::Playback::Replay);
	// A selected train on a track that is not drawn shows no passengers on board.
	QVERIFY(!train->isVisible());
	QCOMPARE(presenter.selectedTrain(), train);
	QVERIFY(!PresenterAccess::trainPaxInfoItem(presenter));
	presenter.paintTrainPassengerInfo(train);
	// A train on a track that is not drawn shows no passengers on board when it is selected.
	QCOMPARE(presenter.selectedTrain(), train);
	QVERIFY(!PresenterAccess::trainPaxInfoItem(presenter));
	presenter.clear();
}

// The passengers on board of a selected train stay in one group from step to step, whose text follows them.
void TestRuntimeCanvas::passengersOnBoardKeepTheirGroup() {
	RunFixture f;
	NetworkPresenter& presenter = f.presenter;
	f.source->passengers = true;
	presenter.showRuntime(f.model);
	GuiSimulationSnapshot snapshot;
	snapshot.timestep = 10;
	snapshot.trains = {trainAt(7.5)};
	snapshot.trains[0].currentOnboardPassengers = 3;
	snapshot.trains[0].maxOnboardPassengers = 40;
	presenter.apply(snapshot, NetworkPresenter::Playback::Replay);
	TrainItemGroup* const train = presenter.runTrains().value(0);
	if (train == nullptr)
		QFAIL("the step draws no train");
	presenter.paintTrainPassengerInfo(train);
	const auto calloutText = [&presenter]() {
		if (PresenterAccess::trainPaxInfoItem(presenter))
			for (QGraphicsItem* child : PresenterAccess::trainPaxInfoItem(presenter)->childItems())
				if (const auto* text = qgraphicsitem_cast<QGraphicsTextItem*>(child))
					return text->toPlainText();
		return QString();
	};
	// A selected train shows its passengers on board.
	QCOMPARE(calloutText(), QStringLiteral("3 of 40 on board"));
	if (PresenterAccess::trainPaxInfoItem(presenter))
		PresenterAccess::trainPaxInfoItem(presenter)->setData(0, QStringLiteral("kept"));
	snapshot.timestep = 11;
	snapshot.trains[0].currentOnboardPassengers = 5;
	presenter.apply(snapshot, NetworkPresenter::Playback::Replay);
	// The next step keeps the group and gives it the new number of passengers.
	QVERIFY(PresenterAccess::trainPaxInfoItem(presenter));
	QCOMPARE(PresenterAccess::trainPaxInfoItem(presenter)->data(0), QVariant(QStringLiteral("kept")));
	QVERIFY(PresenterAccess::trainPaxInfoItem(presenter)->isVisible());
	QCOMPARE(calloutText(), QStringLiteral("5 of 40 on board"));
	presenter.clear();
}

// The tooltip of a head follows its aspect, and a step that keeps the aspect leaves the tooltip as it is.
void TestRuntimeCanvas::headTooltipFollowsAspect() {
	RunFixture f;
	NetworkPresenter& presenter = f.presenter;
	f.source->passengers = true;
	presenter.showRuntime(f.model);
	SignalItem* const head = PresenterAccess::signalsByAheadId(presenter)["s0"].front();
	const auto tip = [head](const QString& state) {
		return QStringLiteral("%1, facing %2\n%3")
			.arg(state, head->reversedDirection ? QStringLiteral("left") : QStringLiteral("right"), head->inspectionIdentity());
	};
	// A head of a run has its tooltip before the first step.
	QCOMPARE(head->toolTip(), tip("Proceed"));
	GuiSimulationSnapshot snapshot;
	snapshot.timestep = 10;
	snapshot.signalStates = {GuiSignalState{"s0", 0, head->reversedDirection, 3, false}};
	presenter.apply(snapshot, NetworkPresenter::Playback::Replay);
	// The tooltip of a head follows its aspect.
	QCOMPARE(head->toolTip(), tip("Stop"));
	head->setToolTip(QStringLiteral("kept"));
	snapshot.timestep = 11;
	presenter.apply(snapshot, NetworkPresenter::Playback::Replay);
	// A step that keeps the aspect of a head does not set its tooltip again.
	QCOMPARE(head->toolTip(), "kept");
	snapshot.signalStates[0].failed = true;
	presenter.apply(snapshot, NetworkPresenter::Playback::Replay);
	// The tooltip of a failed head says so.
	QCOMPARE(head->toolTip(), tip("Failed"));
	presenter.clear();
}

// A step that changes nothing leaves the canvas as it is, and a step that changes a little touches only what it changes.
void TestRuntimeCanvas::stepTouchesOnlyWhatItChanges() {
	RunFixture f;
	QGraphicsScene& scene = f.scene;
	NetworkPresenter& presenter = f.presenter;
	FakeGeometry* const source = f.source;
	source->passengers = true;
	// a second platform, at the middle of the line
	source->track(0).member[0].endNode.stationName = "B";
	source->track(0).member[0].endNode.stationPlatformId = "P2";
	presenter.showRuntime(f.model);
	// The platforms and their passengers show in the near look.
	PresenterAccess::setStationNearLook(presenter, true);
	PresenterAccess::passengerPixmap(presenter) = QPixmap(10, 10);
	const qreal iconSize = PresenterAccess::passengerPixmap(presenter).width() * presenter.presentationScale();
	// The run draws both platforms.
	QCOMPARE(PresenterAccess::allPlatforms(presenter).size(), qsizetype(2));

	GuiSimulationSnapshot snapshot;
	snapshot.timestep = 10;
	snapshot.trains = {trainAt(7.5)};
	snapshot.trains[0].occupiedArcs = {GuiOccupiedArc{0, 0.0}};
	SignalItem* const head = PresenterAccess::signalsByAheadId(presenter)["s0"].front();
	snapshot.signalStates = {GuiSignalState{"s0", 75, head->reversedDirection, 3, false}};
	// listed in the opposite order of the platforms on the canvas
	snapshot.platforms = {GuiPlatformState{"B", "P1", 40, {"p1", "p2", "p3"}}, GuiPlatformState{"B", "P2", 30, {"q1", "q2"}}};
	PlatformItem* const endPlatform = PresenterAccess::allPlatforms(presenter).value(1);
	PlatformItem* const middlePlatform = PresenterAccess::allPlatforms(presenter).value(0);
	// The platforms are in the order of the line.
	QCOMPARE(endPlatform->platformId, "P1");
	QCOMPARE(middlePlatform->platformId, "P2");

	SceneChanges changes(scene);
	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	// A step that adds items is announced by the scene.
	QCOMPARE_GT(changes.take(), 0);
	// Each platform shows an icon for each of its passengers.
	QCOMPARE(iconIds(*endPlatform), Ids({"p1", "p2", "p3"}));
	QCOMPARE(iconIds(*middlePlatform), Ids({"q1", "q2"}));
	// The icons are painted in the order of the platforms, whatever the order of the snapshot.
	QCOMPARE(paintedIcons(scene), Ids({"q1", "q2", "p1", "p2", "p3"}));
	QCOMPARE(paintedIcons(scene), expectedIcons(presenter));

	// the same step again
	int marked = markItems(scene);
	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	ItemChanges itemsNow = itemChanges(scene, marked);
	// A step that changes nothing adds and removes no item.
	QCOMPARE(itemsNow.added, 0);
	QCOMPARE(itemsNow.removed, 0);
	// A step that changes nothing asks for no item to be painted again.
	QCOMPARE(changes.take(), 0);

	// a passenger boards: only its icon goes and the others close up
	PassengerItem* const firstIcon = endPlatform->passengerIcons[0];
	PassengerItem* const lastIcon = endPlatform->passengerIcons[2];
	PresenterAccess::paxIconItem(presenter) = firstIcon;
	snapshot.timestep = 11;
	snapshot.platforms[0].passengerIds = {"p1", "p3"};
	marked = markItems(scene);
	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	itemsNow = itemChanges(scene, marked);
	// A passenger who boards takes one icon away and adds none.
	QCOMPARE(itemsNow.added, 0);
	QCOMPARE(itemsNow.removed, 1);
	// The icons of the other passengers stay.
	QCOMPARE(endPlatform->passengerIcons.size(), qsizetype(2));
	QCOMPARE(endPlatform->passengerIcons[0], firstIcon);
	QCOMPARE(endPlatform->passengerIcons[1], lastIcon);
	// The icons that stay share the width of the platform again.
	QVERIFY(iconAt(*endPlatform, 0, 2, iconSize));
	QVERIFY(iconAt(*endPlatform, 1, 2, iconSize));
	// The selected icon stays selected while its passenger is there.
	QCOMPARE(PresenterAccess::paxIconItem(presenter), firstIcon);
	// A change is announced.
	QCOMPARE_GT(changes.take(), 0);

	// passengers arrive, one of them between the two
	snapshot.timestep = 12;
	snapshot.platforms[0].passengerIds = {"p1", "p4", "p3", "p5"};
	marked = markItems(scene);
	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	itemsNow = itemChanges(scene, marked);
	// Passengers who arrive add one icon each.
	QCOMPARE(itemsNow.added, 2);
	QCOMPARE(itemsNow.removed, 0);
	// The icons of the passengers who were there stay.
	QCOMPARE(iconIds(*endPlatform), Ids({"p1", "p4", "p3", "p5"}));
	QCOMPARE(endPlatform->passengerIcons[0], firstIcon);
	QCOMPARE(endPlatform->passengerIcons[2], lastIcon);
	// Icons that are added are painted in the place of their passenger.
	QCOMPARE(paintedIcons(scene), Ids({"q1", "q2", "p1", "p4", "p3", "p5"}));
	QCOMPARE(paintedIcons(scene), expectedIcons(presenter));
	for (int i = 0; i < 4; ++i)
		QVERIFY2(iconAt(*endPlatform, i, 4, iconSize), "the icons share the width of the platform");

	// the passengers swap places
	snapshot.timestep = 13;
	snapshot.platforms[0].passengerIds = {"p5", "p3", "p1", "p4"};
	marked = markItems(scene);
	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	itemsNow = itemChanges(scene, marked);
	// Passengers who swap places keep their icons.
	QCOMPARE(itemsNow.added, 0);
	QCOMPARE(itemsNow.removed, 0);
	// The icons are painted in the new order.
	QCOMPARE(iconIds(*endPlatform), Ids({"p5", "p3", "p1", "p4"}));
	QCOMPARE(endPlatform->passengerIcons[2], firstIcon);
	QCOMPARE(paintedIcons(scene), Ids({"q1", "q2", "p5", "p3", "p1", "p4"}));
	// The icons move to their new places.
	QVERIFY(iconAt(*endPlatform, 0, 4, iconSize));
	QVERIFY(iconAt(*endPlatform, 2, 4, iconSize));

	// the platform that is not in the snapshot keeps what it shows
	snapshot.timestep = 14;
	snapshot.platforms.pop_back();
	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	// A platform that the snapshot does not list keeps its icons.
	QCOMPARE(iconIds(*middlePlatform), Ids({"q1", "q2"}));

	// nothing of the passengers is drawn while their layer is hidden, and the layer shows the step that is current when it is shown
	const QString middleText = middlePlatform->textIcon->toPlainText();
	const QString endText = endPlatform->textIcon->toPlainText();
	PresenterAccess::passengerLayerVisible(presenter) = false;
	snapshot.timestep = 15;
	snapshot.platforms = {GuiPlatformState{"B", "P1", 40, {"p9"}}, GuiPlatformState{"B", "P2", 30, {}}};
	marked = markItems(scene);
	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	itemsNow = itemChanges(scene, marked);
	// A hidden passenger layer leaves the platforms as they are.
	QCOMPARE(itemsNow.added, 0);
	QCOMPARE(itemsNow.removed, 0);
	QCOMPARE(endPlatform->textIcon->toPlainText(), endText);
	QCOMPARE(middlePlatform->textIcon->toPlainText(), middleText);
	// A hidden layer keeps the icons of the earlier step.
	QCOMPARE(iconIds(*endPlatform).size(), std::size_t(4));
	QCOMPARE(iconIds(*middlePlatform).size(), std::size_t(2));
	PresenterAccess::passengerLayerVisible(presenter) = true;
	presenter.updatePaxIconInfo(&snapshot, NetworkPresenter::Playback::Live);
	// The layer that is shown again has the icons of the current step.
	QCOMPARE(iconIds(*endPlatform), Ids({"p9"}));
	QVERIFY(middlePlatform->passengerIcons.isEmpty());
	// The layer that is shown again has the counters of the current step.
	QCOMPARE(endPlatform->textIcon->toPlainText(), "1 of 40 passengers");
	QCOMPARE(middlePlatform->textIcon->toPlainText(), "0 of 30 passengers");
	// The selected icon is forgotten when its passenger has left.
	QCOMPARE(PresenterAccess::paxIconItem(presenter), nullptr);
	// No icon of a passenger who left is left on the canvas.
	QCOMPARE(paintedIcons(scene), Ids({"p9"}));
	marked = markItems(scene);
	presenter.apply(snapshot, NetworkPresenter::Playback::Replay);
	itemsNow = itemChanges(scene, marked);
	// A platform that is up to date is not drawn again.
	QCOMPARE(itemsNow.added, 0);
	QCOMPARE(itemsNow.removed, 0);
	presenter.clear();
	QVERIFY2(scene.items().isEmpty(), "clear leaves nothing of the run");
}

// A live train glides along the drawn line of its track from where it is drawn, and every step draws it wagon by wagon
// where it is then, so every corner of every body stays on the line around a bend too. The host hears where it is drawn
// at every step.
void TestRuntimeCanvas::trainGlidesAlongBentLine() {
	QGraphicsScene bentScene;
	NetworkView bentView;
	bentView.setScene(&bentScene);
	bentView.resize(400, 300);
	NetworkPresenter bent(&bentScene, &bentView);
	auto geometry = std::make_unique<FakeGeometry>();
	geometry->track(0).sceneTrackId = "T";
	bent.setGeometrySource(std::move(geometry));
	SceneModel bentModel;
	bentModel.tracks = {{"T"}};
	bentModel.nodes = {{"T.a", "T", 0.0, 0.0}, {"T.b", "T", 4.8, 2.0}, {"T.c", "T", 10.0, 0.0}};
	bentModel.arcs = {{"T.arc.1", "T", "T.a", "T.b", 0.0, 0.0, 12.0}, {"T.arc.2", "T", "T.b", "T.c", 0.0, 0.0, 12.0}};
	bentModel.blocks = {{"s0", "T", 5.0}, {"s1", "T", 5.0}};
	// The run draws the track with the line of the preview.
	bent.showPreview(bentModel);
	bent.showRuntime(bentModel);
	bentView.fitToBounds(bentScene.itemsBoundingRect());
	RecordingHost host;
	host.promoted = true;
	bent.setFrameHost(&host);
	qint64 now = 0;
	bent.setClock([&now]() { return now; });
	const TrackPreviewLine* const line = bent.trackLine("T");
	const auto turn = [line]() {
		const QLineF first(line->points[0].x, line->points[0].y, line->points[1].x, line->points[1].y);
		const QLineF second(line->points[1].x, line->points[1].y, line->points[2].x, line->points[2].y);
		return first.angleTo(second);
	};
	// The line of the track bends at its middle node.
	if (line == nullptr)
		QFAIL("the run draws no line of track T");
	QCOMPARE(line->points.size(), std::size_t(3));
	QCOMPARE_GT(turn(), 20.0);
	QCOMPARE_LT(turn(), 340.0);
	// half the width of a body, and a hundredth of it
	const qreal half = 0.10 * bent.track_separation * bent.presentationScale();
	const qreal close = 0.01 * half;

	GuiSimulationSnapshot snapshot;
	snapshot.timestep = 10;
	const auto deliver = [&](double headKm) {
		snapshot.timestep += 1;
		snapshot.trains = {trainOfWagons(3, headKm)};
		bent.apply(snapshot, NetworkPresenter::Playback::Live);
	};
	deliver(4.5);
	TrainItemGroup* const train = bent.trainGroup(0);
	// The train of three wagons is drawn on the bent line.
	if (train == nullptr)
		QFAIL("the step draws no train");
	QVERIFY(train->isVisible());
	QCOMPARE(train->trainPolygonItemList->size(), qsizetype(3));
	const std::vector<QLineF> drawnLine = segmentsOf(*line);
	// The furthest that a corner of a body gets from the line, whether the host heard the centre of the train as it is
	// drawn, and whether every step moved the train. While the whole train is on one leg of the line, that centre is on
	// the line at the middle of its bodies. Around the bend it is the centre of the two pieces of line from the tail to
	// the bend and from the bend to the head, inside the bend and off the line, as the middle of the bodies is, no
	// further than half the width of a body from it.
	qreal farthest = 0.0;
	bool heard = true;
	bool everyStep = true;
	const auto stepBy = [&](qint64 ms) {
		now += ms;
		const QPointF before = bent.trainCenter(0);
		host.moves.clear();
		bent.stepTrains();
		farthest = std::max(farthest, farthestCorner(*train, drawnLine));
		const QPointF center = bent.trainCenter(0);
		const QPointF back = drawnBack(*train);
		const QPointF front = drawnFront(*train);
		const auto onLeg = [&](const QLineF& leg) { return distanceTo({leg}, front) <= close && distanceTo({leg}, back) <= close; };
		const bool oneLeg = onLeg(drawnLine[0]) || onLeg(drawnLine[1]);
		const qreal fromBodies = QLineF(drawnCentre(*train), center).length();
		const QPointF bend = drawnLine[0].p2();
		const qreal toBend = QLineF(back, bend).length();
		const qreal fromBend = QLineF(bend, front).length();
		const QPointF bendCentre = ((back + bend) * toBend + (bend + front) * fromBend) / (2.0 * (toBend + fromBend));
		heard &= host.moves.size() == 1 && QLineF(host.moves.back(), center).length() <= close
			&& (oneLeg ? distanceTo(drawnLine, center) <= close && fromBodies <= close
					   : QLineF(bendCentre, center).length() <= close && fromBodies <= half);
		everyStep &= QLineF(center, before).length() > close;
	};
	const auto headAt = [&](double headKm) { return QLineF(drawnFront(*train), linePointAt(*line, headKm)).length() <= close; };

	// The glide from 4.5 km to 5.7 km lasts five times the update interval, 500 ms, at an even speed, and starts where
	// the train is drawn. Its first 200 ms take the head around the bend at 4.8 km.
	now = 100;
	deliver(5.7);
	// The glide starts where the train is drawn, and the host hears that place.
	QVERIFY(bent.trainMoving(0));
	QVERIFY(headAt(4.5));
	QCOMPARE_LE(QLineF(host.placedAt, bent.trainCenter(0)).length(), close);
	QCOMPARE_LE(distanceTo(drawnLine, host.placedAt), close);
	bool even = true;
	for (int ms = 5; ms <= 200; ms += 5) {
		stepBy(5);
		even &= headAt(4.5 + 1.2 * ms / 500.0);
	}
	QVERIFY2(even, "the head of a gliding train moves along the line at an even speed");

	// An update 200 ms after the last one, during the glide, starts the next glide where the train is drawn. Of the
	// intervals of 100 and 200 ms the glide takes five times the shorter one, 500 ms.
	now = 300;
	deliver(6.3);
	QVERIFY2(headAt(4.5 + 1.2 * 200.0 / 500.0), "a new update during a glide continues from where the train is drawn");
	for (int ms = 5; ms <= 495; ms += 5)
		stepBy(5);
	const bool onItsWay = bent.trainMoving(0);
	stepBy(5);
	// The next glide lasts 500 ms and ends at the new place.
	QVERIFY(onItsWay);
	QVERIFY(!bent.trainMoving(0));
	QVERIFY(headAt(6.3));
	// Every corner of every body stays on the drawn line at every step around the bend.
	QCOMPARE_LE(farthest, half + close);
	QVERIFY2(heard,
		"the host hears the centre of the train as it is drawn at every step: on the line on one leg, and at the "
		"centre of the line under it around the bend");
	QVERIFY2(everyStep, "every step of a glide moves the train");

	// Updates every 40 ms, as at Fastest, each 10 ms after a step. Once the longer intervals are no longer among the last
	// eight but one, a glide lasts 200 ms, so every update finds the train on its way and every step moves it. A glide
	// goes on from the step before its update, so the last one ends 190 ms after its update.
	double head = 6.3;
	bool eachUpdate = true;
	for (int update = 0; update < 12; ++update) {
		now += 10;
		if (update > 0)
			eachUpdate &= bent.trainMoving(0);
		head += 0.1;
		deliver(head);
		for (int step = 0; step < 3; ++step)
			stepBy(10);
	}
	now += 150;
	bent.stepTrains();
	eachUpdate &= bent.trainMoving(0);
	now += 10;
	bent.stepTrains();
	// At updates every 40 ms the train moves at every step, and a glide lasts 200 ms.
	QVERIFY(eachUpdate);
	QVERIFY(everyStep);
	QVERIFY(!bent.trainMoving(0));
	QVERIFY(headAt(head));

	// A pause is not an update interval.
	now += 3000;
	head += 0.1;
	deliver(head);
	now += 199;
	bent.stepTrains();
	const bool beforeEnd = bent.trainMoving(0);
	now += 1;
	bent.stepTrains();
	// The update after a pause glides as long as the ones before.
	QVERIFY(beforeEnd);
	QVERIFY(!bent.trainMoving(0));

	// An event loop too busy to run the steps does not hold the train back: the next update draws it where its glide is
	// by the clock, a fifth of the way.
	const double standing = head;
	now += 40;
	head += 0.1;
	deliver(head);
	now += 40;
	deliver(head + 0.1);
	// An update after steps that did not run draws the train where its glide is by the clock.
	QVERIFY(headAt(standing + 0.1 * 40.0 / 200.0));
	QCOMPARE_LE(QLineF(host.placedAt, bent.trainCenter(0)).length(), close);
	head += 0.1;

	// A train that runs backwards, and one that jumps further than the visible view, is placed at once.
	now += 40;
	deliver(head - 0.1);
	// A train that moves backwards is placed at once.
	QVERIFY(!bent.trainMoving(0));
	QVERIFY(headAt(head - 0.1));
	bentView.zoomBy(40.0);
	now += 40;
	deliver(head + 0.9);
	// A train that jumps further than the visible view is placed at once.
	QVERIFY(!bent.trainMoving(0));
	QVERIFY(headAt(head + 0.9));

	// A train that moves half the visible view at every update glides on, although after a few updates it is drawn more
	// than the visible view behind its newest place: the jump is measured from the place of the last update.
	head += 0.9;
	const QRectF visible = bentView.mapToScene(bentView.viewport()->rect()).boundingRect();
	const qreal view = std::max(visible.width(), visible.height());
	const qreal perKm = QLineF(linePointAt(*line, 10.0), linePointAt(*line, 4.8)).length() / 5.2;
	const double move = 0.5 * view / perKm;
	bool glidesOn = true;
	for (int update = 0; update < 5; ++update) {
		now += 40;
		head += move;
		deliver(head);
		glidesOn &= bent.trainMoving(0);
	}
	// A train that moves less than the visible view at every update glides, however far behind it is drawn.
	QCOMPARE_LT(head, 10.0);
	QVERIFY(glidesOn);
	QCOMPARE_GT(QLineF(drawnFront(*train), linePointAt(*line, head)).length(), view);
	bent.setFrameHost(nullptr);
	bent.setClock({});
}

// Through a crossover from one track to another every step draws the train where it is too, and every corner of every
// body stays on the line it runs on: the first track up to the switch, the connection and the second track after it.
void TestRuntimeCanvas::trainRunsOverCrossover() {
	QGraphicsScene crossScene;
	NetworkView crossView;
	crossView.setScene(&crossScene);
	crossView.resize(400, 300);
	NetworkPresenter cross(&crossScene, &crossView);
	auto geometry = std::make_unique<FakeGeometry>();
	geometry->makeCrossover();
	cross.setGeometrySource(std::move(geometry));
	SceneModel crossModel;
	crossModel.tracks = {{"T"}, {"U"}};
	crossModel.nodes = {{"T.a", "T", 0.0, 0.0}, {"T.b", "T", 10.0, 0.0}, {"U.a", "U", 0.0, 1.0}, {"U.b", "U", 10.0, 1.0}};
	crossModel.arcs = {{"T.arc", "T", "T.a", "T.b", 0.0, 0.0, 12.0}, {"U.arc", "U", "U.a", "U.b", 0.0, 0.0, 12.0}};
	cross.showPreview(crossModel);
	cross.showRuntime(crossModel);
	crossView.fitToBounds(crossScene.itemsBoundingRect());
	RecordingHost host;
	host.promoted = true;
	cross.setFrameHost(&host);
	qint64 now = 0;
	cross.setClock([&now]() { return now; });
	const TrackPreviewLine* const first = cross.trackLine("T");
	const TrackPreviewLine* const second = cross.trackLine("U");
	const qreal half = 0.10 * cross.track_separation * cross.presentationScale();
	const qreal close = 0.01 * half;
	// The two tracks of the crossover are drawn apart.
	if (first == nullptr || second == nullptr)
		QFAIL("the run draws no line of both tracks");
	QCOMPARE_GT(QLineF(linePointAt(*first, 5.0), linePointAt(*second, 5.0)).length(), 20.0 * half);
	GuiSimulationSnapshot snapshot;
	snapshot.timestep = 10;
	snapshot.trains = {trainOfWagons(3, 3.2)};
	cross.apply(snapshot, NetworkPresenter::Playback::Live);
	TrainItemGroup* const train = cross.trainGroup(0);
	// The train of three wagons is drawn on the first track.
	if (train == nullptr)
		QFAIL("the step draws no train");
	QVERIFY(train->isVisible());
	QCOMPARE(train->trainPolygonItemList->size(), qsizetype(3));
	std::vector<QLineF> runsOn = segmentsOf(*first);
	for (const QLineF& segment : segmentsOf(*second))
		runsOn.push_back(segment);
	runsOn.emplace_back(linePointAt(*first, 4.0), linePointAt(*second, 6.0));
	qreal farthest = farthestCorner(*train, runsOn);
	bool centred = true;
	// Updates every 250 ms, 1.2 km apart, take the train from 3.2 km, on the first track, to 8 km, on the second.
	for (int update = 1; update <= 4; ++update) {
		for (int ms = 5; ms <= 250; ms += 5) {
			now += 5;
			if (ms == 250) {
				snapshot.timestep += 1;
				snapshot.trains = {trainOfWagons(3, 3.2 + 1.2 * update)};
				cross.apply(snapshot, NetworkPresenter::Playback::Live);
			} else {
				cross.stepTrains();
			}
			farthest = std::max(farthest, farthestCorner(*train, runsOn));
			centred &= QLineF(drawnCentre(*train), cross.trainCenter(0)).length() <= close;
		}
	}
	while (cross.trainMoving(0)) {
		now += 5;
		cross.stepTrains();
		farthest = std::max(farthest, farthestCorner(*train, runsOn));
		centred &= QLineF(drawnCentre(*train), cross.trainCenter(0)).length() <= close;
	}
	// The train runs over the crossover to its place on the second track.
	QCOMPARE_LE(QLineF(drawnFront(*train), linePointAt(*second, 8.0)).length(), close);
	// Every corner of every body stays on the line through the crossover at every step.
	QCOMPARE_LE(farthest, half + close);
	QVERIFY2(centred, "the centre of the train is the centre of its drawn bodies through the crossover");
	cross.setFrameHost(nullptr);
	cross.setClock({});
}

// Where two sections of a route lie on two tracks drawn side by side, the drawn path steps from one track to the other at
// the single position where the sections meet. The centre of a train that runs over that join in steps of 5 m moves at
// every step about as far as the train, without a jump to the other track, and stays at the centre of its drawn bodies.
void TestRuntimeCanvas::trainCentreStepsOverJoin() {
	QGraphicsScene joinScene;
	NetworkView joinView;
	joinView.setScene(&joinScene);
	joinView.resize(400, 300);
	NetworkPresenter join(&joinScene, &joinView);
	auto geometry = std::make_unique<FakeGeometry>();
	geometry->makeJoin();
	join.setGeometrySource(std::move(geometry));
	SceneModel joinModel;
	joinModel.tracks = {{"T"}, {"U"}};
	// U runs 40 m beside T: further than the width of a body, and less than half a wagon.
	joinModel.nodes = {{"T.a", "T", 0.0, 0.0}, {"T.b", "T", 10.0, 0.0}, {"U.a", "U", 0.0, 0.04}, {"U.b", "U", 10.0, 0.04}};
	joinModel.arcs = {{"T.arc", "T", "T.a", "T.b", 0.0, 0.0, 12.0}, {"U.arc", "U", "U.a", "U.b", 0.0, 0.0, 12.0}};
	join.showPreview(joinModel);
	join.showRuntime(joinModel);
	joinView.fitToBounds(joinScene.itemsBoundingRect());
	const TrackPreviewLine* const first = join.trackLine("T");
	const TrackPreviewLine* const second = join.trackLine("U");
	const qreal half = 0.10 * join.track_separation * join.presentationScale();
	const qreal close = 0.01 * half;
	GuiSimulationSnapshot snapshot;
	snapshot.timestep = 10;
	// A replay frame that places the train of three wagons, 300 m long, with its head the given metres past 4.8 km.
	const auto place = [&](int metres) {
		snapshot.timestep += 1;
		snapshot.trains = {trainOfWagons(3, 4.8 + metres / 1000.0)};
		join.apply(snapshot, NetworkPresenter::Playback::Replay);
	};
	place(0);
	const TrainItemGroup* const train = join.trainGroup(0);
	// The train of three wagons is drawn before the join.
	if (first == nullptr || second == nullptr)
		QFAIL("the run draws no line of both tracks");
	if (train == nullptr)
		QFAIL("the step draws no train");
	QVERIFY(train->isVisible());
	// A step of 5 m moves the centre 5 m along the tracks and, while the join is under the train, sideways by 5/300 of
	// the distance between the tracks, as 5 m of the 300 m of the train pass from one track to the other. A jump to the
	// other track would move it the whole distance at once.
	const qreal step = QLineF(linePointAt(*first, 4.8), linePointAt(*first, 4.805)).length();
	const qreal side = QLineF(linePointAt(*first, 5.0), linePointAt(*second, 5.0)).length();
	const qreal length = QLineF(linePointAt(*first, 4.8), linePointAt(*first, 5.1)).length();
	const qreal most = step * (1.0 + side / length) + close;
	// The tracks of the join are drawn further apart than a body is wide and than the train moves in a step.
	QCOMPARE_GT(side, 2.0 * half);
	QCOMPARE_GT(side, 5.0 * most);
	qreal farthestMove = 0.0;
	qreal farthestFromBodies = 0.0;
	QPointF center = join.trainCenter(0);
	for (int metres = 5; metres <= 600; metres += 5) {
		place(metres);
		const QPointF next = join.trainCenter(0);
		farthestMove = std::max(farthestMove, QLineF(center, next).length());
		farthestFromBodies = std::max(farthestFromBodies, QLineF(drawnCentre(*train), next).length());
		center = next;
	}
	// The train runs over the join onto the second track.
	QCOMPARE_LE(QLineF(drawnFront(*train), linePointAt(*second, 5.4)).length(), close);
	// The centre of the train moves about as far as the train at every step over the join.
	QCOMPARE_LE(farthestMove, most);
	// Only the wagon over the join is drawn across from one track to the other, so the middle of the bodies is off the
	// track under the train by about a sixth of the distance between the tracks. The centre of the train stays at the
	// middle of its drawn bodies over the join.
	QCOMPARE_LE(farthestFromBodies, half);
}

// A train that the host does not follow is drawn again at a step only while it is near the visible scene, within as far
// again on every side.
void TestRuntimeCanvas::drawsOnlyTrainsNearVisibleScene() {
	QGraphicsScene farScene;
	NetworkView farView;
	farView.setScene(&farScene);
	farView.resize(400, 300);
	// The view is laid out, and no event loop runs, so no paint changes the clock of the trains.
	farView.show();
	NetworkPresenter far(&farScene, &farView);
	auto geometry = std::make_unique<FakeGeometry>();
	geometry->track(0).sceneTrackId = "T";
	far.setGeometrySource(std::move(geometry));
	SceneModel farModel;
	farModel.tracks = {{"T"}};
	farModel.nodes = {{"T.a", "T", 0.0, 0.0}, {"T.b", "T", 10.0, 0.0}};
	farModel.arcs = {{"T.arc", "T", "T.a", "T.b", 0.0, 0.0, 12.0}};
	far.showPreview(farModel);
	far.showRuntime(farModel);
	farView.fitToBounds(farScene.itemsBoundingRect());
	RecordingHost host;
	far.setFrameHost(&host);
	qint64 now = 0;
	far.setClock([&now]() { return now; });
	const TrackPreviewLine* const line = far.trackLine("T");
	const qreal close = 0.001 * far.track_separation * far.presentationScale();
	GuiSimulationSnapshot snapshot;
	snapshot.timestep = 10;
	const auto deliver = [&](double headKm) {
		snapshot.timestep += 1;
		snapshot.trains = {trainOfWagons(3, headKm)};
		far.apply(snapshot, NetworkPresenter::Playback::Live);
	};
	// a glide of 500 ms from 4.5 km to 4.8 km
	deliver(4.5);
	now = 100;
	deliver(4.8);
	TrainItemGroup* const train = far.trainGroup(0);
	// The train that is not followed glides.
	if (line == nullptr)
		QFAIL("the run draws no line of track T");
	if (train == nullptr)
		QFAIL("the step draws no train");
	QVERIFY(far.trainMoving(0));
	// The view shows about 0.5 km around 0.5 km.
	farView.zoomBy(20.0);
	farView.centerOnExactly(linePointAt(*line, 0.5));
	const QPointF before = drawnFront(*train);
	now += 50;
	far.stepTrains();
	// A step does not draw a train far outside the visible scene again.
	QCOMPARE_LE(QLineF(drawnFront(*train), before).length(), close);
	QVERIFY(far.trainMoving(0));
	farView.centerOnExactly(linePointAt(*line, 4.6));
	now += 25;
	far.stepTrains();
	// A step draws it where it is by the clock once it is near the visible scene.
	QCOMPARE_LE(QLineF(drawnFront(*train), linePointAt(*line, 4.5 + 0.3 * 75.0 / 500.0)).length(), close);
	// The update after a step that left the train out draws it where its glide is by the clock.
	farView.centerOnExactly(linePointAt(*line, 0.5));
	now += 10;
	far.stepTrains();
	now += 10;
	deliver(5.0);
	// An update draws a train that the last step left out where its glide is by the clock.
	QCOMPARE_LE(QLineF(drawnFront(*train), linePointAt(*line, 4.5 + 0.3 * 95.0 / 500.0)).length(), close);
	far.setFrameHost(nullptr);
	far.setClock({});
}

// A train enters at the start of its route, at 0 km, and runs 20 m in every update of 100 ms. Its middle reaches the
// start of the route between the second and the third update, and its tail at the fifth. Until then only its head is
// drawn.
void TestRuntimeCanvas::enteringTrainGlidesOnward() {
	MotionFixture f;
	NetworkPresenter& presenter = f.presenter;
	qint64& now = f.now;
	f.update(0.0);
	bool glides = true;
	bool headSteady = true;
	bool centreOnward = true;
	double centre = presenter.trainCenter(0).x();
	const auto centreMovesOn = [&]() {
		const double next = presenter.trainCenter(0).x();
		centreOnward &= next >= centre - 1e-9;
		centre = next;
	};
	const auto stepFor = [&](int ms) {
		for (int elapsed = 10; elapsed <= ms; elapsed += 10) {
			now += 10;
			presenter.stepTrains();
			centreMovesOn();
		}
	};
	for (int step = 1; step <= 8; ++step) {
		stepFor(100);
		const double head = f.drawnHead();
		f.update(0.02 * step);
		glides &= presenter.trainMoving(0);
		headSteady &= std::fabs(f.drawnHead() - head) < 1e-6;
		centreMovesOn();
	}
	stepFor(500);
	// A train that enters its route glides from where it is drawn at every update.
	QVERIFY(glides);
	QVERIFY(headSteady);
	// The centre of a train that enters its route never moves back, also when its middle reaches the start.
	QVERIFY(centreOnward);
	QVERIFY(near(centre, 110.0));
	QVERIFY(!presenter.trainMoving(0));
}

// A glide that starts at the end of a section, where the next section begins, starts where the train is drawn and goes
// on into the next section: a tenth of the way through its 500 ms after 50 ms.
void TestRuntimeCanvas::glideStartsAtSectionEnd() {
	MotionFixture f;
	NetworkPresenter& presenter = f.presenter;
	f.update(5.05);
	const double boundary = drawnMiddle(*presenter.runTrains()[0]).x();
	f.now += 100;
	f.update(5.15);
	bool boundaryGlide = presenter.trainMoving(0) && near(boundary, 5000.0) && near(drawnMiddle(*presenter.runTrains()[0]).x(), boundary);
	f.now += 50;
	presenter.stepTrains();
	boundaryGlide &= near(drawnMiddle(*presenter.runTrains()[0]).x(), 5000.0 + 100.0 / 10.0);
	QVERIFY2(boundaryGlide, "a glide that starts at the end of a section starts where the train is drawn");
}

// At a steady 200 ms between updates, steps of 10 ms move the drawn middle by about the same distance, from one glide to
// the next. A glide of 600 ms spans three updates, so the train takes a few updates to reach its speed after it
// appears: the steps of the first five updates are left out.
void TestRuntimeCanvas::steadyRhythmGivesEvenSpeed() {
	MotionFixture f;
	NetworkPresenter& presenter = f.presenter;
	std::vector<double> steps;
	double previous = 0.0;
	for (int tick = 0; tick <= 300; ++tick) {
		f.now = tick * 10;
		// The steps run at every tick, as their timer runs whatever the updates do, and an update comes just after the
		// step of its tick.
		presenter.stepTrains();
		if (tick % 20 == 0)
			f.update(1.0 + 0.04 * (tick / 20.0));
		const double middle = drawnMiddle(*presenter.runTrains()[0]).x();
		if (tick > 100)
			steps.push_back(middle - previous);
		previous = middle;
	}
	const auto [smallest, largest] = std::minmax_element(steps.begin(), steps.end());
	// At a steady rhythm a gliding train moves at an even speed across the updates.
	QVERIFY(!steps.empty());
	QCOMPARE_GT(*smallest, 0.0);
	QCOMPARE_LE(*largest, 1.25 * *smallest);
}

// With irregular gaps between the updates the drawn middle never moves back.
void TestRuntimeCanvas::irregularGapsNeverMoveTrainBack() {
	MotionFixture f;
	NetworkPresenter& presenter = f.presenter;
	f.update(1.0);
	bool onward = true;
	double previous = drawnMiddle(*presenter.runTrains()[0]).x();
	double headKm = 1.0;
	for (const int gap : {100, 60, 250, 30, 170, 90, 400, 40, 120, 200, 20, 300}) {
		for (int ms = 10; ms <= gap; ms += 10) {
			f.now += 10;
			if (ms == gap) {
				headKm += 0.01;
				f.update(headKm);
			} else {
				presenter.stepTrains();
			}
			const double middle = drawnMiddle(*presenter.runTrains()[0]).x();
			onward &= middle >= previous - 1e-9;
			previous = middle;
		}
	}
	QVERIFY2(onward, "with irregular gaps between the updates a gliding train never moves back");
}

// One gap of 900 ms, after a slow frame, in a rhythm of 33 ms leaves the glides at five times the rhythm, 165 ms. The
// glide after the gap starts at 1.04 km, where the train stands. The update after the next step leaves the train where
// that step drew it, and its glide goes on from there from the time of that step.
void TestRuntimeCanvas::longGapDoesNotSlowGlides() {
	MotionFixture f;
	NetworkPresenter& presenter = f.presenter;
	qint64& now = f.now;
	f.update(1.0);
	for (int step = 1; step <= 4; ++step) {
		now += 33;
		f.update(1.0 + 0.01 * step);
	}
	now += 900;
	f.update(1.05);
	now += 10;
	presenter.stepTrains();
	const double from = 1040.0 + 10.0 * 10.0 / 165.0;
	const bool afterGap = near(f.drawnHead(), from);
	now += 23;
	f.update(1.06);
	const bool stays = near(f.drawnHead(), from);
	now += 10;
	presenter.stepTrains();
	// One long gap between updates does not slow the glides that follow it.
	QVERIFY(afterGap);
	QVERIFY(stays);
	QVERIFY(near(f.drawnHead(), from + (1060.0 - from) * 33.0 / 165.0));
}

// An update that comes while a train glides, between two steps, leaves the train where the last step drew it. The next
// step moves it on from there at the speed of the glide that the update starts, as far as the time since the step
// before the update, so every move on screen comes from a step. The train runs 10 m in each update of 40 ms, so its
// glides last 200 ms, and the steps come every 16 ms, between the updates.
void TestRuntimeCanvas::updateLeavesGlidingTrainInPlace() {
	MotionFixture f;
	NetworkPresenter& presenter = f.presenter;
	qint64& now = f.now;
	double headKm = 1.0;
	f.update(headKm);
	bool updateKeepsPlace = true;
	bool stepAtGlideSpeed = true;
	int checkedUpdates = 0;
	qint64 lastStep = 0;
	qint64 nextStep = 8;
	for (int count = 1; count <= 25; ++count) {
		for (; nextStep < 40 * count; nextStep += 16) {
			now = nextStep;
			presenter.stepTrains();
			lastStep = nextStep;
		}
		now = 40 * count;
		const double before = f.drawnHead();
		const QPointF centreBefore = presenter.trainCenter(0);
		headKm += 0.01;
		f.update(headKm);
		if (count < 10)
			continue;
		++checkedUpdates;
		const double after = f.drawnHead();
		updateKeepsPlace &= after == before && presenter.trainCenter(0) == centreBefore;
		now = nextStep;
		presenter.stepTrains();
		stepAtGlideSpeed &=
			near(f.drawnHead(), after + (headKm * 1000.0 - after) * static_cast<double>(nextStep - lastStep) / 200.0);
		lastStep = nextStep;
		nextStep += 16;
	}
	// An update leaves a gliding train where the last step drew it, and the next step moves it on at the new speed.
	QCOMPARE(checkedUpdates, 16);
	QVERIFY(updateKeepsPlace);
	QVERIFY(stepAtGlideSpeed);
}

// The timer of the steps runs on the event loop at its own pace while the updates come, here every 5 ms, more often than
// its 16 ms, on a clock that runs. The updates do not hold its steps off: in 600 ms its steps, about 36, move the train
// between two updates at least 6 times.
void TestRuntimeCanvas::updatesDoNotHoldOffSteps() {
	MotionFixture f;
	QElapsedTimer running;
	running.start();
	f.presenter.setClock([&running]() { return running.elapsed(); });
	double headKm = 1.0;
	f.update(headKm);
	double atUpdate = f.drawnHead();
	int liveUpdates = 0;
	int movedBetween = 0;
	QTimer updater;
	updater.setTimerType(Qt::PreciseTimer);
	QObject::connect(&updater, &QTimer::timeout, &updater, [&]() {
		movedBetween += f.drawnHead() != atUpdate ? 1 : 0;
		headKm += 0.001;
		f.update(headKm);
		atUpdate = f.drawnHead();
		++liveUpdates;
	});
	updater.start(5);
	QEventLoop wait;
	QTimer::singleShot(600, &wait, &QEventLoop::quit);
	wait.exec();
	updater.stop();
	// Updates that come more often than the steps do not hold them off.
	QCOMPARE_GE(liveUpdates, 20);
	QCOMPARE_GE(movedBetween, 6);
}

// A train at a constant simulated speed whose updates come unevenly, 20, 60, 25 and 55 ms apart, moves at an even speed:
// after the first 24 updates, every step of 5 ms moves it by the same distance within a tenth. Its glides last five
// times the longest interval but one, 300 ms, so each update changes the speed only a little.
void TestRuntimeCanvas::unevenUpdatesGiveEvenSpeed() {
	MotionFixture f;
	NetworkPresenter& presenter = f.presenter;
	qint64& now = f.now;
	double headKm = 1.0;
	f.update(headKm);
	std::vector<double> moves;
	double previous = f.drawnHead();
	int updates = 0;
	for (int cycle = 0; cycle < 12; ++cycle) {
		for (const int gap : {20, 60, 25, 55}) {
			for (int ms = 5; ms <= gap; ms += 5) {
				now += 5;
				// An update comes just after the step of its tick.
				presenter.stepTrains();
				if (ms == gap) {
					headKm += 0.01;
					f.update(headKm);
					++updates;
				}
				const double head = f.drawnHead();
				if (updates > 24)
					moves.push_back(head - previous);
				previous = head;
			}
		}
	}
	std::vector<double> sortedMoves = moves;
	std::sort(sortedMoves.begin(), sortedMoves.end());
	const double medianMove = sortedMoves.empty() ? 0.0 : sortedMoves[sortedMoves.size() / 2];
	// A train whose updates come unevenly moves at an even speed from one step to the next.
	QVERIFY(!moves.empty());
	QCOMPARE_GE(sortedMoves.front(), 0.9 * medianMove);
	QCOMPARE_LE(sortedMoves.back(), 1.1 * medianMove);

	// At a pause the train goes on to the place of its last update at the same speed, stops there at the end of its
	// glide, and never passes it.
	const double last = headKm * 1000.0;
	bool beforeLast = true;
	bool sameSpeed = true;
	for (int ms = 5; ms < 300; ms += 5) {
		now += 5;
		presenter.stepTrains();
		beforeLast &= f.drawnHead() < last && presenter.trainMoving(0);
		sameSpeed &= std::fabs((f.drawnHead() - previous) - medianMove) <= 0.1 * medianMove;
		previous = f.drawnHead();
	}
	bool stops = true;
	for (int ms = 0; ms < 1000; ms += 5) {
		now += 5;
		presenter.stepTrains();
		stops &= near(f.drawnHead(), last) && !presenter.trainMoving(0);
	}
	// A pause stops the train at the place of its last update, which it never passes.
	QVERIFY(beforeLast);
	QVERIFY(sameSpeed);
	QVERIFY(stops);
}

// A train that stops at 1.1 km, after updates of 50 ms that move it 10 m each, goes on in the glide of its last move,
// 250 ms, while the updates that follow leave it at its place. When that glide ends it stands at its place, and from
// then on no step and no update changes its body. It never passes its place.
void TestRuntimeCanvas::stoppingTrainReachesItsPlace() {
	MotionFixture f;
	NetworkPresenter& presenter = f.presenter;
	qint64& now = f.now;
	const double stopKm = 1.0 + 0.01 * 10;
	for (int step = 0; step <= 10; ++step) {
		now += 50;
		f.update(1.0 + 0.01 * step);
	}
	const double place = stopKm * 1000.0;
	const auto body = [&presenter]() { return presenter.runTrains()[0]->trainPolygonItemList->at(0)->polygon(); };
	const auto sameBody = [](const QPolygonF& left, const QPolygonF& right) {
		return std::equal(left.begin(), left.end(), right.begin(), right.end(),
			[](const QPointF& a, const QPointF& b) { return a.x() == b.x() && a.y() == b.y(); });
	};
	bool glidesOn = true;
	bool standsStill = true;
	bool neverPasses = true;
	QPolygonF stood;
	for (int ms = 5; ms <= 1500; ms += 5) {
		now += 5;
		presenter.stepTrains();
		if (ms % 50 == 0)
			f.update(stopKm);
		neverPasses &= f.drawnHead() <= place + 1e-6;
		if (ms < 250) {
			glidesOn &= f.drawnHead() < place - 1e-6 && presenter.trainMoving(0);
		} else {
			if (stood.isEmpty())
				stood = body();
			standsStill &= near(f.drawnHead(), place) && !presenter.trainsMoving() && sameBody(body(), stood);
		}
	}
	// A train that stops reaches its place when its last glide ends, and then no step changes its body.
	QVERIFY(glidesOn);
	QVERIFY(standsStill);
	QVERIFY(neverPasses);
}

// A train that changes its route or its direction is placed at once, also where it could glide on.
void TestRuntimeCanvas::newRouteOrDirectionPlacesTrainAtOnce() {
	MotionFixture f;
	NetworkPresenter& presenter = f.presenter;
	qint64& now = f.now;
	GuiSimulationSnapshot& snapshot = f.snapshot;
	const auto send = [&](double atKm, int route, bool reversed) {
		GuiTrainState state = trainAt(atKm);
		state.routeIndex = route;
		state.reversedDirection = reversed;
		snapshot.timestep += 1;
		snapshot.trains = {state};
		presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	};
	send(5.0, 0, false);
	now += 40;
	send(5.1, 0, false);
	const bool glidesFirst = presenter.trainMoving(0);
	now += 40;
	send(5.2, 1, false);
	const bool newRoute = !presenter.trainMoving(0) && near(drawnMiddle(*presenter.runTrains()[0]).x(), 5150.0);
	now += 40;
	send(5.3, 1, false);
	const bool glidesOnIt = presenter.trainMoving(0);
	now += 40;
	send(5.4, 1, true);
	// A train that changes its route or its direction is placed at once.
	QVERIFY(glidesFirst);
	QVERIFY(newRoute);
	QVERIFY(glidesOnIt);
	QVERIFY(!presenter.trainMoving(0));
	QVERIFY(near(drawnMiddle(*presenter.runTrains()[0]).x(), 5350.0));
}

// A followed train at a constant simulated speed whose updates come unevenly, 20, 60, 25 and 55 ms apart, moves the view
// by even steps: after the first 24 updates, the centre of the view moves by the same distance within a tenth from one
// frame of 16 ms to the next.
void TestRuntimeCanvas::followedTrainMovesViewEvenly() {
	QGraphicsScene followScene;
	NetworkView followView;
	followView.setScene(&followScene);
	followView.resize(400, 300);
	// The view is laid out, and no event loop runs, so only the test moves the trains and the view.
	followView.show();
	NetworkPresenter followed(&followScene, &followView);
	auto geometry = std::make_unique<FakeGeometry>();
	geometry->track(0).sceneTrackId = "T";
	followed.setGeometrySource(std::move(geometry));
	SceneModel followModel;
	followModel.tracks = {{"T"}};
	followModel.nodes = {{"T.a", "T", 0.0, 0.0}, {"T.b", "T", 10.0, 0.0}};
	followModel.arcs = {{"T.arc", "T", "T.a", "T.b", 0.0, 0.0, 12.0}};
	followed.showPreview(followModel);
	followed.showRuntime(followModel);
	followView.fitToBounds(followScene.itemsBoundingRect());
	// The view shows about 0.5 km.
	followView.zoomBy(20.0);
	qint64 now = 0;
	FollowCamera camera(&followView);
	camera.setClock([&now]() { return now; });
	followed.setClock([&now]() { return now; });
	FollowingHost host(camera);
	host.presenter = &followed;
	followed.setFrameHost(&host);
	GuiSimulationSnapshot snapshot;
	snapshot.timestep = 10;
	double headKm = 2.0;
	const auto deliver = [&]() {
		snapshot.timestep += 1;
		snapshot.trains = {trainOfWagons(3, headKm)};
		followed.apply(snapshot, NetworkPresenter::Playback::Live);
	};
	deliver();
	const auto centre = [&followView]() { return followView.mapToScene(followView.viewport()->rect().center()); };
	const qreal pixel = 1.0 / std::abs(followView.transform().m11());
	const int gaps[] = {20, 60, 25, 55};
	int updates = 0;
	qint64 nextUpdate = gaps[0];
	qint64 nextFrame = 16;
	QPointF previous = centre();
	std::vector<qreal> moves;
	while (updates < 48) {
		if (nextUpdate <= nextFrame) {
			now = nextUpdate;
			headKm += 0.01;
			deliver();
			++updates;
			nextUpdate += gaps[updates % 4];
		} else {
			now = nextFrame;
			followed.stepTrains();
			const QPointF here = centre();
			if (updates > 24)
				moves.push_back(QLineF(here, previous).length() / pixel);
			previous = here;
			nextFrame += 16;
		}
	}
	std::vector<qreal> sorted = moves;
	std::sort(sorted.begin(), sorted.end());
	const qreal median = sorted.empty() ? 0.0 : sorted[sorted.size() / 2];
	// A followed train whose updates come unevenly moves the view by even steps.
	QVERIFY(camera.hasTarget());
	QVERIFY(!camera.running());
	QCOMPARE_GT(median, 1.0);
	QCOMPARE_GE(sorted.front(), 0.9 * median);
	QCOMPARE_LE(sorted.back(), 1.1 * median);
	followed.setFrameHost(nullptr);
	followed.setClock({});
}

// The trains of a snapshot are found by their index, whatever the order of the snapshot.
void TestRuntimeCanvas::findsTrainsByIndex() {
	RunFixture f;
	NetworkPresenter& presenter = f.presenter;
	presenter.showRuntime(f.model);
	const auto train = [](int index, double headKm) {
		GuiTrainState state = trainAt(headKm);
		state.index = index;
		return state;
	};
	qint64 now = 0;
	presenter.setClock([&now]() { return now; });
	GuiSimulationSnapshot snapshot;
	snapshot.timestep = 10;
	snapshot.trains = {train(3, 2.5), train(1, 5.5), train(2, 7.5)};
	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	// Each train of the snapshot is added.
	QCOMPARE(presenter.runTrains().size(), qsizetype(3));

	snapshot.timestep = 11;
	snapshot.trains = {train(2, 8.5), train(3, 3.5), train(1, 6.5)};
	now = 100;
	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	// past the end of the glides
	now = 1000;
	presenter.stepTrains();
	const auto headOf = [&presenter](int index) {
		for (const TrainItemGroup* group : presenter.runTrains())
			if (group->index == index)
				return group->trainPolygonItemList->at(0)->polygon()[0].x();
		return -1.0;
	};
	// Each train of the snapshot is moved and none is added twice.
	QCOMPARE(presenter.runTrains().size(), qsizetype(3));
	QVERIFY(near(headOf(3), 3500.0));
	QVERIFY(near(headOf(1), 6500.0));
	QVERIFY(near(headOf(2), 8500.0));
	presenter.clear();
	presenter.setClock({});
	// The canvas is empty before the first round: clear leaves nothing of the runs before it.
	QVERIFY(presenterEmpty(presenter));
	QVERIFY(f.scene.items().isEmpty());
}

// The canvas is built, filled with the steps of a run and of its replay, with a selection on it, and cleared again and
// again. Nothing of an earlier round is left, and the next round draws what the first one drew.
void TestRuntimeCanvas::roundsLeaveNothingBehind() {
	RunFixture f;
	QGraphicsScene& scene = f.scene;
	NetworkPresenter& presenter = f.presenter;
	FakeGeometry* const source = f.source;
	PresenterAccess::passengerPixmap(presenter) = QPixmap(10, 10);
	// The canvas is empty before the first round.
	QVERIFY(presenterEmpty(presenter));
	QVERIFY(scene.items().isEmpty());
	qsizetype itemsAtEnd[2] = {0, 0};
	for (int round = 0; round < 20; ++round) {
		source->passengers = round % 2 == 1;
		presenter.showRuntime(f.model);
		SignalItem* const head = PresenterAccess::signalsByAheadId(presenter)["s0"].front();

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
		TrainItemGroup* const train = presenter.runTrains().value(0);
		presenter.paintTrainPassengerInfo(train);
		presenter.setInspectedSignal(head);
		presenter.setHighlight(train);
		if (source->passengers) {
			PassengerItem* const icon = PresenterAccess::allPlatforms(presenter).value(0)->passengerIcons.value(0);
			presenter.paintPassengerInfoIcon(icon, passengerStateIn(&snapshot, icon->passengerId), false);
		}

		// the train runs on, passengers board and arrive, and a message of a virtual coupling comes and goes
		for (int step = 0; step < 4; ++step) {
			snapshot.timestep = 11 + step;
			snapshot.trains = {trainAt(8.0 + step * 0.5)};
			snapshot.trains[0].occupiedArcs = {GuiOccupiedArc{0, 0.0}};
			snapshot.virtualCouplingMessages.clear();
			if (step == 1)
				snapshot.virtualCouplingMessages.push_back({12, "T1", "message"});
			if (source->passengers)
				snapshot.platforms[0].passengerIds = step % 2 == 0 ? Ids({"p1", "p3", "p4"}) : Ids({"p1", "p5"});
			presenter.apply(snapshot, NetworkPresenter::Playback::Live);
		}
		snapshot.trains[0].outOfSimulation = true;
		presenter.apply(snapshot, NetworkPresenter::Playback::Live);

		// the replay of the run, forward and back
		presenter.stopTrainAnimations();
		for (const int time : {20, 15, 25, 10}) {
			snapshot.timestep = time;
			snapshot.trains = {trainAt(7.5 + (time - 10) * 0.1)};
			snapshot.trains[0].occupiedArcs = {GuiOccupiedArc{0, 0.0}};
			if (source->passengers)
				snapshot.platforms[0].passengerIds = time % 2 == 0 ? Ids({"p1", "p2"}) : Ids({"p2", "p1", "p6"});
			presenter.apply(snapshot, NetworkPresenter::Playback::Replay);
		}
		// A round holds one train and one badge, whatever the earlier rounds drew.
		QCOMPARE(presenter.runTrains().size(), qsizetype(1));
		QCOMPARE(PresenterAccess::trainBadges(presenter).size(), qsizetype(1));
		// The selection is on the canvas before it is cleared.
		QVERIFY(PresenterAccess::trainPaxInfoItem(presenter));
		QVERIFY(presenter.inspectedSignal());
		QVERIFY(presenter.hasHighlight());
		QVERIFY(!source->passengers || PresenterAccess::paxIconInfoItem(presenter));
		itemsAtEnd[round % 2] = round < 2 ? scene.items().size() : itemsAtEnd[round % 2];
		// A round draws as many items as the first round of its kind.
		QCOMPARE(scene.items().size(), itemsAtEnd[round % 2]);

		presenter.clear();
		QApplication::processEvents();
		// Clear leaves no item, entry or pointer of the round.
		QVERIFY(presenterEmpty(presenter));
		QVERIFY(scene.items().isEmpty());
	}
}

// The heads of a run name their track by its id in the scene and their sections by the labels of the scene of the run,
// in the tooltip and in Find. The runtime number of the track and the section ids stay on the heads, and the ids find a
// head too.
void TestRuntimeCanvas::headsUseSceneLabels() {
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
	for (const SignalItem* head : labelPresenter.runSignals())
		if (!head->reversedDirection)
			forward = head;
	// The presenter gives the id in the scene of a track line of the run.
	QCOMPARE(labelPresenter.sceneTrackId(0), "T");
	QVERIFY(labelPresenter.sceneTrackId(1).empty());
	// A head of a run keeps its track number and section ids and gets the track id and the section labels.
	if (forward == nullptr)
		QFAIL("the run has no head facing right");
	QCOMPARE(forward->trackID, 0);
	QCOMPARE(forward->sceneTrackId, "T");
	QCOMPARE(forward->sectionAheadId, "s1");
	QCOMPARE(forward->sectionAheadLabel, "T 5.000 to 10.000 km");
	QCOMPARE(forward->sectionBehindId, "s0");
	QCOMPARE(forward->sectionBehindLabel, "T 0.000 to 5.000 km");
	// The tooltip of a head of a run names the track by its id.
	QCOMPARE(forward->inspectionIdentity(), "Track T at 5 km");
	const QList<NetworkPresenter::FindTarget> targets = labelPresenter.findTargets();
	const auto target = [&targets](const QString& label) {
		const auto found = std::find_if(targets.cbegin(), targets.cend(),
			[&label](const NetworkPresenter::FindTarget& candidate) { return candidate.label == label; });
		return found == targets.cend() ? nullptr : &*found;
	};
	const NetworkPresenter::FindTarget* signalTarget = target("Signal: track T at 5 km, facing right");
	// Find names a head by its track, position and direction and keeps the ids and labels of its sections as keys.
	if (signalTarget == nullptr)
		QFAIL("Find has no target for the head");
	QCOMPARE(signalTarget->item, forward);
	QCOMPARE(signalTarget->keys, "s1 s0 T 5.000 to 10.000 km T 0.000 to 5.000 km");
	QVERIFY2(target("Station: B (track T)") != nullptr, "Find names the track of a station node by its id");
	labelPresenter.clear();
}

// A run draws a track whose view sets double_switch as the original drew a double switch: the middle arc and no node
// dot. Without that view the track of four nodes keeps its three arcs and the dot at its end.
void TestRuntimeCanvas::doubleSwitchDrawsMiddleArc() {
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
	// A run draws the three arcs of a track of four nodes and the dot at its end.
	QCOMPARE(PresenterAccess::allArcs(switchPresenter).size(), qsizetype(3));
	QCOMPARE(dots(), 1);
	switchPresenter.clear();
	switchModel.trackViews = {{"B0", 0, 0, true, true}};
	switchPresenter.showRuntime(switchModel);
	// A run draws the middle arc of a double switch and no node dot.
	QCOMPARE(PresenterAccess::allArcs(switchPresenter).size(), qsizetype(1));
	QCOMPARE(PresenterAccess::allArcs(switchPresenter)[0]->arc, &track.member[1]);
	QCOMPARE(dots(), 0);
	switchPresenter.clear();
}

// With the station B lower down the line runs diagonally. A platform lies along it on its upper side, while its counter
// stays level above it and its passengers share its length on its long axis.
void TestRuntimeCanvas::platformLiesAlongDiagonalTrack() {
	RunFixture f;
	NetworkPresenter& presenter = f.presenter;
	FakeGeometry* const source = f.source;
	// The passenger icons are 10 pixels wide.
	PresenterAccess::passengerPixmap(presenter) = QPixmap(10, 10);
	source->passengers = true;
	source->station(1).Y = 5.0;
	source->track(0).member[0].endNode.stationName = "B";
	source->track(0).member[0].endNode.stationPlatformId = "P2";
	presenter.showRuntime(f.model);
	// The platforms and their passengers show in the near look.
	PresenterAccess::setStationNearLook(presenter, true);
	const QLineF lastArc = drawnArc(presenter, source->track(0).member[1]);
	const QLineF unit = lastArc.unitVector();
	const QPointF direction(unit.dx(), unit.dy());
	const qreal size = presenter.station_node_size * presenter.presentationScale();
	PlatformItem* const middlePlatform = PresenterAccess::allPlatforms(presenter).value(0);
	PlatformItem* const endPlatform = PresenterAccess::allPlatforms(presenter).value(1);
	// A platform lies along its diagonal track, on the upper side.
	QCOMPARE_GT(direction.y(), 0.1);
	if (middlePlatform == nullptr || endPlatform == nullptr)
		QFAIL("the run draws no two platforms");
	QVERIFY(platformAlong(*middlePlatform, lastArc.p1(), direction, size));
	QVERIFY(platformAlong(*endPlatform, lastArc.p2(), direction, size));

	GuiSimulationSnapshot snapshot;
	snapshot.timestep = 10;
	snapshot.platforms = {GuiPlatformState{"B", "P1", 40, {"p1", "p2", "p3"}}, GuiPlatformState{"B", "P2", 30, {"q1", "q2"}}};
	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	const qreal iconSize = PresenterAccess::passengerPixmap(presenter).width() * presenter.presentationScale();
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
	QVERIFY2(upright, "the counter and the passenger icons of a turned platform stay level, the icons on its long axis");
	presenter.clear();
}

// Without the geometry of its stations a track line is drawn one track separation higher for each level.
void TestRuntimeCanvas::levelWithoutStationGeometryIsDrawnHigher() {
	RunFixture f;
	NetworkPresenter& presenter = f.presenter;
	FakeGeometry* const source = f.source;
	// The station B lies lower down, and the middle of the line has a second platform of it, in a run with passengers.
	source->passengers = true;
	source->station(1).Y = 5.0;
	source->track(0).member[0].endNode.stationName = "B";
	source->track(0).member[0].endNode.stationPlatformId = "P2";
	BlockSet& track = source->track(0);
	track.hasGraphLayout = true;
	track.graphID = 1;
	track.region = 1; // no station is in this region
	presenter.showRuntime(f.model);
	bool above = PresenterAccess::allArcs(presenter).size() == 2;
	for (const TrackLineItem* arc : PresenterAccess::allArcs(presenter))
		above = above && near(arc->line().y1(), -presenter.track_separation) && near(arc->line().y2(), -presenter.track_separation);
	QVERIFY2(above, "a track line on level 1 without station geometry is drawn above level 0");
	presenter.clear();
}

// With the preview of the scene a run puts the pictures of the stations on a higher schematic level where the preview
// put them.
void TestRuntimeCanvas::runPutsPicturesWherePreviewDoes() {
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
		for (StationOverlayItem* overlay : levelPresenter.stationOverlays())
			if (const QGraphicsPixmapItem* picture = PresenterAccess::stationPictures(levelPresenter).value(overlay, nullptr))
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
	QVERIFY2(same, "a run with the preview puts the station pictures of a higher schematic level where the preview put them");
}

// Drawing a run with a connection and a station lower down, and a train on it, leaves the source as it was: the presenter
// keeps the places it works out for the canvas to itself.
void TestRuntimeCanvas::drawingLeavesSourceUnchanged() {
	RunFixture f;
	NetworkPresenter& presenter = f.presenter;
	FakeGeometry* const source = f.source;
	source->makeCrossover();
	source->addCrossoverConnection();
	source->station(1).Y = 5.0;
	const std::vector<double> before = canvasFields(*source);
	presenter.showRuntime(f.model);
	GuiSimulationSnapshot snapshot;
	snapshot.timestep = 10;
	snapshot.trains = {trainAt(5.0)};
	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	// The run draws the arcs of both tracks and the train.
	QCOMPARE(PresenterAccess::allArcs(presenter).size(), qsizetype(4));
	QCOMPARE(presenter.runTrains().size(), qsizetype(1));
	QVERIFY2(canvasFields(*source) == before, "drawing a run leaves the canvas fields of its source as they were");
	presenter.clear();
}

QTEST_MAIN(TestRuntimeCanvas)
#include "test_runtimecanvas.moc"
