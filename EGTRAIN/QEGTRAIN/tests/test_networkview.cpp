#include "graphics/NetworkView.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/VirtualArcItem.h"

#include <QApplication>
#include <QGraphicsRectItem>
#include <QImage>
#include <QNativeGestureEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <cmath>
#include <iostream>
#include <string>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static bool near(qreal left, qreal right, qreal epsilon = 1e-6) {
	return std::abs(left - right) <= epsilon;
}

static bool endpointInside(const NetworkView& view, const QPointF& point) {
	const QPoint mapped = view.mapFromScene(point);
	const QRect viewport = view.viewport()->rect();
	return mapped.x() >= 24 && mapped.y() >= 24
		&& mapped.x() <= viewport.width() - 24
		&& mapped.y() <= viewport.height() - 24;
}

static const QColor kCanvas(0x10, 0x1a, 0x22);
static const QColor kGrid(0x18, 0x28, 0x32);

// Columns (or rows) of the image that are mostly grid-coloured.
static QList<int> gridLines(const QImage& image, bool vertical) {
	QList<int> lines;
	const int outer = vertical ? image.width() : image.height();
	const int inner = vertical ? image.height() : image.width();
	for (int i = 0; i < outer; ++i) {
		int hits = 0;
		for (int j = 0; j < inner; ++j)
			hits += (vertical ? image.pixelColor(i, j) : image.pixelColor(j, i)) == kGrid ? 1 : 0;
		if (hits * 10 > inner * 9)
			lines.append(i);
	}
	return lines;
}

static bool spacingWithinBand(const QList<int>& lines) {
	if (lines.size() < 3)
		return false;
	for (int i = 1; i < lines.size(); ++i) {
		const int gap = lines[i] - lines[i - 1];
		if (gap < 23 || gap > 97)
			return false;
	}
	const qreal mean = qreal(lines.last() - lines.first()) / (lines.size() - 1);
	return mean >= 24.0 - 0.5 && mean <= 96.0 + 0.5;
}

static QImage renderBackground(NetworkView& view) {
	QApplication::processEvents();
	return view.viewport()->grab().toImage().convertToFormat(QImage::Format_RGB32);
}

