#include "graphics/items/StationOverlayItem.h"

#include "graphics/NetworkScene.h"
#include "graphics/items/HighlightEffect.h"
#include "graphics/items/NodeItem.h"
#include "graphics/items/SelectionCueItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"
#include "graphics/VisualPolish.h"

#include <QApplication>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <utility>

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

struct MarkRig {
	StationNodeItem* node = nullptr;
	StationOverlayItem* overlay = nullptr;
	StationMarkItem* mark = nullptr;
	QGraphicsTextItem* name = nullptr;
};

// Builds a station mark as the presenter does, with the frame of 30 pixels at kPresentation (12 scene units) standing
// on the first node, the name centred on it, both bound to the square of the first node, and the node centres given.
MarkRig addMarkRig(QGraphicsScene& scene, const QString& stationName, const QList<QPointF>& centres) {
	MarkRig rig;
	const QPointF anchor = centres.first();
	rig.node = new StationNodeItem(QRectF(-2.0, -2.0, 4.0, 4.0));
	rig.node->setPos(anchor);
	scene.addItem(rig.node);
	rig.overlay = new StationOverlayItem(stationName, anchor, classifyStation(true));
	rig.overlay->setSceneDecoration(true);
	scene.addItem(rig.overlay);
	rig.mark = new StationMarkItem(StationClass::Station, 30);
	scene.addItem(rig.mark);
	rig.mark->setScale(kPresentation);
	rig.mark->setPos(anchor.x() - 15.0 * kPresentation, anchor.y() - 30.0 * kPresentation);
	rig.mark->setAcceptedMouseButtons(Qt::NoButton);
	QFont font;
	font.setPixelSize(6);
	rig.name = scene.addText(stationName, font);
	rig.name->setScale(kPresentation);
	rig.name->setPos(anchor - QPointF(rig.name->boundingRect().width() * kPresentation / 2.0, rig.name->boundingRect().height() * kPresentation / 2.0));
	rig.name->setAcceptedMouseButtons(Qt::NoButton);
	rig.overlay->attachArtwork(rig.mark, rig.name, 1);
	const QPointF markPosition = rig.mark->scenePos();
	rig.mark->setParentItem(rig.node);
	rig.mark->setPos(rig.node->mapFromScene(markPosition));
	rig.overlay->setNodeCentres(centres);
	return rig;
}

// The bottom centre of the frame of the far look in scene coordinates.
QPointF frameAnchorInScene(const MarkRig& rig) {
	return rig.mark->mapToScene(QPointF(15.0, 30.0));
}

QPointF nameAnchorInScene(const MarkRig& rig) {
	return rig.name->mapToScene(QPointF(rig.name->boundingRect().width() / 2.0, 0.0));
}

bool samePoint(const QPointF& a, const QPointF& b) {
	return QLineF(a, b).length() < 1e-6;
}

bool containsRect(const QRectF& outer, const QRectF& inner) {
	return outer.adjusted(-1e-6, -1e-6, 1e-6, 1e-6).contains(inner);
}

QList<bool> hiddenNames(const QList<StationRig>& rigs) {
	QList<bool> hidden;
	for (const StationRig& rig : rigs)
		hidden.append(rig.overlay->isNameHiddenByCollision());
	return hidden;
}
}

class TestStationOverlay : public QObject {
	Q_OBJECT

private slots:
	void paintsStationSquare();
	void boundaryScalesAtFit();
	void overlayGeometry();
	void displayName_data();
	void displayName();
	void keepsSourceStationName();
	void nameLayer();
	void viewportPlacement();
	void matchesEveryPlatformIdentity();
	void ranksStations();
	void clickPassesThroughOverlay();
	void artworkResolvesToStationNode();
	void readableItemScale();
	void pictogramKeepsAnchorsAcrossScales();
	void enlargedPictogramTakesClicks();
	void resolvesNameCollisions();
	void equalStationsKeepOrderWhenPanned();
	void nameGivesWayToPictogram();
	void markAtPlaceOfFirstNode();
	void farMarkStandsOnHull_data();
	void farMarkStandsOnHull();
	void nameAvoidsTracks();
	void spotsOfDiagonalStation();
	void nameAbovePinInNearLook();
	void nameGivesWayToOtherMark();
	void nameGivesWayToPinInFarLookOnly();
	void nameGivesWayToSquaresInNearLook();
	void nearLookKeepsPinWithoutSquares();
	void nameAvoidsHiddenMark();
	void screenPinStaysInBounds_data();
	void screenPinStaysInBounds();
	void nameHaloKeepsCrossingsOffLetters_data();
	void nameHaloKeepsCrossingsOffLetters();
};

void TestStationOverlay::paintsStationSquare() {
	const StationVisual stationVisual = classifyStation();
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
	// The station node paints the historical square.
	QCOMPARE_GT(stationImage.pixelColor(12, 12).alpha(), 0);
	QCOMPARE_GT(stationImage.pixelColor(4, 4).alpha(), 0);
}

void TestStationOverlay::boundaryScalesAtFit() {
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
	QVERIFY2(boundary->childItems().isEmpty(),
		"node has no transparent Fit target for every paint to visit; the scene picks it in its pick circle");
	// The scene-sized boundary scales proportionally at Fit.
	QCOMPARE_LT(boundaryView.viewportTransform().mapRect(boundary->rect()).width(), 3.0);
	const QList<QGraphicsItem*> hit = boundaryScene.items(boundary->rect().center(),
		Qt::IntersectsItemShape, Qt::DescendingOrder);
	QVERIFY2(hit.contains(boundary), "scene-sized boundary retains its semantic hit shape");
}

void TestStationOverlay::overlayGeometry() {
	const StationOverlayItem overlay("KogeNord", QPointF(40.0, 50.0), classifyStation());
	// Station text paints above signals and below train badges.
	QCOMPARE_GT(overlay.zValue(), 3.0);
	QCOMPARE_LT(overlay.zValue(), 5.0);
	QVERIFY2(overlay.flags().testFlag(QGraphicsItem::ItemIsSelectable),
		"overlay remains programmatically selectable");
	const QRectF symbol = overlay.symbolRect();
	const QRectF right = overlay.labelRect();
	// The station symbol stays compact.
	QVERIFY(qFuzzyCompare(symbol.width(), 16.0));
	QVERIFY(qFuzzyCompare(symbol.height(), 16.0));
	QVERIFY2(qFuzzyCompare(overlay.combinedRect().left(), symbol.left()), "combined bounds include symbol");
	QVERIFY2(qFuzzyCompare(right.left(), symbol.right() + 8.0), "right label gap is eight logical pixels");
	// The stable anchor is retained.
	QCOMPARE(overlay.stableAnchor(), QPointF(40.0, 50.0));
}

void TestStationOverlay::displayName_data() {
	QTest::addColumn<QString>("source");
	QTest::addColumn<QString>("shown");
	QTest::newRow("Copenhagen mixed-case suffix remains one run") << "DanshojBBx" << "Danshoj BBx";
	QTest::newRow("second Copenhagen mixed-case suffix remains one run") << "RyparkenBBx" << "Ryparken BBx";
	QTest::newRow("Copenhagen acronym prefix remains one word") << "KBHallen" << "KB Hallen";
	QTest::newRow("legacy camel-case boundary remains split") << "KogeNord" << "Koge Nord";
	QTest::newRow("Italian acronym prefix and camel-case boundary remain split") << "PMBivioAdda" << "PM Bivio Adda";
	QTest::newRow("three-letter acronym prefix and camel-case boundary remain split") << "XYZMainStation" << "XYZ Main Station";
	QTest::newRow("trailing two-letter acronym remains intact") << "FlintholmCH" << "Flintholm CH";
	QTest::newRow("multiple word boundaries preserve the trailing acronym") << "NyEllebjergAE" << "Ny Ellebjerg AE";
}

