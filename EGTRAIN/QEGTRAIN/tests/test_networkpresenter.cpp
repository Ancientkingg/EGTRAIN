// SceneModel.h has a member named signals, so it comes before the Qt headers.
#include "scene/SceneModel.h"

#include "graphics/NetworkPresenter.h"
#include "tests/NetworkPresenterTestAccess.h"

#include "simulation/GuiSimulationSnapshot.h"
#include "graphics/NetworkScene.h"
#include "graphics/NetworkView.h"
#include "graphics/VisualPolish.h"
#include "graphics/items/HighlightEffect.h"
#include "graphics/items/NetworkMarkItem.h"
#include "graphics/items/PassengerItem.h"
#include "graphics/items/PlatformItem.h"
#include "graphics/items/SelectionCueItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/TrainBadgeItem.h"
#include "graphics/items/TrainItemGroup.h"
#include "scene/SectionInventory.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QGraphicsItemGroup>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QLineF>
#include <QLoggingCategory>
#include <QMap>
#include <QPixmap>
#include <QTest>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// A track line that counts how often it is painted.
class CountingTrack final : public TrackLineItem {
public:
	using TrackLineItem::TrackLineItem;
	int paints = 0;

	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override {
		++paints;
		TrackLineItem::paint(painter, option, widget);
	}
};

// A scene that counts the queued calls it receives.
class QueueCountingScene final : public QGraphicsScene {
public:
	int queuedCalls = 0;

protected:
	bool event(QEvent* event) override {
		if (event->type() == QEvent::MetaCall)
			++queuedCalls;
		return QGraphicsScene::event(event);
	}
};

// Runs the event loop for the given time.
static void pump(int milliseconds) {
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < milliseconds)
		QApplication::processEvents(QEventLoop::AllEvents, 20);
}

// Runs the event loop until the condition holds, for at most two seconds, and then a little longer so that the
// painting that is still waiting gets its turn.
template <class Condition>
static void waitFor(Condition condition) {
	QElapsedTimer timer;
	timer.start();
	while (!condition() && timer.elapsed() < 2000)
		QApplication::processEvents(QEventLoop::AllEvents, 20);
	pump(100);
}

// Adds one item to every registry and sets every pointer, the way a run and a selection do.
static void fill(NetworkPresenter& presenter, QGraphicsScene& scene) {
	auto* train = new TrainItemGroup;
	scene.addItem(train);
	PresenterAccess::allTrains(presenter).push_back(train);
	PresenterAccess::trainPaxItem(presenter) = train;
	auto* badge = new TrainBadgeItem;
	scene.addItem(badge);
	PresenterAccess::trainBadges(presenter).insert(0, badge);

	auto* signal = new SignalItem(QRectF(0, 0, 4, 4));
	scene.addItem(signal);
	PresenterAccess::allSignals(presenter).push_back(signal);
	PresenterAccess::signalsByAheadId(presenter)["section"].push_back(signal);
	presenter.setInspectedSignal(signal);
	auto* signalDecoration = scene.addLine(0, 0, 5, 5);
	PresenterAccess::signalDecorations(presenter).push_back(signalDecoration);

	auto* track = new TrackLineItem(QLineF(0, 0, 10, 0));
	scene.addItem(track);
	PresenterAccess::allArcs(presenter).push_back(track);
	PresenterAccess::tracksBySectionId(presenter)["section"].push_back(track);
	PresenterAccess::tracksByOccupiedArc(presenter)[{0, 0.0}] = track;
	PresenterAccess::activeTrackItems(presenter).insert(track);

	auto* platform = new PlatformItem(QRectF(0, 0, 8, 2));
	scene.addItem(platform);
	PresenterAccess::allPlatforms(presenter).push_back(platform);
	auto* overlay = new StationOverlayItem(QStringLiteral("Station"), QPointF(3, 3), classifyStation());
	scene.addItem(overlay);
	PresenterAccess::stationOverlays(presenter).push_back(overlay);
	PresenterAccess::stationDecorations(presenter).push_back(overlay);
	auto* picture = scene.addPixmap(QPixmap(4, 4));
	PresenterAccess::stationPictures(presenter).insert(overlay, picture);
	PresenterAccess::stationLabels(presenter).push_back(scene.addText(QStringLiteral("Station")));

	auto* message = new QGraphicsItemGroup;
	scene.addItem(message);
	PresenterAccess::vcMessageItems(presenter).insert(0, message);

	auto* trainInfo = new QGraphicsItemGroup;
	scene.addItem(trainInfo);
	PresenterAccess::trainPaxInfoItem(presenter) = trainInfo;
	auto* passenger = new PassengerItem(QPixmap(2, 2));
	scene.addItem(passenger);
	PresenterAccess::paxIconItem(presenter) = passenger;
	auto* passengerInfo = new QGraphicsItemGroup;
	scene.addItem(passengerInfo);
	PresenterAccess::paxIconInfoItem(presenter) = passengerInfo;

	presenter.setHighlight(badge);
}

// Two tracks of a line with a connection at one end and three stations.
static SceneModel previewModel() {
	SceneModel scene;
	scene.tracks = {{"B0"}, {"B1"}};
	scene.nodes = {
		{"B0.Gvc", "B0", 0.0, 1.0},
		{"B0.Gdg", "B0", 28.0, 2.0},
		{"B0.Ut", "B0", 64.0, 3.0},
		{"B1.Ut", "B1", 100.0, -1.0},
		{"B1.Gdg", "B1", 136.0, -2.0},
		{"B1.Gvc", "B1", 164.0, -3.0},
	};
	scene.arcs = {
		{"B0.arc.1", "B0", "B0.Gvc", "B0.Gdg", 0.0, 0.0, 12.0},
		{"B0.arc.2", "B0", "B0.Gdg", "B0.Ut", 0.0, 0.0, 12.0},
		{"B1.arc.1", "B1", "B1.Ut", "B1.Gdg", 0.0, 0.0, 12.0},
		{"B1.arc.2", "B1", "B1.Gdg", "B1.Gvc", 0.0, 0.0, 12.0},
	};
	scene.blocks = {
		{"B0.block.1", "B0", 28.0},
		{"B0.block.2", "B0", 36.0},
		{"B1.block.1", "B1", 36.0},
		{"B1.block.2", "B1", 64.0},
	};
	scene.connections.push_back({"switch.7", "B0.Ut", "B1.Ut", false, 0.0});
	scene.trackViews = {{"B0", 0, 0}, {"B1", 1, 1}};
	scene.stationViews = {
		{"Gvc", 52.0, 0.0, {{0, 0.0}, {1, 164.0}}, {}},
		{"Gdg", 52.25, 0.28, {{0, 28.0}, {1, 136.0}}, {}},
		{"Ut", 52.5, 0.64, {{0, 64.0}, {1, 100.0}}, {}},
	};
	for (const auto& stationData : {
			 std::pair<const char*, const char*>{"Gvc", "B0.Gvc"},
			 std::pair<const char*, const char*>{"Gdg", "B0.Gdg"},
			 std::pair<const char*, const char*>{"Ut", "B0.Ut"}}) {
		SceneStation station;
		station.id = stationData.first;
		station.name = stationData.first;
		station.platforms.push_back({std::string(stationData.first) + ".platform", {stationData.second}});
		scene.stations.push_back(std::move(station));
	}
	return scene;
}

