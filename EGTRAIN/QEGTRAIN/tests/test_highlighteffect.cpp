#include "graphics/items/HighlightEffect.h"

#include "graphics/NetworkScene.h"
#include "graphics/VisualPolish.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/NodeItem.h"
#include "graphics/items/PassengerItem.h"
#include "graphics/items/SelectionCueItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"

#include <QApplication>
#include <QBrush>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPointer>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

static const QColor kCanvas(0x10, 0x1a, 0x22);

// The scene emits its change signal some events after an item changes.
static void settle() {
	for (int pass = 0; pass < 3; ++pass)
		QApplication::processEvents();
}

static int cueCount(const QGraphicsScene& scene) {
	int count = 0;
	for (const QGraphicsItem* item : scene.items())
		if (item->type() == SelectionCueItem::Type)
			++count;
	return count;
}

static QImage renderScene(QGraphicsScene& scene, const QRectF& source, qreal scale) {
	QImage image(QSize(qRound(source.width() * scale), qRound(source.height() * scale)), QImage::Format_ARGB32_Premultiplied);
	image.fill(kCanvas);
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing, true);
	scene.render(&painter, QRectF(QPointF(), QSizeF(image.size())), source);
	return image;
}

static bool isCueColor(const QColor& color) {
	return std::abs(color.red() - kSelectionCueColor.red()) <= 12 && std::abs(color.green() - kSelectionCueColor.green()) <= 12
		&& std::abs(color.blue() - kSelectionCueColor.blue()) <= 12;
}

// Number and bounds of the pixels in the cue colour.
static int cuePixels(const QImage& image, QRect* bounds = nullptr) {
	int count = 0;
	QRect box;
	for (int y = 0; y < image.height(); ++y) {
		for (int x = 0; x < image.width(); ++x) {
			if (!isCueColor(image.pixelColor(x, y)))
				continue;
			++count;
			box = box.isNull() ? QRect(x, y, 1, 1) : box.united(QRect(x, y, 1, 1));
		}
	}
	if (bounds)
		*bounds = box;
	return count;
}

static double luminance(const QColor& color) {
	const auto channel = [](double value) {
		value /= 255.0;
		return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
	};
	return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green()) + 0.0722 * channel(color.blue());
}