void TestStationOverlay::displayName() {
	QFETCH(QString, source);
	QFETCH(QString, shown);
	QCOMPARE(StationOverlayItem::displayName(source), shown);
}

void TestStationOverlay::keepsSourceStationName() {
	const StationOverlayItem acronymOverlay("KBHallen", QPointF(), classifyStation());
	// Display formatting leaves the source station identity unchanged.
	QCOMPARE(acronymOverlay.stationName(), QStringLiteral("KBHallen"));
	QCOMPARE(acronymOverlay.displayName(), QStringLiteral("KB Hallen"));
}

void TestStationOverlay::nameLayer() {
	StationOverlayItem overlay("KogeNord", QPointF(40.0, 50.0), classifyStation());
	overlay.setNameVisible(false);
	QVERIFY2(!overlay.isLabelVisible(), "station-name layer hides the label without hiding the station item");
	QVERIFY2(overlay.isVisible(), "station-name layer does not hide the station symbol");
	overlay.setNameVisible(true);
	QVERIFY2(overlay.isLabelVisible(), "station-name layer restores the label");
}

void TestStationOverlay::viewportPlacement() {
	const StationOverlayItem overlay("KogeNord", QPointF(40.0, 50.0), classifyStation());
	const QRectF inset(0.0, 0.0, 220.0, 120.0);
	const StationOverlayItem::ViewportPlacement rightPlacement =
		overlay.placementForSide(StationOverlayItem::LabelSide::Right, QPointF(80.0, 60.0), inset);
	// Viewport placement prefers the right side.
	QCOMPARE(rightPlacement.side, StationOverlayItem::LabelSide::Right);
	QVERIFY2(rightPlacement.fits, "right-side placement fits in the viewport");
	QVERIFY2(qFuzzyCompare(rightPlacement.labelRect.left(), rightPlacement.symbolRect.right() + 8.0),
		"production placement keeps the right eight-pixel gap");
	const StationOverlayItem::ViewportPlacement leftPlacement =
		overlay.placementForSide(StationOverlayItem::LabelSide::Left, QPointF(80.0, 60.0), inset);
	// Viewport placement supports the left side.
	QCOMPARE(leftPlacement.side, StationOverlayItem::LabelSide::Left);
	QVERIFY2(qFuzzyCompare(leftPlacement.symbolRect.left(), leftPlacement.labelRect.right() + 8.0),
		"production placement keeps the left eight-pixel gap");
	const StationOverlayItem::ViewportPlacement abovePlacement =
		overlay.placementForSide(StationOverlayItem::LabelSide::Above, QPointF(80.0, 60.0), inset);
	QVERIFY2(qFuzzyCompare(abovePlacement.labelRect.bottom() + 8.0,
				 abovePlacement.symbolRect.top()),
		"production placement keeps the upper eight-pixel gap");
	const StationOverlayItem::ViewportPlacement belowPlacement =
		overlay.placementForSide(StationOverlayItem::LabelSide::Below, QPointF(80.0, 60.0), inset);
	QVERIFY2(qFuzzyCompare(belowPlacement.symbolRect.bottom() + 8.0,
				 belowPlacement.labelRect.top()),
		"production placement keeps the lower eight-pixel gap");
	const StationOverlayItem::ViewportPlacement edgePlacement =
		overlay.preferredViewportPlacement(QPointF(2.0, 2.0), inset);
	QVERIFY2(edgePlacement.fits, "edge placement clamps the complete overlay into the viewport");
	QVERIFY2(inset.contains(edgePlacement.combinedRect), "clamped overlay stays inside the viewport inset");
	const StationOverlayItem::ViewportPlacement offscreenPlacement =
		overlay.preferredViewportPlacement(QPointF(80.0, -500.0), inset);
	// Far-offscreen station overlays are not pinned to the viewport edge.
	QVERIFY(offscreenPlacement.offset.isNull());
	QVERIFY(!offscreenPlacement.fits);
}

void TestStationOverlay::matchesEveryPlatformIdentity() {
	StationOverlayItem multiNode("MultiNode", QPointF(8.0, 8.0), classifyStation());
	multiNode.setSourceIdentities({{-2521.0, 21}, {-2522.0, 22}});
	// One station overlay matches every assigned platform identity.
	QCOMPARE(multiNode.sourceIdentityCount(), 2);
	QVERIFY(multiNode.matchesSourceIdentity(-2521.0, 21));
	QVERIFY(multiNode.matchesSourceIdentity(-2522.0, 22));
	// The first assigned platform remains the deterministic representative identity.
	QCOMPARE_EQ(multiNode.sourceNodeId(), -2521.0);
	QCOMPARE(multiNode.sourceTrack(), 21);
	multiNode.setNetworkDegree(3, true, true);
	QVERIFY2(multiNode.isInterchange(), "multi-node station keeps interchange flag");
	QVERIFY2(multiNode.isEndpoint(), "multi-node station keeps endpoint flag");
}

void TestStationOverlay::ranksStations() {
	const StationVisual stationVisual = classifyStation();
	QList<StationOverlayItem*> candidates;
	StationOverlayItem selected("Zulu", QPointF(0.0, 0.0), stationVisual);
	StationOverlayItem followed("Alpha", QPointF(1.0, 1.0), stationVisual);
	StationOverlayItem interchange("Beta", QPointF(2.0, 2.0), stationVisual, 3);
	StationOverlayItem endpoint("Gamma", QPointF(3.0, 3.0), stationVisual, 1);
	StationOverlayItem stop("Delta", QPointF(4.0, 4.0), stationVisual);
	selected.setSelected(true);
	followed.setFollowed(true);
	candidates << &stop << &endpoint << &interchange << &followed << &selected;
	std::sort(candidates.begin(), candidates.end(), [](const auto* a, const auto* b) {
		return StationOverlayItem::priorityLess(*a, *b, QPointF(0.0, 0.0));
	});
	QVERIFY2(candidates.at(0) == &selected, "selected station has first priority");
	QVERIFY2(candidates.at(1) == &followed, "followed station has second priority");
	QVERIFY2(candidates.at(2) == &interchange, "interchange has third priority");
	QVERIFY2(candidates.at(3) == &endpoint, "endpoint has fourth priority");
	QVERIFY2(candidates.at(4) == &stop, "ordinary station has remaining priority");
	StationOverlayItem nameZulu("ZuluTie", QPointF(40.0, 0.0), stationVisual);
	StationOverlayItem nameAlpha("AlphaTie", QPointF(-40.0, 0.0), stationVisual);
	QVERIFY2(StationOverlayItem::priorityLess(nameAlpha, nameZulu, QPointF()),
		"station name breaks equal-distance priority ties");
}

void TestStationOverlay::clickPassesThroughOverlay() {
	NetworkScene scene(nullptr);
	QGraphicsView view(&scene);
	view.resize(240, 180);
	StationNodeItem station(QRectF(-10.0, -10.0, 20.0, 20.0));
	station.setPos(0.0, 0.0);
	StationOverlayItem decoration("Koge", QPointF(0.0, 0.0), classifyStation());
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
	// A left click passes through the station overlay.
	QCOMPARE(stationClicks, 1);
	QVERIFY2(decoration.isSelected(), "semantic station selection survives default scene dispatch");
	QVERIFY2(sendContextMenu(scene, view, QPointF(0.0, 0.0)), "context event accepted through station overlay");
	QVERIFY2(contextTarget == &station, "context menu preserves station semantic target");
}