// Whether count platform bars are on the scene, each an item of the scene of its own at z 2, over the squares and the
// station marks they hold, that lies along the line at a station node. A bar is parallel, within half a degree, to the
// mean of the directions of the segments on both sides of its node, or to the last segment at the end of the line. Its
// centre is 1.15 square sides from the node on the upper side of the track, or on the left of a vertical track.
static bool barsAlong(QGraphicsScene& scene, const TrackPreviewLine& line, int count) {
	const auto pointAt = [&line](std::size_t index) {
		return QPointF(line.points[index].x, line.points[index].y + line.displayOffset);
	};
	const auto unit = [](const QPointF& vector) { return vector / std::hypot(vector.x(), vector.y()); };
	const auto along = [&](const PlatformItem* bar, const StationNodeItem* square) {
		const QPointF node = square->rect().center();
		std::size_t index = 1;
		while (index < line.points.size() && QLineF(pointAt(index), node).length() > 1e-6)
			++index;
		if (index == line.points.size())
			return false;
		QPointF direction = unit(node - pointAt(index - 1));
		if (index + 1 < line.points.size())
			direction = unit(direction + unit(pointAt(index + 1) - node));
		if (direction.x() < 0 || (direction.x() == 0 && direction.y() < 0))
			direction = -direction;
		const QPointF up = direction.x() == 0 ? QPointF(-1, 0) : QPointF(direction.y(), -direction.x());
		const QRectF rect = bar->rect();
		const QLineF axis(bar->mapToScene(QPointF(rect.left(), rect.center().y())),
			bar->mapToScene(QPointF(rect.right(), rect.center().y())));
		return std::fabs(axis.dx() * direction.y() - axis.dy() * direction.x()) <= axis.length() * std::sin(qDegreesToRadians(0.5))
			&& QLineF(bar->mapToScene(rect.center()), node + up * 1.15 * square->rect().width()).length() <= 1e-6;
	};
	QList<const StationNodeItem*> squares;
	for (QGraphicsItem* item : scene.items())
		if (const auto* square = qgraphicsitem_cast<StationNodeItem*>(item))
			squares.append(square);
	int bars = 0;
	for (QGraphicsItem* item : scene.items()) {
		const auto* bar = qgraphicsitem_cast<PlatformItem*>(item);
		if (!bar)
			continue;
		if (bar->parentItem() || bar->zValue() != 2.0
			|| std::none_of(squares.cbegin(), squares.cend(), [&](const StationNodeItem* square) { return along(bar, square); }))
			return false;
		++bars;
	}
	return bars == count;
}

static bool registriesEmpty(const NetworkPresenter& presenter) {
	return presenter.runTrains().isEmpty() && PresenterAccess::trainBadges(presenter).isEmpty() && presenter.runSignals().isEmpty()
		&& PresenterAccess::signalsByAheadId(presenter).empty() && PresenterAccess::signalDecorations(presenter).isEmpty() && PresenterAccess::allArcs(presenter).isEmpty()
		&& PresenterAccess::tracksBySectionId(presenter).empty() && PresenterAccess::tracksByOccupiedArc(presenter).empty() && PresenterAccess::activeTrackItems(presenter).empty()
		&& PresenterAccess::allPlatforms(presenter).isEmpty() && presenter.stationOverlays().isEmpty() && PresenterAccess::stationDecorations(presenter).isEmpty()
		&& PresenterAccess::stationLabels(presenter).isEmpty() && PresenterAccess::stationPictures(presenter).isEmpty() && PresenterAccess::vcMessageItems(presenter).isEmpty();
}

static bool pointersNull(const NetworkPresenter& presenter) {
	return !PresenterAccess::inspectedSignal(presenter) && !presenter.selectedTrain() && !PresenterAccess::trainPaxInfoItem(presenter)
		&& !PresenterAccess::paxIconItem(presenter) && !PresenterAccess::paxIconInfoItem(presenter) && !presenter.hasHighlight();
}

// The preview model with a station of each class. Ut gets three connection ends, and a station placed by position has
// no platform.
static SceneModel classesModel() {
	SceneModel classes = previewModel();
	classes.connections.push_back({"switch.8", "B0.Ut", "B1.Gdg", false, 0.0});
	classes.connections.push_back({"switch.9", "B0.Ut", "B1.Gvc", false, 0.0});
	// Gdg also has a platform on the other line.
	classes.stations[1].platforms.push_back({"Gdg.platform.2", {"B1.Gdg"}});
	SceneStation halt;
	halt.id = halt.name = "Hlt";
	halt.hasPosition = true;
	halt.positionKm = 10.0;
	classes.stations.push_back(halt);
	// A halt at the position of B1.Gvc, which no platform takes, has a square there.
	SceneStation squareHalt;
	squareHalt.id = squareHalt.name = "Hsq";
	squareHalt.hasPosition = true;
	squareHalt.positionKm = 164.0;
	classes.stations.push_back(squareHalt);
	return classes;
}

// A presenter that draws previewModel() on a scene and fits a view that is not shown.
struct PreviewFixture {
	QGraphicsScene scene;
	NetworkView view;
	NetworkPresenter presenter{&scene, &view};
	const SceneModel model = previewModel();
	// Track B1 selected in the infrastructure panel.
	NetworkPresenter::PreviewOptions options;

	PreviewFixture() {
		view.setScene(&scene);
		options.selectedFacet = "tracks";
		options.selectedId = "B1";
	}

	// The platform bars among the station decorations that are shown or hidden.
	qsizetype bars(bool visible) const {
		return std::count_if(PresenterAccess::stationDecorations(presenter).cbegin(), PresenterAccess::stationDecorations(presenter).cend(),
			[visible](QGraphicsItem* item) { return qgraphicsitem_cast<PlatformItem*>(item) && item->isVisible() == visible; });
	}
};

// The distance from point to the nearest of the polylines.
static qreal distanceTo(const QList<QPolygonF>& lines, const QPointF& point) {
	qreal best = std::numeric_limits<qreal>::max();
	for (const QPolygonF& line : lines)
		for (qsizetype index = 0; index < line.size(); ++index) {
			const QPointF start = line[index];
			const QPointF segment = (index + 1 < line.size() ? line[index + 1] : start) - start;
			const qreal lengthSquared = QPointF::dotProduct(segment, segment);
			const qreal ratio = lengthSquared > 0.0 ? std::clamp(QPointF::dotProduct(point - start, segment) / lengthSquared, 0.0, 1.0) : 0.0;
			best = std::min(best, QLineF(start + ratio * segment, point).length());
		}
	return best;
}

// The ids of the targets of the rings of a mark, sorted.
static QStringList ringIds(const NetworkMarkItem& mark) {
	QStringList ids;
	for (const QGraphicsItem* target : mark.ringTargets())
		ids << target->data(PreviewGraphics::Id).toString();
	ids.sort();
	return ids;
}

// The track lines that are drawn in the colour of the selection cue.
static int cueColouredTracks(QGraphicsScene& scene) {
	int count = 0;
	for (QGraphicsItem* item : scene.items())
		if (const auto* track = qgraphicsitem_cast<TrackLineItem*>(item); track && track->pen().color() == kSelectionCueColor)
			++count;
	return count;
}

// A presenter that draws the committed scene Paimpol in a view of 800 by 600 pixels. The scene gets a signal on 36-B0, a
// block dependency, a station boundary and a signalling area on B0 from 2 to 3 km, which Paimpol does not have.
struct PaimpolFixture {
	QGraphicsScene scene;
	NetworkView view;
	NetworkPresenter presenter{&scene, &view};
	SceneModel model;
	SceneSectionInventory inventory;

	PaimpolFixture() {
		view.setScene(&scene);
		view.setFrameShape(QFrame::NoFrame);
		view.resize(800, 600);
		view.show();
		model = loadScene(QFINDTESTDATA("../Scenes/Paimpol").toStdString()).scene;
		sceneSignals(model).push_back({"sig.36", "36-B0"});
		model.blockDependencies.push_back({"36-B0", "38-B0"});
		model.stationBoundaries.push_back({"40-B0", true, "42-B0", false});
		model.signallingAreas.push_back({"area.B0", 2.0, 3.0, 0, "B0"});
		inventory = buildSceneSectionInventory(model);
	}

