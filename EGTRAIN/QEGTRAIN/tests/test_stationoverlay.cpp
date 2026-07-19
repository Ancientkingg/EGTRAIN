#include "graphics/items/StationOverlayItem.h"

#include "graphics/NetworkScene.h"
#include "graphics/items/StationNodeItem.h"

#include <QApplication>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QMouseEvent>

#include <cmath>
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
	event.setPos(view.mapFromScene(scenePos));
	event.setScenePos(scenePos);
	event.setScreenPos(view.viewport()->mapToGlobal(view.mapFromScene(scenePos)));
	event.setButtonDownPos(Qt::LeftButton, event.pos());
	event.setButtonDownScenePos(Qt::LeftButton, scenePos);
	event.setButtonDownScreenPos(Qt::LeftButton, event.screenPos());
	event.setWidget(view.viewport());
	scene.mousePressEvent(&event);
}

static bool sendContextMenu(NetworkScene& scene, QGraphicsView& view, const QPointF& scenePos) {
	QGraphicsSceneContextMenuEvent event(QEvent::GraphicsSceneContextMenu);
	event.setReason(QGraphicsSceneContextMenuEvent::Mouse);
	event.setScenePos(scenePos);
	event.setScreenPos(view.viewport()->mapToGlobal(view.mapFromScene(scenePos)));
	event.setWidget(view.viewport());
	scene.contextMenuEvent(&event);
	return event.isAccepted();
}

int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	bool ok = true;

	StationVisual platform = classifyStation(true, 1);
	StationOverlayItem overlay("KogeNord", QPointF(40.0, 50.0), platform);
	const QRectF symbol = overlay.symbolRect();
	const QRectF right = overlay.labelRect();
	ok &= expect(qFuzzyCompare(overlay.combinedRect().left(), symbol.left()), "combined bounds include symbol");
	ok &= expect(qFuzzyCompare(right.left(), symbol.right() + 8.0), "right label gap is eight logical pixels");
	overlay.setLabelSide(StationOverlayItem::LabelSide::Left);
	const QRectF left = overlay.labelRect();
	ok &= expect(qFuzzyCompare(symbol.left(), left.right() + 8.0), "left label gap is eight logical pixels");
	ok &= expect(overlay.stableAnchor() == QPointF(40.0, 50.0), "stable anchor is retained");
	overlay.setViewportOffset(QPointF(-3.0, 7.0));
	ok &= expect(overlay.viewportOffset() == QPointF(-3.0, 7.0), "viewport clamp offset is separate");
	ok &= expect(overlay.displayName(QStringLiteral("KogeNord")) == QStringLiteral("Koge Nord"), "camel-case display conversion");

	QList<StationOverlayItem*> candidates;
	StationOverlayItem selected("Zulu", QPointF(0.0, 0.0), platform);
	StationOverlayItem followed("Alpha", QPointF(1.0, 1.0), platform);
	StationOverlayItem interchange("Beta", QPointF(2.0, 2.0), classifyStation(true, 3));
	StationOverlayItem endpoint("Gamma", QPointF(3.0, 3.0), platform);
	StationOverlayItem stop("Delta", QPointF(4.0, 4.0), classifyStation(false, 0));
	selected.setSelected(true);
	followed.setFollowed(true);
	interchange.setDegree(3);
	endpoint.setDegree(1);
	candidates << &stop << &endpoint << &interchange << &followed << &selected;
	std::sort(candidates.begin(), candidates.end(), [](const auto* a, const auto* b) {
		return StationOverlayItem::priorityLess(*a, *b, QPointF(0.0, 0.0));
	});
	ok &= expect(candidates.at(0) == &selected, "selected station has first priority");
	ok &= expect(candidates.at(1) == &followed, "followed station has second priority");
	ok &= expect(candidates.at(2) == &interchange, "interchange has third priority");
	ok &= expect(candidates.at(3) == &endpoint, "endpoint has fourth priority");
	ok &= expect(candidates.at(4) == &stop, "stop marker has remaining priority");

	{
		QGraphicsScene hoverScene;
		auto* hovered = new StationOverlayItem("Hovered", QPointF(0.0, 0.0), platform);
		hovered->setLayoutVisible(false);
		hoverScene.addItem(hovered);
		ok &= expect(!hovered->isLabelVisible(), "layout culling hides label");
		QGraphicsSceneHoverEvent hoverEnter(QEvent::GraphicsSceneHoverEnter);
		hoverEnter.setPos(QPointF(0.0, 0.0));
		hoverEnter.setScenePos(hovered->stableAnchor());
		hoverScene.sendEvent(hovered, &hoverEnter);
		ok &= expect(hovered->isLabelVisible(), "hover reveals culled label");
		QGraphicsSceneHoverEvent hoverLeave(QEvent::GraphicsSceneHoverLeave);
		hoverLeave.setPos(QPointF(0.0, 0.0));
		hoverLeave.setScenePos(hovered->stableAnchor());
		hoverScene.sendEvent(hovered, &hoverLeave);
		ok &= expect(!hovered->isLabelVisible(), "hover leave hides culled label");
		hovered->setSelected(true);
		ok &= expect(hovered->isLabelVisible(), "selection reveals culled label");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		view.resize(240, 180);
		StationNodeItem station(QRectF(-10.0, -10.0, 20.0, 20.0));
		station.setPos(0.0, 0.0);
		StationOverlayItem decoration("Koge", QPointF(0.0, 0.0), platform);
		decoration.setZValue(3.0);
		scene.addItem(&station);
		scene.addItem(&decoration);
		int stationClicks = 0;
		QGraphicsItem* contextTarget = nullptr;
		QObject::connect(&scene, &NetworkScene::MousePressedOnStationNode,
			[&](StationNodeItem*) { ++stationClicks; });
		QObject::connect(&scene, &NetworkScene::ContextMenuRequested,
			[&](QGraphicsItem* item, const QPointF&, const QPoint&, bool) { contextTarget = item; });
		sendLeftClick(scene, view, QPointF(0.0, 0.0));
		ok &= expect(stationClicks == 1, "left click passes through station overlay");
		ok &= expect(sendContextMenu(scene, view, QPointF(0.0, 0.0)), "context event accepted through station overlay");
		ok &= expect(contextTarget == &station, "context menu preserves station semantic target");
	}

	if (!ok)
		return 1;
	std::cout << "all StationOverlayItem tests passed\n";
	return 0;
}
