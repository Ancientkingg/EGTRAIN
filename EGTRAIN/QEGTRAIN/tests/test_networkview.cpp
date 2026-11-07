#include "graphics/NetworkView.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/VirtualArcItem.h"

#include <QApplication>
#include <QGraphicsRectItem>
#include <QImage>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QScrollBar>
#include <QTest>
#include <QWheelEvent>
#include <cmath>
#include <utility>

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

// A shown view of 640 by 480 pixels on a track, a virtual arc, a connection and an overlay far away.
struct ViewFixture {
	NetworkView view;
	QGraphicsScene scene;
	TrackLineItem* track = nullptr;
	VirtualArcItem* arc = nullptr;
	ConnectionItem* connection = nullptr;
	// The viewport updates since the fixture was made.
	int updates = 0;

	ViewFixture() {
		view.setScene(&scene);
		view.resize(640, 480);
		view.show();
		track = new TrackLineItem(QLineF(0.0, 0.0, 100.0, 0.0));
		scene.addItem(track);
		arc = new VirtualArcItem(QLineF(100.0, 0.0, 150.0, 50.0), QLineF(150.0, 50.0, 200.0, 0.0));
		scene.addItem(arc);
		connection = new ConnectionItem(QLineF(200.0, 0.0, 240.0, 40.0));
		scene.addItem(connection);
		auto* farOverlay = scene.addRect(QRectF(100000.0, 100000.0, 10.0, 10.0));
		farOverlay->setFlag(QGraphicsItem::ItemIgnoresTransformations);
		QObject::connect(&view, &NetworkView::viewportChanged, [this]() { ++updates; });
	}
};

// The pointer position of the wheel, trackpad and pinch events, right of the centre of the viewport at Fit.
static QPoint wheelPosition(const NetworkView& view) {
	return view.viewport()->rect().center() + QPoint(20, 0);
}

static QPointF centreOf(const NetworkView& view) {
	return view.mapToScene(view.viewport()->rect().center());
}

// How far apart two scene points are on the screen, in logical pixels.
static qreal pixelsApart(const NetworkView& view, const QPointF& from, const QPointF& to) {
	return QLineF(from, to).length() * std::abs(view.transform().m11());
}

// A drag with the left button from the middle of the viewport by offset, in four moves, as the user moves the view.
static void dragView(NetworkView& view, const QPoint& offset) {
	const QPoint from = view.viewport()->rect().center();
	const auto send = [&view](QEvent::Type type, const QPoint& at, Qt::MouseButton button, Qt::MouseButtons held) {
		QMouseEvent event(type, QPointF(at), QPointF(view.viewport()->mapToGlobal(at)), button, held, Qt::NoModifier);
		QApplication::sendEvent(view.viewport(), &event);
	};
	send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
	for (int step = 1; step <= 4; ++step)
		send(QEvent::MouseMove, from + offset * step / 4, Qt::NoButton, Qt::LeftButton);
	send(QEvent::MouseButtonRelease, from + offset, Qt::LeftButton, Qt::NoButton);
}

class TestNetworkView : public QObject {
	Q_OBJECT

private slots:
	void drawsBackgroundGrid_data();
	void drawsBackgroundGrid();
	void fitsShortView();
	void fitsTopology();
	void fitsPreviewBounds();
	void wheelZoomsAroundPointer();
	void wheelZoomsAroundPointerAtEdge();
	void highResolutionWheelZoomsProportionally();
	void twoFingerScrollingPans();
	void pansAtFit();
	void panStopsAtEdgeOfNetwork_data();
	void panStopsAtEdgeOfNetwork();
	void pinchZooms();
	void programmaticZoomEmitsOneUpdate();
	void keepsCentreAndClampsZoom();
	void resizeKeepsFitOrZoom();
	void resizeKeepsPan_data();
	void resizeKeepsPan();
	void zoomLabelsUseMapNotation();
};

void TestNetworkView::drawsBackgroundGrid_data() {
	QTest::addColumn<QList<double>>("zooms");
	QTest::newRow("3x") << QList<double>{3.0};
	QTest::newRow("20x") << QList<double>{3.0, 20.0 / 3.0};
}