void TestStationOverlay::artworkResolvesToStationNode() {
	NetworkScene scene(nullptr);
	QGraphicsView view(&scene);
	view.resize(240, 180);
	StationNodeItem station(QRectF(-10.0, -10.0, 20.0, 20.0));
	StationOverlayItem sceneOverlay("SceneStation", QPointF(0.0, 0.0), classifyStation());
	sceneOverlay.setSceneDecoration(true);
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
	// Legacy screen collision does not leave an unrelated station hit target.
	QVERIFY(sceneOverlay.shape().isEmpty());
	QCOMPARE(sceneOverlay.acceptedMouseButtons(), Qt::MouseButtons(Qt::NoButton));
	sendLeftClick(scene, view, QPointF(100.0, 0.0));
	// Scene-scaled artwork resolves to its semantic station node.
	QCOMPARE(clicks, 1);
	sendContextMenu(scene, view, QPointF(100.0, 0.0));
	QVERIFY2(contextTarget == &station, "artwork context menu resolves to its station node");
}

void TestStationOverlay::readableItemScale() {
	const qreal natural = 300.0 * kPresentation;
	// A scene size above the minimum is unchanged.
	QCOMPARE_EQ(StationOverlayItem::readableItemScale(300.0, kPresentation, 1.0, 24.0), kPresentation);
	const qreal small = StationOverlayItem::readableItemScale(300.0, kPresentation, 0.01, 24.0);
	// Below the minimum the size on screen is exact.
	QCOMPARE_LT(std::abs(small * 300.0 * 0.01 - 24.0), 1e-9);
	// Below the minimum the size does not depend on the scene scale.
	QCOMPARE_EQ(StationOverlayItem::readableItemScale(300.0, 0.2, 0.01, 24.0), small);
	const qreal crossover = 24.0 / natural;
	const qreal below = StationOverlayItem::readableItemScale(300.0, kPresentation, crossover * 0.999, 24.0);
	const qreal above = StationOverlayItem::readableItemScale(300.0, kPresentation, crossover * 1.001, 24.0);
	// The size has no jump at the crossover.
	QCOMPARE_LT(std::abs(below - above) / above, 0.01);
	qreal previous = 0.0;
	bool monotonic = true;
	for (qreal viewScale : {0.001, 0.005, 0.01, 0.05, crossover, 0.5, 1.0, 4.0}) {
		const qreal onScreen = StationOverlayItem::readableItemScale(300.0, kPresentation, viewScale, 24.0)
			* 300.0 * viewScale;
		monotonic = monotonic && onScreen >= previous;
		previous = onScreen;
	}
	QVERIFY2(monotonic, "the size on screen never shrinks when zooming in");
}

void TestStationOverlay::pictogramKeepsAnchorsAcrossScales() {
	QGraphicsScene scene;
	QGraphicsView view(&scene);
	const StationRig rig = addStationRig(scene, "Paimpol", QPointF(100.0, 200.0), 2, QPointF(0.0, 40.0));
	rig.overlay->setSourceIdentities({{-3.0, 7}});
	// The pictogram stands on its artwork point after the node became its parent.
	QCOMPARE_LT(QLineF(pictureAnchorInScene(rig), QPointF(100.0, 240.0)).length(), 1e-6);
	const QPointF pictureAnchor = pictureAnchorInScene(rig);
	const QPointF nameAnchor = nameAnchorInScene(rig);
	const QRectF naturalPicture = rig.picture->sceneBoundingRect();
	const QRectF naturalName = rig.name->sceneBoundingRect();
	// The overlay links its artwork.
	QVERIFY(rig.overlay->pictureItem() == rig.picture);
	QVERIFY(rig.overlay->nameItem() == rig.name);
	QCOMPARE(rig.overlay->platformCount(), 2);
	view.setTransform(QTransform::fromScale(0.01, 0.01));
	rig.overlay->applyViewScale(0.01);
	const QTransform device = view.viewportTransform();
	const qreal pictureHeight = deviceRect(device, rig.picture).height();
	// The pictogram is 24 pixels high on screen at a small view scale.
	QCOMPARE_GE(pictureHeight, 23.5);
	QCOMPARE_LT(pictureHeight, 25.0);
	const qreal fontPixels = rig.name->font().pixelSize() * rig.name->sceneTransform().m22() * device.m22();
	// The name has a 12 pixel font on screen at a small view scale.
	QCOMPARE_LT(std::abs(fontPixels - 12.0), 0.12);
	QCOMPARE_GE(deviceRect(device, rig.name).height(), 12.0);
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
	QVERIFY2(stable, "anchors, parent and identity are unchanged across view scales");
	rig.overlay->applyViewScale(1.0);
	const auto sameRect = [](const QRectF& a, const QRectF& b) {
		return QLineF(a.topLeft(), b.topLeft()).length() < 1e-6
			&& QLineF(a.bottomRight(), b.bottomRight()).length() < 1e-6;
	};
	// The scene size returns at a large view scale.
	QVERIFY(rig.picture->transform().isIdentity());
	QVERIFY(rig.name->transform().isIdentity());
	QVERIFY(sameRect(rig.picture->sceneBoundingRect(), naturalPicture));
	QVERIFY(sameRect(rig.name->sceneBoundingRect(), naturalName));
}

void TestStationOverlay::enlargedPictogramTakesClicks() {
	NetworkScene scene(nullptr);
	QGraphicsView view(&scene);
	view.resize(240, 180);
	view.setTransform(QTransform::fromScale(0.01, 0.01));
	const StationRig rig = addStationRig(scene, "ClickStation", QPointF(0.0, 0.0), 1, QPointF(0.0, 300.0));
	int clicks = 0;
	QObject::connect(&scene, &NetworkScene::MousePressedOnStationNode,
		[&](StationNodeItem* item) { if (item == rig.node) ++clicks; });
	const QPointF inside(500.0, -500.0);
	QVERIFY2(!rig.picture->sceneBoundingRect().contains(inside), "the click point is outside the scene-sized pictogram");
	sendLeftClick(scene, view, inside);
	// A click beside the scene-sized pictogram selects nothing.
	QCOMPARE(clicks, 0);
	rig.overlay->applyViewScale(0.01);
	QVERIFY2(rig.picture->sceneBoundingRect().contains(inside), "the enlarged pictogram covers the click point");
	sendLeftClick(scene, view, inside);
	// A click inside the enlarged pictogram selects its station.
	QCOMPARE(clicks, 1);
}