	// The row `id` of `facet` selected, on the first generation of the model.
	static NetworkPresenter::PreviewOptions options(const std::string& facet, const std::string& id, int routeSection = -1) {
		NetworkPresenter::PreviewOptions options;
		options.selectedFacet = facet;
		options.selectedId = id;
		options.selectedRouteSection = routeSection;
		options.modelGeneration = 1;
		return options;
	}
	// Draws the preview again with the row selected; the mark, or null.
	const NetworkMarkItem* show(const std::string& facet, const std::string& id, int routeSection = -1) {
		presenter.clear();
		presenter.showPreview(model, options(facet, id, routeSection));
		return presenter.selectionMark();
	}
	// The point of the preview at the node; the origin when the preview has none.
	QPointF at(const std::string& nodeId) const {
		const auto node = std::find_if(model.nodes.cbegin(), model.nodes.cend(), [&nodeId](const SceneNode& candidate) {
			return candidate.id == nodeId;
		});
		const TrackPreviewLine* line = node == model.nodes.cend() ? nullptr : presenter.trackLine(node->trackId);
		QPointF point;
		if (line)
			previewPointAtNode(*line, nodeId, line->displayOffset, point);
		return point;
	}
	// Points inside the section: on a block section the points of its track at a quarter, the half and three quarters of
	// its chainage, on a switch section the middle of each step from one of its nodes to the next, the connection included.
	QList<QPointF> middles(const std::string& sectionId) const {
		QList<QPointF> points;
		const SceneSectionDescriptor* section = inventory.exact(sectionId);
		if (section == nullptr)
			return points;
		if (section->connectionDerived) {
			for (std::size_t index = 1; index < section->nodeIds.size(); ++index)
				points << (at(section->nodeIds[index - 1]) + at(section->nodeIds[index])) / 2.0;
			return points;
		}
		const TrackPreviewLine* line = presenter.trackLine(section->firstTrackId);
		for (const double share : {0.25, 0.5, 0.75}) {
			QPointF point;
			if (line && previewPointAtX(*line, section->startKm + share * (section->endKm - section->startKm), line->displayOffset, point))
				points << point;
		}
		return points;
	}
	// The sections of which every middle, or with `every` false any middle, lies on a stretch of the mark: within a
	// millionth of the width of the preview.
	QStringList marked(const NetworkMarkItem& mark, const std::vector<std::string>& sectionIds, bool every = true) const {
		const qreal near = presenter.previewFit().bounds.width() * 1e-6;
		const auto on = [&](const QPointF& point) { return distanceTo(mark.stretches(), point) <= near; };
		QStringList hits;
		for (const std::string& sectionId : sectionIds) {
			const QList<QPointF> points = middles(sectionId);
			if (!points.isEmpty()
				&& (every ? std::all_of(points.cbegin(), points.cend(), on) : std::any_of(points.cbegin(), points.cend(), on)))
				hits << QString::fromStdString(sectionId);
		}
		return hits;
	}
	// The sections of which any middle lies on a stretch of the mark.
	QStringList touched(const NetworkMarkItem& mark, const std::vector<std::string>& sectionIds) const {
		return marked(mark, sectionIds, false);
	}
};

class TestNetworkPresenter : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void clearEmptiesRegistries();
	void clearWorksAfterSceneIsGone();
	void clearWorksWithoutScene();
	void previewCyclesDrawSameItems();
	void previewMarksSelectedTrack();
	void previewDrawsPlatformBars();
	void platformBarsOfVerticalTrackLieOnItsLeft();
	void previewWithSelectionCyclesDrawSameItems();
	void stationMarksFollowClass();
	void stationMarksGetSquareCentres();
	void haltsKeepSizeOnScreenInNearLook();
	void modelWithoutTrackLinesDrawsNothing();
	void previewHeadNamesSection();
	void stationPicturesCountFromLevelZero();
	void previewLeavesNoQueuedCall();
	void paintsOnlyTracksThatChangeState();
	void doubleSwitchDrawsMiddleArc();
	void platformMarksItsNodeAndBar();
	void routeMarksItsSectionsAndConnections();
	void stationsHaveMarksOfTheirOwn();
	void nodeAndBlockMarkThatItem();
	void otherKindsHaveMarks();
	void anotherRowKeepsTheView();
	void markInPlaceNeedsTheDrawnModel();
	void markColoursStandApart();
};

void TestNetworkPresenter::initTestCase() {
	// The entity icons are resources of the application, which this test does not have.
	QLoggingCategory::setFilterRules(QStringLiteral("qt.svg.warning=false"));
}

void TestNetworkPresenter::clearEmptiesRegistries() {
	QGraphicsScene scene;
	NetworkPresenter presenter(&scene, nullptr);
	// A new presenter has no registry entry and no pointer.
	QVERIFY(registriesEmpty(presenter));
	QVERIFY(pointersNull(presenter));
	QVERIFY(scene.items().isEmpty());
	for (int cycle = 0; cycle < 3; ++cycle) {
		fill(presenter, scene);
		scene.addText(QStringLiteral("not registered"));
		// The filled presenter holds the items.
		QVERIFY(!registriesEmpty(presenter));
		QVERIFY(!pointersNull(presenter));
		QVERIFY(!scene.items().isEmpty());
		presenter.clear();
		QVERIFY2(registriesEmpty(presenter), "clear empties every registry");
		QVERIFY2(pointersNull(presenter), "clear resets every pointer");
		QVERIFY2(scene.items().isEmpty(), "clear leaves the scene as empty as a new scene");
	}
}

void TestNetworkPresenter::clearWorksAfterSceneIsGone() {
	auto scene = std::make_unique<QGraphicsScene>();
	NetworkPresenter presenter(scene.get(), nullptr);
	fill(presenter, *scene);
	scene.reset();
	presenter.clear();
	// Clear empties the registries after the scene is gone.
	QVERIFY(registriesEmpty(presenter));
	QVERIFY(pointersNull(presenter));
}

void TestNetworkPresenter::clearWorksWithoutScene() {
	NetworkPresenter presenter(nullptr, nullptr);
	PresenterAccess::trainBadges(presenter).insert(0, nullptr);
	presenter.clear();
	QVERIFY2(registriesEmpty(presenter), "clear works without a scene");
}

void TestNetworkPresenter::previewCyclesDrawSameItems() {
	PreviewFixture f;
	NetworkPresenter& presenter = f.presenter;
	qsizetype drawnItems = 0;
	for (int cycle = 0; cycle < 25; ++cycle) {
		const NetworkPresenter::PreviewOutcome outcome = presenter.showPreview(f.model);
		// The preview draws both track lines and fits the view.
		QVERIFY(outcome.available);
		QCOMPARE(outcome.trackLines, 2);
		QVERIFY(outcome.fitted);
		// The preview fills the registries and keeps the preview it drew.
		QVERIFY(!presenter.stationOverlays().isEmpty());
		QVERIFY(!PresenterAccess::signalDecorations(presenter).isEmpty());
		QVERIFY(presenter.previewHasSignals());
		QVERIFY(!presenter.previewFit().bounds.isEmpty());
		QVERIFY(presenter.trackLine("B0"));
		QVERIFY(!presenter.trackLine("none"));
		if (cycle == 0)
			drawnItems = f.scene.items().size();
		// Every cycle draws the same items.
		QCOMPARE_GT(drawnItems, 0);
		QCOMPARE(f.scene.items().size(), drawnItems);
		presenter.clear();
		// Clear after a preview leaves the registries empty and the scene as empty as a new scene.
		QVERIFY(registriesEmpty(presenter));
		QVERIFY(pointersNull(presenter));
		QVERIFY(f.scene.items().isEmpty());
	}
}

