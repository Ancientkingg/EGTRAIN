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
	StationVisual stopVisual = classifyStation(false, 0);
	StationOverlayItem overlay("KogeNord", QPointF(40.0, 50.0), platform);
	ok &= expect(overlay.flags().testFlag(QGraphicsItem::ItemIsSelectable),
		"overlay remains programmatically selectable");
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
	overlay.setNameVisible(false);
	ok &= expect(!overlay.isLabelVisible(), "station-name layer hides the label without hiding the station item");
	ok &= expect(overlay.isVisible(), "station-name layer does not hide the station symbol");
	overlay.setNameVisible(true);
	ok &= expect(overlay.isLabelVisible(), "station-name layer restores the label");
	const QRectF inset(0.0, 0.0, 220.0, 120.0);
	const StationOverlayItem::ViewportPlacement rightPlacement =
		overlay.placementForSide(StationOverlayItem::LabelSide::Right, QPointF(80.0, 60.0), inset);
	ok &= expect(rightPlacement.side == StationOverlayItem::LabelSide::Right,
		"viewport placement prefers the right side");
	ok &= expect(rightPlacement.fits, "right-side placement fits in the viewport");
	ok &= expect(qFuzzyCompare(rightPlacement.labelRect.left(), rightPlacement.symbolRect.right() + 8.0),
		"production placement keeps the right eight-pixel gap");
	const StationOverlayItem::ViewportPlacement leftPlacement =
		overlay.placementForSide(StationOverlayItem::LabelSide::Left, QPointF(80.0, 60.0), inset);
	ok &= expect(leftPlacement.side == StationOverlayItem::LabelSide::Left,
		"viewport placement supports the left side");
	ok &= expect(qFuzzyCompare(leftPlacement.symbolRect.left(), leftPlacement.labelRect.right() + 8.0),
		"production placement keeps the left eight-pixel gap");
	const StationOverlayItem::ViewportPlacement abovePlacement =
		overlay.placementForSide(StationOverlayItem::LabelSide::Above, QPointF(80.0, 60.0), inset);
	ok &= expect(qFuzzyCompare(abovePlacement.labelRect.bottom() + 8.0,
		abovePlacement.symbolRect.top()), "production placement keeps the upper eight-pixel gap");
	const StationOverlayItem::ViewportPlacement belowPlacement =
		overlay.placementForSide(StationOverlayItem::LabelSide::Below, QPointF(80.0, 60.0), inset);
	ok &= expect(qFuzzyCompare(belowPlacement.symbolRect.bottom() + 8.0,
		belowPlacement.labelRect.top()), "production placement keeps the lower eight-pixel gap");
	const StationOverlayItem::ViewportPlacement edgePlacement =
		overlay.preferredViewportPlacement(QPointF(2.0, 2.0), inset);
	ok &= expect(edgePlacement.fits, "edge placement clamps the complete overlay into the viewport");
	ok &= expect(inset.contains(edgePlacement.combinedRect), "clamped overlay stays inside the viewport inset");

	StationOverlayItem multiNode("MultiNode", QPointF(8.0, 8.0), platform);
	multiNode.setNetworkDegree(3, true, true);
	ok &= expect(multiNode.isInterchange(), "multi-node station keeps interchange flag");
	ok &= expect(multiNode.isEndpoint(), "multi-node station keeps endpoint flag");
	StationOverlayItem platformAfterDegree("PlatformAfterDegree", QPointF(12.0, 12.0), platform);
	platformAfterDegree.setDegree(3);
	StationOverlayItem stopAfterDegree("StopAfterDegree", QPointF(12.0, 12.0), stopVisual);
	stopAfterDegree.setDegree(3);
	ok &= expect(StationOverlayItem::priorityLess(platformAfterDegree, stopAfterDegree, QPointF()),
		"platform class remains ahead of stop marker after interchange classification");

	QList<StationOverlayItem*> candidates;
	StationOverlayItem selected("Zulu", QPointF(0.0, 0.0), platform);
	StationOverlayItem followed("Alpha", QPointF(1.0, 1.0), platform);
	StationOverlayItem interchange("Beta", QPointF(2.0, 2.0), classifyStation(true, 3));
	StationOverlayItem endpoint("Gamma", QPointF(3.0, 3.0), platform);
	StationOverlayItem stop("Delta", QPointF(4.0, 4.0), stopVisual);
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
	StationOverlayItem platformPriority("ZuluPlatform", QPointF(30.0, 0.0), platform);
	StationOverlayItem stopPriority("AlphaStop", QPointF(30.0, 0.0), stop.visual());
	ok &= expect(StationOverlayItem::priorityLess(platformPriority, stopPriority, QPointF()),
		"platform precedes stop marker at equal distance");
	StationOverlayItem nameZulu("ZuluTie", QPointF(40.0, 0.0), platform);
	StationOverlayItem nameAlpha("AlphaTie", QPointF(-40.0, 0.0), platform);
	ok &= expect(StationOverlayItem::priorityLess(nameAlpha, nameZulu, QPointF()),
		"station name breaks equal-distance priority ties");

	{
		QGraphicsScene hoverScene;
		auto* hovered = new StationOverlayItem("Hovered", QPointF(0.0, 0.0), platform);
		hovered->setLayoutVisible(false);
		hovered->setCollisionBlocked(true);
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
		ok &= expect(hovered->isLabelVisible(), "selection reveals collision-blocked label");
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
			[&](StationNodeItem*) {
				++stationClicks;
				decoration.setSelected(true);
			});
		QObject::connect(&scene, &NetworkScene::ContextMenuRequested,
			[&](QGraphicsItem* item, const QPointF&, const QPoint&, bool) { contextTarget = item; });
		sendLeftClick(scene, view, QPointF(0.0, 0.0));
		ok &= expect(stationClicks == 1, "left click passes through station overlay");
		ok &= expect(decoration.isSelected(), "semantic station selection survives default scene dispatch");
		ok &= expect(sendContextMenu(scene, view, QPointF(0.0, 0.0)), "context event accepted through station overlay");
		ok &= expect(contextTarget == &station, "context menu preserves station semantic target");
	}

	if (!ok)
		return 1;
	std::cout << "all StationOverlayItem tests passed\n";
	return 0;
}