void TestStationOverlay::resolvesNameCollisions() {
	QGraphicsScene scene;
	const QTransform small = QTransform::fromScale(0.01, 0.01);
	QList<StationRig> rigs;
	rigs << addStationRig(scene, "Alpha", QPointF(0.0, 0.0), 1)
		 << addStationRig(scene, "Beta", QPointF(200.0, 0.0), 3)
		 << addStationRig(scene, "Gamma", QPointF(400.0, 0.0), 2);
	applyAll(rigs, 0.01);
	StationOverlayItem::resolveNameCollisions({rigs[0].overlay, rigs[1].overlay, rigs[2].overlay}, small);
	// Of three overlapping names the one of the station with most platforms stays.
	QCOMPARE(hiddenNames(rigs), QList<bool>({true, false, true}));
	QVERIFY2(std::all_of(rigs.cbegin(), rigs.cend(), [](const StationRig& rig) {
		return rig.picture->isVisible() && rig.overlay->isVisible();
	}),
		"colliding names never hide a pictogram");
	const QList<bool> first = hiddenNames(rigs);
	StationOverlayItem::resolveNameCollisions({rigs[0].overlay, rigs[1].overlay, rigs[2].overlay}, small);
	// The collision result is the same when run twice.
	QCOMPARE(hiddenNames(rigs), first);
	StationOverlayItem::resolveNameCollisions({rigs[2].overlay, rigs[1].overlay, rigs[0].overlay}, small);
	// The collision result does not depend on the list order.
	QCOMPARE(hiddenNames(rigs), first);
	QTransform panned = small;
	panned.translate(4000.0, -2500.0);
	StationOverlayItem::resolveNameCollisions({rigs[0].overlay, rigs[1].overlay, rigs[2].overlay}, panned);
	// Panning does not change which names are hidden.
	QCOMPARE(hiddenNames(rigs), first);
	rigs[0].overlay->setFollowed(true);
	StationOverlayItem::resolveNameCollisions({rigs[0].overlay, rigs[1].overlay, rigs[2].overlay}, small);
	// A followed station ranks above one with more platforms.
	QCOMPARE(hiddenNames(rigs), QList<bool>({false, true, true}));
	rigs[2].overlay->setSelected(true);
	StationOverlayItem::resolveNameCollisions({rigs[0].overlay, rigs[1].overlay, rigs[2].overlay}, small);
	// Selected and followed stations both keep their name.
	QCOMPARE(hiddenNames(rigs), QList<bool>({false, true, false}));
	rigs[0].overlay->setFollowed(false);
	StationOverlayItem::resolveNameCollisions({rigs[0].overlay, rigs[1].overlay, rigs[2].overlay}, small);
	// A selected station ranks above all others.
	QCOMPARE(hiddenNames(rigs), QList<bool>({true, true, false}));
	rigs[2].overlay->setSelected(false);
	applyAll(rigs, 1.0);
	StationOverlayItem::resolveNameCollisions({rigs[0].overlay, rigs[1].overlay, rigs[2].overlay},
		QTransform::fromScale(1.0, 1.0));
	// All names are visible after zooming in.
	QCOMPARE(hiddenNames(rigs), QList<bool>({false, false, false}));
}

// Equal priority except for the position: the tie break must not follow the viewport.
void TestStationOverlay::equalStationsKeepOrderWhenPanned() {
	QGraphicsScene scene;
	const QTransform small = QTransform::fromScale(0.01, 0.01);
	QList<StationRig> rigs;
	rigs << addStationRig(scene, "Near", QPointF(1000.0, 0.0), 2)
		 << addStationRig(scene, "Far", QPointF(1200.0, 0.0), 2);
	applyAll(rigs, 0.01);
	StationOverlayItem::resolveNameCollisions({rigs[0].overlay, rigs[1].overlay}, small);
	const QList<bool> first = hiddenNames(rigs);
	// Of two equal stations with overlapping names one stays.
	QCOMPARE(first.count(true), qsizetype(1));
	QTransform panned = small;
	panned.translate(-3000.0, 0.0);
	StationOverlayItem::resolveNameCollisions({rigs[0].overlay, rigs[1].overlay}, panned);
	// Panning does not reorder equal stations.
	QCOMPARE(hiddenNames(rigs), first);
}

// The name of the upper station lies on the pictogram of the lower one. The names
// themselves are apart, so only the pictogram can hide the upper name.
void TestStationOverlay::nameGivesWayToPictogram() {
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
	// The upper name lies on the lower pictogram and the names are apart.
	QVERIFY(upperName.intersects(lowerPicture));
	QVERIFY(!upperName.adjusted(-3.0, -3.0, 3.0, 3.0).intersects(lowerName));
	QVERIFY(!lowerName.adjusted(-3.0, -3.0, 3.0, 3.0).intersects(upperPicture));
	const QList<StationOverlayItem*> both{rigs[0].overlay, rigs[1].overlay};
	StationOverlayItem::resolveNameCollisions(both, small);
	// A name gives way to the pictogram of another station, even of one that ranks lower.
	QCOMPARE(hiddenNames(rigs), QList<bool>({true, false}));
	// A name on a pictogram hides no pictogram.
	QVERIFY(rigs[0].picture->isVisible());
	QVERIFY(rigs[1].picture->isVisible());
	rigs[0].overlay->setSelected(true);
	StationOverlayItem::resolveNameCollisions(both, small);
	// A selected station keeps its name on the pictogram of another station.
	QCOMPARE(hiddenNames(rigs), QList<bool>({false, false}));
	rigs[0].overlay->setSelected(false);
	rigs[1].picture->setVisible(false);
	StationOverlayItem::resolveNameCollisions(both, small);
	// A hidden pictogram hides no name.
	QCOMPARE(hiddenNames(rigs), QList<bool>({false, false}));
	rigs[1].picture->setVisible(true);
	StationOverlayItem::resolveNameCollisions({rigs[0].overlay}, small);
	QVERIFY2(hiddenNames(rigs).first() == false, "the own pictogram of a station does not hide its name");
}