void TestNetworkPresenter::previewMarksSelectedTrack() {
	PreviewFixture f;
	f.presenter.showPreview(f.model, f.options);
	QVERIFY2(f.presenter.previewHasSelectedTrack(), "a selected track is marked");
	// The mark is a band along the whole drawn track, which keeps the colour of its speed.
	const NetworkMarkItem* mark = f.presenter.selectionMark();
	if (mark == nullptr)
		QFAIL("a selected track has a mark");
	const TrackPreviewLine* line = f.presenter.trackLine("B1");
	QVERIFY(line);
	QPolygonF whole;
	for (const TrackPreviewPoint& point : line->points)
		whole << QPointF(point.x, point.y + line->displayOffset);
	QCOMPARE(mark->stretches(), QList<QPolygonF>{whole});
	QCOMPARE(cueColouredTracks(f.scene), 0);
}

// The preview draws the platform bars without being asked, among the station decorations, hidden in the far look and
// shown in the near look.
void TestNetworkPresenter::previewDrawsPlatformBars() {
	PreviewFixture f;
	NetworkPresenter& presenter = f.presenter;
	presenter.showPreview(f.model, f.options);
	// The preview draws the platform bars, hidden in the far look.
	QCOMPARE_GT(f.bars(false), 0);
	QCOMPARE(f.bars(true), qsizetype(0));
	PresenterAccess::setStationNearLook(presenter, true);
	// The platform bars show in the near look.
	QCOMPARE_GT(f.bars(true), 0);
	QCOMPARE(f.bars(false), qsizetype(0));
	// B0 runs diagonally and bends at Gdg, its station between Gvc and Ut.
	QVERIFY2(barsAlong(f.scene, *presenter.trackLine("B0"), 2),
		"a platform bar is a scene item over the station marks and lies along its track, on the upper side");
	const auto drawnBars = f.bars(true) + f.bars(false);
	presenter.clear();
	// A virtual station gets no platform bar, as it gets no overlay.
	SceneModel withVirtual = f.model;
	withVirtual.stations.push_back({"virtualLink", "virtualLink", false, 0.0, {{"virtualLink.platform", {"B1.Gdg"}}}});
	presenter.showPreview(withVirtual);
	// A virtual station gets no platform bar.
	QCOMPARE(f.bars(true) + f.bars(false), drawnBars);
}

void TestNetworkPresenter::platformBarsOfVerticalTrackLieOnItsLeft() {
	PreviewFixture f;
	NetworkPresenter& presenter = f.presenter;
	QGraphicsScene& scene = f.scene;
	// With Ut at the longitude of Gdg the track runs straight up from Gdg to Ut.
	SceneModel vertical = f.model;
	vertical.stationViews[2].longitude = 0.28;
	presenter.showPreview(vertical, f.options);
	QVERIFY2(barsAlong(scene, *presenter.trackLine("B0"), 2), "the platform bar of a vertical track lies on its left");
	// Ut ends the line, so its bar follows the vertical segment alone: it stands upright, left of the node.
	bool upright = false;
	if (const TrackPreviewLine* verticalLine = presenter.trackLine("B0"); verticalLine && !verticalLine->points.empty()) {
		const QPointF ut(verticalLine->points.back().x, verticalLine->points.back().y + verticalLine->displayOffset);
		qreal side = 0.0;
		for (QGraphicsItem* item : scene.items())
			if (const auto* square = qgraphicsitem_cast<StationNodeItem*>(item); square && QLineF(square->rect().center(), ut).length() < 1e-6)
				side = square->rect().width();
		// The bar of Ut is the one within two square sides of it.
		for (QGraphicsItem* item : scene.items()) {
			const auto* bar = qgraphicsitem_cast<PlatformItem*>(item);
			if (bar && QLineF(bar->mapToScene(bar->rect().center()), ut).length() < 2.0 * side)
				upright = std::fabs(bar->rotation() - 90.0) < 1e-9 && bar->mapToScene(bar->rect().center()).x() < ut.x();
		}
	}
	QVERIFY2(upright, "the platform bar at the vertical end of a line stands upright on its left");
}

void TestNetworkPresenter::previewWithSelectionCyclesDrawSameItems() {
	PreviewFixture f;
	NetworkPresenter& presenter = f.presenter;
	qsizetype drawnWithOptions = 0;
	for (int cycle = 0; cycle < 25; ++cycle) {
		presenter.showPreview(f.model, f.options);
		// Every cycle marks the selection.
		QVERIFY(presenter.selectionMark());
		if (cycle == 0)
			drawnWithOptions = f.scene.items().size();
		// Every cycle with a selection draws the same items.
		QCOMPARE_GT(drawnWithOptions, 0);
		QCOMPARE(f.scene.items().size(), drawnWithOptions);
		presenter.clear();
		// Clear after a preview with options leaves the registries empty and the scene as empty as a new scene.
		QVERIFY(registriesEmpty(presenter));
		QVERIFY(pointersNull(presenter));
		QVERIFY(f.scene.items().isEmpty());
	}
}

void TestNetworkPresenter::stationMarksFollowClass() {
	PreviewFixture f;
	NetworkPresenter& presenter = f.presenter;
	presenter.showPreview(classesModel());
	QStringList marks;
	for (StationOverlayItem* overlay : presenter.stationOverlays()) {
		const auto* picture = static_cast<const StationMarkItem*>(PresenterAccess::stationPictures(presenter).value(overlay, nullptr));
		if (picture && picture->stationClass() == overlay->visual().cls)
			marks << overlay->stationName() + "=" + QString::number(static_cast<int>(overlay->visual().cls));
	}
	marks.sort();
	// A station gets the mark of its class from its connections and platforms.
	QCOMPARE(marks, QStringList({"Gdg=1", "Gvc=1", "Hlt=0", "Hsq=0", "Ut=2"}));
}

// The mark of a station with platforms gets the centres of their station squares, one without them those of the
// squares at its position, as in a run.
void TestNetworkPresenter::stationMarksGetSquareCentres() {
	PreviewFixture f;
	NetworkPresenter& presenter = f.presenter;
	const SceneModel classes = classesModel();
	presenter.showPreview(classes);
	bool centred = !presenter.stationOverlays().isEmpty();
	for (StationOverlayItem* overlay : presenter.stationOverlays()) {
		const auto* mark = dynamic_cast<const StationMarkItem*>(PresenterAccess::stationPictures(presenter).value(overlay, nullptr));
		const auto station = std::find_if(classes.stations.cbegin(), classes.stations.cend(), [overlay](const SceneStation& candidate) {
			return QString::fromStdString(candidate.id) == overlay->data(PreviewGraphics::Id).toString();
		});
		if (!mark || station == classes.stations.cend()) {
			centred = false;
			continue;
		}
		QList<QPointF> expected;
		for (const auto& platform : station->platforms)
			for (const auto& nodeId : platform.nodeIds)
				for (QGraphicsItem* item : f.scene.items())
					if (qgraphicsitem_cast<StationNodeItem*>(item)
						&& item->data(PreviewGraphics::Id).toString() == QString::fromStdString(nodeId)
						&& !expected.contains(item->sceneBoundingRect().center()))
						expected.append(item->sceneBoundingRect().center());
		if (station->platforms.empty())
			for (QGraphicsItem* item : f.scene.items())
				if (qgraphicsitem_cast<StationNodeItem*>(item)
					&& item->data(PreviewGraphics::StationId).toString() == QString::fromStdString(station->id))
					expected.append(item->sceneBoundingRect().center());
		centred = centred && mark->nodeCentres() == expected && (overlay->stationName() != "Gdg" || expected.size() == 2)
			&& (overlay->stationName() != "Hsq" || expected.size() == 1) && (overlay->stationName() != "Hlt" || expected.isEmpty());
	}
	QVERIFY2(centred, "the preview gives each station mark the centres of its station squares");
}