void TestNetworkView::drawsBackgroundGrid() {
	QFETCH(QList<double>, zooms);
	NetworkView view;
	QGraphicsScene scene;
	view.setScene(&scene);
	view.setFrameShape(QFrame::NoFrame);
	view.resize(640, 480);
	view.show();
	view.fitToBounds(QRectF(0.0, 0.0, 1000.0, 600.0));
	for (const double zoom : zooms)
		view.zoomBy(zoom);

	const QImage image = renderBackground(view);
	const QList<int> columns = gridLines(image, true);
	const QList<int> rows = gridLines(image, false);
	// Grid lines exist.
	QVERIFY(!columns.isEmpty());
	QVERIFY(!rows.isEmpty());
	// The grid spacing is 24 to 96 pixels.
	QVERIFY(spacingWithinBand(columns));
	QVERIFY(spacingWithinBand(rows));

	int freeX = -1;
	for (int x = 0; x < image.width() && freeX < 0; ++x)
		if (!columns.contains(x))
			freeX = x;
	int freeY = -1;
	for (int y = 0; y < image.height() && freeY < 0; ++y)
		if (!rows.contains(y))
			freeY = y;
	// The canvas colour lies between the grid lines.
	QCOMPARE_GE(freeX, 0);
	QCOMPARE_GE(freeY, 0);
	QCOMPARE(image.pixelColor(freeX, freeY), kCanvas);

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
	QVERIFY2(followed, "panning moves the grid with the scene");
}

void TestNetworkView::fitsShortView() {
	// The fit box of the Paimpol preview in a view of 55 pixels: the padding leaves most of the height for the line.
	NetworkView shortView;
	QGraphicsScene shortScene;
	shortView.setScene(&shortScene);
	shortView.setFrameShape(QFrame::NoFrame);
	shortView.resize(423, 55);
	shortView.show();
	shortView.fitToBounds(QRectF(-1853.1, -4507.4, 40768.2, 8894.9));
	// Fit in a short view draws the line of the box at least 150 pixels wide.
	QCOMPARE_GE(37062.0 * shortView.fittedScale(), 150.0);
}