// A station mark at the place of its first node: the mark and the area take the node centres within 32 frames of
// the anchor, and a station without one has none. The area takes the platform bars there as well.
void TestStationOverlay::markAtPlaceOfFirstNode() {
	QGraphicsScene scene;
	const QPointF first(0.0, 0.0);
	const QPointF second(30.0, 40.0);
	const QPointF elsewhere(1000.0, 0.0);
	const MarkRig rig = addMarkRig(scene, "Alpha", {first, second, elsewhere});
	// A station node more than 32 frames from the anchor is left out of the mark and the area.
	QCOMPARE(rig.mark->nodeCentres(), QList<QPointF>({first, second}));
	rig.overlay->setNodeCentres({elsewhere});
	QVERIFY2(rig.mark->nodeCentres().isEmpty(), "a station with no node at its place has no node centres");
	rig.overlay->setNodeCentres({first, second, elsewhere});
	rig.overlay->setPlatformBars({QPolygonF(QRectF(-30.0, -40.0, 60.0, 12.0)), QPolygonF(QRectF(970.0, -6.0, 60.0, 12.0))});

	// Far look: tracks 4 pixels apart. The frame of 24 pixels (240 scene units) stands on the line between the two
	// nodes, halfway along it, and the name is a quarter frame under the footprint of the squares.
	QGraphicsView view(&scene);
	view.setTransform(QTransform::fromScale(0.1, 0.1));
	rig.overlay->applyViewScale(0.1, false);
	const QRectF farFootprint = stationFootprintRect({first, second}, StationClass::Station, 240.0);
	const qreal frameHeight = view.viewportTransform().mapRect(rig.mark->mapRectToScene(QRectF(0.0, 0.0, 30.0, 30.0))).height();
	// The far mark stands on the line between two diagonal station nodes, 24 pixels high.
	QVERIFY(!rig.mark->isNearLook());
	QVERIFY(samePoint(frameAnchorInScene(rig), (first + second) / 2.0));
	QCOMPARE_LT(std::abs(frameHeight - 24.0), 0.01);
	QVERIFY2(samePoint(nameAnchorInScene(rig), QPointF(farFootprint.center().x(), farFootprint.bottom() + 60.0)),
		"the name is centred under the squares in the far look, at the x of the mark");
	const QGraphicsPathItem* area = rig.overlay->areaItem();
	if (area == nullptr)
		QFAIL("the far look shows the station area under the tracks, without input");
	// The far look shows the station area under the tracks, without input.
	QVERIFY(area->scene() == &scene);
	QVERIFY(area->isVisible());
	QCOMPARE_EQ(area->zValue(), StationOverlayItem::AreaZValue);
	QCOMPARE_LT(area->zValue(), 0.0);
	QCOMPARE(area->acceptedMouseButtons(), Qt::MouseButtons(Qt::NoButton));
	QVERIFY(!area->acceptHoverEvents());
	QCOMPARE(area->pen().style(), Qt::NoPen);
	QCOMPARE(area->brush().color(), stationAreaColor());
	const QRectF areaBounds = area->path().boundingRect();
	// The far area covers the station nodes, not the bars, and reaches 6 pixels past them.
	QVERIFY(area->path().contains(QPointF(15.0, 20.0)));
	QVERIFY(!area->path().contains(elsewhere));
	QVERIFY(containsRect(QRectF(-60.5, -60.5, 151.0, 161.0), areaBounds));
	QVERIFY(containsRect(areaBounds, QRectF(-59.5, -59.5, 149.0, 159.0)));

	// Near look: tracks 20 pixels apart. The mark draws nothing and has no bounds and no shape, so that the squares
	// show and take the clicks; it stays visible for the layer switches and in its place. The name keeps a quarter
	// frame of 24 pixels (48 scene units) under the squares, and the area covers the platform bar at the place of the
	// station.
	view.setTransform(QTransform::fromScale(0.5, 0.5));
	rig.overlay->applyViewScale(0.5, true);
	const QRectF nearFootprint = stationFootprintRect({first, second}, StationClass::Station, 48.0);
	// The near mark stays visible and in its place.
	QVERIFY(rig.mark->isNearLook());
	QVERIFY(rig.mark->isVisible());
	QVERIFY(samePoint(frameAnchorInScene(rig), (first + second) / 2.0));
	QImage nearImage(64, 64, QImage::Format_ARGB32_Premultiplied);
	nearImage.fill(Qt::transparent);
	{
		QPainter painter(&nearImage);
		painter.translate(32.0, 32.0);
		rig.mark->paint(&painter, nullptr, nullptr);
	}
	bool nearPainted = false;
	for (int y = 0; y < nearImage.height(); ++y)
		for (int x = 0; x < nearImage.width(); ++x)
			nearPainted = nearPainted || nearImage.pixelColor(x, y).alpha() > 0;
	// The near mark paints nothing and has no bounds and no shape.
	QVERIFY(!nearPainted);
	QVERIFY(rig.mark->boundingRect().isEmpty());
	QVERIFY(rig.mark->shape().isEmpty());
	QVERIFY2(samePoint(nameAnchorInScene(rig), QPointF(nearFootprint.center().x(), nearFootprint.bottom() + 12.0)),
		"the name is centred under the squares in the near look");
	const QRectF nearAreaBounds = area->path().boundingRect();
	// The near look shows the area over the nodes and the bar at the place of the station, 6 pixels past them.
	QVERIFY(area->isVisible());
	QVERIFY(area->path().contains(QPointF(-30.0, -40.0)));
	QVERIFY(area->path().contains(QPointF(30.0, -28.0)));
	QVERIFY(!area->path().contains(QPointF(1000.0, 0.0)));
	QVERIFY(containsRect(QRectF(-42.5, -52.5, 85.0, 105.0), nearAreaBounds));
	QVERIFY(containsRect(nearAreaBounds, QRectF(-41.5, -51.5, 83.0, 103.0)));
	auto* effect = new HighlightEffect(Qt::blue, 1.0);
	rig.node->setGraphicsEffect(effect);
	effect->setViewScale(0.5);
	if (effect->cue() == nullptr)
		QFAIL("the selection cue of the station square holds the square alone in the near look");
	// The selection cue of the station square holds the square alone in the near look.
	QCOMPARE(effect->cue()->targetRect(), rig.node->mapRectToScene(rig.node->rect()));
	rig.node->setGraphicsEffect(nullptr);

	// One mark size in both looks, so that the names that show do not depend on the look.
	const QRectF nearRect = rig.overlay->pictogramSceneRect();
	QVERIFY2(containsRect(nearRect, nearFootprint), "the pictogram rect holds the squares");
	rig.overlay->applyViewScale(0.5, false);
	// The pictogram rect is the same in both looks and holds the pin.
	QVERIFY(!rig.mark->isNearLook());
	QCOMPARE(rig.mark->boundingRect(), rig.mark->farBoundingRect());
	QCOMPARE(rig.overlay->pictogramSceneRect(), nearRect);
	QVERIFY(containsRect(nearRect, rig.mark->sceneBoundingRect()));

	// Back to the far look: the mark, the name and the area return to their places.
	view.setTransform(QTransform::fromScale(0.1, 0.1));
	rig.overlay->applyViewScale(0.1, false);
	// The far mark, its name and the area return after the near look.
	QVERIFY(samePoint(frameAnchorInScene(rig), (first + second) / 2.0));
	QVERIFY(samePoint(nameAnchorInScene(rig), QPointF(farFootprint.center().x(), farFootprint.bottom() + 60.0)));
	QVERIFY(area->isVisible());
	QCOMPARE(area->path().boundingRect(), areaBounds);
	rig.node->setVisible(false);
	rig.overlay->applyViewScale(0.1, false);
	QVERIFY2(!area->isVisible(), "the area hides with the mark");
	rig.node->setVisible(true);
	rig.overlay->applyViewScale(0.1, false);
	QVERIFY2(area->isVisible(), "the area shows again with the mark");
	rig.node->setVisible(false);
	rig.overlay->applyViewScale(0.5, true);
	QVERIFY2(!area->isVisible(), "the area hides with the mark in the near look");
	rig.node->setVisible(true);
}

void TestStationOverlay::farMarkStandsOnHull_data() {
	QTest::addColumn<QList<QPointF>>("centres");
	QTest::addColumn<QPointF>("foot");
	QTest::newRow("the far mark of one node stands on it") << QList<QPointF>{QPointF(0.0, 0.0)} << QPointF(0.0, 0.0);
	QTest::newRow("the far mark of a row of nodes stands on its middle")
		<< QList<QPointF>{QPointF(0.0, 0.0), QPointF(40.0, 0.0), QPointF(80.0, 0.0)} << QPointF(40.0, 0.0);
	QTest::newRow("the far mark of a stack of nodes stands on its top")
		<< QList<QPointF>{QPointF(0.0, 40.0), QPointF(0.0, 0.0)} << QPointF(0.0, 0.0);
	QTest::newRow("the far mark of a triangle of nodes stands on its upper edge, under the top of their box")
		<< QList<QPointF>{QPointF(0.0, 0.0), QPointF(0.0, 40.0), QPointF(40.0, 40.0)} << QPointF(20.0, 20.0);
	QTest::newRow("the far mark stands on the edge of the hull that passes the middle of the box")
		<< QList<QPointF>{QPointF(0.0, 40.0), QPointF(20.0, 0.0), QPointF(80.0, 40.0)} << QPointF(40.0, 40.0 / 3.0);
}

// The far frame stands on the top edge of the hull of the node centres at the x of the middle of the squares: on
// the top centre of their box for one node, a row and a stack, and on the area of a diagonal or triangular station.
void TestStationOverlay::farMarkStandsOnHull() {
	QFETCH(QList<QPointF>, centres);
	QFETCH(QPointF, foot);
	QGraphicsScene scene;
	const MarkRig rig = addMarkRig(scene, "Shape", centres);
	rig.overlay->applyViewScale(0.1, false);
	QVERIFY(samePoint(frameAnchorInScene(rig), foot));
}

