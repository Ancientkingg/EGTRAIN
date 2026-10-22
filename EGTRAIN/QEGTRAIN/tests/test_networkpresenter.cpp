// SceneModel.h has a member named signals, so it comes before the Qt headers.
#include "scene/SceneModel.h"

#include "graphics/NetworkPresenter.h"

#include "app/GuiSimulationSnapshot.h"
#include "graphics/NetworkScene.h"
#include "graphics/NetworkView.h"
#include "graphics/VisualPolish.h"
#include "graphics/items/HighlightEffect.h"
#include "graphics/items/PassengerItem.h"
#include "graphics/items/PlatformItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/TrainBadgeItem.h"
#include "graphics/items/TrainItemGroup.h"

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
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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
	presenter.allTrains.push_back(train);
	presenter.trainPaxItem = train;
	auto* badge = new TrainBadgeItem;
	scene.addItem(badge);
	presenter.m_trainBadges.insert(0, badge);
	presenter.m_prevTrainPositions.insert(0, QPointF(1, 2));

	auto* signal = new SignalItem(QRectF(0, 0, 4, 4));
	scene.addItem(signal);
	presenter.allSignals.push_back(signal);
	presenter.m_signalsByAheadId["section"].push_back(signal);
	presenter.m_inspectedSignal = signal;
	auto* signalDecoration = scene.addLine(0, 0, 5, 5);
	presenter.m_signalDecorations.push_back(signalDecoration);

	auto* track = new TrackLineItem(QLineF(0, 0, 10, 0));
	scene.addItem(track);
	presenter.allArcs.push_back(track);
	presenter.m_tracksBySectionId["section"].push_back(track);
	presenter.m_tracksByOccupiedArc[{0, 0.0}] = track;
	presenter.m_activeTrackItems.insert(track);

	auto* platform = new PlatformItem(QRectF(0, 0, 8, 2));
	scene.addItem(platform);
	presenter.allPlatforms.push_back(platform);
	auto* overlay = new StationOverlayItem(QStringLiteral("Station"), QPointF(3, 3), classifyStation());
	scene.addItem(overlay);
	presenter.m_stationOverlays.push_back(overlay);
	presenter.m_stationDecorations.push_back(overlay);
	auto* picture = scene.addPixmap(QPixmap(4, 4));
	presenter.m_stationPictures.insert(overlay, picture);
	presenter.m_stationLabels.push_back(scene.addText(QStringLiteral("Station")));

	auto* message = new QGraphicsItemGroup;
	scene.addItem(message);
	presenter.m_vcMessageItems.insert(0, message);

	auto* trainInfo = new QGraphicsItemGroup;
	scene.addItem(trainInfo);
	presenter.trainPaxInfoItem = trainInfo;
	auto* passenger = new PassengerItem(QPixmap(2, 2));
	scene.addItem(passenger);
	presenter.paxIconItem = passenger;
	auto* passengerInfo = new QGraphicsItemGroup;
	scene.addItem(passengerInfo);
	presenter.paxIconInfoItem = passengerInfo;

	presenter.effect = new HighlightEffect(Qt::blue, 1);
	badge->setGraphicsEffect(presenter.effect);
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
	return presenter.allTrains.isEmpty() && presenter.m_trainBadges.isEmpty() && presenter.m_prevTrainPositions.isEmpty()
		&& presenter.allSignals.isEmpty() && presenter.m_signalsByAheadId.empty() && presenter.m_signalDecorations.isEmpty()
		&& presenter.allArcs.isEmpty() && presenter.m_tracksBySectionId.empty() && presenter.m_tracksByOccupiedArc.empty()
		&& presenter.m_activeTrackItems.empty() && presenter.allPlatforms.isEmpty() && presenter.m_stationOverlays.isEmpty()
		&& presenter.m_stationDecorations.isEmpty() && presenter.m_stationLabels.isEmpty() && presenter.m_stationPictures.isEmpty()
		&& presenter.m_vcMessageItems.isEmpty();
}

static bool pointersNull(const NetworkPresenter& presenter) {
	return !presenter.m_inspectedSignal && !presenter.trainPaxItem && !presenter.trainPaxInfoItem
		&& !presenter.paxIconItem && !presenter.paxIconInfoItem && !presenter.effect;
}