// In the near look before a run, as in a run, the halt with a square shows its square alone, and the halt without one
// keeps its pin and its name at their smallest size on screen at every zoom.
void TestNetworkPresenter::haltsKeepSizeOnScreenInNearLook() {
	PreviewFixture f;
	NetworkPresenter& presenter = f.presenter;
	presenter.showPreview(classesModel());
	const auto overlayNamed = [&presenter](const QString& name) -> StationOverlayItem* {
		for (StationOverlayItem* overlay : presenter.stationOverlays())
			if (overlay->stationName() == name)
				return overlay;
		return nullptr;
	};
	StationOverlayItem* withSquare = overlayNamed("Hsq");
	StationOverlayItem* withoutSquare = overlayNamed("Hlt");
	bool squareAlone = withSquare && withoutSquare;
	QList<QSizeF> keptPin;
	QList<qreal> keptName;
	for (const qreal viewScale : {4.0, 10.0}) {
		if (!squareAlone)
			break;
		withSquare->applyViewScale(viewScale, true);
		withoutSquare->applyViewScale(viewScale, true);
		const auto* mark = static_cast<const StationMarkItem*>(withSquare->pictureItem());
		const auto* pin = static_cast<const StationMarkItem*>(withoutSquare->pictureItem());
		squareAlone = mark->isNearLook() && mark->sceneBoundingRect().isEmpty() && !pin->isNearLook();
		keptPin << pin->sceneBoundingRect().size() * viewScale;
		keptName << withoutSquare->nameItem()->sceneBoundingRect().height() * viewScale;
	}
	QVERIFY2(squareAlone, "before a run a halt with a square draws no pin in the near look");
	// A pin kept in the near look and its name keep their size on screen at every zoom.
	QCOMPARE(keptPin.size(), qsizetype(2));
	QCOMPARE_LT(std::abs(keptPin[0].width() - keptPin[1].width()), 1e-6);
	QCOMPARE_LT(std::abs(keptPin[0].height() - keptPin[1].height()), 1e-6);
	QCOMPARE_LT(std::abs(keptPin[0].width() - StationOverlayItem::MinPictogramPixels), 1.0);
	QCOMPARE_LT(std::abs(keptName[0] - keptName[1]), 1e-6);
}

void TestNetworkPresenter::modelWithoutTrackLinesDrawsNothing() {
	PreviewFixture f;
	NetworkPresenter& presenter = f.presenter;
	presenter.showPreview(classesModel());
	presenter.clear();
	const NetworkPresenter::PreviewOutcome empty = presenter.showPreview(SceneModel());
	// A model without track lines draws nothing.
	QVERIFY(!empty.available);
	QVERIFY(!empty.fitted);
	QVERIFY(f.scene.items().isEmpty());
	QVERIFY(registriesEmpty(presenter));
}

// A head of the preview names its section by its label, after the authored signals that protect it, and the presenter
// finds the signal of the preview by its track and section.
void TestNetworkPresenter::previewHeadNamesSection() {
	QGraphicsScene scene;
	NetworkPresenter presenter(&scene, nullptr);
	SceneModel model = previewModel();
	sceneSignals(model).push_back({"sig1", "@B0.block.2@"});
	presenter.showPreview(model);
	QStringList heads;
	for (QGraphicsItem* item : PresenterAccess::signalDecorations(presenter))
		if (const auto* head = qgraphicsitem_cast<SignalItem*>(item))
			heads << head->inspectionIdentity() + " | " + QString::fromStdString(head->sectionAheadId + " | " + head->sectionAheadLabel);
	heads.removeDuplicates();
	heads.sort();
	const QStringList expected{
		"Track B0 at 28 km, section B0 28.000 to 64.000 km (signal sig1) | @B0.block.2@ | B0 28.000 to 64.000 km",
		"Track B1 at 136 km, section B1 136.000 to 164.000 km | @B1.block.2@ | B1 136.000 to 164.000 km"};
	// A head of the preview names its section by its label and keeps its id.
	QCOMPARE(heads, expected);
	const TrackPreviewSignal* head = presenter.previewSignal("B0", "@B0.block.2@");
	// The signal of the preview is found by its track and its section.
	if (head == nullptr)
		QFAIL("the preview has no signal on track B0 for the section");
	QCOMPARE(head->label, "sig1 (B0 28.000 to 64.000 km)");
	QVERIFY(!presenter.previewSignal("B1", "@B0.block.2@"));
	presenter.clear();
}

// On a schematic line the picture and the name of a station count from level 0, whatever level its platform is on. A
// named offset counts in the original levels of track_separation units.
void TestNetworkPresenter::stationPicturesCountFromLevelZero() {
	SceneModel model;
	model.tracks = {{"low"}, {"high"}};
	model.nodes = {{"low.a", "low", 0.0, 0.0}, {"low.b", "low", 10.0, 0.0}, {"high.a", "high", 0.0, 0.0},
		{"high.b", "high", 10.0, 0.0}};
	model.arcs = {{"low.arc", "low", "low.a", "low.b", 0.0, 0.0, 12.0},
		{"high.arc", "high", "high.a", "high.b", 0.0, 0.0, 12.0}};
	model.blocks = {{"low.block", "low", 10.0}, {"high.block", "high", 10.0}};
	model.trackViews = {{"low", 0, 0}, {"high", 2, 0}};
	model.stationViews = {{"A", 1.0, 0.0, {{0, 0.0}}, {}}, {"Koge", 1.0, 0.1, {{0, 10.0}}, {}}};
	for (const auto& stationData : {
			 std::pair<const char*, const char*>{"A", "high.a"}, std::pair<const char*, const char*>{"Koge", "high.b"}}) {
		SceneStation station;
		station.id = station.name = stationData.first;
		station.platforms.push_back({std::string(stationData.first) + ".platform", {stationData.second}});
		model.stations.push_back(std::move(station));
	}
	QGraphicsScene scene;
	NetworkPresenter presenter(&scene, nullptr);
	// The bottom centre of the picture of each station, by name.
	const auto pictureAnchors = [&presenter]() {
		QMap<QString, QPointF> anchors;
		for (StationOverlayItem* overlay : presenter.stationOverlays())
			if (const QGraphicsPixmapItem* picture = PresenterAccess::stationPictures(presenter).value(overlay, nullptr))
				anchors.insert(overlay->stationName(),
					picture->mapToScene(QPointF(presenter.station_size / 2.0, presenter.station_size)));
		return anchors;
	};
	presenter.showPreview(model);
	const TrackPreviewLine* low = presenter.trackLine("low");
	const TrackPreviewLine* high = presenter.trackLine("high");
	const QMap<QString, QPointF> anchors = pictureAnchors();
	// Station pictures on a higher schematic level count from level 0.
	if (low == nullptr || high == nullptr)
		QFAIL("the preview has no line of both tracks");
	QCOMPARE_GT(low->levelSpacing, 0.0);
	QCOMPARE(anchors.size(), qsizetype(2));
	const qreal levelZero = low->points.front().y + low->displayOffset;
	const QPointF plain = anchors.value("A");
	const QPointF named = anchors.value("Koge");
	QCOMPARE_LT(std::fabs(plain.x() - high->points.front().x), 1e-6);
	QCOMPARE_LT(std::fabs(plain.y() - (levelZero + 1200.0 * presenter.presentationScale())), 1e-6);
	QCOMPARE_LT(std::fabs(named.x() - high->points.back().x), 1e-6);
	QCOMPARE_LT(std::fabs(named.y() - (levelZero + (1200.0 + 900.0) / presenter.track_separation * low->levelSpacing)), 1e-6);

	// Without the station views the lines are not schematic, and a picture stays at its station.
	model.stationViews.clear();
	presenter.clear();
	presenter.showPreview(model);
	const TrackPreviewLine* unmapped = presenter.trackLine("high");
	const QMap<QString, QPointF> unmappedAnchors = pictureAnchors();
	QPointF station;
	// A station picture on a higher level of a line that is not schematic stays at its station.
	if (unmapped == nullptr)
		QFAIL("the preview has no line of track high");
	QCOMPARE_NE(unmapped->displayOffset, 0.0);
	QVERIFY(unmappedAnchors.contains("A"));
	QVERIFY(previewPointAtNode(*unmapped, "high.a", unmapped->displayOffset, station));
	QCOMPARE_LT(QLineF(unmappedAnchors.value("A"), station).length(), 1e-6);
}