// A name whose text under the squares crosses a visible track line goes above the mark, then right of the squares,
// then left of them; with no free spot it stays under the squares.
void TestStationOverlay::nameAvoidsTracks() {
	QGraphicsScene scene;
	const QTransform far = QTransform::fromScale(0.1, 0.1);
	const MarkRig rig = addMarkRig(scene, "Alpha", {QPointF(0.0, 0.0), QPointF(0.0, 40.0)});
	const QRectF squares = stationFootprintRect(rig.mark->nodeCentres(), StationClass::Station, 240.0);
	const auto place = [&]() {
		rig.overlay->applyViewScale(0.2, false);
		rig.overlay->applyViewScale(0.1, false);
		StationOverlayItem::resolveNameCollisions({rig.overlay}, far);
		return rig.name->sceneBoundingRect();
	};
	const QRectF under = place();
	// A name without tracks in its way stays under the squares.
	QCOMPARE_GT(under.top(), squares.bottom());
	QCOMPARE_LT(std::abs(under.center().x() - squares.center().x()), 1e-6);
	auto* below = new TrackLineItem(QLineF(-2000.0, under.center().y(), 2000.0, under.center().y()));
	scene.addItem(below);
	const QRectF above = place();
	// A name whose spot under the squares crosses a track goes above the mark.
	QCOMPARE_LT(above.bottom(), frameAnchorInScene(rig).y() - 240.0);
	QCOMPARE_LT(std::abs(above.center().x() - squares.center().x()), 1e-6);
	below->setVisible(false);
	StationOverlayItem::resolveNameCollisions({rig.overlay}, far);
	// A name keeps its spot until the mark is placed again.
	QCOMPARE(rig.name->sceneBoundingRect(), above);
	// A hidden track does not move a name.
	QCOMPARE(place(), under);
	below->setVisible(true);
	scene.addItem(new TrackLineItem(QLineF(-2000.0, above.center().y(), 2000.0, above.center().y())));
	const QRectF rightSpot = place();
	// A name with tracks under the squares and above the mark goes right of the squares.
	QCOMPARE_GT(rightSpot.left(), squares.right());
	QCOMPARE_LT(rightSpot.top(), squares.center().y());
	QCOMPARE_GT(rightSpot.bottom(), squares.center().y());
	scene.addItem(new TrackLineItem(QLineF(rightSpot.center().x(), -2000.0, rightSpot.center().x(), 2000.0)));
	const QRectF leftSpot = place();
	// Then it goes left of the squares.
	QCOMPARE_LT(leftSpot.right(), squares.left());
	QCOMPARE_LT(leftSpot.top(), squares.center().y());
	QCOMPARE_GT(leftSpot.bottom(), squares.center().y());
	scene.addItem(new TrackLineItem(QLineF(leftSpot.center().x(), -2000.0, leftSpot.center().x(), 2000.0)));
	// A name with no free spot stays under the squares.
	QCOMPARE(place(), under);
}

// The frame of a diagonal station stands on its area, under the top of the box of its node centres. With the four
// spots by the squares and the frame blocked, its name goes above a frame on the top centre of that box.
void TestStationOverlay::spotsOfDiagonalStation() {
	QGraphicsScene scene;
	const QTransform far = QTransform::fromScale(0.1, 0.1);
	const MarkRig rig = addMarkRig(scene, "Alpha", {QPointF(0.0, 0.0), QPointF(-270.0, -270.0), QPointF(270.0, 270.0)});
	const auto place = [&]() {
		rig.overlay->applyViewScale(0.2, false);
		rig.overlay->applyViewScale(0.1, false);
		StationOverlayItem::resolveNameCollisions({rig.overlay}, far);
		return rig.name->sceneBoundingRect();
	};
	const QRectF under = place();
	scene.addItem(new TrackLineItem(QLineF(-2000.0, under.center().y(), 2000.0, under.center().y())));
	const QRectF above = place();
	scene.addItem(new TrackLineItem(QLineF(-2000.0, above.center().y(), 2000.0, above.center().y())));
	const QRectF rightSpot = place();
	scene.addItem(new TrackLineItem(QLineF(rightSpot.center().x(), -2000.0, rightSpot.center().x(), 2000.0)));
	const QRectF leftSpot = place();
	scene.addItem(new TrackLineItem(QLineF(leftSpot.center().x(), -2000.0, leftSpot.center().x(), 2000.0)));
	const QRectF last = place();
	// The first four spots of a diagonal station are under the squares, above its frame, right and left of the squares.
	QVERIFY(samePoint(frameAnchorInScene(rig), QPointF(0.0, 0.0)));
	QCOMPARE_LT(above.bottom(), -240.0);
	QCOMPARE_GT(above.bottom(), -270.0 - 240.0);
	QCOMPARE_GT(rightSpot.left(), 0.0);
	QCOMPARE_LT(leftSpot.right(), 0.0);
	// A name with its first four spots blocked goes above a frame on the top of the box of the node centres.
	QCOMPARE_LT(last.bottom(), -270.0 - 240.0);
	QCOMPARE_LT(std::abs(last.center().x()), 1e-6);
}

// A name whose spot under the squares crosses a track goes above the pin, and in the near look, which draws no
// pin, just above the squares, so that it does not float away from them.
void TestStationOverlay::nameAbovePinInNearLook() {
	QGraphicsScene scene;
	const QTransform view = QTransform::fromScale(0.1, 0.1);
	const MarkRig rig = addMarkRig(scene, "Alpha", {QPointF(0.0, 0.0), QPointF(0.0, 40.0)});
	rig.overlay->applyViewScale(0.1, false);
	StationOverlayItem::resolveNameCollisions({rig.overlay}, view);
	const QRectF under = rig.name->sceneBoundingRect();
	scene.addItem(new TrackLineItem(QLineF(-2000.0, under.center().y(), 2000.0, under.center().y())));
	rig.overlay->applyViewScale(0.2, false);
	rig.overlay->applyViewScale(0.1, false);
	StationOverlayItem::resolveNameCollisions({rig.overlay}, view);
	const QRectF farName = rig.name->sceneBoundingRect();
	rig.overlay->applyViewScale(0.1, true);
	StationOverlayItem::resolveNameCollisions({rig.overlay}, view);
	const QRectF nearName = rig.name->sceneBoundingRect();
	const QRectF squares = stationFootprintRect(rig.mark->nodeCentres(), StationClass::Station, 240.0);
	// A name above the pin goes just above the squares in the near look.
	QCOMPARE_LT(farName.bottom(), frameAnchorInScene(rig).y() - 240.0);
	QVERIFY(rig.mark->isNearLook());
	QCOMPARE_LT(nearName.bottom(), squares.top());
	QCOMPARE_GT(nearName.bottom(), farName.bottom() + 120.0);
	QCOMPARE_LT(std::abs(nearName.center().x() - farName.center().x()), 1e-6);
	QVERIFY(!rig.overlay->isNameHiddenByCollision());
	rig.overlay->applyViewScale(0.1, false);
	StationOverlayItem::resolveNameCollisions({rig.overlay}, view);
	// And when it switches back to the pin.
	QVERIFY(!rig.mark->isNearLook());
	QCOMPARE(rig.name->sceneBoundingRect(), farName);
}

