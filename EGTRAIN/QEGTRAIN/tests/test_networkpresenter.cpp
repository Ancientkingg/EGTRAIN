// SceneModel.h has a member named signals, so it comes before the Qt headers.
#include "scene/SceneModel.h"

#include "graphics/NetworkPresenter.h"

#include "app/GuiSimulationSnapshot.h"
#include "graphics/NetworkView.h"
#include "graphics/VisualPolish.h"
#include "graphics/items/HighlightEffect.h"
#include "graphics/items/PassengerItem.h"
#include "graphics/items/PlatformItem.h"
#include "graphics/items/SignalItem.h"
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
#include <QLoggingCategory>
#include <QPixmap>

#include <algorithm>
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
	// The station icons are resources of the application, which this test does not have.
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
		int drawnItems = 0;
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
		const int stationDecorations = presenter.m_stationDecorations.size();
		presenter.clear();
		options.platformBars = true;
		presenter.showPreview(model, options);
		ok &= expect(presenter.m_stationDecorations.size() > stationDecorations, "platform bars are added to the station decorations");
		presenter.clear();

		options.markSelection = true;
		int drawnWithOptions = 0;
		for (int cycle = 0; cycle < 25; ++cycle) {
			presenter.showPreview(model, options);
			if (cycle == 0)
				drawnWithOptions = scene.items().size();
			ok &= expect(drawnWithOptions > 0 && scene.items().size() == drawnWithOptions,
				"every cycle with a selection and platform bars draws the same items");
			presenter.clear();
			ok &= expect(registriesEmpty(presenter) && pointersNull(presenter) && scene.items().isEmpty(),
				"clear after a preview with options leaves the registries empty and the scene as empty as a new scene");
		}

		const NetworkPresenter::PreviewOutcome empty = presenter.showPreview(SceneModel());
		ok &= expect(!empty.available && !empty.fitted && scene.items().isEmpty() && registriesEmpty(presenter),
			"a model without track lines draws nothing");
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

	if (!ok)
		return 1;

	std::cout << "all NetworkPresenter tests passed\n";
	return 0;
}
