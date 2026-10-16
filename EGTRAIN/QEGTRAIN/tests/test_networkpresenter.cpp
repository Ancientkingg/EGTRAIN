#include "graphics/NetworkPresenter.h"

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
	bool ok = true;

	{
		QGraphicsScene scene;
		NetworkPresenter presenter(&scene);
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
		NetworkPresenter presenter(scene.get());
		fill(presenter, *scene);
		scene.reset();
		presenter.clear();
		ok &= expect(registriesEmpty(presenter) && pointersNull(presenter),
			"clear empties the registries after the scene is gone");
	}
	{
		NetworkPresenter presenter(nullptr);
		presenter.m_prevTrainPositions.insert(0, QPointF());
		presenter.clear();
		ok &= expect(registriesEmpty(presenter), "clear works without a scene");
	}

	if (!ok)
		return 1;

	std::cout << "all NetworkPresenter tests passed\n";
	return 0;
}
