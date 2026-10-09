#include "graphics/items/StationOverlayItem.h"

#include "graphics/NetworkScene.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/NodeItem.h"

#include <QApplication>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>

#include <algorithm>
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

namespace {
constexpr qreal kPresentation = 0.4;

struct StationRig {
	StationNodeItem* node = nullptr;
	StationOverlayItem* overlay = nullptr;
	QGraphicsPixmapItem* picture = nullptr;
	QGraphicsTextItem* name = nullptr;
};

// Builds a station as the application does: a scene-scaled pictogram above the
// artwork point and a name centred on it, both children or siblings of the node.
StationRig addStationRig(QGraphicsScene& scene, const QString& stationName, const QPointF& anchor,
	int platformCount, const QPointF& artworkOffset = QPointF()) {
	StationRig rig;
	const QPointF artwork = anchor + artworkOffset;
	rig.node = new StationNodeItem(QRectF(-10.0, -10.0, 20.0, 20.0));
	rig.node->setPos(anchor);
	scene.addItem(rig.node);
	rig.overlay = new StationOverlayItem(stationName, anchor, classifyStation());
	rig.overlay->setSceneDecoration(true);
	scene.addItem(rig.overlay);
	QPixmap pixmap(300, 300);
	pixmap.fill(Qt::white);
	rig.picture = scene.addPixmap(pixmap);
	rig.picture->setScale(kPresentation);
	rig.picture->setPos(artwork.x() - 150.0 * kPresentation, artwork.y() - 300.0 * kPresentation);
	rig.picture->setAcceptedMouseButtons(Qt::NoButton);
	QFont font;
	font.setPixelSize(60);
	rig.name = scene.addText(stationName, font);
	rig.name->setScale(kPresentation);
	rig.name->setPos(artwork - QPointF(rig.name->boundingRect().width() * kPresentation / 2.0, rig.name->boundingRect().height() * kPresentation / 2.0));
	rig.name->setAcceptedMouseButtons(Qt::NoButton);
	rig.overlay->attachArtwork(rig.picture, rig.name, platformCount);
	const QPointF picturePosition = rig.picture->scenePos();
	rig.picture->setParentItem(rig.node);
	rig.picture->setPos(rig.node->mapFromScene(picturePosition));
	return rig;
}

QPointF pictureAnchorInScene(const StationRig& rig) {
	const QPixmap& pixmap = rig.picture->pixmap();
	return rig.picture->mapToScene(rig.picture->offset() + QPointF(pixmap.width() / 2.0, pixmap.height()));
}

QPointF nameAnchorInScene(const StationRig& rig) {
	return rig.name->mapToScene(QPointF(rig.name->boundingRect().width() / 2.0, 0.0));
}

QRectF deviceRect(const QTransform& transform, const QGraphicsItem* item) {
	return transform.mapRect(item->sceneBoundingRect());
}

void applyAll(const QList<StationRig>& rigs, qreal viewScale) {
	for (const StationRig& rig : rigs)
		rig.overlay->applyViewScale(viewScale);
}

QList<bool> hiddenNames(const QList<StationRig>& rigs) {
	QList<bool> hidden;
	for (const StationRig& rig : rigs)
		hidden.append(rig.overlay->isNameHiddenByCollision());
	return hidden;
}
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
			&& stationImage.pixelColor(4, 4).alpha() > 0,
		"station node paints the historical square");
	QGraphicsScene boundaryScene;
	QGraphicsView boundaryView(&boundaryScene);
	boundaryView.setFrameShape(QFrame::NoFrame);
	boundaryView.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	boundaryView.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	boundaryView.setBackgroundBrush(Qt::black);
	boundaryView.setSceneRect(QRectF(-300.0, -300.0, 800.0, 800.0));
	boundaryView.resize(48, 48);
	auto* boundary = new NodeItem(QRectF(123.0, 205.0, 28.0, 28.0));
	boundary->setPen(QPen(Qt::lightGray));
	boundary->setBrush(Qt::lightGray);
	boundaryScene.addItem(boundary);
	boundaryView.scale(0.05, 0.05);
	boundaryView.show();
	boundaryView.centerOn(boundary->rect().center());
	QApplication::processEvents();
	const QImage boundaryImage = boundaryView.viewport()->grab().toImage();
	ok &= expect(boundary->childItems().size() == 1
			&& boundary->childItems().first()->flags().testFlag(QGraphicsItem::ItemIgnoresTransformations),
		"transparent Fit target remains available for semantic picks");
	ok &= expect(boundaryView.viewportTransform().mapRect(boundary->rect()).width() < 3.0,
		"scene-sized boundary scales proportionally at Fit");
	const QList<QGraphicsItem*> hit = boundaryScene.items(boundary->rect().center(),
		Qt::IntersectsItemShape, Qt::DescendingOrder);
	ok &= expect(hit.contains(boundary), "scene-sized boundary retains its semantic hit shape");
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
					 abovePlacement.symbolRect.top()),
		"production placement keeps the upper eight-pixel gap");
	const StationOverlayItem::ViewportPlacement belowPlacement =
		overlay.placementForSide(StationOverlayItem::LabelSide::Below, QPointF(80.0, 60.0), inset);
	ok &= expect(qFuzzyCompare(belowPlacement.symbolRect.bottom() + 8.0,
					 belowPlacement.labelRect.top()),
		"production placement keeps the lower eight-pixel gap");
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

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		view.resize(240, 180);
		StationNodeItem station(QRectF(-10.0, -10.0, 20.0, 20.0));
		StationOverlayItem sceneOverlay("SceneStation", QPointF(0.0, 0.0), stationVisual);
		sceneOverlay.setSceneDecoration(true);
		sceneOverlay.setFitCollisionOffset(QPointF(20.0, 0.0));
		QPixmap artwork(30, 30);
		artwork.fill(Qt::white);
		auto* picture = new QGraphicsPixmapItem(artwork, &station);
		picture->setPos(85.0, -15.0);
		picture->setAcceptedMouseButtons(Qt::NoButton);
		scene.addItem(&station);
		scene.addItem(&sceneOverlay);
		int clicks = 0;
		QGraphicsItem* contextTarget = nullptr;
		QObject::connect(&scene, &NetworkScene::MousePressedOnStationNode,
			[&](StationNodeItem* item) { if (item == &station) ++clicks; });
		QObject::connect(&scene, &NetworkScene::ContextMenuRequested,
			[&](QGraphicsItem* item, const QPointF&, const QPoint&, bool) { contextTarget = item; });
		ok &= expect(sceneOverlay.shape().isEmpty() && sceneOverlay.acceptedMouseButtons() == Qt::NoButton,
			"legacy screen collision does not leave an unrelated station hit target");
		sendLeftClick(scene, view, QPointF(100.0, 0.0));
		ok &= expect(clicks == 1, "scene-scaled artwork resolves to its semantic station node");
		sendContextMenu(scene, view, QPointF(100.0, 0.0));
		ok &= expect(contextTarget == &station, "artwork context menu resolves to its station node");
	}


	{
		const qreal natural = 300.0 * kPresentation;
		ok &= expect(StationOverlayItem::readableItemScale(300.0, kPresentation, 1.0, 24.0) == kPresentation,
			"a scene size above the minimum is unchanged");
		const qreal small = StationOverlayItem::readableItemScale(300.0, kPresentation, 0.01, 24.0);
		ok &= expect(std::abs(small * 300.0 * 0.01 - 24.0) < 1e-9, "below the minimum the size on screen is exact");
		ok &= expect(StationOverlayItem::readableItemScale(300.0, 0.2, 0.01, 24.0) == small,
			"below the minimum the size does not depend on the scene scale");
		const qreal crossover = 24.0 / natural;
		const qreal below = StationOverlayItem::readableItemScale(300.0, kPresentation, crossover * 0.999, 24.0);
		const qreal above = StationOverlayItem::readableItemScale(300.0, kPresentation, crossover * 1.001, 24.0);
		ok &= expect(std::abs(below - above) / above < 0.01, "the size has no jump at the crossover");
		qreal previous = 0.0;
		bool monotonic = true;
		for (qreal viewScale : {0.001, 0.005, 0.01, 0.05, crossover, 0.5, 1.0, 4.0}) {
			const qreal onScreen = StationOverlayItem::readableItemScale(300.0, kPresentation, viewScale, 24.0)
				* 300.0 * viewScale;
			monotonic = monotonic && onScreen >= previous;
			previous = onScreen;
		}
		ok &= expect(monotonic, "the size on screen never shrinks when zooming in");
	}

	{
		QGraphicsScene scene;
		QGraphicsView view(&scene);
		const StationRig rig = addStationRig(scene, "Paimpol", QPointF(100.0, 200.0), 2, QPointF(0.0, 40.0));
		rig.overlay->setSourceIdentities({{-3.0, 7}});
		ok &= expect(QLineF(pictureAnchorInScene(rig), QPointF(100.0, 240.0)).length() < 1e-6,
			"the pictogram stands on its artwork point after the node became its parent");
		const QPointF pictureAnchor = pictureAnchorInScene(rig);
		const QPointF nameAnchor = nameAnchorInScene(rig);
		const QRectF naturalPicture = rig.picture->sceneBoundingRect();
		const QRectF naturalName = rig.name->sceneBoundingRect();
		ok &= expect(rig.overlay->pictureItem() == rig.picture && rig.overlay->nameItem() == rig.name
				&& rig.overlay->platformCount() == 2,
			"the overlay links its artwork");
		view.setTransform(QTransform::fromScale(0.01, 0.01));
		rig.overlay->applyViewScale(0.01);
		const QTransform device = view.viewportTransform();
		const qreal pictureHeight = deviceRect(device, rig.picture).height();
		ok &= expect(pictureHeight >= 23.5 && pictureHeight < 25.0,
			"the pictogram is 24 pixels high on screen at a small view scale");
		const qreal fontPixels = rig.name->font().pixelSize() * rig.name->sceneTransform().m22() * device.m22();
		ok &= expect(std::abs(fontPixels - 12.0) < 0.12 && deviceRect(device, rig.name).height() >= 12.0,
			"the name has a 12 pixel font on screen at a small view scale");
		bool stable = true;
		for (qreal viewScale : {0.005, 0.01, 0.05, 0.2, 1.0, 3.0, 0.01}) {
			view.setTransform(QTransform::fromScale(viewScale, viewScale));
			rig.overlay->applyViewScale(viewScale);
			stable = stable && QLineF(pictureAnchorInScene(rig), pictureAnchor).length() < 1e-6
				&& QLineF(nameAnchorInScene(rig), nameAnchor).length() < 1e-6
				&& rig.overlay->stableAnchor() == QPointF(100.0, 200.0)
				&& rig.picture->parentItem() == rig.node
				&& rig.overlay->matchesSourceIdentity(-3.0, 7)
				&& rig.overlay->sourceIdentityCount() == 1;
		}
		ok &= expect(stable, "anchors, parent and identity are unchanged across view scales");
		rig.overlay->applyViewScale(1.0);
		const auto sameRect = [](const QRectF& a, const QRectF& b) {
			return QLineF(a.topLeft(), b.topLeft()).length() < 1e-6
				&& QLineF(a.bottomRight(), b.bottomRight()).length() < 1e-6;
		};
		ok &= expect(rig.picture->transform().isIdentity() && rig.name->transform().isIdentity()
				&& sameRect(rig.picture->sceneBoundingRect(), naturalPicture)
				&& sameRect(rig.name->sceneBoundingRect(), naturalName),
			"the scene size returns at a large view scale");
	}

	{
		NetworkScene scene(nullptr);
		QGraphicsView view(&scene);
		view.resize(240, 180);
		view.setTransform(QTransform::fromScale(0.01, 0.01));
		const StationRig rig = addStationRig(scene, "ClickStation", QPointF(0.0, 0.0), 1, QPointF(0.0, 300.0));
		int clicks = 0;
		QObject::connect(&scene, &NetworkScene::MousePressedOnStationNode,
			[&](StationNodeItem* item) { if (item == rig.node) ++clicks; });
		const QPointF inside(500.0, -500.0);
		ok &= expect(!rig.picture->sceneBoundingRect().contains(inside), "the click point is outside the scene-sized pictogram");
		sendLeftClick(scene, view, inside);
		ok &= expect(clicks == 0, "a click beside the scene-sized pictogram selects nothing");
		rig.overlay->applyViewScale(0.01);
		ok &= expect(rig.picture->sceneBoundingRect().contains(inside), "the enlarged pictogram covers the click point");
		sendLeftClick(scene, view, inside);
		ok &= expect(clicks == 1, "a click inside the enlarged pictogram selects its station");
	}

	{
		QGraphicsScene scene;
		const QTransform small = QTransform::fromScale(0.01, 0.01);
		QList<StationRig> rigs;
		rigs << addStationRig(scene, "Alpha", QPointF(0.0, 0.0), 1)
			 << addStationRig(scene, "Beta", QPointF(200.0, 0.0), 3)
			 << addStationRig(scene, "Gamma", QPointF(400.0, 0.0), 2);
		applyAll(rigs, 0.01);
		StationOverlayItem::resolveNameCollisions({rigs[0].overlay, rigs[1].overlay, rigs[2].overlay}, small);
		ok &= expect(hiddenNames(rigs) == QList<bool>({true, false, true}),
			"of three overlapping names the one of the station with most platforms stays");
		ok &= expect(std::all_of(rigs.cbegin(), rigs.cend(), [](const StationRig& rig) {
			return rig.picture->isVisible() && rig.overlay->isVisible();
		}),
			"colliding names never hide a pictogram");
		const QList<bool> first = hiddenNames(rigs);
		StationOverlayItem::resolveNameCollisions({rigs[0].overlay, rigs[1].overlay, rigs[2].overlay}, small);
		ok &= expect(hiddenNames(rigs) == first, "the collision result is the same when run twice");
		StationOverlayItem::resolveNameCollisions({rigs[2].overlay, rigs[1].overlay, rigs[0].overlay}, small);
		ok &= expect(hiddenNames(rigs) == first, "the collision result does not depend on the list order");
		QTransform panned = small;
		panned.translate(4000.0, -2500.0);
		StationOverlayItem::resolveNameCollisions({rigs[0].overlay, rigs[1].overlay, rigs[2].overlay}, panned);
		ok &= expect(hiddenNames(rigs) == first, "panning does not change which names are hidden");
		rigs[0].overlay->setFollowed(true);
		StationOverlayItem::resolveNameCollisions({rigs[0].overlay, rigs[1].overlay, rigs[2].overlay}, small);
		ok &= expect(hiddenNames(rigs) == QList<bool>({false, true, true}),
			"a followed station ranks above one with more platforms");
		rigs[2].overlay->setSelected(true);
		StationOverlayItem::resolveNameCollisions({rigs[0].overlay, rigs[1].overlay, rigs[2].overlay}, small);
		ok &= expect(hiddenNames(rigs) == QList<bool>({false, true, false}),
			"selected and followed stations both keep their name");
		rigs[0].overlay->setFollowed(false);
		StationOverlayItem::resolveNameCollisions({rigs[0].overlay, rigs[1].overlay, rigs[2].overlay}, small);
		ok &= expect(hiddenNames(rigs) == QList<bool>({true, true, false}),
			"a selected station ranks above all others");
		rigs[2].overlay->setSelected(false);
		applyAll(rigs, 1.0);
		StationOverlayItem::resolveNameCollisions({rigs[0].overlay, rigs[1].overlay, rigs[2].overlay},
			QTransform::fromScale(1.0, 1.0));
		ok &= expect(hiddenNames(rigs) == QList<bool>({false, false, false}),
			"all names are visible after zooming in");
	}

	{
		// Equal priority except for the position: the tie break must not follow the viewport.
		QGraphicsScene scene;
		const QTransform small = QTransform::fromScale(0.01, 0.01);
		QList<StationRig> rigs;
		rigs << addStationRig(scene, "Near", QPointF(1000.0, 0.0), 2)
			 << addStationRig(scene, "Far", QPointF(1200.0, 0.0), 2);
		applyAll(rigs, 0.01);
		StationOverlayItem::resolveNameCollisions({rigs[0].overlay, rigs[1].overlay}, small);
		const QList<bool> first = hiddenNames(rigs);
		ok &= expect(first.count(true) == 1, "of two equal stations with overlapping names one stays");
		QTransform panned = small;
		panned.translate(-3000.0, 0.0);
		StationOverlayItem::resolveNameCollisions({rigs[0].overlay, rigs[1].overlay}, panned);
		ok &= expect(hiddenNames(rigs) == first, "panning does not reorder equal stations");
	}

	{
		// The name of the upper station lies on the pictogram of the lower one. The names
		// themselves are apart, so only the pictogram can hide the upper name.
		QGraphicsScene scene;
		const QTransform small = QTransform::fromScale(0.01, 0.01);
		QList<StationRig> rigs;
		rigs << addStationRig(scene, "Upper", QPointF(0.0, 0.0), 5)
			 << addStationRig(scene, "Lower", QPointF(0.0, 2200.0), 1);
		applyAll(rigs, 0.01);
		const QRectF upperName = deviceRect(small, rigs[0].name);
		const QRectF lowerName = deviceRect(small, rigs[1].name);
		const QRectF lowerPicture = deviceRect(small, rigs[1].picture);
		const QRectF upperPicture = deviceRect(small, rigs[0].picture);
		if (!expect(upperName.intersects(lowerPicture) && !upperName.adjusted(-3.0, -3.0, 3.0, 3.0).intersects(lowerName)
					&& !lowerName.adjusted(-3.0, -3.0, 3.0, 3.0).intersects(upperPicture),
				"the upper name lies on the lower pictogram and the names are apart"))
			return 1;
		const QList<StationOverlayItem*> both{rigs[0].overlay, rigs[1].overlay};
		StationOverlayItem::resolveNameCollisions(both, small);
		ok &= expect(hiddenNames(rigs) == QList<bool>({true, false}),
			"a name gives way to the pictogram of another station, even of one that ranks lower");
		ok &= expect(rigs[0].picture->isVisible() && rigs[1].picture->isVisible(),
			"a name on a pictogram hides no pictogram");
		rigs[0].overlay->setSelected(true);
		StationOverlayItem::resolveNameCollisions(both, small);
		ok &= expect(hiddenNames(rigs) == QList<bool>({false, false}),
			"a selected station keeps its name on the pictogram of another station");
		rigs[0].overlay->setSelected(false);
		rigs[1].picture->setVisible(false);
		StationOverlayItem::resolveNameCollisions(both, small);
		ok &= expect(hiddenNames(rigs) == QList<bool>({false, false}),
			"a hidden pictogram hides no name");
		rigs[1].picture->setVisible(true);
		StationOverlayItem::resolveNameCollisions({rigs[0].overlay}, small);
		ok &= expect(hiddenNames(rigs).first() == false, "the own pictogram of a station does not hide its name");
	}

	if (!ok)
		return 1;
	std::cout << "all StationOverlayItem tests passed\n";
	return 0;
}