// A preview leaves no work queued on the scene, so previews that follow each other without the event loop in between do
// not pile it up.
void TestNetworkPresenter::previewLeavesNoQueuedCall() {
	QueueCountingScene scene;
	NetworkPresenter presenter(&scene, nullptr);
	const SceneModel model = previewModel();
	QCoreApplication::sendPostedEvents(&scene, QEvent::MetaCall);
	for (int cycle = 0; cycle < 5; ++cycle) {
		presenter.showPreview(model);
		scene.queuedCalls = 0;
		QCoreApplication::sendPostedEvents(&scene, QEvent::MetaCall);
		// A preview leaves no queued call on the scene.
		QCOMPARE(scene.queuedCalls, 0);
		presenter.clear();
	}
}

// Only the track lines whose state changes are painted again.
void TestNetworkPresenter::paintsOnlyTracksThatChangeState() {
	QGraphicsScene scene;
	NetworkPresenter presenter(&scene, nullptr);
	// Track lines far apart, so that the painting of one does not reach another.
	auto* prepared = new CountingTrack(QLineF(0, 0, 100, 0));
	auto* occupied = new CountingTrack(QLineF(0, 200, 100, 200));
	auto* blocked = new CountingTrack(QLineF(0, 400, 100, 400));
	auto* bothStates = new CountingTrack(QLineF(0, 600, 100, 600));
	const std::vector<CountingTrack*> tracks{prepared, occupied, blocked, bothStates};
	for (CountingTrack* track : tracks) {
		scene.addItem(track);
		PresenterAccess::allArcs(presenter).push_back(track);
	}
	PresenterAccess::tracksBySectionId(presenter)["prepared"] = {prepared, bothStates};
	PresenterAccess::tracksBySectionId(presenter)["blocked"] = {blocked};
	PresenterAccess::tracksByOccupiedArc(presenter)[{0, 0.0}] = occupied;
	PresenterAccess::tracksByOccupiedArc(presenter)[{1, 0.0}] = blocked;
	PresenterAccess::tracksByOccupiedArc(presenter)[{2, 0.0}] = bothStates;

	QGraphicsView view(&scene);
	view.resize(300, 400);
	view.show();
	pump(200);
	view.fitInView(scene.itemsBoundingRect(), Qt::KeepAspectRatio);
	const auto resetPaints = [&tracks]() {
		for (CountingTrack* track : tracks)
			track->paints = 0;
	};
	const auto paintedCount = [&tracks](auto paintedWhen) {
		return std::count_if(tracks.cbegin(), tracks.cend(), [paintedWhen](const CountingTrack* track) { return paintedWhen(track->paints); });
	};

	GuiSimulationSnapshot snapshot;
	snapshot.sectionStates = {GuiSectionState{"prepared", true, false}, GuiSectionState{"blocked", false, true}};
	snapshot.trains.resize(1);
	snapshot.trains[0].occupiedArcs = {GuiOccupiedArc{0, 0.0}, GuiOccupiedArc{2, 0.0}};
	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	// A track line shows the state of its section or of the arc that a train occupies, whichever ranks highest.
	QCOMPARE(prepared->operationalState(), TrackOperationalState::Prepared);
	QCOMPARE(occupied->operationalState(), TrackOperationalState::Occupied);
	QCOMPARE(blocked->operationalState(), TrackOperationalState::Blocked);
	QCOMPARE(bothStates->operationalState(), TrackOperationalState::Occupied);
	// The track lines of the arcs that a train occupies are occupied.
	QVERIFY(occupied->isOccupied());
	QVERIFY(bothStates->isOccupied());
	QVERIFY(!prepared->isOccupied());
	QVERIFY(!blocked->isOccupied());
	// The track lines that show a state are the active ones.
	QCOMPARE(PresenterAccess::activeTrackItems(presenter).size(), std::size_t(4));
	waitFor([&]() { return paintedCount([](int paints) { return paints > 0; }) == 4; });
	// The view paints the track lines that changed state.
	QCOMPARE(paintedCount([](int paints) { return paints > 0; }), std::ptrdiff_t(4));

	resetPaints();
	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	pump(300);
	// A step that changes no state paints no track line.
	QCOMPARE(paintedCount([](int paints) { return paints > 0; }), std::ptrdiff_t(0));

	// the train leaves the occupied arc and the section is blocked as well: only these two track lines change
	snapshot.trains[0].occupiedArcs = {GuiOccupiedArc{2, 0.0}};
	snapshot.sectionStates[1].prepared = true;
	snapshot.sectionStates[0].blocked = true;
	presenter.apply(snapshot, NetworkPresenter::Playback::Live);
	// A blocked section outranks the other states.
	QCOMPARE(occupied->operationalState(), TrackOperationalState::Free);
	QCOMPARE(bothStates->operationalState(), TrackOperationalState::Blocked);
	QCOMPARE(prepared->operationalState(), TrackOperationalState::Blocked);
	QCOMPARE(blocked->operationalState(), TrackOperationalState::Blocked);
	// A blocked track line that a train occupies stays occupied.
	QVERIFY(bothStates->isOccupied());
	QVERIFY(!occupied->isOccupied());
	// A track line that is free again is no longer active.
	QCOMPARE(PresenterAccess::activeTrackItems(presenter).size(), std::size_t(3));
	QVERIFY(!PresenterAccess::activeTrackItems(presenter).count(occupied));
	waitFor([occupied]() { return occupied->paints > 0; });
	// Only the track lines that changed state are painted.
	QCOMPARE_GT(occupied->paints, 0);
	QCOMPARE_GT(prepared->paints, 0);
	QCOMPARE_GT(bothStates->paints, 0);
	QCOMPARE(blocked->paints, 0);

	presenter.clearOperationalTrackStates();
	// Every track line is free after they are cleared.
	QVERIFY(PresenterAccess::activeTrackItems(presenter).empty());
	QCOMPARE(prepared->operationalState(), TrackOperationalState::Free);
	QCOMPARE(bothStates->operationalState(), TrackOperationalState::Free);
	view.hide();
	presenter.clear();
}

// A track view with double_switch draws a track of four nodes as the original drew a double switch: the middle arc and
// no node dot. Without it the track keeps its three arcs and the dot at its end.
void TestNetworkPresenter::doubleSwitchDrawsMiddleArc() {
	QGraphicsScene scene;
	NetworkPresenter presenter(&scene, nullptr);
	SceneModel model;
	model.tracks = {{"B0"}};
	model.nodes = {{"B0.1", "B0", 0.0, 0.0}, {"B0.2", "B0", 1.0, 0.0}, {"B0.3", "B0", 2.0, 0.0}, {"B0.4", "B0", 3.0, 0.0}};
	model.arcs = {{"B0.arc.1", "B0", "B0.1", "B0.2", 0.0, 0.0, 12.0}, {"B0.arc.2", "B0", "B0.2", "B0.3", 0.0, 0.0, 12.0},
		{"B0.arc.3", "B0", "B0.3", "B0.4", 0.0, 0.0, 12.0}};
	model.trackViews = {{"B0", 0, 0}};
	const auto drawn = [&scene]() {
		std::vector<std::string> arcs;
		int dots = 0;
		for (QGraphicsItem* item : scene.items()) {
			if (item->data(PreviewGraphics::Kind).toString() == QLatin1String("arc"))
				arcs.push_back(item->data(PreviewGraphics::Id).toString().toStdString());
			const auto* node = qgraphicsitem_cast<NodeItem*>(item);
			if (node && node->rect().width() > 0.0)
				++dots;
		}
		std::sort(arcs.begin(), arcs.end());
		return std::make_pair(arcs, dots);
	};
	presenter.showPreview(model);
	QVERIFY2(drawn() == std::make_pair(std::vector<std::string>{"B0.arc.1", "B0.arc.2", "B0.arc.3"}, 1),
		"a track of four nodes draws its three arcs and the dot at its end");
	presenter.clear();
	model.trackViews[0].doubleSwitch = true;
	presenter.showPreview(model);
	QVERIFY2(drawn() == std::make_pair(std::vector<std::string>{"B0.arc.2"}, 0),
		"a double switch draws its middle arc and no node dot");
	presenter.clear();
}