// A name gives way in the same order to the mark of another station.
void TestStationOverlay::nameGivesWayToOtherMark() {
	QGraphicsScene scene;
	const QTransform far = QTransform::fromScale(0.1, 0.1);
	const MarkRig upper = addMarkRig(scene, "Upper", {QPointF(0.0, 0.0)});
	const MarkRig lower = addMarkRig(scene, "Lower", {QPointF(0.0, 400.0)});
	upper.overlay->applyViewScale(0.1, false);
	lower.overlay->applyViewScale(0.1, false);
	StationOverlayItem::resolveNameCollisions({upper.overlay, lower.overlay}, far);
	const QRectF name = upper.name->sceneBoundingRect();
	// A name whose spot under the squares covers the mark of another station goes above its own mark.
	QCOMPARE_LT(name.bottom(), frameAnchorInScene(upper).y() - 240.0);
	QVERIFY(!upper.overlay->isNameHiddenByCollision());
	QVERIFY(!name.intersects(lower.overlay->pictogramSceneRect()));
}

// A name with every other spot blocked stays under the squares, where it covers the pin of another station: it
// gives way to that pin in the far look, which draws it, and not in the near look, which does not. The squares of
// the lower station are below the name.
void TestStationOverlay::nameGivesWayToPinInFarLookOnly() {
	QGraphicsScene scene;
	const QTransform far = QTransform::fromScale(0.1, 0.1);
	const MarkRig upper = addMarkRig(scene, "Upper", {QPointF(0.0, 0.0)});
	const MarkRig lower = addMarkRig(scene, "Lower", {QPointF(0.0, 600.0)});
	const auto place = [&](bool nearLook) {
		for (const MarkRig* rig : {&upper, &lower}) {
			rig->overlay->applyViewScale(0.2, nearLook);
			rig->overlay->applyViewScale(0.1, nearLook);
		}
		StationOverlayItem::resolveNameCollisions({upper.overlay, lower.overlay}, far);
		return upper.name->sceneBoundingRect();
	};
	const QRectF above = place(false);
	scene.addItem(new TrackLineItem(QLineF(-2000.0, above.center().y(), 2000.0, above.center().y())));
	const QRectF rightSpot = place(false);
	scene.addItem(new TrackLineItem(QLineF(rightSpot.center().x(), -2000.0, rightSpot.center().x(), 2000.0)));
	const QRectF leftSpot = place(false);
	scene.addItem(new TrackLineItem(QLineF(leftSpot.center().x(), -2000.0, leftSpot.center().x(), 2000.0)));
	const QRectF under = place(false);
	const bool farHidden = upper.overlay->isNameHiddenByCollision();
	place(true);
	// A name gives way to the pin of another station in the far look and not in the near look, which draws none.
	QVERIFY(under.intersects(lower.overlay->pictogramSceneRect()));
	QVERIFY(farHidden);
	QVERIFY(lower.mark->isNearLook());
	QVERIFY(!upper.overlay->isNameHiddenByCollision());
}

// In the near look a name still gives way to the squares of another station, which show. The name of the upper
// station, with every other spot blocked, stays under its squares, where the squares of the lower station are.
void TestStationOverlay::nameGivesWayToSquaresInNearLook() {
	QGraphicsScene scene;
	const QTransform zoomed = QTransform::fromScale(0.5, 0.5);
	const MarkRig upper = addMarkRig(scene, "Upper", {QPointF(0.0, 0.0)});
	const MarkRig lower = addMarkRig(scene, "Lower", {QPointF(0.0, 50.0)});
	const auto place = [&]() {
		for (const MarkRig* rig : {&upper, &lower}) {
			rig->overlay->applyViewScale(1.0, true);
			rig->overlay->applyViewScale(0.5, true);
		}
		StationOverlayItem::resolveNameCollisions({upper.overlay, lower.overlay}, zoomed);
		return upper.name->sceneBoundingRect();
	};
	const QRectF above = place();
	scene.addItem(new TrackLineItem(QLineF(-2000.0, above.center().y(), 2000.0, above.center().y())));
	const QRectF rightSpot = place();
	scene.addItem(new TrackLineItem(QLineF(rightSpot.center().x(), -2000.0, rightSpot.center().x(), 2000.0)));
	const QRectF leftSpot = place();
	scene.addItem(new TrackLineItem(QLineF(leftSpot.center().x(), -2000.0, leftSpot.center().x(), 2000.0)));
	const QRectF under = place();
	// A name gives way to the squares of another station in the near look.
	QVERIFY(under.contains(QPointF(0.0, 50.0)));
	QVERIFY(lower.mark->isNearLook());
	QVERIFY(upper.overlay->isNameHiddenByCollision());
}