void TestNetworkView::fitsTopology() {
	ViewFixture f;
	NetworkView& view = f.view;
	view.fitToTopology();
	// Fit uses the exact painted endpoint union, excluding overlays and selection polygons.
	QCOMPARE(view.topologyBounds(), QRectF(0.0, 0.0, 240.0, 50.0));
	// Track endpoints stay inside the fit inset.
	QVERIFY(endpointInside(view, f.track->line().p1()));
	QVERIFY(endpointInside(view, f.track->line().p2()));
	// First virtual-arc segment endpoints stay inside the fit inset.
	QVERIFY(endpointInside(view, f.arc->line().p1()));
	QVERIFY(endpointInside(view, f.arc->line().p2()));
	// Second virtual-arc segment endpoints stay inside the fit inset.
	QVERIFY(endpointInside(view, f.arc->secondPaintedLine().p1()));
	QVERIFY(endpointInside(view, f.arc->secondPaintedLine().p2()));
	// Connection endpoints stay inside the fit inset.
	QVERIFY(endpointInside(view, f.connection->line().p1()));
	QVERIFY(endpointInside(view, f.connection->line().p2()));
	// Fit hides both scrollbars.
	QCOMPARE(view.horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
	QCOMPARE(view.verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
	// Fit reports the baseline label.
	QVERIFY(near(view.zoomRatio(), 1.0));
	QCOMPARE(view.zoomLabel(), "Fit");
}

void TestNetworkView::fitsPreviewBounds() {
	ViewFixture f;
	NetworkView& view = f.view;
	view.fitToTopology();
	const QRectF previewBounds(-20.0, -10.0, 600.0, 120.0);
	view.fitToBounds(previewBounds);
	// Explicit fit stores preview bounds.
	QCOMPARE(view.topologyBounds(), previewBounds);
	// Explicit fit establishes the preview baseline.
	QVERIFY(near(view.zoomRatio(), 1.0));
	QCOMPARE(view.zoomLabel(), "Fit");
	// The padded bounds hold the bounds of the fit and are inside the viewport at Fit, to the pixel that centring rounds.
	const QRectF padded = view.paddedTopologyBounds();
	QVERIFY(padded.contains(previewBounds));
	QVERIFY(QRectF(view.viewport()->rect()).adjusted(-1.0, -1.0, 1.0, 1.0).contains(view.viewportTransform().mapRect(padded)));
	view.zoomBy(1.15);
	QVERIFY2(near(view.zoomRatio(), 1.15, 1e-5), "preview zoom is relative to its fitted baseline");
	// A zoom keeps the padded bounds of the fit.
	QCOMPARE(view.paddedTopologyBounds(), padded);
	view.fitToBounds(previewBounds);
	// Explicit fit restores the preview baseline.
	QVERIFY(near(view.zoomRatio(), 1.0));
	QCOMPARE(view.topologyBounds(), previewBounds);
	const QRectF normalizedPreviewBounds(0.0, 0.0, 0.7, 0.15);
	view.fitToBounds(normalizedPreviewBounds);
	// Fit expands normalized preview coordinates instead of treating them as one-unit geometry.
	QCOMPARE_GT(view.fittedScale(), 500.0);
	view.fitToBounds(previewBounds);

	f.updates = 0;
	view.fitToBounds(previewBounds);
	// Explicit fit emits one viewport update.
	QCOMPARE(f.updates, 1);
	// Explicit fit restores the preview baseline after a topology fit.
	QVERIFY(near(view.zoomRatio(), 1.0));
	QCOMPARE(view.topologyBounds(), previewBounds);
}

void TestNetworkView::wheelZoomsAroundPointer() {
	ViewFixture f;
	NetworkView& view = f.view;
	view.fitToTopology();
	const QPoint wheelPos = wheelPosition(view);
	f.updates = 0;
	const QPointF scenePointBeforeWheel = view.mapToScene(wheelPos);
	QWheelEvent wheel(wheelPos, view.viewport()->mapToGlobal(wheelPos), QPoint(0, 0), QPoint(0, 120),
		Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
	QApplication::sendEvent(view.viewport(), &wheel);
	QVERIFY2(near(view.zoomRatio(), 1.15, 1e-5), "one wheel step applies one 1.15 zoom");
	// Wheel zoom keeps the scene point under the pointer fixed.
	QCOMPARE_LT(QLineF(scenePointBeforeWheel, view.mapToScene(wheelPos)).length(), 0.5);
	// One wheel zoom emits one viewport update.
	QCOMPARE(f.updates, 1);
}

// The left end of the track stays under the pointer while the view zooms in from Fit around it, although the view then
// shows the scene left of the network.
void TestNetworkView::wheelZoomsAroundPointerAtEdge() {
	ViewFixture f;
	NetworkView& view = f.view;
	view.fitToTopology();
	const QPointF end = f.track->line().p1();
	const QPoint wheelPos = view.mapFromScene(end);
	const QPointF atEnd = view.mapToScene(wheelPos);
	for (int notch = 1; notch <= 5; ++notch) {
		const QPointF underPointer = view.mapToScene(wheelPos);
		QWheelEvent wheel(wheelPos, view.viewport()->mapToGlobal(wheelPos), QPoint(0, 0), QPoint(0, 120), Qt::NoButton,
			Qt::NoModifier, Qt::ScrollUpdate, false);
		QApplication::sendEvent(view.viewport(), &wheel);
		// Each wheel step keeps the scene point under the pointer to a fraction of a pixel.
		QCOMPARE_LE(pixelsApart(view, underPointer, view.mapToScene(wheelPos)), 0.01);
	}
	QVERIFY2(near(view.zoomRatio(), std::pow(1.15, 5.0), 1e-5), "five wheel steps apply five 1.15 zooms");
	// The end of the track is still under the pointer, left of the middle of the view.
	QCOMPARE_LE(pixelsApart(view, atEnd, view.mapToScene(wheelPos)), 0.01);
	QCOMPARE_LT(wheelPos.x(), view.viewport()->rect().center().x());
}

void TestNetworkView::highResolutionWheelZoomsProportionally() {
	ViewFixture f;
	NetworkView& view = f.view;
	view.fitToTopology();
	const QPoint wheelPos = wheelPosition(view);
	f.updates = 0;
	QWheelEvent fineWheel(wheelPos, view.viewport()->mapToGlobal(wheelPos), QPoint(0, 0), QPoint(0, 30),
		Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
	QApplication::sendEvent(view.viewport(), &fineWheel);
	QVERIFY2(near(view.zoomRatio(), std::pow(1.15, 0.25), 1e-5), "high-resolution wheel deltas zoom proportionally");
	// One high-resolution wheel event emits one viewport update.
	QCOMPARE(f.updates, 1);
}

void TestNetworkView::twoFingerScrollingPans() {
	ViewFixture f;
	NetworkView& view = f.view;
	view.fitToTopology();
	const QPoint wheelPos = wheelPosition(view);
	view.zoomBy(6.0);
	f.updates = 0;
	const QPointF centerBeforeTrackpad = view.mapToScene(view.viewport()->rect().center());
	QWheelEvent trackpadScroll(wheelPos, view.viewport()->mapToGlobal(wheelPos), QPoint(24, -18), QPoint(24, -18),
		Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
	QApplication::sendEvent(view.viewport(), &trackpadScroll);
	const QPointF centerAfterTrackpad = view.mapToScene(view.viewport()->rect().center());
	QVERIFY2(near(view.zoomRatio(), 6.0, 1e-5), "two-finger scrolling pans without zooming");
	// Two-finger scrolling pans in both axes.
	QCOMPARE_GT(qAbs(centerAfterTrackpad.x() - centerBeforeTrackpad.x()), 0.1);
	QCOMPARE_GT(qAbs(centerAfterTrackpad.y() - centerBeforeTrackpad.y()), 0.1);
	// One two-finger scroll event emits one viewport update.
	QCOMPARE(f.updates, 1);
}

// At Fit a drag and a two-finger scroll move the network on both axes, and Fit centres it again.
void TestNetworkView::pansAtFit() {
	ViewFixture f;
	NetworkView& view = f.view;
	view.fitToTopology();
	const QPointF fitted = centreOf(view);
	const qreal pixel = 1.0 / view.fittedScale();
	f.updates = 0;
	dragView(view, QPoint(-90, -40));
	// A drag to the upper left moves the network with the pointer, so the middle of the view shows a point to the lower
	// right of the centre of the network.
	QCOMPARE_LE(std::abs((centreOf(view).x() - fitted.x()) / pixel - 90.0), 1.0);
	QCOMPARE_LE(std::abs((centreOf(view).y() - fitted.y()) / pixel - 40.0), 1.0);
	QCOMPARE(view.zoomLabel(), "Fit");
	// The drag emits a viewport update.
	QCOMPARE_GT(f.updates, 0);

	const QPointF dragged = centreOf(view);
	const QPoint wheelPos = wheelPosition(view);
	QWheelEvent trackpadScroll(wheelPos, view.viewport()->mapToGlobal(wheelPos), QPoint(30, 20), QPoint(30, 20),
		Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
	QApplication::sendEvent(view.viewport(), &trackpadScroll);
	// A two-finger scroll at Fit moves the network on both axes.
	QCOMPARE_LE(std::abs((dragged.x() - centreOf(view).x()) / pixel - 30.0), 1.0);
	QCOMPARE_LE(std::abs((dragged.y() - centreOf(view).y()) / pixel - 20.0), 1.0);
	QCOMPARE(view.zoomLabel(), "Fit");

	view.fitToTopology();
	// Fit centres the network again.
	QCOMPARE_LE(pixelsApart(view, fitted, centreOf(view)), 0.5);
}

void TestNetworkView::panStopsAtEdgeOfNetwork_data() {
	QTest::addColumn<double>("zoom");
	QTest::newRow("Fit") << 1.0;
	QTest::newRow("4x") << 4.0;
}

// However far the view is dragged or scrolled, it moves until an edge of the network reaches the middle of the view, and
// no further.
void TestNetworkView::panStopsAtEdgeOfNetwork() {
	QFETCH(double, zoom);
	ViewFixture f;
	NetworkView& view = f.view;
	view.fitToTopology();
	view.zoomBy(zoom);
	const QRectF bounds = view.topologyBounds();
	// A drag to the lower right brings the upper left corner of the network to the middle, and so on.
	const QList<std::pair<QPoint, QPointF>> drags = {{QPoint(3000, 3000), bounds.topLeft()},
		{QPoint(-3000, 3000), bounds.topRight()}, {QPoint(3000, -3000), bounds.bottomLeft()},
		{QPoint(-3000, -3000), bounds.bottomRight()}};
	for (const auto& [offset, corner] : drags) {
		dragView(view, offset);
		// The drag stops with the corner of the network in the middle of the view.
		QCOMPARE_LE(pixelsApart(view, corner, centreOf(view)), 1.5);
	}
	const QPoint wheelPos = wheelPosition(view);
	QWheelEvent trackpadScroll(wheelPos, view.viewport()->mapToGlobal(wheelPos), QPoint(3000, 3000), QPoint(3000, 3000),
		Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
	QApplication::sendEvent(view.viewport(), &trackpadScroll);
	// A two-finger scroll stops at the same edge.
	QCOMPARE_LE(pixelsApart(view, bounds.topLeft(), centreOf(view)), 1.5);
}

void TestNetworkView::pinchZooms() {
	ViewFixture f;
	NetworkView& view = f.view;
	view.fitToTopology();
	const QPoint wheelPos = wheelPosition(view);
	f.updates = 0;
	QNativeGestureEvent pinch(Qt::ZoomNativeGesture, QPointingDevice::primaryPointingDevice(), 2, wheelPos, wheelPos,
		view.viewport()->mapToGlobal(wheelPos), 0.05, QPointF(), 1);
	QApplication::sendEvent(view.viewport(), &pinch);
	QVERIFY2(near(view.zoomRatio(), 1.05, 1e-5), "native trackpad pinch zooms continuously");
	// One pinch event emits one viewport update.
	QCOMPARE(f.updates, 1);
}

// A zoom of the view that the program asks for applies or not, and emits one viewport update when it applies.
static void checkProgrammaticZoom(ViewFixture& f, qreal factor, bool expectedApplied, const char* message) {
	const int before = f.updates;
	const bool applied = f.view.zoomBy(factor);
	QVERIFY2(applied == expectedApplied, message);
	// Each programmatic zoom emits exactly one update when applied.
	QCOMPARE(f.updates, before + (expectedApplied ? 1 : 0));
}

void TestNetworkView::programmaticZoomEmitsOneUpdate() {
	ViewFixture f;
	NetworkView& view = f.view;
	view.fitToTopology();
	f.updates = 0;
	checkProgrammaticZoom(f, 1.15, true, "programmatic zoom-in applies");
	checkProgrammaticZoom(f, 1.0 / 1.15, true, "programmatic zoom-out applies");
	checkProgrammaticZoom(f, 1.0 / 1.15, false, "zoom-out at Fit is a no-op");
	checkProgrammaticZoom(f, 2.0, true, "zoom-in above Fit applies");
	if (QTest::currentTestFailed())
		return;
	const QPointF centerBeforePan = view.mapToScene(view.viewport()->rect().center());
	view.centerOn(QPointF(180.0, 25.0));
	// Hidden scrollbars still permit panning above Fit.
	QCOMPARE_GT(QLineF(centerBeforePan, view.mapToScene(view.viewport()->rect().center())).length(), 1.0);
	// Zoom above Fit keeps the existing scrollbar policy.
	QCOMPARE(view.horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
	QCOMPARE(view.verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
}

// A programmatic centre survives a resize, and from 64 times Fit the zoom goes on to the station-detail maximum and
// back down to Fit.
void TestNetworkView::keepsCentreAndClampsZoom() {
	ViewFixture f;
	NetworkView& view = f.view;
	view.fitToTopology();
	const QPoint wheelPos = wheelPosition(view);
	view.zoomBy(2.0);
	f.updates = 0;
	const QPointF requestedCenter(120.0, 25.0);
	view.centerOn(requestedCenter);
	// Programmatic center emits one viewport update.
	QCOMPARE(f.updates, 1);
	view.resize(800, 600);
	QApplication::processEvents();
	const QPointF centerAfterProgrammaticResize = view.mapToScene(view.viewport()->rect().center());
	// Resize preserves a programmatic scene center.
	QCOMPARE_LT(QLineF(requestedCenter, centerAfterProgrammaticResize).length(), 0.5);
	view.resize(640, 480);
	QApplication::processEvents();
	view.zoomBy(32.0);
	QWheelEvent detailWheel(wheelPos, view.viewport()->mapToGlobal(wheelPos), QPoint(), QPoint(0, 120),
		Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
	QApplication::sendEvent(view.viewport(), &detailWheel);
	QVERIFY2(near(view.zoomRatio(), 64.0 * 1.15, 1e-5), "wheel zoom continues beyond the former 64x limit");
	checkProgrammaticZoom(f, 1000.0, true, "zoom-in clamps at the station-detail maximum");
	if (QTest::currentTestFailed())
		return;
	// Zoom-in reaches the 640x station-detail maximum.
	QVERIFY(near(view.zoomRatio(), NetworkView::maximumZoomRatio(), 1e-5));
	QCOMPARE(view.zoomLabel(), "640x");
	checkProgrammaticZoom(f, 1.15, false, "zoom-in at maximum is a no-op");
	checkProgrammaticZoom(f, 1.0 / 1000.0, true, "zoom-out clamps at Fit");
	if (QTest::currentTestFailed())
		return;
	// Zoom-out clamps at Fit.
	QVERIFY(near(view.zoomRatio(), 1.0, 1e-5));
	QCOMPARE(view.zoomLabel(), "Fit");
	checkProgrammaticZoom(f, 1.0 / 1.15, false, "zoom-out at Fit remains a no-op");
	if (QTest::currentTestFailed())
		return;
	// Fit hides both scrollbars after zooming.
	QCOMPARE(view.horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
	QCOMPARE(view.verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
}

void TestNetworkView::resizeKeepsFitOrZoom() {
	ViewFixture f;
	NetworkView& view = f.view;
	view.fitToTopology();
	const qreal firstBaseline = view.fittedScale();
	view.resize(800, 600);
	QApplication::processEvents();
	QVERIFY2(near(view.zoomRatio(), 1.0, 1e-5), "resize at Fit remains at Fit");
	QVERIFY2(!near(firstBaseline, view.fittedScale(), 1e-5), "resize recomputes the fitted baseline");
	view.zoomBy(3.0);
	const QPointF centerBeforeResize = view.mapToScene(view.viewport()->rect().center());
	view.resize(640, 480);
	QApplication::processEvents();
	const QPointF centerAfterResize = view.mapToScene(view.viewport()->rect().center());
	QVERIFY2(near(view.zoomRatio(), 3.0, 1e-5), "resize away from Fit preserves the zoom ratio");
	// Resize away from Fit preserves the viewed scene center.
	QCOMPARE_LT(QLineF(centerBeforeResize, centerAfterResize).length(), 0.5);
}

void TestNetworkView::resizeKeepsPan_data() {
	QTest::addColumn<double>("zoom");
	QTest::newRow("Fit") << 1.0;
	QTest::newRow("3x") << 3.0;
}

// A resize keeps the place that the user dragged to in the middle of the view, at Fit with the network fitted to the new
// size, and it does so to a fraction of a pixel, so that the view does not creep while the window is resized.
void TestNetworkView::resizeKeepsPan() {
	QFETCH(double, zoom);
	ViewFixture f;
	NetworkView& view = f.view;
	view.fitToTopology();
	view.zoomBy(zoom);
	const QPointF before = centreOf(view);
	dragView(view, QPoint(-100, -30));
	const QPointF panned = centreOf(view);
	// The drag moved the view.
	QCOMPARE_GT(pixelsApart(view, before, panned), 50.0);
	for (int step = 0; step < 40; ++step) {
		view.resize(640 + (step % 7) * 13, 480 + (step % 5) * 11);
		QApplication::processEvents();
		// Each resize keeps the zoom ratio and the dragged place in the middle.
		QVERIFY(near(view.zoomRatio(), zoom, 1e-5));
		QCOMPARE_LE(pixelsApart(view, panned, centreOf(view)), 0.01);
	}
}

void TestNetworkView::zoomLabelsUseMapNotation() {
	ViewFixture f;
	NetworkView& view = f.view;
	view.fitToTopology();
	view.zoomBy(2.0);
	// Integral zoom labels use map notation.
	QCOMPARE(view.zoomLabel(), "2x");
	view.zoomBy(NetworkView::maximumZoomRatio());
	// The maximum zoom label uses map notation.
	QCOMPARE(view.zoomLabel(), "640x");
}

QTEST_MAIN(TestNetworkView)
#include "test_networkview.moc"