// A platform marks its node with a ring and its bar with a halo, and not the rest of its track.
void TestNetworkPresenter::platformMarksItsNodeAndBar() {
	PaimpolFixture f;
	const NetworkMarkItem* mark = f.show("platforms", "Gourland\nGourland.platform.1");
	if (mark == nullptr)
		QFAIL("a platform has a mark");
	QCOMPARE(ringIds(*mark), QStringList{"B0.node.41"});
	QVERIFY2(mark->stretches().isEmpty(), "a platform marks no stretch of its track");
	QCOMPARE(mark->haloTargets().size(), qsizetype(1));
	const auto* bar = qgraphicsitem_cast<const PlatformItem*>(mark->haloTargets().front());
	if (bar == nullptr)
		QFAIL("the halo of a platform is on its bar");
	// The bar is the one of the node, 1.15 squares from it.
	const qreal square = f.presenter.station_node_size * f.presenter.presentationScale();
	QCOMPARE_LT(QLineF(bar->mapToScene(bar->rect().center()), f.at("B0.node.41")).length(), 1.2 * square);
	// No track is drawn in another colour, and the legend has no selected track.
	QCOMPARE(cueColouredTracks(f.scene), 0);
	QVERIFY(!f.presenter.previewHasSelectedTrack());
}

// A route marks the course of each of its sections in travel order, B4 and B5 and the connections to them included, and
// not the sections of B0 that it passes by on them.
void TestNetworkPresenter::routeMarksItsSectionsAndConnections() {
	PaimpolFixture f;
	const NetworkMarkItem* mark = f.show("routes", "route2");
	if (mark == nullptr)
		QFAIL("a route has a mark");
	QCOMPARE(mark->stretches().size(), qsizetype(124));
	const std::vector<std::string> taken = {"@32-B0@-9.539000/@0-B4@-9.549000", "@1-B4@", "@2-B4@",
		"@3-B4@-10.347000/@35-B0@-10.357000", "@103-B0@-30.866000/@0-B5@-30.889000", "@1-B5@",
		"@2-B5@-31.342000/@105-B0@-31.365000", "@36-B0@", "@102-B0@"};
	QStringList all;
	for (const std::string& section : taken)
		all << QString::fromStdString(section);
	QCOMPARE(f.marked(*mark, taken), all);
	QCOMPARE(f.touched(*mark, {"@33-B0@", "@34-B0@", "@104-B0@"}), QStringList());
	// The connections are drawn from node to node.
	for (const auto& [from, to] : {std::pair<const char*, const char*>{"B0.node.101", "B4.node.3"}, {"B4.node.9", "B0.node.107"},
			 {"B0.node.310", "B5.node.2"}, {"B5.node.7", "B0.node.316"}})
		QCOMPARE_LT(distanceTo(mark->stretches(), (f.at(from) + f.at(to)) / 2.0), f.presenter.previewFit().bounds.width() * 1e-6);
	// The direction shows: the stretches run in travel order, from the start of 0-B3 to the end of 1-B2.
	QVERIFY(mark->directed());
	const SceneSectionDescriptor* first = f.inventory.exact("@0-B3@");
	const SceneSectionDescriptor* last = f.inventory.exact("@1-B2@");
	QVERIFY(first && last);
	QCOMPARE(mark->stretches().front(), previewSectionCourse(f.presenter.trackPreview(), *first));
	QCOMPARE(mark->stretches().back(), previewSectionCourse(f.presenter.trackPreview(), *last));
	QVERIFY(mark->emphasis().isEmpty());
	// The section chosen under Route sections is drawn over the route in the second colour.
	mark = f.show("routes", "route2", 3);
	if (mark == nullptr)
		QFAIL("a route has a mark");
	const SceneSectionDescriptor* chosen = f.inventory.exact("@3-B0@");
	QVERIFY(chosen);
	QCOMPARE(mark->emphasis(), QList<QPolygonF>{previewSectionCourse(f.presenter.trackPreview(), *chosen)});
	QCOMPARE(mark->stretches().size(), qsizetype(124));
}

// A station marks its own station mark and platform nodes, so two stations on one track have different marks.
void TestNetworkPresenter::stationsHaveMarksOfTheirOwn() {
	PaimpolFixture f;
	const NetworkMarkItem* mark = f.show("stations", "Gourland");
	if (mark == nullptr)
		QFAIL("a station has a mark");
	const QStringList gourland = ringIds(*mark);
	QVERIFY(mark->stretches().isEmpty());
	mark = f.show("stations", "Frynaudour");
	if (mark == nullptr)
		QFAIL("a station has a mark");
	const QStringList frynaudour = ringIds(*mark);
	QCOMPARE(gourland, QStringList({"B0.node.41", "Gourland"}));
	QCOMPARE(frynaudour, QStringList({"B0.node.241", "Frynaudour"}));
	// A station with platforms on two tracks rings both nodes.
	mark = f.show("stations", "Tregonneau_Squiffiec");
	if (mark == nullptr)
		QFAIL("a station has a mark");
	QCOMPARE(ringIds(*mark), QStringList({"B0.node.102", "B4.node.4", "Tregonneau_Squiffiec"}));
	QCOMPARE(cueColouredTracks(f.scene), 0);
}

// A node is ringed, and a block marks the course of its section only.
void TestNetworkPresenter::nodeAndBlockMarkThatItem() {
	PaimpolFixture f;
	const NetworkMarkItem* mark = f.show("nodes", "B0.node.41");
	if (mark == nullptr)
		QFAIL("a node has a mark");
	QCOMPARE(ringIds(*mark), QStringList{"B0.node.41"});
	QVERIFY(mark->stretches().isEmpty());
	mark = f.show("blocks", "36-B0");
	if (mark == nullptr)
		QFAIL("a block has a mark");
	const SceneSectionDescriptor* section = f.inventory.exact("@36-B0@");
	QVERIFY(section);
	QCOMPARE(mark->stretches(), QList<QPolygonF>{previewSectionCourse(f.presenter.trackPreview(), *section)});
	QCOMPARE(f.marked(*mark, {"@36-B0@"}), QStringList{"@36-B0@"});
	QCOMPARE(f.touched(*mark, {"@35-B0@", "@36-B0@", "@37-B0@"}), QStringList{"@36-B0@"});
	// An arc and a connection mark the line between their ends.
	mark = f.show("connections", "connection.5");
	if (mark == nullptr)
		QFAIL("a connection has a mark");
	QCOMPARE(mark->stretches(), QList<QPolygonF>{QPolygonF({f.at("B0.node.101"), f.at("B4.node.3")})});
	QCOMPARE(cueColouredTracks(f.scene), 0);
}