// In the near look a station without a square at its place keeps its pin, the only thing of it to see and to
// click, while a station with squares draws nothing.
void TestStationOverlay::nearLookKeepsPinWithoutSquares() {
	NetworkScene scene(nullptr);
	QGraphicsView view(&scene);
	view.resize(240, 180);
	view.setTransform(QTransform::fromScale(0.5, 0.5));
	const MarkRig squares = addMarkRig(scene, "Squares", {QPointF(0.0, 0.0)});
	const MarkRig none = addMarkRig(scene, "None", {QPointF(400.0, 0.0)});
	none.overlay->setNodeCentres({QPointF(2000.0, 0.0)});
	const auto painted = [](StationMarkItem* mark) {
		QImage image(64, 64, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		{
			QPainter painter(&image);
			painter.translate(17.0, 17.0);
			mark->paint(&painter, nullptr, nullptr);
		}
		for (int y = 0; y < image.height(); ++y)
			for (int x = 0; x < image.width(); ++x)
				if (image.pixelColor(x, y).alpha() > 0)
					return true;
		return false;
	};
	const StationNodeItem* picked = nullptr;
	QObject::connect(&scene, &NetworkScene::MousePressedOnStationNode, [&](StationNodeItem* item) { picked = item; });
	// The centre of the head of the pin, 0.78 of the frame above its tip.
	const auto head = [](const MarkRig& rig) { return rig.mark->mapToScene(QPointF(15.0, 30.0 - 0.78 * 30.0)); };
	squares.overlay->applyViewScale(0.5, true);
	none.overlay->applyViewScale(0.5, true);
	// In the near look a mark with squares paints nothing.
	QVERIFY(squares.mark->isNearLook());
	QVERIFY(!painted(squares.mark));
	QVERIFY(squares.mark->shape().isEmpty());
	sendLeftClick(scene, view, head(squares));
	QVERIFY2(!picked, "and a click where its pin would be picks nothing");
	// In the near look a mark without squares paints its pin on its anchor.
	QVERIFY(!none.mark->isNearLook());
	QVERIFY(painted(none.mark));
	QVERIFY(!none.mark->boundingRect().isEmpty());
	QVERIFY(samePoint(frameAnchorInScene(none), QPointF(400.0, 0.0)));
	sendLeftClick(scene, view, head(none));
	QVERIFY2(picked == none.node, "and a click on the pin picks its station");
	// The click target is the pin, not its frame, and the bounds hold the halo around it.
	const QPainterPath pin = none.mark->shape();
	// The shape of a pin is the pin itself, inside its bounds.
	QVERIFY(pin.contains(QPointF(15.0, 30.0 - 0.78 * 30.0)));
	QVERIFY(!pin.contains(QPointF(1.0, 1.0)));
	QVERIFY(none.mark->boundingRect().contains(pin.boundingRect()));
	QVERIFY(none.mark->boundingRect() != pin.boundingRect());
	picked = nullptr;
	sendLeftClick(scene, view, none.mark->mapToScene(QPointF(1.0, 1.0)));
	QVERIFY2(!picked, "a click in a corner of the frame of a pin, beside the pin, picks nothing");
}

// A name avoids the mark of another station also while that mark is hidden, so that it is not hidden when the mark
// shows again: the names are not placed again then.
void TestStationOverlay::nameAvoidsHiddenMark() {
	QGraphicsScene scene;
	const QTransform far = QTransform::fromScale(0.1, 0.1);
	const MarkRig upper = addMarkRig(scene, "Upper", {QPointF(0.0, 0.0)});
	const MarkRig lower = addMarkRig(scene, "Lower", {QPointF(0.0, 400.0)});
	lower.mark->setVisible(false);
	upper.overlay->applyViewScale(0.1, false);
	lower.overlay->applyViewScale(0.1, false);
	StationOverlayItem::resolveNameCollisions({upper.overlay, lower.overlay}, far);
	lower.mark->setVisible(true);
	StationOverlayItem::resolveNameCollisions({upper.overlay, lower.overlay}, far);
	// A name placed while the mark of another station is hidden stays clear of it when it shows.
	QVERIFY(!upper.overlay->isNameHiddenByCollision());
	QVERIFY(!upper.name->sceneBoundingRect().intersects(lower.overlay->pictogramSceneRect()));
}

void TestStationOverlay::screenPinStaysInBounds_data() {
	QTest::addColumn<bool>("hasPlatformId");
	QTest::addColumn<int>("connectionCount");
	QTest::newRow("three connections") << false << 3;
	QTest::newRow("platform") << true << 0;
	QTest::newRow("plain") << false << 0;
}

// The screen-space pin of an overlay without scene artwork stays inside its bounds, blue only for a major station.
void TestStationOverlay::screenPinStaysInBounds() {
	QFETCH(bool, hasPlatformId);
	QFETCH(int, connectionCount);
	const StationVisual visual = classifyStation(hasPlatformId, connectionCount);
	StationOverlayItem boundsOverlay("Bounds", QPointF(), visual);
	QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	{
		QPainter painter(&image);
		painter.translate(100.0, 50.0);
		boundsOverlay.paint(&painter, nullptr, nullptr);
	}
	const QRect bounds = boundsOverlay.boundingRect().translated(100.0, 50.0).toAlignedRect();
	int outside = 0;
	bool blue = false;
	for (int y = 0; y < image.height(); ++y)
		for (int x = 0; x < image.width(); ++x) {
			outside += !bounds.contains(x, y) && image.pixelColor(x, y).alpha() > 0;
			blue = blue || (image.pixelColor(x, y).blue() > image.pixelColor(x, y).red() + 80 && image.pixelColor(x, y).alpha() == 255);
		}
	// The screen-space pin stays inside the bounds of its overlay.
	QCOMPARE(outside, 0);
	QCOMPARE(blue, visual.cls == StationClass::Major);
}

void TestStationOverlay::nameHaloKeepsCrossingsOffLetters_data() {
	QTest::addColumn<int>("pixels");
	QTest::addColumn<qreal>("ratio");
	QTest::newRow("12 pixels at two device pixels") << 12 << qreal(2.0);
	QTest::newRow("80 pixels") << 80 << qreal(1.0);
}

// A station name paints over a track and a train that cross it, and its halo in the canvas colour keeps them off the
// letters: at the smallest size of a name, 12 pixels, on a screen of two device pixels to the pixel, where Qt draws
// the letters from its glyph cache, and at 80 pixels, where it fills their outlines.
void TestStationOverlay::nameHaloKeepsCrossingsOffLetters() {
	QFETCH(int, pixels);
	QFETCH(qreal, ratio);
	QGraphicsScene scene;
	scene.setBackgroundBrush(canvasColor());
	QFont font;
	font.setPixelSize(pixels);
	auto* name = new StationNameItem("HHH", font);
	scene.addItem(name);
	const QRectF box = name->boundingRect();
	const QRect area = box.adjusted(-8.0, -8.0, 8.0, 8.0).toAlignedRect();
	const auto render = [&scene, &area, deviceRatio = ratio]() {
		QImage image(area.size() * deviceRatio, QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(deviceRatio);
		QPainter painter(&image);
		scene.render(&painter, QRectF(QPointF(), area.size()), area);
		return image;
	};
	const QImage alone = render();
	auto* track = new TrackLineItem(QLineF(box.center().x(), area.top(), box.center().x(), area.bottom()));
	track->setPen(QPen(QColor(150, 150, 150), box.width() / 3.0));
	scene.addItem(track);
	auto* train = new TrainItemGroup();
	auto* body = new TrainBodyItem(QPolygonF(QRectF(area.left(), box.center().y() - 6.0, area.width(), 12.0)));
	body->setBrush(QColor(240, 200, 40));
	body->setPen(Qt::NoPen);
	train->addToGroup(body);
	scene.addItem(train);
	const QImage crossed = render();
	const auto distance = [](const QColor& a, const QColor& b) {
		return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
	};
	// A letter is white more than half, in each channel. The letters and a ring of a pixel around them look the same
	// with the track and the train under them as without, but for less than a quarter of the way to the colour of the
	// track (374 from the canvas): where the halo follows the outlines of the glyphs, the font engine can draw a letter
	// up to a pixel off them.
	const auto letter = [&alone](int x, int y) {
		const QColor colour = alone.pixelColor(x, y);
		return std::min({colour.red(), colour.green(), colour.blue()}) > 128;
	};
	int letters = 0, coveredLetters = 0, ring = 0, crossedRing = 0;
	for (int y = 1; y + 1 < alone.height(); ++y)
		for (int x = 1; x + 1 < alone.width(); ++x) {
			const bool changed = distance(alone.pixelColor(x, y), crossed.pixelColor(x, y)) > 90;
			if (letter(x, y)) {
				++letters;
				coveredLetters += changed;
				continue;
			}
			bool besideLetter = false;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					besideLetter = besideLetter || letter(x + dx, y + dy);
			ring += besideLetter;
			crossedRing += besideLetter && changed;
		}
	// Without the track and the train the halo does not show: two pixels out from the letters, where the halo lies
	// and the letters do not reach, the render has the canvas colour.
	const QColor canvas = canvasColor();
	int outerRing = 0, offCanvas = 0;
	for (int y = 2; y + 2 < alone.height(); ++y)
		for (int x = 2; x + 2 < alone.width(); ++x) {
			bool besideLetter = false;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					besideLetter = besideLetter || letter(x + dx, y + dy);
			if (besideLetter || !(letter(x - 2, y) || letter(x + 2, y) || letter(x, y - 2) || letter(x, y + 2)))
				continue;
			++outerRing;
			offCanvas += distance(alone.pixelColor(x, y), canvas) > 24;
		}
	// A station name is stacked over the tracks and the trains.
	QCOMPARE_GT(name->zValue(), track->zValue());
	QCOMPARE_GT(name->zValue(), train->zValue());
	// A track and a train under a station name leave its letters white.
	QCOMPARE_GT(letters, 100);
	QCOMPARE(coveredLetters, 0);
	// The halo of a station name keeps a track and a train off its letters.
	QCOMPARE_GT(ring, 100);
	QCOMPARE(crossedRing, 0);
	// The halo of a station name has the colour of the canvas.
	QCOMPARE_GT(outerRing, 50);
	QCOMPARE(offCanvas, 0);
	// And the track and the train still show beside the name.
	QCOMPARE_GT(distance(crossed.pixelColor(crossed.width() / 2, 0), canvas), 90);
	QCOMPARE_GT(distance(crossed.pixelColor(0, static_cast<int>((box.center().y() - area.top()) * ratio)), canvas), 90);
}

QTEST_MAIN(TestStationOverlay)
#include "test_stationoverlay.moc"
