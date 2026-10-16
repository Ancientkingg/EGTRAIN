// SceneModel.h has a member named signals, so it comes before the Qt headers.
#include "scene/SceneModel.h"

#include "graphics/NetworkPresenter.h"

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
#include <QGraphicsItemGroup>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QLoggingCategory>
#include <QPixmap>

#include <iostream>
#include <memory>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
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
		for (int cycle = 0; cycle < 3; ++cycle) {
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

		const NetworkPresenter::PreviewOutcome empty = presenter.showPreview(SceneModel());
		ok &= expect(!empty.available && !empty.fitted && scene.items().isEmpty() && registriesEmpty(presenter),
			"a model without track lines draws nothing");
	}

	if (!ok)
		return 1;

	std::cout << "all NetworkPresenter tests passed\n";
	return 0;
}
