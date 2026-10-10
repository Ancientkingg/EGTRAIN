#include "graphics/items/HighlightEffect.h"

#include "graphics/VisualPolish.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/NodeItem.h"
#include "graphics/items/PassengerItem.h"
#include "graphics/items/SelectionCueItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationNodeItem.h"
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

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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

int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	bool ok = true;

	// The effect draws its source unchanged.
	{
		QGraphicsScene scene;
		QGraphicsView view(&scene);
		auto* item = new QGraphicsRectItem(QRectF(0.0, 0.0, 10.0, 10.0));
		const QPen sourcePen(Qt::darkGray);
		const QBrush sourceBrush(Qt::yellow);
		item->setPen(sourcePen);
		item->setBrush(sourceBrush);
		scene.addItem(item);
		item->setGraphicsEffect(new HighlightEffect(Qt::blue, 1.0));
		ok &= expect(item->pen() == sourcePen && item->brush() == sourceBrush, "highlight effect preserves source pen and brush");
		ok &= expect(cueCount(scene) == 1, "installing the effect adds exactly one cue");
	}

	// Lifetime of the cue in every order of destruction.
	{
		QGraphicsScene scene;
		QGraphicsView view(&scene);
		auto* first = new QGraphicsRectItem(QRectF(0.0, 0.0, 10.0, 10.0));
		auto* second = new QGraphicsRectItem(QRectF(100.0, 0.0, 10.0, 10.0));
		scene.addItem(first);
		scene.addItem(second);
		auto* effect = new HighlightEffect(Qt::blue, 1.0);
		first->setGraphicsEffect(effect);
		ok &= expect(cueCount(scene) == 1 && effect->cue(), "one cue per installed effect");
		second->setGraphicsEffect(effect);
		ok &= expect(cueCount(scene) == 1 && first->graphicsEffect() == nullptr, "moving the effect leaves one cue");
		ok &= expect(effect->cue() && effect->cue()->targetRect().center() == QPointF(105.0, 5.0), "the moved cue surrounds the new target");
		delete effect;
		ok &= expect(cueCount(scene) == 0 && second->graphicsEffect() == nullptr, "deleting the effect removes the cue");

		effect = new HighlightEffect(Qt::blue, 1.0);
		first->setGraphicsEffect(effect);
		first->setGraphicsEffect(nullptr);
		ok &= expect(cueCount(scene) == 0, "removing the effect from its item removes the cue");

		QPointer<HighlightEffect> guarded = new HighlightEffect(Qt::blue, 1.0);
		second->setGraphicsEffect(guarded);
		delete second;
		ok &= expect(guarded.isNull() && cueCount(scene) == 0, "deleting the target deletes the effect and the cue");

		// The scene deletes the cue before the target.
		auto* third = new QGraphicsRectItem(QRectF(0.0, 0.0, 10.0, 10.0));
		scene.addItem(third);
		effect = new HighlightEffect(Qt::blue, 1.0);
		third->setGraphicsEffect(effect);
		delete effect->cue();
		ok &= expect(cueCount(scene) == 0 && effect->cue() == nullptr, "a deleted cue is forgotten by the effect");
		third->setGraphicsEffect(nullptr);

		// Clearing the scene with a selected target.
		auto* fourth = new QGraphicsRectItem(QRectF(0.0, 0.0, 10.0, 10.0));
		scene.addItem(fourth);
		fourth->setGraphicsEffect(new HighlightEffect(Qt::blue, 1.0));
		scene.clear();
		ok &= expect(scene.items().isEmpty(), "clearing the scene with a selected target does not crash");
	}
	{
		auto* scene = new QGraphicsScene;
		QGraphicsView view(scene);
		auto* item = new QGraphicsRectItem(QRectF(0.0, 0.0, 10.0, 10.0));
		scene->addItem(item);
		QPointer<HighlightEffect> effect = new HighlightEffect(Qt::blue, 1.0);
		item->setGraphicsEffect(effect);
		delete scene;
		ok &= expect(effect.isNull(), "deleting the scene deletes the effect with the selected target");
	}

	// The cue follows its target.
	{
		QGraphicsScene scene;
		QGraphicsView view(&scene);
		view.resize(300, 200);
		view.scale(0.5, 0.5);
		auto* item = new QGraphicsRectItem(QRectF(-50.0, -50.0, 100.0, 100.0));
		scene.addItem(item);
		auto* effect = new HighlightEffect(Qt::blue, 1.0);
		item->setGraphicsEffect(effect);
		SelectionCueItem* cue = effect->cue();
		ok &= expect(cue && std::abs(cue->viewScale() - 0.5) < 1e-9, "the cue starts with the scale of the view");
		effect->setViewScale(2.0);
		ok &= expect(std::abs(cue->viewScale() - 2.0) < 1e-9, "the view scale reaches the cue");
		effect->setViewScale(0.0);
		ok &= expect(std::abs(cue->viewScale() - 2.0) < 1e-9, "a scale that is not positive is ignored");
		settle();
		item->setPos(500.0, 300.0);
		settle();
		ok &= expect(cue->targetRect().center() == QPointF(500.0, 300.0), "moving the target moves the cue");
		item->hide();
		settle();
		ok &= expect(!cue->isVisible(), "hiding the target hides the cue");
		item->show();
		settle();
		ok &= expect(cue->isVisible(), "showing the target shows the cue");
	}

	// The ring is stacked just below the target.
	{
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
		ok &= expect(effect->cue() && effect->cue()->zValue() < target->zValue() && effect->cue()->zValue() > under->zValue(),
			"the cue is stacked just below its target");
		const QImage image = renderScene(scene, QRectF(-30.0, -30.0, 60.0, 60.0), 1.0);
		ok &= expect(isCueColor(image.pixelColor(17, 30)), "the ring covers an item that lies under the target");
		ok &= expect(image.pixelColor(43, 30) == QColor(Qt::green), "an item over the target covers the ring");
		ok &= expect(image.pixelColor(30, 30) == QColor(Qt::red), "the ring does not cover the target");
	}

	// The artwork of a station is part of the station.
	{
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
		ok &= expect(effect->cue() && effect->cue()->targetRect() == expected, "a station node is surrounded together with its pictogram");
		station->setGraphicsEffect(nullptr);
		artwork->setGraphicsEffect(effect = new HighlightEffect(Qt::blue, 1.0));
		ok &= expect(effect->cue() && effect->cue()->targetRect() == expected, "the pictogram of a station in the preview stands for the station");
	}
	// Every kind of item: cue-coloured pixels around it at an overview scale, none without the effect.
	for (const Kind& kind : kinds()) {
		const QRectF source(-15000.0, -10000.0, 30000.0, 20000.0);
		const qreal scale = 0.01;
		QGraphicsScene scene;
		QGraphicsView view(&scene);
		QGraphicsItem* item = kind.make();
		scene.addItem(item);
		const std::string name = kind.name;
		const int without = cuePixels(renderScene(scene, source, scale));
		auto* effect = new HighlightEffect(Qt::blue, 1.0);
		item->setGraphicsEffect(effect);
		effect->setViewScale(scale);
		QRect bounds;
		const int with = cuePixels(renderScene(scene, source, scale), &bounds);
		ok &= expect(without == 0, ("no cue pixels without the effect: " + name).c_str());
		ok &= expect(with > 24, ("cue pixels with the effect: " + name).c_str());
		ok &= expect(bounds.width() >= 16 && (kind.line || bounds.height() >= 16), ("the ring is at least 16 pixels: " + name).c_str());
		ok &= expect(bounds.width() < 60 && bounds.height() < 60, ("the ring stays near the item: " + name).c_str());
		item->setGraphicsEffect(nullptr);
		ok &= expect(cuePixels(renderScene(scene, source, scale)) == 0, ("no cue pixels after the effect is removed: " + name).c_str());
	}

	// The item keeps its colours, and a large item gets a ring of its size plus the padding.
	{
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
		ok &= expect(marked.pixelColor(100, 100) == lamp, "the centre of a signal head keeps its aspect colour");
		bool same = true;
		for (int y = 86; y <= 114; ++y)
			for (int x = 86; x <= 114; ++x)
				same &= marked.pixelColor(x, y) == plain.pixelColor(x, y);
		ok &= expect(same, "the face of a signal head is not changed by the cue");
		ok &= expect(cuePixels(marked) > 24, "the cue is drawn around a signal head");
	}
	{
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
		ok &= expect(marked.pixelColor(150, 150) == QColor(Qt::white) && marked.pixelColor(150, 149) == QColor(Qt::white),
			"a connection keeps its white line");
		bool same = true;
		for (int x = 100; x <= 200; ++x)
			for (int y = 147; y <= 152; ++y)
				same &= marked.pixelColor(x, y) == plain.pixelColor(x, y);
		ok &= expect(same, "the cue does not touch the line of a connection");
		QRect bounds;
		cuePixels(marked, &bounds);
		ok &= expect(bounds.height() >= 11 && bounds.height() <= 16, "a line has a halo of about 6 pixels on both sides");
	}
	{
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
		ok &= expect(bounds.width() >= 204 && bounds.width() <= 212 && bounds.height() >= 204 && bounds.height() <= 212,
			"the ring of a large item is its size plus the padding");
	}

	// The colour.
	{
		const QColor black(Qt::black);
		ok &= expect(contrast(kSelectionCueColor, black) >= 3.0, "the cue colour has a contrast of 3 to 1 against black");
		ok &= expect(contrast(kSelectionCueColor, kCanvas) >= 3.0, "the cue colour has a contrast of 3 to 1 against the canvas");
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
		ok &= expect(nearest >= 70.0, "the cue colour differs from every colour of the canvas");
		std::cout << "nearest colour distance " << nearest << "\n";
	}

	// A train group frees the list of its body items. A copy of the list shares its data with the group's list,
	// so the copy is detached only after the group has deleted its list. Nothing touches the group's list after the copy.
	{
		const auto makeGroup = []() {
			auto* made = new TrainItemGroup;
			made->trainPolygonItemList = new QList<TrainBodyItem*>();
			for (const QRectF& rect : {QRectF(-40.0, -4.0, 36.0, 8.0), QRectF(4.0, -4.0, 36.0, 8.0)})
				made->trainPolygonItemList->push_back(new TrainBodyItem(QPolygonF(rect), made));
			return made;
		};

		TrainItemGroup* group = makeGroup();
		const QList<TrainBodyItem*> probe = *group->trainPolygonItemList;
		ok &= expect(!probe.isDetached(), "the probe shares the body list while the group is alive");
		delete group;
		ok &= expect(probe.isDetached(), "deleting a train group frees its body list");

		QGraphicsScene scene;
		TrainItemGroup* sceneGroup = makeGroup();
		scene.addItem(sceneGroup);
		const QList<TrainBodyItem*> sceneProbe = *sceneGroup->trainPolygonItemList;
		ok &= expect(!sceneProbe.isDetached(), "the probe shares the body list of a group in a scene");
		scene.clear();
		ok &= expect(sceneProbe.isDetached(), "clearing the scene frees the body list of its train groups");
	}

	if (!ok)
		return 1;

	std::cout << "all HighlightEffect tests passed\n";
	return 0;
}