int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	// The entity icons are resources of the application, which this test does not have.
	QLoggingCategory::setFilterRules(QStringLiteral("qt.svg.warning=false"));
	bool ok = true;

	{
		QGraphicsScene scene;
		NetworkPresenter presenter(&scene, nullptr);
		ok &= expect(registriesEmpty(presenter) && pointersNull(presenter) && scene.items().isEmpty(),
			"a new presenter has no registry entry and no pointer");
		for (int cycle = 0; cycle < 3; ++cycle) {
			fill(presenter, scene);
			scene.addText(QStringLiteral("not registered"));
			ok &= expect(!registriesEmpty(presenter) && !pointersNull(presenter) && !scene.items().isEmpty(),
				"the filled presenter holds the items");
			presenter.clear();
			ok &= expect(registriesEmpty(presenter), "clear empties every registry");
			ok &= expect(pointersNull(presenter), "clear resets every pointer");
			ok &= expect(scene.items().isEmpty(), "clear leaves the scene as empty as a new scene");
		}
	}
	{
		auto scene = std::make_unique<QGraphicsScene>();
		NetworkPresenter presenter(scene.get(), nullptr);
		fill(presenter, *scene);
		scene.reset();
		presenter.clear();
		ok &= expect(registriesEmpty(presenter) && pointersNull(presenter),
			"clear empties the registries after the scene is gone");
	}
	{
		NetworkPresenter presenter(nullptr, nullptr);
		presenter.m_prevTrainPositions.insert(0, QPointF());
		presenter.clear();
		ok &= expect(registriesEmpty(presenter), "clear works without a scene");
	}

	{
		QGraphicsScene scene;
		NetworkView view;
		view.setScene(&scene);
		NetworkPresenter presenter(&scene, &view);
		const SceneModel model = previewModel();
		qsizetype drawnItems = 0;
		for (int cycle = 0; cycle < 25; ++cycle) {
			const NetworkPresenter::PreviewOutcome outcome = presenter.showPreview(model);
			ok &= expect(outcome.available && outcome.trackLines == 2 && outcome.fitted,
				"the preview draws both track lines and fits the view");
			ok &= expect(!presenter.m_stationOverlays.isEmpty() && !presenter.m_signalDecorations.isEmpty()
					&& presenter.m_previewHasSignals && !presenter.m_previewFitBounds.isEmpty()
					&& presenter.trackLine("B0") && !presenter.trackLine("none"),
				"the preview fills the registries and keeps the preview it drew");
			if (cycle == 0)
				drawnItems = scene.items().size();
			ok &= expect(drawnItems > 0 && scene.items().size() == drawnItems, "every cycle draws the same items");
			presenter.clear();
			ok &= expect(registriesEmpty(presenter) && pointersNull(presenter) && scene.items().isEmpty(),
				"clear after a preview leaves the registries empty and the scene as empty as a new scene");
		}

		NetworkPresenter::PreviewOptions options;
		options.selectedFacet = "tracks";
		options.selectedId = "B1";
		presenter.showPreview(model, options);
		ok &= expect(presenter.m_previewHasSelectedTrack, "a selected track is marked");
		// The preview draws the platform bars without being asked, among the station decorations, hidden in the far look
		// and shown in the near look.
		const auto bars = [&presenter](bool visible) {
			return std::count_if(presenter.m_stationDecorations.cbegin(), presenter.m_stationDecorations.cend(),
				[visible](QGraphicsItem* item) { return qgraphicsitem_cast<PlatformItem*>(item) && item->isVisible() == visible; });
		};
		ok &= expect(bars(false) > 0 && bars(true) == 0, "the preview draws the platform bars, hidden in the far look");
		presenter.setStationNearLook(true);
		ok &= expect(bars(true) > 0 && bars(false) == 0, "the platform bars show in the near look");
		// B0 runs diagonally and bends at Gdg, its station between Gvc and Ut.
		ok &= expect(barsAlong(scene, *presenter.trackLine("B0"), 2), "a platform bar is a scene item over the station marks and lies along its track, on the upper side");
		const auto drawnBars = bars(true) + bars(false);
		presenter.clear();
		// A virtual station gets no platform bar, as it gets no overlay.
		SceneModel withVirtual = model;
		withVirtual.stations.push_back({"virtualLink", "virtualLink", false, 0.0, {{"virtualLink.platform", {"B1.Gdg"}}}});
		presenter.showPreview(withVirtual);
		ok &= expect(bars(true) + bars(false) == drawnBars, "a virtual station gets no platform bar");
		presenter.clear();
		// With Ut at the longitude of Gdg the track runs straight up from Gdg to Ut.
		SceneModel vertical = model;
		vertical.stationViews[2].longitude = 0.28;
		presenter.showPreview(vertical, options);
		ok &= expect(barsAlong(scene, *presenter.trackLine("B0"), 2), "the platform bar of a vertical track lies on its left");
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
		ok &= expect(upright, "the platform bar at the vertical end of a line stands upright on its left");
		presenter.clear();

		options.markSelection = true;
		qsizetype drawnWithOptions = 0;
		for (int cycle = 0; cycle < 25; ++cycle) {
			presenter.showPreview(model, options);
			if (cycle == 0)
				drawnWithOptions = scene.items().size();
			ok &= expect(drawnWithOptions > 0 && scene.items().size() == drawnWithOptions,
				"every cycle with a selection draws the same items");
			presenter.clear();
			ok &= expect(registriesEmpty(presenter) && pointersNull(presenter) && scene.items().isEmpty(),
				"clear after a preview with options leaves the registries empty and the scene as empty as a new scene");
		}

		// Ut gets three connection ends, and a station placed by position has no platform.
		SceneModel classes = model;
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
		presenter.showPreview(classes);
		QStringList marks;
		for (StationOverlayItem* overlay : presenter.m_stationOverlays) {
			const auto* picture = static_cast<const StationMarkItem*>(presenter.m_stationPictures.value(overlay, nullptr));
			if (picture && picture->stationClass() == overlay->visual().cls)
				marks << overlay->stationName() + "=" + QString::number(static_cast<int>(overlay->visual().cls));
		}
		marks.sort();
		ok &= expect(marks == QStringList({"Gdg=1", "Gvc=1", "Hlt=0", "Hsq=0", "Ut=2"}),
			"a station gets the mark of its class from its connections and platforms");
		// The mark of a station with platforms gets the centres of their station squares, one without them those of the
		// squares at its position, as in a run.
		bool centred = !presenter.m_stationOverlays.isEmpty();
		for (StationOverlayItem* overlay : presenter.m_stationOverlays) {
			const auto* mark = dynamic_cast<const StationMarkItem*>(presenter.m_stationPictures.value(overlay, nullptr));
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
					for (QGraphicsItem* item : scene.items())
						if (qgraphicsitem_cast<StationNodeItem*>(item)
							&& item->data(PreviewGraphics::Id).toString() == QString::fromStdString(nodeId)
							&& !expected.contains(item->sceneBoundingRect().center()))
							expected.append(item->sceneBoundingRect().center());
			if (station->platforms.empty())
				for (QGraphicsItem* item : scene.items())
					if (qgraphicsitem_cast<StationNodeItem*>(item)
						&& item->data(PreviewGraphics::StationId).toString() == QString::fromStdString(station->id))
						expected.append(item->sceneBoundingRect().center());
			centred = centred && mark->nodeCentres() == expected && (overlay->stationName() != "Gdg" || expected.size() == 2)
				&& (overlay->stationName() != "Hsq" || expected.size() == 1) && (overlay->stationName() != "Hlt" || expected.isEmpty());
		}
		ok &= expect(centred, "the preview gives each station mark the centres of its station squares");
		// In the near look before a run, as in a run, the halt with a square shows its square alone, and the halt without
		// one keeps its pin and its name at their smallest size on screen at every zoom.
		const auto overlayNamed = [&presenter](const QString& name) -> StationOverlayItem* {
			for (StationOverlayItem* overlay : presenter.m_stationOverlays)
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
		ok &= expect(squareAlone, "before a run a halt with a square draws no pin in the near look");
		ok &= expect(keptPin.size() == 2 && std::abs(keptPin[0].width() - keptPin[1].width()) < 1e-6
				&& std::abs(keptPin[0].height() - keptPin[1].height()) < 1e-6
				&& std::abs(keptPin[0].width() - StationOverlayItem::MinPictogramPixels) < 1.0
				&& std::abs(keptName[0] - keptName[1]) < 1e-6,
			"a pin kept in the near look and its name keep their size on screen at every zoom");
		presenter.clear();

		const NetworkPresenter::PreviewOutcome empty = presenter.showPreview(SceneModel());
		ok &= expect(!empty.available && !empty.fitted && scene.items().isEmpty() && registriesEmpty(presenter),
			"a model without track lines draws nothing");
	}

	// A head of the preview names its section by its label, after the authored signals that protect it, and the
	// presenter finds the signal of the preview by its track and section.
	{
		QGraphicsScene scene;
		NetworkPresenter presenter(&scene, nullptr);
		SceneModel model = previewModel();
		sceneSignals(model).push_back({"sig1", "@B0.block.2@"});
		presenter.showPreview(model);
		QStringList heads;
		for (QGraphicsItem* item : presenter.m_signalDecorations)
			if (const auto* head = qgraphicsitem_cast<SignalItem*>(item))
				heads << head->inspectionIdentity() + " | " + QString::fromStdString(head->sectionAheadId + " | " + head->sectionAheadLabel);
		heads.removeDuplicates();
		heads.sort();
		const QStringList expected{
			"Track B0 at 28 km; section B0 28.000 to 64.000 km; signal ID sig1 | @B0.block.2@ | B0 28.000 to 64.000 km",
			"Track B1 at 136 km; section B1 136.000 to 164.000 km | @B1.block.2@ | B1 136.000 to 164.000 km"};
		ok &= expect(heads == expected, "a head of the preview names its section by its label and keeps its id");
		const TrackPreviewSignal* head = presenter.previewSignal("B0", "@B0.block.2@");
		ok &= expect(head && head->label == "sig1 (B0 28.000 to 64.000 km)" && !presenter.previewSignal("B1", "@B0.block.2@"),
			"the signal of the preview is found by its track and its section");
		presenter.clear();
	}

	// On a schematic line the picture and the name of a station count from level 0, whatever level its platform is
	// on. A named offset counts in the original levels of track_separation units.
	{
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
			for (StationOverlayItem* overlay : presenter.m_stationOverlays)
				if (const QGraphicsPixmapItem* picture = presenter.m_stationPictures.value(overlay, nullptr))
					anchors.insert(overlay->stationName(),
						picture->mapToScene(QPointF(presenter.station_size / 2.0, presenter.station_size)));
			return anchors;
		};
		presenter.showPreview(model);
		const TrackPreviewLine* low = presenter.trackLine("low");
		const TrackPreviewLine* high = presenter.trackLine("high");
		const QMap<QString, QPointF> anchors = pictureAnchors();
		bool placed = low && high && low->levelSpacing > 0.0 && anchors.size() == 2;
		if (placed) {
			const qreal levelZero = low->points.front().y + low->displayOffset;
			const QPointF plain = anchors.value("A");
			const QPointF named = anchors.value("Koge");
			placed = std::fabs(plain.x() - high->points.front().x) < 1e-6
				&& std::fabs(plain.y() - (levelZero + 1200.0 * presenter.presentationScale())) < 1e-6
				&& std::fabs(named.x() - high->points.back().x) < 1e-6
				&& std::fabs(named.y() - (levelZero + (1200.0 + 900.0) / presenter.track_separation * low->levelSpacing))
					< 1e-6;
		}
		ok &= expect(placed, "station pictures on a higher schematic level count from level 0");

		// Without the station views the lines are not schematic, and a picture stays at its station.
		model.stationViews.clear();
		presenter.clear();
		presenter.showPreview(model);
		const TrackPreviewLine* unmapped = presenter.trackLine("high");
		const QMap<QString, QPointF> unmappedAnchors = pictureAnchors();
		QPointF station;
		ok &= expect(unmapped && unmapped->displayOffset != 0.0 && unmappedAnchors.contains("A")
				&& previewPointAtNode(*unmapped, "high.a", unmapped->displayOffset, station)
				&& QLineF(unmappedAnchors.value("A"), station).length() < 1e-6,
			"a station picture on a higher level of a line that is not schematic stays at its station");
	}

	// A preview leaves no work queued on the scene, so previews that follow each other without the event loop in
	// between do not pile it up.
	{
		QueueCountingScene scene;
		NetworkPresenter presenter(&scene, nullptr);
		const SceneModel model = previewModel();
		QCoreApplication::sendPostedEvents(&scene, QEvent::MetaCall);
		for (int cycle = 0; cycle < 5; ++cycle) {
			presenter.showPreview(model);
			scene.queuedCalls = 0;
			QCoreApplication::sendPostedEvents(&scene, QEvent::MetaCall);
			ok &= expect(scene.queuedCalls == 0, "a preview leaves no queued call on the scene");
			presenter.clear();
		}
	}

	// Only the track lines whose state changes are painted again.
	{
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
			presenter.allArcs.push_back(track);
		}
		presenter.m_tracksBySectionId["prepared"] = {prepared, bothStates};
		presenter.m_tracksBySectionId["blocked"] = {blocked};
		presenter.m_tracksByOccupiedArc[{0, 0.0}] = occupied;
		presenter.m_tracksByOccupiedArc[{1, 0.0}] = blocked;
		presenter.m_tracksByOccupiedArc[{2, 0.0}] = bothStates;

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
		ok &= expect(prepared->operationalState() == TrackOperationalState::Prepared
				&& occupied->operationalState() == TrackOperationalState::Occupied
				&& blocked->operationalState() == TrackOperationalState::Blocked
				&& bothStates->operationalState() == TrackOperationalState::Occupied,
			"a track line shows the state of its section or of the arc that a train occupies, whichever ranks highest");
		ok &= expect(occupied->isOccupied() && bothStates->isOccupied() && !prepared->isOccupied() && !blocked->isOccupied(),
			"the track lines of the arcs that a train occupies are occupied");
		ok &= expect(presenter.m_activeTrackItems.size() == 4, "the track lines that show a state are the active ones");
		waitFor([&]() { return paintedCount([](int paints) { return paints > 0; }) == 4; });
		ok &= expect(paintedCount([](int paints) { return paints > 0; }) == 4, "the view paints the track lines that changed state");

		resetPaints();
		presenter.apply(snapshot, NetworkPresenter::Playback::Live);
		pump(300);
		ok &= expect(paintedCount([](int paints) { return paints > 0; }) == 0, "a step that changes no state paints no track line");

		// the train leaves the occupied arc and the section is blocked as well: only these two track lines change
		snapshot.trains[0].occupiedArcs = {GuiOccupiedArc{2, 0.0}};
		snapshot.sectionStates[1].prepared = true;
		snapshot.sectionStates[0].blocked = true;
		presenter.apply(snapshot, NetworkPresenter::Playback::Live);
		ok &= expect(occupied->operationalState() == TrackOperationalState::Free
				&& bothStates->operationalState() == TrackOperationalState::Blocked
				&& prepared->operationalState() == TrackOperationalState::Blocked
				&& blocked->operationalState() == TrackOperationalState::Blocked,
			"a blocked section outranks the other states");
		ok &= expect(bothStates->isOccupied() && !occupied->isOccupied(), "a blocked track line that a train occupies stays occupied");
		ok &= expect(presenter.m_activeTrackItems.size() == 3 && !presenter.m_activeTrackItems.count(occupied),
			"a track line that is free again is no longer active");
		waitFor([occupied]() { return occupied->paints > 0; });
		ok &= expect(occupied->paints > 0 && prepared->paints > 0 && bothStates->paints > 0 && blocked->paints == 0,
			"only the track lines that changed state are painted");

		presenter.clearOperationalTrackStates();
		ok &= expect(presenter.m_activeTrackItems.empty() && prepared->operationalState() == TrackOperationalState::Free
				&& bothStates->operationalState() == TrackOperationalState::Free,
			"every track line is free after they are cleared");
		view.hide();
		presenter.clear();
	}

	// A track view with double_switch draws a track of four nodes as the original drew a double switch: the middle arc
	// and no node dot. Without it the track keeps its three arcs and the dot at its end.
	{
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
		ok &= expect(drawn() == std::make_pair(std::vector<std::string>{"B0.arc.1", "B0.arc.2", "B0.arc.3"}, 1),
			"a track of four nodes draws its three arcs and the dot at its end");
		presenter.clear();
		model.trackViews[0].doubleSwitch = true;
		presenter.showPreview(model);
		ok &= expect(drawn() == std::make_pair(std::vector<std::string>{"B0.arc.2"}, 0),
			"a double switch draws its middle arc and no node dot");
		presenter.clear();
	}

	if (!ok)
		return 1;

	std::cout << "all NetworkPresenter tests passed\n";
	return 0;
}
