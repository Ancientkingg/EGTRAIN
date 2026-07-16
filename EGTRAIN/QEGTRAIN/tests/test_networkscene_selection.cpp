#include "graphics/NetworkScene.h"

#include <QApplication>
#include <QGraphicsPixmapItem>
#include <QGraphicsTextItem>
#include <QGraphicsView>

#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static void sendLeftClick(NetworkScene& scene, QGraphicsView& view, const QPointF& scenePos) {
	QGraphicsSceneMouseEvent event(QEvent::GraphicsSceneMousePress);
	event.setButton(Qt::LeftButton);
	event.setButtons(Qt::LeftButton);
	event.setScenePos(scenePos);
	event.setWidget(view.viewport());
	scene.mousePressEvent(&event);
}

static void scaleView(QGraphicsView& view) {
	view.resize(240, 180);
	view.scale(2.0, 2.0);
}

int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	bool ok = true;

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		scaleView(view);

		StationNodeItem station(QRectF(-10.0, -10.0, 20.0, 20.0));
		QPixmap decorationPixmap(24, 24);
		decorationPixmap.fill(Qt::white);
		QGraphicsPixmapItem decoration(decorationPixmap);
		decoration.setPos(-12.0, -12.0);
		decoration.setFlag(QGraphicsItem::ItemIgnoresTransformations);
		decoration.setZValue(10.0);
		scene.addItem(&station);
		scene.addItem(&decoration);

		int stationClicks = 0;
		int disableHighlight = 0;
		QObject::connect(&scene, &NetworkScene::MousePressedOnStationNode, [&](StationNodeItem*) { ++stationClicks; });
		QObject::connect(&scene, &NetworkScene::DisableHighlight, [&]() { ++disableHighlight; });

		sendLeftClick(scene, view, QPointF(0.0, 0.0));
		ok &= expect(stationClicks == 1, "station click passes through higher-z ignored-transform decoration");
		ok &= expect(disableHighlight == 0, "station click does not disable highlight");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		scaleView(view);

		StationNodeItem station(QRectF(-10.0, -10.0, 20.0, 20.0));
		QGraphicsRectItem child(QRectF(-8.0, -8.0, 16.0, 16.0), &station);
		child.setZValue(10.0);
		scene.addItem(&station);

		int stationClicks = 0;
		QObject::connect(&scene, &NetworkScene::MousePressedOnStationNode, [&](StationNodeItem*) { ++stationClicks; });

		sendLeftClick(scene, view, QPointF(0.0, 0.0));
		ok &= expect(stationClicks == 1, "station click resolves through unrecognized child");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		scaleView(view);

		TrackLineItem track(QLineF(-60.0, 0.0, 60.0, 0.0));
		QGraphicsTextItem decoration;
		decoration.setPlainText("label");
		decoration.setPos(-20.0, -10.0);
		decoration.setZValue(10.0);
		scene.addItem(&track);
		scene.addItem(&decoration);

		int arcClicks = 0;
		int disableHighlight = 0;
		QObject::connect(&scene, &NetworkScene::MousePressedOnArc, [&](TrackLineItem*) { ++arcClicks; });
		QObject::connect(&scene, &NetworkScene::DisableHighlight, [&]() { ++disableHighlight; });

		sendLeftClick(scene, view, QPointF(0.0, 0.0));
		ok &= expect(arcClicks == 1, "track click passes through higher-z text decoration");
		ok &= expect(disableHighlight == 0, "track click does not disable highlight");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		scaleView(view);

		int entitySignals = 0;
		int disableHighlight = 0;
		QObject::connect(&scene, &NetworkScene::MousePressedOnNode, [&](NodeItem*) { ++entitySignals; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnStationNode, [&](StationNodeItem*) { ++entitySignals; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnArc, [&](TrackLineItem*) { ++entitySignals; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnConnection, [&](ConnectionItem*) { ++entitySignals; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnSignal, [&](SignalItem*) { ++entitySignals; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnTrain, [&](TrainBodyItem*) { ++entitySignals; });
		QObject::connect(&scene, &NetworkScene::MousePressedOnPassenger, [&](PassengerItem*) { ++entitySignals; });
		QObject::connect(&scene, &NetworkScene::DisableHighlight, [&]() { ++disableHighlight; });

		sendLeftClick(scene, view, QPointF(100.0, 100.0));
		ok &= expect(entitySignals == 0, "empty click emits no entity signal");
		ok &= expect(disableHighlight == 1, "empty click disables highlight once");
	}

	if (!ok)
		return 1;

	std::cout << "all NetworkScene selection tests passed\n";
	return 0;
}