static bool checkBackgroundGrid(NetworkView& view, const char* zoom) {
	bool ok = true;
	const std::string label = zoom;
	const QImage image = renderBackground(view);
	const QList<int> columns = gridLines(image, true);
	const QList<int> rows = gridLines(image, false);
	ok &= expect(!columns.isEmpty() && !rows.isEmpty(), (label + ": grid lines exist").c_str());
	ok &= expect(spacingWithinBand(columns) && spacingWithinBand(rows),
		(label + ": grid spacing is 24 to 96 pixels").c_str());

	int freeX = -1;
	for (int x = 0; x < image.width() && freeX < 0; ++x)
		if (!columns.contains(x))
			freeX = x;
	int freeY = -1;
	for (int y = 0; y < image.height() && freeY < 0; ++y)
		if (!rows.contains(y))
			freeY = y;
	ok &= expect(freeX >= 0 && freeY >= 0 && image.pixelColor(freeX, freeY) == kCanvas,
		(label + ": canvas colour between grid lines").c_str());

	// A pan that is not a multiple of the spacing moves every line by the same amount.
	QScrollBar* bar = view.horizontalScrollBar();
	const int pan = 7;
	const int before = bar->value();
	bar->setValue(before + pan);
	const int moved = bar->value() - before;
	const QList<int> shifted = gridLines(renderBackground(view), true);
	bool followed = moved != 0 && !shifted.isEmpty();
	for (int column : columns) {
		const int target = column - moved;
		if (target >= 0 && target < image.width() && !shifted.contains(target))
			followed = false;
	}
	ok &= expect(followed, (label + ": panning moves the grid with the scene").c_str());
	bar->setValue(before);
	return ok;
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	NetworkView view;
	QGraphicsScene scene;
	view.setScene(&scene);
	view.resize(640, 480);
	view.show();

	auto* track = new TrackLineItem(QLineF(0.0, 0.0, 100.0, 0.0));
	scene.addItem(track);
	auto* arc = new VirtualArcItem(QLineF(100.0, 0.0, 150.0, 50.0),
		QLineF(150.0, 50.0, 200.0, 0.0));
	scene.addItem(arc);
	auto* connection = new ConnectionItem(QLineF(200.0, 0.0, 240.0, 40.0));
	scene.addItem(connection);
	auto* farOverlay = scene.addRect(QRectF(100000.0, 100000.0, 10.0, 10.0));
	farOverlay->setFlag(QGraphicsItem::ItemIgnoresTransformations);

	bool ok = true;
	{
		NetworkView gridView;
		QGraphicsScene gridScene;
		gridView.setScene(&gridScene);
		gridView.setFrameShape(QFrame::NoFrame);
		gridView.resize(640, 480);
		gridView.show();
		gridView.fitToBounds(QRectF(0.0, 0.0, 1000.0, 600.0));
		gridView.zoomBy(3.0);
		ok &= checkBackgroundGrid(gridView, "3x");
		gridView.zoomBy(20.0 / 3.0);
		ok &= checkBackgroundGrid(gridView, "20x");
	}
	view.fitToTopology();
	const QRectF topology = view.topologyBounds();
	const qreal fittedScale = view.fittedScale();
	ok &= expect(topology == QRectF(0.0, 0.0, 240.0, 50.0),
		"Fit uses the exact painted endpoint union, excluding overlays and selection polygons");
	ok &= expect(endpointInside(view, track->line().p1()) && endpointInside(view, track->line().p2()),
		"track endpoints stay inside the fit inset");
	ok &= expect(endpointInside(view, arc->line().p1()) && endpointInside(view, arc->line().p2()),
		"first virtual-arc segment endpoints stay inside the fit inset");
	ok &= expect(endpointInside(view, arc->secondPaintedLine().p1()) && endpointInside(view, arc->secondPaintedLine().p2()),
		"second virtual-arc segment endpoints stay inside the fit inset");
	ok &= expect(endpointInside(view, connection->line().p1()) && endpointInside(view, connection->line().p2()),
		"connection endpoints stay inside the fit inset");
	ok &= expect(view.horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff
			&& view.verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
		"Fit hides both scrollbars");
	ok &= expect(near(view.zoomRatio(), 1.0) && view.zoomLabel() == "Fit",
		"Fit reports the baseline label");
	const QRectF previewBounds(-20.0, -10.0, 600.0, 120.0);
	view.fitToBounds(previewBounds);
	ok &= expect(view.topologyBounds() == previewBounds, "explicit fit stores preview bounds");
	ok &= expect(near(view.zoomRatio(), 1.0) && view.zoomLabel() == "Fit",
		"explicit fit establishes the preview baseline");
	view.zoomBy(1.15);
	ok &= expect(near(view.zoomRatio(), 1.15, 1e-5), "preview zoom is relative to its fitted baseline");
	view.fitToBounds(previewBounds);
	ok &= expect(near(view.zoomRatio(), 1.0) && view.topologyBounds() == previewBounds,
		"explicit fit restores the preview baseline");
	const QRectF normalizedPreviewBounds(0.0, 0.0, 0.7, 0.15);
	view.fitToBounds(normalizedPreviewBounds);
	ok &= expect(view.fittedScale() > 500.0,
		"Fit expands normalized preview coordinates instead of treating them as one-unit geometry");
	view.fitToBounds(previewBounds);

	int updates = 0;
	QObject::connect(&view, &NetworkView::viewportChanged, [&updates]() { ++updates; });
	view.fitToBounds(previewBounds);
	ok &= expect(updates == 1, "explicit fit emits one viewport update");
	ok &= expect(near(view.zoomRatio(), 1.0) && view.topologyBounds() == previewBounds,
		"explicit fit restores the preview baseline after a topology fit");
	view.fitToTopology();
	updates = 0;
	const QPoint wheelPos = view.viewport()->rect().center() + QPoint(20, 0);
	const QPointF scenePointBeforeWheel = view.mapToScene(wheelPos);
	QWheelEvent wheel(wheelPos, view.viewport()->mapToGlobal(wheelPos), QPoint(0, 0), QPoint(0, 120),
		Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
	QApplication::sendEvent(view.viewport(), &wheel);
	ok &= expect(near(view.zoomRatio(), 1.15, 1e-5), "one wheel step applies one 1.15 zoom");
	ok &= expect(QLineF(scenePointBeforeWheel, view.mapToScene(wheelPos)).length() < 0.5,
		"wheel zoom keeps the scene point under the pointer fixed");
	ok &= expect(updates == 1, "one wheel zoom emits one viewport update");

	view.fitToTopology();
	updates = 0;
	QWheelEvent fineWheel(wheelPos, view.viewport()->mapToGlobal(wheelPos), QPoint(0, 0), QPoint(0, 30),
		Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
	QApplication::sendEvent(view.viewport(), &fineWheel);
	ok &= expect(near(view.zoomRatio(), std::pow(1.15, 0.25), 1e-5),
		"high-resolution wheel deltas zoom proportionally");
	ok &= expect(updates == 1, "one high-resolution wheel event emits one viewport update");

	view.fitToTopology();
	view.zoomBy(6.0);
	updates = 0;
	const QPointF centerBeforeTrackpad = view.mapToScene(view.viewport()->rect().center());
	QWheelEvent trackpadScroll(wheelPos, view.viewport()->mapToGlobal(wheelPos), QPoint(24, -18), QPoint(24, -18),
		Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
	QApplication::sendEvent(view.viewport(), &trackpadScroll);
	const QPointF centerAfterTrackpad = view.mapToScene(view.viewport()->rect().center());
	ok &= expect(near(view.zoomRatio(), 6.0, 1e-5), "two-finger scrolling pans without zooming");
	ok &= expect(qAbs(centerAfterTrackpad.x() - centerBeforeTrackpad.x()) > 0.1
			&& qAbs(centerAfterTrackpad.y() - centerBeforeTrackpad.y()) > 0.1,
		"two-finger scrolling pans in both axes");
	ok &= expect(updates == 1, "one two-finger scroll event emits one viewport update");

	view.fitToTopology();
	updates = 0;
	QNativeGestureEvent pinch(Qt::ZoomNativeGesture, nullptr, wheelPos, wheelPos,
		view.viewport()->mapToGlobal(wheelPos), 0.05, 1, 0);
	QApplication::sendEvent(view.viewport(), &pinch);
	ok &= expect(near(view.zoomRatio(), 1.05, 1e-5), "native trackpad pinch zooms continuously");
	ok &= expect(updates == 1, "one pinch event emits one viewport update");

	view.fitToTopology();
	updates = 0;
	auto checkProgrammaticZoom = [&view, &updates, &ok](qreal factor, bool expectedApplied, const char* message) {
		const int before = updates;
		const bool applied = view.zoomBy(factor);
		ok &= expect(applied == expectedApplied, message);
		ok &= expect(updates == before + (expectedApplied ? 1 : 0),
			"each programmatic zoom emits exactly one update when applied");
	};
	checkProgrammaticZoom(1.15, true, "programmatic zoom-in applies");
	checkProgrammaticZoom(1.0 / 1.15, true, "programmatic zoom-out applies");
	checkProgrammaticZoom(1.0 / 1.15, false, "zoom-out at Fit is a no-op");
	checkProgrammaticZoom(2.0, true, "zoom-in above Fit applies");
	const QPointF centerBeforePan = view.mapToScene(view.viewport()->rect().center());
	view.centerOn(QPointF(180.0, 25.0));
	ok &= expect(QLineF(centerBeforePan, view.mapToScene(view.viewport()->rect().center())).length() > 1.0,
		"hidden scrollbars still permit panning above Fit");
	ok &= expect(view.horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff
			&& view.verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
		"zoom above Fit keeps the existing scrollbar policy");
	view.fitToTopology();
	view.zoomBy(2.0);
	updates = 0;
	const QPointF requestedCenter(120.0, 25.0);
	view.centerOn(requestedCenter);
	ok &= expect(updates == 1, "programmatic center emits one viewport update");
	view.resize(800, 600);
	QApplication::processEvents();
	const QPointF centerAfterProgrammaticResize = view.mapToScene(view.viewport()->rect().center());
	ok &= expect(QLineF(requestedCenter, centerAfterProgrammaticResize).length() < 0.5,
		"resize preserves a programmatic scene center");
	view.resize(640, 480);
	QApplication::processEvents();
	view.zoomBy(32.0);
	QWheelEvent detailWheel(wheelPos, view.viewport()->mapToGlobal(wheelPos), QPoint(), QPoint(0, 120),
		Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
	QApplication::sendEvent(view.viewport(), &detailWheel);
	ok &= expect(near(view.zoomRatio(), 64.0 * 1.15, 1e-5),
		"wheel zoom continues beyond the former 64x limit");
	checkProgrammaticZoom(1000.0, true, "zoom-in clamps at the station-detail maximum");
	ok &= expect(near(view.zoomRatio(), NetworkView::maximumZoomRatio(), 1e-5)
			&& view.zoomLabel() == "640x",
		"zoom-in reaches the 640x station-detail maximum");
	checkProgrammaticZoom(1.15, false, "zoom-in at maximum is a no-op");
	checkProgrammaticZoom(1.0 / 1000.0, true, "zoom-out clamps at Fit");
	ok &= expect(near(view.zoomRatio(), 1.0, 1e-5) && view.zoomLabel() == "Fit",
		"zoom-out clamps at Fit");
	checkProgrammaticZoom(1.0 / 1.15, false, "zoom-out at Fit remains a no-op");
	ok &= expect(view.horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff
			&& view.verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
		"Fit hides both scrollbars after zooming");

	view.fitToTopology();
	const qreal firstBaseline = view.fittedScale();
	view.resize(800, 600);
	QApplication::processEvents();
	ok &= expect(near(view.zoomRatio(), 1.0, 1e-5), "resize at Fit remains at Fit");
	ok &= expect(!near(firstBaseline, view.fittedScale(), 1e-5), "resize recomputes the fitted baseline");
	view.zoomBy(3.0);
	const QPointF centerBeforeResize = view.mapToScene(view.viewport()->rect().center());
	view.resize(640, 480);
	QApplication::processEvents();
	const QPointF centerAfterResize = view.mapToScene(view.viewport()->rect().center());
	ok &= expect(near(view.zoomRatio(), 3.0, 1e-5), "resize away from Fit preserves the zoom ratio");
	ok &= expect(QLineF(centerBeforeResize, centerAfterResize).length() < 0.5,
		"resize away from Fit preserves the viewed scene center");

	view.fitToTopology();
	view.zoomBy(2.0);
	ok &= expect(view.zoomLabel() == "2x", "integral zoom labels use map notation");
	view.zoomBy(NetworkView::maximumZoomRatio());
	ok &= expect(view.zoomLabel() == "640x", "maximum zoom label uses map notation");

	Q_UNUSED(fittedScale);
	return ok ? 0 : 1;
}