// A signal rings its heads, and a signalling area, a single-track restriction, a station boundary and a block dependency
// mark the sections or the kilometres that they name. The kinds without an id are named by their row.
void TestNetworkPresenter::otherKindsHaveMarks() {
	PaimpolFixture f;
	const NetworkMarkItem* mark = f.show("signals", "sig.36");
	if (mark == nullptr)
		QFAIL("a signal has a mark");
	QCOMPARE(ringIds(*mark), QStringList({"@36-B0@", "@36-B0@"}));
	QVERIFY(std::all_of(mark->ringTargets().cbegin(), mark->ringTargets().cend(),
		[](const QGraphicsItem* target) { return qgraphicsitem_cast<const SignalItem*>(target) != nullptr; }));

	mark = f.show("signalling_areas", "area.B0");
	if (mark == nullptr)
		QFAIL("a signalling area has a mark");
	const TrackPreviewLine* b0 = f.presenter.trackLine("B0");
	QVERIFY(b0);
	QPointF start;
	QPointF end;
	QVERIFY(previewPointAtX(*b0, 2.0, b0->displayOffset, start));
	QVERIFY(previewPointAtX(*b0, 3.0, b0->displayOffset, end));
	QCOMPARE(mark->stretches().size(), qsizetype(1));
	QCOMPARE(mark->stretches().front().front(), start);
	QCOMPARE(mark->stretches().front().back(), end);
	const qreal near = f.presenter.previewFit().bounds.width() * 1e-6;
	QCOMPARE_LT(distanceTo(mark->stretches(), f.at("B0.node.41")), near);

	// The first restriction runs from 2-B0 to 31-B0, between two switch sections.
	mark = f.show("single_track_restrictions", "0");
	if (mark == nullptr)
		QFAIL("a single-track restriction has a mark");
	QCOMPARE(mark->stretches().size(), qsizetype(32));
	QCOMPARE(f.marked(*mark, {"@2-B0@", "@20-B0@", "@31-B0@", "@1-B3@-0.250000/@1-B0@-0.300000", "@32-B0@-9.539000/@0-B4@-9.549000"})
				 .size(),
		qsizetype(5));
	QCOMPARE(f.touched(*mark, {"@33-B0@", "@50-B0@"}), QStringList());

	mark = f.show("station_boundaries", "0");
	if (mark == nullptr)
		QFAIL("a station boundary has a mark");
	QCOMPARE(f.marked(*mark, {"@40-B0@", "@42-B0@"}), QStringList({"@40-B0@", "@42-B0@"}));
	QCOMPARE(f.touched(*mark, {"@39-B0@", "@41-B0@", "@43-B0@"}), QStringList());

	mark = f.show("block_dependencies", "0");
	if (mark == nullptr)
		QFAIL("a block dependency has a mark");
	QCOMPARE(f.marked(*mark, {"@36-B0@", "@38-B0@"}), QStringList({"@36-B0@", "@38-B0@"}));
	QCOMPARE(f.touched(*mark, {"@37-B0@"}), QStringList());
	QVERIFY2(!f.show("block_dependencies", "1"), "a row that does not exist has no mark");
}

// Choosing another row moves the mark and keeps the zoom and the position of the view; only a mark that is wholly outside
// the view moves it, onto the mark at the same zoom.
void TestNetworkPresenter::anotherRowKeepsTheView() {
	PaimpolFixture f;
	f.show("stations", "Gourland");
	QVERIFY(f.view.zoomBy(8.0));
	f.view.centerOn(f.at("B0.node.41"));
	const QTransform transform = f.view.transform();
	const auto centre = [&f]() { return f.view.mapToScene(f.view.viewport()->rect().center()); };
	const QPointF before = centre();
	const qsizetype items = f.scene.items().size();
	QVERIFY(f.presenter.markPreviewSelection(f.model, PaimpolFixture::options("platforms", "Gourland\nGourland.platform.1")));
	QCOMPARE(f.view.transform(), transform);
	QCOMPARE(centre(), before);
	// Only the mark is replaced.
	QCOMPARE(f.scene.items().size(), items);
	if (f.presenter.selectionMark() == nullptr)
		QFAIL("the platform has a mark");
	QCOMPARE(ringIds(*f.presenter.selectionMark()), QStringList{"B0.node.41"});

	const QPointF frynaudour = f.at("B0.node.241");
	QVERIFY(!f.view.mapToScene(f.view.viewport()->rect()).boundingRect().contains(frynaudour));
	QVERIFY(f.presenter.markPreviewSelection(f.model, PaimpolFixture::options("stations", "Frynaudour")));
	QCOMPARE(f.view.transform(), transform);
	QVERIFY2(f.view.mapToScene(f.view.viewport()->rect()).boundingRect().contains(frynaudour),
		"a mark wholly outside the view centres the view on it");
}

// A mark goes in place of another only on the preview of the model generation that it was drawn from, so the caller draws
// the preview again after an edit.
void TestNetworkPresenter::markInPlaceNeedsTheDrawnModel() {
	PaimpolFixture f;
	f.show("stations", "Gourland");
	const NetworkMarkItem* drawn = f.presenter.selectionMark();
	QVERIFY(drawn);
	QVERIFY(f.presenter.previewShown());
	NetworkPresenter::PreviewOptions edited = PaimpolFixture::options("stations", "Frynaudour");
	edited.modelGeneration = 2;
	QVERIFY2(!f.presenter.markPreviewSelection(f.model, edited), "an edited model is drawn again");
	edited.modelGeneration.reset();
	QVERIFY2(!f.presenter.markPreviewSelection(f.model, edited), "a model of unknown generation is drawn again");
	QCOMPARE(f.presenter.selectionMark(), drawn);
	f.presenter.clear();
	QVERIFY(!f.presenter.previewShown());
	QVERIFY2(!f.presenter.markPreviewSelection(f.model, PaimpolFixture::options("stations", "Frynaudour")),
		"without a preview nothing is marked in place");
}

// The colours of the mark read on the canvas and on the area of a station, are neither red nor green, and differ from the
// selection cue and the other colours of the canvas.
void TestNetworkPresenter::markColoursStandApart() {
	const auto channel = [](int value) {
		const double unit = value / 255.0;
		return unit <= 0.03928 ? unit / 12.92 : std::pow((unit + 0.055) / 1.055, 2.4);
	};
	const auto luminance = [&channel](const QColor& color) {
		return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green()) + 0.0722 * channel(color.blue());
	};
	const auto contrast = [&luminance](const QColor& first, const QColor& second) {
		const double a = luminance(first);
		const double b = luminance(second);
		return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
	};
	const auto distance = [](const QColor& first, const QColor& second) {
		return std::hypot(first.red() - second.red(), first.green() - second.green(), first.blue() - second.blue());
	};
	std::vector<QColor> used = {kSelectionCueColor, QColor(Qt::white), QColor(Qt::red), QColor(Qt::yellow), QColor(Qt::green),
		defaultTrainFill(), defaultTrainOutline(), stationMarkFill(), QColor(80, 120, 210)};
	for (const double speed : {0.0, 20.0, 40.0, 60.0})
		used.push_back(classifyTrackSpeed(speed).color);
	for (const TrackOperationalState state : {TrackOperationalState::Prepared, TrackOperationalState::Occupied, TrackOperationalState::Blocked})
		used.push_back(classifyTrackState(state).color);
	for (const int code : {0, 75, 180, 270, -1})
		used.push_back(classifySignalAspect(code).lamp);
	const QColor stationArea(30, 47, 73);
	for (const QColor& colour : {kNetworkMarkColor, kNetworkMarkEmphasisColor}) {
		QCOMPARE_GE(contrast(colour, canvasColor()), 4.5);
		QCOMPARE_GE(contrast(colour, stationArea), 4.5);
		const int hue = colour.hsvHue();
		QVERIFY2(hue > 20 && hue < 340 && (hue < 80 || hue > 160), "the mark is neither red nor green");
		double nearest = 1e9;
		for (const QColor& other : used)
			nearest = std::min(nearest, distance(colour, other));
		QCOMPARE_GE(nearest, 70.0);
	}
	// The two colours of the mark tell apart.
	QCOMPARE_GE(distance(kNetworkMarkColor, kNetworkMarkEmphasisColor), 70.0);
}

QTEST_MAIN(TestNetworkPresenter)
#include "test_networkpresenter.moc"
