#include "graphics/items/StationOverlayItem.h"

#include "graphics/NetworkScene.h"
#include "graphics/items/StationNodeItem.h"

#include <QApplication>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>

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

	StationVisual stationVisual = classifyStation();
	QImage stationImage(24, 24, QImage::Format_ARGB32_Premultiplied);
	stationImage.fill(Qt::transparent);
	{
		QPainter painter(&stationImage);
		painter.translate(12.0, 12.0);
		StationNodeItem station(QRectF(-8.0, -8.0, 16.0, 16.0));
		station.setPen(QPen(stationVisual.outline));
		station.setBrush(stationVisual.fill);
		station.paint(&painter, nullptr, nullptr);
	}
	ok &= expect(stationImage.pixelColor(12, 12).alpha() > 0
			&& stationImage.pixelColor(4, 4).alpha() == 0,
		"station node paints a circle instead of a rectangle");
	StationOverlayItem overlay("KogeNord", QPointF(40.0, 50.0), stationVisual);
	ok &= expect(overlay.zValue() > 3.0 && overlay.zValue() < 5.0,
		"station text paints above signals and below train badges");
	ok &= expect(overlay.flags().testFlag(QGraphicsItem::ItemIsSelectable),
		"overlay remains programmatically selectable");
	const QRectF symbol = overlay.symbolRect();
	const QRectF right = overlay.labelRect();
	ok &= expect(qFuzzyCompare(symbol.width(), 16.0) && qFuzzyCompare(symbol.height(), 16.0),
		"station symbol stays compact");
	overlay.setLabelScale(2.0);
	ok &= expect(qFuzzyCompare(overlay.labelScale(), 2.0)
			&& overlay.labelRect().width() > right.width()
			&& overlay.symbolRect() == symbol,
		"station label grows independently from its fixed-size symbol");
	overlay.setLabelScale(1.0);
	ok &= expect(qFuzzyCompare(overlay.combinedRect().left(), symbol.left()), "combined bounds include symbol");
	ok &= expect(qFuzzyCompare(right.left(), symbol.right() + 8.0), "right label gap is eight logical pixels");
	overlay.setLabelSide(StationOverlayItem::LabelSide::Left);
	const QRectF left = overlay.labelRect();
	ok &= expect(qFuzzyCompare(symbol.left(), left.right() + 8.0), "left label gap is eight logical pixels");
	ok &= expect(overlay.stableAnchor() == QPointF(40.0, 50.0), "stable anchor is retained");
	overlay.setViewportOffset(QPointF(-3.0, 7.0));
	ok &= expect(overlay.viewportOffset() == QPointF(-3.0, 7.0), "viewport clamp offset is separate");
	overlay.setFitCollisionOffset(QPointF(20.0, 0.0));
	ok &= expect(overlay.fitCollisionOffset() == QPointF(20.0, 0.0)
			&& overlay.deviceSymbolRect() == symbol.translated(17.0, 7.0),
		"Fit collision offset moves only the symbol after viewport clamping");
	overlay.setFitCollisionOffset(QPointF());
	ok &= expect(overlay.viewportOffset() == QPointF(-3.0, 7.0)
			&& overlay.fitCollisionOffset().isNull(),
		"clearing the Fit collision offset preserves viewport clamping");
	ok &= expect(StationOverlayItem::displayName(QStringLiteral("DanshojBBx")) == QStringLiteral("Danshoj BBx"),
		"Copenhagen mixed-case suffix remains one run");
	ok &= expect(StationOverlayItem::displayName(QStringLiteral("RyparkenBBx")) == QStringLiteral("Ryparken BBx"),
		"second Copenhagen mixed-case suffix remains one run");
	ok &= expect(StationOverlayItem::displayName(QStringLiteral("KBHallen")) == QStringLiteral("KB Hallen"),
		"Copenhagen acronym prefix remains one word");
	ok &= expect(StationOverlayItem::displayName(QStringLiteral("KogeNord")) == QStringLiteral("Koge Nord"),
		"legacy camel-case boundary remains split");
	ok &= expect(StationOverlayItem::displayName(QStringLiteral("PMBivioAdda")) == QStringLiteral("PM Bivio Adda"),
		"Italian acronym prefix and camel-case boundary remain split");
	ok &= expect(StationOverlayItem::displayName(QStringLiteral("NBTCentralStation")) == QStringLiteral("NBT Central Station"),
		"Lebanon acronym prefix and camel-case boundary remain split");
	ok &= expect(StationOverlayItem::displayName(QStringLiteral("FlintholmCH")) == QStringLiteral("Flintholm CH"),
		"trailing two-letter acronym remains intact");
	ok &= expect(StationOverlayItem::displayName(QStringLiteral("NyEllebjergAE")) == QStringLiteral("Ny Ellebjerg AE"),
		"multiple word boundaries preserve the trailing acronym");
	StationOverlayItem acronymOverlay("KBHallen", QPointF(), stationVisual);
	ok &= expect(acronymOverlay.stationName() == QStringLiteral("KBHallen")
			&& acronymOverlay.displayName() == QStringLiteral("KB Hallen"),
		"display formatting leaves the source station identity unchanged");
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
	const StationOverlayItem::ViewportPlacement offscreenPlacement =
		overlay.preferredViewportPlacement(QPointF(80.0, -500.0), inset);
	ok &= expect(offscreenPlacement.offset.isNull() && !offscreenPlacement.fits,
		"far-offscreen station overlays are not pinned to the viewport edge");

	const QRectF collisionInset(0.0, 0.0, 120.0, 80.0);
	const QRectF collisionSymbol(42.0, 32.0, 16.0, 16.0);
	bool foundCollisionOffset = false;
	const QPointF deterministicOffset = StationOverlayItem::firstFitCollisionOffset(
		collisionSymbol, collisionInset, {collisionSymbol}, {}, &foundCollisionOffset);
	ok &= expect(foundCollisionOffset && deterministicOffset == QPointF(20.0, 0.0),
		"Fit collision search deterministically displaces the lower-priority symbol to the right");
	const QPointF repeatedOffset = StationOverlayItem::firstFitCollisionOffset(
		collisionSymbol, collisionInset, {collisionSymbol}, {}, &foundCollisionOffset);
	ok &= expect(foundCollisionOffset && repeatedOffset == deterministicOffset,
		"Fit collision search is stable across repeated layouts");
	const QRectF tightInset = collisionSymbol;
	StationOverlayItem::firstFitCollisionOffset(
		collisionSymbol, tightInset, {collisionSymbol}, {}, &foundCollisionOffset);
	ok &= expect(!foundCollisionOffset,
		"Fit collision search reports suppression when no candidate remains inside the viewport");

	StationOverlayItem multiNode("MultiNode", QPointF(8.0, 8.0), stationVisual);
	multiNode.setSourceIdentities({{-2521.0, 21}, {-2522.0, 22}});
	ok &= expect(multiNode.sourceIdentityCount() == 2
			&& multiNode.matchesSourceIdentity(-2521.0, 21)
			&& multiNode.matchesSourceIdentity(-2522.0, 22),
		"one station overlay matches every assigned platform identity");
	ok &= expect(multiNode.sourceNodeId() == -2521.0 && multiNode.sourceTrack() == 21,
		"first assigned platform remains the deterministic representative identity");
	multiNode.clearSourceIdentities();
	ok &= expect(!multiNode.hasSourceIdentity()
			&& !multiNode.matchesSourceIdentity(-2521.0, 21),
		"clearing station identities removes every platform match");
	multiNode.setNetworkDegree(3, true, true);
	ok &= expect(multiNode.isInterchange(), "multi-node station keeps interchange flag");
	ok &= expect(multiNode.isEndpoint(), "multi-node station keeps endpoint flag");
	QList<StationOverlayItem*> candidates;
	StationOverlayItem selected("Zulu", QPointF(0.0, 0.0), stationVisual);
	StationOverlayItem followed("Alpha", QPointF(1.0, 1.0), stationVisual);
	StationOverlayItem interchange("Beta", QPointF(2.0, 2.0), stationVisual);
	StationOverlayItem endpoint("Gamma", QPointF(3.0, 3.0), stationVisual);
	StationOverlayItem stop("Delta", QPointF(4.0, 4.0), stationVisual);
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
	ok &= expect(candidates.at(4) == &stop, "ordinary station has remaining priority");
	StationOverlayItem nameZulu("ZuluTie", QPointF(40.0, 0.0), stationVisual);
	StationOverlayItem nameAlpha("AlphaTie", QPointF(-40.0, 0.0), stationVisual);
	ok &= expect(StationOverlayItem::priorityLess(nameAlpha, nameZulu, QPointF()),
		"station name breaks equal-distance priority ties");

	{
		QGraphicsScene hoverScene;
		auto* hovered = new StationOverlayItem("Hovered", QPointF(0.0, 0.0), stationVisual);
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
		scene.setSceneRect(-100.0, -100.0, 200.0, 200.0);
		QGraphicsView view(&scene);
		view.resize(240, 180);
		view.scale(2.0, 2.0);
		view.show();
		QApplication::processEvents();
		auto* displaced = new StationOverlayItem(
			"ExactSourceStation", QPointF(0.0, 0.0), stationVisual);
		displaced->setVisualScale(0.75);
		displaced->setNameVisible(false);
		QString clickedSource;
		displaced->setDisplacedClickHandler(
			[&clickedSource](const QString& stationName) { clickedSource = stationName; });
		displaced->setFitCollisionOffset(QPointF(20.0, 0.0));
		scene.addItem(displaced);
		QApplication::processEvents();
		ok &= expect(displaced->scale() == 1.0
				&& displaced->symbolRect().size() == QSizeF(12.0, 12.0)
				&& displaced->deviceSymbolRect().center() == QPointF(20.0, 0.0),
			"0.75 overlays expose their actual fixed-device symbol geometry and offsets");
		const QPoint displacedViewportPos = view.mapFromScene(displaced->stableAnchor()) + QPoint(20, 0);
		const QPoint displacedScreenPos = view.viewport()->mapToGlobal(displacedViewportPos);
		QMouseEvent press(QEvent::MouseButtonPress, QPointF(displacedViewportPos),
			QPointF(displacedScreenPos), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(view.viewport(), &press);
		QMouseEvent release(QEvent::MouseButtonRelease, QPointF(displacedViewportPos),
			QPointF(displacedScreenPos), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(view.viewport(), &release);
		QApplication::processEvents();
		ok &= expect(clickedSource == QStringLiteral("ExactSourceStation"),
			"a real view click uses the same 20 device-pixel offset as 0.75 collision geometry");
		displaced->setFitSymbolVisible(false);
		ok &= expect(displaced->acceptedMouseButtons() == Qt::NoButton,
			"a suppressed Fit symbol leaves node and label input available");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		view.resize(240, 180);
		StationNodeItem station(QRectF(-10.0, -10.0, 20.0, 20.0));
		station.setPos(0.0, 0.0);
		StationOverlayItem decoration("Koge", QPointF(0.0, 0.0), stationVisual);
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