static double contrast(const QColor& first, const QColor& second) {
	const double a = luminance(first);
	const double b = luminance(second);
	return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

static double distance(const QColor& first, const QColor& second) {
	const double r = first.red() - second.red();
	const double g = first.green() - second.green();
	const double b = first.blue() - second.blue();
	return std::sqrt(r * r + g * g + b * b);
}

struct Kind {
	const char* name;
	bool line;
	std::function<QGraphicsItem*()> make;
};

static std::vector<Kind> kinds() {
	QPixmap glyph(24, 24);
	glyph.fill(Qt::white);
	return {
		{"node", false, []() {
			 auto* item = new NodeItem(QRectF(-4.5, -4.5, 9.0, 9.0));
			 item->setBrush(Qt::lightGray);
			 return item;
		 }},
		{"signal", false, []() {
			 auto* item = new SignalItem(QRectF(-5.0, -5.0, 10.0, 10.0));
			 item->setAspectCode(180);
			 return item;
		 }},
		{"station node", false, []() {
			 auto* item = new StationNodeItem(QRectF(-10.0, -10.0, 20.0, 20.0));
			 item->setBrush(Qt::gray);
			 return item;
		 }},
		{"connection", true, []() {
			 auto* item = new ConnectionItem(QLineF(-1000.0, 0.0, 1000.0, 0.0));
			 QPen pen(Qt::white, 2);
			 pen.setCosmetic(true);
			 item->setPen(pen);
			 return item;
		 }},
		{"arc", true, []() {
			 auto* item = new TrackLineItem(QLineF(-1000.0, 0.0, 1000.0, 400.0));
			 QPen pen(QColor(80, 80, 80), 3);
			 pen.setCosmetic(true);
			 item->setPen(pen);
			 return item;
		 }},
		{"train", false, []() {
			 auto* group = new TrainItemGroup;
			 for (const QRectF& rect : {QRectF(-40.0, -4.0, 36.0, 8.0), QRectF(4.0, -4.0, 36.0, 8.0)}) {
				 auto* body = new TrainBodyItem(QPolygonF(rect), group);
				 body->setBrush(defaultTrainFill());
			 }
			 return group;
		 }},
		{"passenger", false, [glyph]() {
			 auto* item = new PassengerItem(glyph);
			 item->setPos(-12.0, -12.0);
			 return item;
		 }},
	};
}

class TestHighlightEffect : public QObject {
	Q_OBJECT

private slots:
	void drawsSourceUnchanged();
	void cueLifetime();
	void deletingSceneDeletesEffect();
	void itemThatLeftSceneIsNotFound();
	void cueFollowsTarget();
	void ringIsStackedBelowTarget();
	void stationArtworkIsPartOfStation();
	void previewSquareTakesMarkOfStation();
	void ringAroundEveryKind_data();
	void ringAroundEveryKind();
	void signalHeadKeepsColours();
	void connectionKeepsLine();
	void largeItemRing();
	void cueColour();
	void trainGroupFreesBodyList();
};

// The effect draws its source unchanged.
void TestHighlightEffect::drawsSourceUnchanged() {
	QGraphicsScene scene;
	QGraphicsView view(&scene);
	auto* item = new QGraphicsRectItem(QRectF(0.0, 0.0, 10.0, 10.0));
	const QPen sourcePen(Qt::darkGray);
	const QBrush sourceBrush(Qt::yellow);
	item->setPen(sourcePen);
	item->setBrush(sourceBrush);
	scene.addItem(item);
	item->setGraphicsEffect(new HighlightEffect(Qt::blue, 1.0));
	// The highlight effect preserves the source pen and brush.
	QCOMPARE(item->pen(), sourcePen);
	QCOMPARE(item->brush(), sourceBrush);
	// Installing the effect adds exactly one cue.
	QCOMPARE(cueCount(scene), 1);
}

// Lifetime of the cue in every order of destruction.
void TestHighlightEffect::cueLifetime() {
	QGraphicsScene scene;
	QGraphicsView view(&scene);
	auto* first = new QGraphicsRectItem(QRectF(0.0, 0.0, 10.0, 10.0));
	auto* second = new QGraphicsRectItem(QRectF(100.0, 0.0, 10.0, 10.0));
	scene.addItem(first);
	scene.addItem(second);
	auto* effect = new HighlightEffect(Qt::blue, 1.0);
	first->setGraphicsEffect(effect);
	// One cue per installed effect.
	QCOMPARE(cueCount(scene), 1);
	QVERIFY(effect->cue() != nullptr);
	second->setGraphicsEffect(effect);
	// Moving the effect leaves one cue.
	QCOMPARE(cueCount(scene), 1);
	QVERIFY(first->graphicsEffect() == nullptr);
	if (effect->cue() == nullptr)
		QFAIL("the moved cue surrounds the new target");
	QCOMPARE(effect->cue()->targetRect().center(), QPointF(105.0, 5.0));
	delete effect;
	// Deleting the effect removes the cue.
	QCOMPARE(cueCount(scene), 0);
	QVERIFY(second->graphicsEffect() == nullptr);

	effect = new HighlightEffect(Qt::blue, 1.0);
	first->setGraphicsEffect(effect);
	first->setGraphicsEffect(nullptr);
	// Removing the effect from its item removes the cue.
	QCOMPARE(cueCount(scene), 0);

	QPointer<HighlightEffect> guarded = new HighlightEffect(Qt::blue, 1.0);
	second->setGraphicsEffect(guarded);
	delete second;
	// Deleting the target deletes the effect and the cue.
	QVERIFY(guarded.isNull());
	QCOMPARE(cueCount(scene), 0);

	// The scene deletes the cue before the target.
	auto* third = new QGraphicsRectItem(QRectF(0.0, 0.0, 10.0, 10.0));
	scene.addItem(third);
	effect = new HighlightEffect(Qt::blue, 1.0);
	third->setGraphicsEffect(effect);
	delete effect->cue();
	// A deleted cue is forgotten by the effect.
	QCOMPARE(cueCount(scene), 0);
	QVERIFY(effect->cue() == nullptr);
	third->setGraphicsEffect(nullptr);

	// Clearing the scene with a selected target.
	auto* fourth = new QGraphicsRectItem(QRectF(0.0, 0.0, 10.0, 10.0));
	scene.addItem(fourth);
	fourth->setGraphicsEffect(new HighlightEffect(Qt::blue, 1.0));
	scene.clear();
	QVERIFY2(scene.items().isEmpty(), "clearing the scene with a selected target does not crash");
}

void TestHighlightEffect::deletingSceneDeletesEffect() {
	auto* scene = new QGraphicsScene;
	QGraphicsView view(scene);
	auto* item = new QGraphicsRectItem(QRectF(0.0, 0.0, 10.0, 10.0));
	scene->addItem(item);
	QPointer<HighlightEffect> effect = new HighlightEffect(Qt::blue, 1.0);
	item->setGraphicsEffect(effect);
	delete scene;
	QVERIFY2(effect.isNull(), "deleting the scene deletes the effect with the selected target");
}

// A selected item that leaves the scene after the zoom changed is gone from the scene index. The index of depth 1
// splits the scene at x = 500 and the item lies left of it; at a zoom of 0.001 one pixel of the view covers the scene.
void TestHighlightEffect::itemThatLeftSceneIsNotFound() {
	QGraphicsScene scene(QRectF(0.0, 0.0, 1000.0, 1000.0));
	scene.setBspTreeDepth(1);
	QGraphicsView view(&scene);
	view.setTransform(QTransform::fromScale(0.001, 0.001));
	auto* item = new QGraphicsRectItem(QRectF(490.0, 10.0, 9.0, 9.0));
	scene.addItem(item);
	item->setGraphicsEffect(new HighlightEffect(Qt::blue, 1.0));
	scene.items(scene.sceneRect()); // indexes the item
	view.setTransform(QTransform());
	scene.removeItem(item);
	const bool found = scene.items(scene.sceneRect()).contains(item);
	delete item;
	QVERIFY2(!found, "a selected item that left the scene is not found in it");
}

// The cue follows its target.
void TestHighlightEffect::cueFollowsTarget() {
	QGraphicsScene scene;
	QGraphicsView view(&scene);
	view.resize(300, 200);
	view.scale(0.5, 0.5);
	auto* item = new QGraphicsRectItem(QRectF(-50.0, -50.0, 100.0, 100.0));
	scene.addItem(item);
	auto* effect = new HighlightEffect(Qt::blue, 1.0);
	item->setGraphicsEffect(effect);
	SelectionCueItem* cue = effect->cue();
	if (cue == nullptr)
		QFAIL("the cue starts with the scale of the view");
	// The cue starts with the scale of the view.
	QCOMPARE_LT(std::abs(cue->viewScale() - 0.5), 1e-9);
	effect->setViewScale(2.0);
	// The view scale reaches the cue.
	QCOMPARE_LT(std::abs(cue->viewScale() - 2.0), 1e-9);
	effect->setViewScale(0.0);
	// A scale that is not positive is ignored.
	QCOMPARE_LT(std::abs(cue->viewScale() - 2.0), 1e-9);
	settle();
	item->setPos(500.0, 300.0);
	settle();
	// Moving the target moves the cue.
	QCOMPARE(cue->targetRect().center(), QPointF(500.0, 300.0));
	item->hide();
	settle();
	QVERIFY2(!cue->isVisible(), "hiding the target hides the cue");
	item->show();
	settle();
	QVERIFY2(cue->isVisible(), "showing the target shows the cue");
}

// The ring is stacked just below the target.
void TestHighlightEffect::ringIsStackedBelowTarget() {
	QGraphicsScene scene;
	QGraphicsView view(&scene);
	auto* target = new QGraphicsRectItem(QRectF(-10.0, -10.0, 20.0, 20.0));
	target->setBrush(Qt::red);
	target->setPen(Qt::NoPen);
	target->setZValue(2.0);
	auto* under = new QGraphicsRectItem(QRectF(-18.0, -5.0, 6.0, 10.0));
	under->setBrush(Qt::blue);
	under->setPen(Qt::NoPen);
	under->setZValue(1.0);
	auto* over = new QGraphicsRectItem(QRectF(12.0, -5.0, 6.0, 10.0));
	over->setBrush(Qt::green);
	over->setPen(Qt::NoPen);
	over->setZValue(3.0);
	scene.addItem(target);
	scene.addItem(under);
	scene.addItem(over);
	auto* effect = new HighlightEffect(Qt::blue, 1.0);
	target->setGraphicsEffect(effect);
	effect->setViewScale(1.0);
	if (effect->cue() == nullptr)
		QFAIL("the cue is stacked just below its target");
	// The cue is stacked just below its target.
	QCOMPARE_LT(effect->cue()->zValue(), target->zValue());
	QCOMPARE_GT(effect->cue()->zValue(), under->zValue());
	const QImage image = renderScene(scene, QRectF(-30.0, -30.0, 60.0, 60.0), 1.0);
	QVERIFY2(isCueColor(image.pixelColor(17, 30)), "the ring covers an item that lies under the target");
	// An item over the target covers the ring.
	QCOMPARE(image.pixelColor(43, 30), QColor(Qt::green));
	// The ring does not cover the target.
	QCOMPARE(image.pixelColor(30, 30), QColor(Qt::red));
}

// The artwork of a station is part of the station.
void TestHighlightEffect::stationArtworkIsPartOfStation() {
	QGraphicsScene scene;
	QGraphicsView view(&scene);
	QPixmap picture(24, 24);
	picture.fill(Qt::white);
	auto* station = new StationNodeItem(QRectF(-10.0, -10.0, 20.0, 20.0));
	scene.addItem(station);
	auto* artwork = new QGraphicsPixmapItem(picture, station);
	artwork->setPos(30.0, -40.0);
	auto* effect = new HighlightEffect(Qt::blue, 1.0);
	station->setGraphicsEffect(effect);
	const QRectF expected = QRectF(-10.0, -10.0, 20.0, 20.0).united(QRectF(30.0, -40.0, 24.0, 24.0));
	if (effect->cue() == nullptr)
		QFAIL("a station node is surrounded together with its pictogram");
	// A station node is surrounded together with its pictogram.
	QCOMPARE(effect->cue()->targetRect(), expected);
	station->setGraphicsEffect(nullptr);
	artwork->setGraphicsEffect(effect = new HighlightEffect(Qt::blue, 1.0));
	if (effect->cue() == nullptr)
		QFAIL("the pictogram of a station in the preview stands for the station");
	// The pictogram of a station in the preview stands for the station.
	QCOMPARE(effect->cue()->targetRect(), expected);
}

// A station square of the preview that does not hold the mark is surrounded with the mark of its station when it is
// at the place of the mark; a square beyond the place rule, more than 32 frames from the anchor, is surrounded alone.
void TestHighlightEffect::previewSquareTakesMarkOfStation() {
	QGraphicsScene scene;
	QGraphicsView view(&scene);
	auto* anchor = new StationNodeItem(QRectF(-10.0, -10.0, 20.0, 20.0));
	auto* other = new StationNodeItem(QRectF(-10.0, 30.0, 20.0, 20.0));
	auto* beyond = new StationNodeItem(QRectF(-10.0, 990.0, 20.0, 20.0));
	for (StationNodeItem* square : {anchor, other, beyond}) {
		square->setData(PreviewGraphics::StationId, QStringLiteral("north"));
		scene.addItem(square);
	}
	auto* mark = new StationMarkItem(StationClass::Station, 24, anchor);
	mark->setPos(30.0, -40.0);
	auto* overlay = new StationOverlayItem(QStringLiteral("North"), QPointF(), classifyStation(true));
	overlay->setData(PreviewGraphics::Id, QStringLiteral("north"));
	scene.addItem(overlay);
	overlay->attachArtwork(mark, nullptr, 1);
	overlay->setNodeCentres({QPointF(0.0, 0.0), QPointF(0.0, 40.0), QPointF(0.0, 1000.0)});
	auto* effect = new HighlightEffect(Qt::blue, 1.0);
	other->setGraphicsEffect(effect);
	if (effect->cue() == nullptr)
		QFAIL("a station square of the preview is surrounded together with the mark of its station");
	// A station square of the preview is surrounded together with the mark of its station.
	QCOMPARE(effect->cue()->targetRect(), QRectF(-10.0, 30.0, 20.0, 20.0).united(mark->sceneBoundingRect()));
	beyond->setGraphicsEffect(effect = new HighlightEffect(Qt::blue, 1.0));
	if (effect->cue() == nullptr)
		QFAIL("a station square of the preview beyond the place of its mark is surrounded alone");
	// A station square of the preview beyond the place of its mark is surrounded alone.
	QCOMPARE(effect->cue()->targetRect(), QRectF(-10.0, 990.0, 20.0, 20.0));
}

void TestHighlightEffect::ringAroundEveryKind_data() {
	QTest::addColumn<int>("kindIndex");
	const std::vector<Kind> all = kinds();
	for (std::size_t index = 0; index < all.size(); ++index)
		QTest::newRow(all[index].name) << static_cast<int>(index);
}

// Every kind of item: cue-coloured pixels around it at an overview scale, none without the effect.
void TestHighlightEffect::ringAroundEveryKind() {
	QFETCH(int, kindIndex);
	const Kind kind = kinds()[static_cast<std::size_t>(kindIndex)];
	const QRectF source(-15000.0, -10000.0, 30000.0, 20000.0);
	const qreal scale = 0.01;
	QGraphicsScene scene;
	QGraphicsView view(&scene);
	QGraphicsItem* item = kind.make();
	scene.addItem(item);
	const int without = cuePixels(renderScene(scene, source, scale));
	auto* effect = new HighlightEffect(Qt::blue, 1.0);
	item->setGraphicsEffect(effect);
	effect->setViewScale(scale);
	QRect bounds;
	const int with = cuePixels(renderScene(scene, source, scale), &bounds);
	// No cue pixels without the effect.
	QCOMPARE(without, 0);
	// Cue pixels with the effect.
	QCOMPARE_GT(with, 24);
	// The ring is at least 16 pixels.
	QCOMPARE_GE(bounds.width(), 16);
	QVERIFY(kind.line || bounds.height() >= 16);
	// The ring stays near the item.
	QCOMPARE_LT(bounds.width(), 60);
	QCOMPARE_LT(bounds.height(), 60);
	item->setGraphicsEffect(nullptr);
	// No cue pixels after the effect is removed.
	QCOMPARE(cuePixels(renderScene(scene, source, scale)), 0);
}

// The item keeps its colours, and a large item gets a ring of its size plus the padding.
void TestHighlightEffect::signalHeadKeepsColours() {
	const QRectF source(-50.0, -50.0, 100.0, 100.0);
	QGraphicsScene scene;
	QGraphicsView view(&scene);
	auto* head = new SignalItem(QRectF(-10.0, -10.0, 20.0, 20.0));
	head->setAspectCode(180);
	scene.addItem(head);
	const QImage plain = renderScene(scene, source, 2.0);
	auto* effect = new HighlightEffect(Qt::blue, 1.0);
	head->setGraphicsEffect(effect);
	effect->setViewScale(2.0);
	const QImage marked = renderScene(scene, source, 2.0);
	const QColor lamp = classifySignalAspect(180).lamp;
	// The centre of a signal head keeps its aspect colour.
	QCOMPARE(marked.pixelColor(100, 100), lamp);
	bool same = true;
	for (int y = 86; y <= 114; ++y)
		for (int x = 86; x <= 114; ++x)
			same &= marked.pixelColor(x, y) == plain.pixelColor(x, y);
	QVERIFY2(same, "the face of a signal head is not changed by the cue");
	// The cue is drawn around a signal head.
	QCOMPARE_GT(cuePixels(marked), 24);
}

void TestHighlightEffect::connectionKeepsLine() {
	const QRectF source(-150.0, -150.0, 300.0, 300.0);
	QGraphicsScene scene;
	QGraphicsView view(&scene);
	ConnectionItem* connection = static_cast<ConnectionItem*>(kinds()[3].make());
	scene.addItem(connection);
	const QImage plain = renderScene(scene, source, 1.0);
	auto* effect = new HighlightEffect(Qt::blue, 1.0);
	connection->setGraphicsEffect(effect);
	effect->setViewScale(1.0);
	const QImage marked = renderScene(scene, source, 1.0);
	// A connection keeps its white line.
	QCOMPARE(marked.pixelColor(150, 150), QColor(Qt::white));
	QCOMPARE(marked.pixelColor(150, 149), QColor(Qt::white));
	bool same = true;
	for (int x = 100; x <= 200; ++x)
		for (int y = 147; y <= 152; ++y)
			same &= marked.pixelColor(x, y) == plain.pixelColor(x, y);
	QVERIFY2(same, "the cue does not touch the line of a connection");
	QRect bounds;
	cuePixels(marked, &bounds);
	// A line has a halo of about 6 pixels on both sides.
	QCOMPARE_GE(bounds.height(), 11);
	QCOMPARE_LE(bounds.height(), 16);
}

void TestHighlightEffect::largeItemRing() {
	const QRectF source(-150.0, -150.0, 300.0, 300.0);
	QGraphicsScene scene;
	QGraphicsView view(&scene);
	auto* station = new StationNodeItem(QRectF(-100.0, -100.0, 200.0, 200.0));
	scene.addItem(station);
	auto* effect = new HighlightEffect(Qt::blue, 1.0);
	station->setGraphicsEffect(effect);
	effect->setViewScale(1.0);
	QRect bounds;
	cuePixels(renderScene(scene, source, 1.0), &bounds);
	// The ring of a large item is its size plus the padding.
	QCOMPARE_GE(bounds.width(), 204);
	QCOMPARE_LE(bounds.width(), 212);
	QCOMPARE_GE(bounds.height(), 204);
	QCOMPARE_LE(bounds.height(), 212);
}

// The colour.
void TestHighlightEffect::cueColour() {
	const QColor black(Qt::black);
	// The cue colour has a contrast of 3 to 1 against black.
	QCOMPARE_GE(contrast(kSelectionCueColor, black), 3.0);
	// The cue colour has a contrast of 3 to 1 against the canvas.
	QCOMPARE_GE(contrast(kSelectionCueColor, kCanvas), 3.0);
	std::vector<QColor> used = {QColor(Qt::white), QColor(Qt::red), QColor(Qt::yellow), QColor(Qt::green), QColor(255, 110, 90),
		defaultTrainFill(), defaultTrainOutline()};
	for (const double speed : {0.0, 20.0, 40.0, 60.0})
		used.push_back(classifyTrackSpeed(speed).color);
	used.push_back(freeTrackVisual().color);
	for (const TrackOperationalState state : {TrackOperationalState::Prepared, TrackOperationalState::Occupied, TrackOperationalState::Blocked})
		used.push_back(classifyTrackState(state).color);
	for (const int code : {0, 75, 180, 270, -1})
		used.push_back(classifySignalAspect(code).lamp);
	for (const bool platform : {false, true}) {
		used.push_back(classifyStation(platform, 0).fill);
		used.push_back(classifyStation(platform, 0).outline);
	}
	for (const char* type : {"", "IC", "ICE", "Sprinter", "Freight", "Cargo"}) {
		const TrainVisual visual = classifyTrainType(type, type);
		used.push_back(visual.fill);
		used.push_back(visual.outline);
	}
	double nearest = 1e9;
	for (const QColor& color : used)
		nearest = std::min(nearest, distance(kSelectionCueColor, color));
	qInfo("nearest colour distance %g", nearest);
	// The cue colour differs from every colour of the canvas.
	QCOMPARE_GE(nearest, 70.0);
}

// A train group frees the list of its body items. A copy of the list shares its data with the group's list,
// so the copy is detached only after the group has deleted its list. Nothing touches the group's list after the copy.
void TestHighlightEffect::trainGroupFreesBodyList() {
	const auto makeGroup = []() {
		auto* made = new TrainItemGroup;
		made->trainPolygonItemList = new QList<TrainBodyItem*>();
		for (const QRectF& rect : {QRectF(-40.0, -4.0, 36.0, 8.0), QRectF(4.0, -4.0, 36.0, 8.0)})
			made->trainPolygonItemList->push_back(new TrainBodyItem(QPolygonF(rect), made));
		return made;
	};

	TrainItemGroup* group = makeGroup();
	const QList<TrainBodyItem*> probe = *group->trainPolygonItemList;
	const bool shared = !probe.isDetached();
	delete group;
	QVERIFY2(shared, "the probe shares the body list while the group is alive");
	QVERIFY2(probe.isDetached(), "deleting a train group frees its body list");

	QGraphicsScene scene;
	TrainItemGroup* sceneGroup = makeGroup();
	scene.addItem(sceneGroup);
	const QList<TrainBodyItem*> sceneProbe = *sceneGroup->trainPolygonItemList;
	QVERIFY2(!sceneProbe.isDetached(), "the probe shares the body list of a group in a scene");
	scene.clear();
	QVERIFY2(sceneProbe.isDetached(), "clearing the scene frees the body list of its train groups");
}

QTEST_MAIN(TestHighlightEffect)
#include "test_highlighteffect.moc"
