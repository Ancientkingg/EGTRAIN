#include "graphics/FollowCamera.h"
#include "graphics/NetworkView.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QGraphicsScene>
#include <QLineF>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTest>
#include <QThread>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

static QPointF centerOf(const NetworkView& view) {
	return view.mapToScene(view.viewport()->rect().center());
}

static qreal distance(const QPointF& from, const QPointF& to) {
	return QLineF(from, to).length();
}

// One logical pixel in scene units.
static qreal pixelOf(const NetworkView& view) {
	return 1.0 / std::abs(view.transform().m11());
}

// How far the centre of the view is from a point, in logical pixels.
static qreal offInPixels(const NetworkView& view, const QPointF& point) {
	return distance(centerOf(view), point) / pixelOf(view);
}

// A scene of 4000 by 2000 units shown at 8 times the fitted zoom, so the view covers about
// 530 by 400 units, with a clock the test advances by hand.
struct Rig {
	QGraphicsScene scene;
	NetworkView view;
	FollowCamera camera;
	qint64 now = 0;

	explicit Rig(qreal zoom = 8.0)
		: camera(&view) {
		camera.setClock([this]() { return now; });
		view.setScene(&scene);
		view.setFrameShape(QFrame::NoFrame);
		view.resize(800, 600);
		view.show();
		view.fitToBounds(QRectF(0.0, 0.0, 4000.0, 2000.0));
		view.zoomBy(zoom);
		QApplication::processEvents();
	}

	void tick() {
		now += FollowCamera::tickIntervalMs;
		camera.tick();
	}

	// Pans the view by the user, by a number of logical pixels along x.
	void pan(int pixels) {
		QScrollBar* bar = view.horizontalScrollBar();
		bar->setValue(bar->value() + pixels);
	}
};

// Presses or releases the left button in the middle of the view, as at the start or the end of a drag.
static void sendLeftButton(NetworkView& view, QEvent::Type type) {
	const QPoint at = view.viewport()->rect().center();
	const Qt::MouseButtons held = type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton;
	QMouseEvent event(type, QPointF(at), QPointF(view.viewport()->mapToGlobal(at)), Qt::LeftButton, held, Qt::NoModifier);
	QApplication::sendEvent(view.viewport(), &event);
}

// Comparisons that fail print the measured numbers, so that a failure on another platform can be read from its log.
class TestFollowCamera : public QObject {
	Q_OBJECT

private slots:
	void idle();
	void followsMovingTarget();
	void snaps();
	void cutsToFarTarget();
	void catchesUpAfterPan();
	void catchUpEndsWhileTargetStands();
	void staysWhereUserPannedWithoutTarget();
	void keepsZoomAnchor();
	void holdsAxisAtEdge();
	void staysAtFit();
	void settles();
	void reportsMovesOfCamera();
	void holdsForUser();
	void stopsWithView();
};

void TestFollowCamera::idle() {
	Rig rig;
	// A new camera is idle.
	QVERIFY(!rig.camera.running());
	QVERIFY(!rig.camera.hasTarget());
	rig.camera.follow(QPointF(std::nan(""), 1000.0), true);
	QVERIFY2(!rig.camera.hasTarget(), "a target that is not finite is ignored");
	rig.camera.follow(QPointF(1000.0, 1000.0), true);
	rig.camera.follow(QPointF(1010.0, 1000.0), false);
	// Following a target needs no timer.
	QVERIFY(rig.camera.hasTarget());
	QVERIFY(!rig.camera.running());
	rig.pan(-100);
	rig.camera.follow(QPointF(1020.0, 1000.0), false);
	QVERIFY2(rig.camera.running(), "a target after a pan starts a catch-up");
	rig.camera.stop();
	// Stop ends the catch-up and forgets the target.
	QVERIFY(!rig.camera.running());
	QVERIFY(!rig.camera.hasTarget());
}

// The window passes the drawn centre of the followed train at every step of its glide. The view is on it at every
// step, to a fraction of a pixel, also between whole pixels.
void TestFollowCamera::followsMovingTarget() {
	Rig rig;
	const qreal pixel = pixelOf(rig.view);
	QPointF target(1000.0, 1000.0);
	rig.camera.follow(target, true);
	qreal largest = offInPixels(rig.view, target);
	for (int step = 0; step < 200; ++step) {
		// a train that moves 1.37 pixels to the right and 0.29 pixels down per frame
		target += QPointF(1.37 * pixel, 0.29 * pixel);
		rig.camera.follow(target, false);
		largest = std::max(largest, offInPixels(rig.view, target));
		QVERIFY(!rig.camera.running());
	}
	// The view is on the target after every step, to a hundredth of a pixel.
	QCOMPARE_LE(largest, 0.01);
	QVERIFY2(!rig.camera.running(), "following a moving target needs no timer");
}

void TestFollowCamera::snaps() {
	Rig rig;
	const QPointF target(1500.0, 900.0);
	rig.camera.follow(target, true);
	// A snap moves the view at once.
	QCOMPARE_LE(offInPixels(rig.view, target), 0.01);
	QVERIFY2(!rig.camera.running(), "a snap leaves no timer running");

	rig.camera.stop();
	rig.pan(-200);
	const QPointF first(1200.0, 1000.0);
	rig.camera.follow(first, false);
	// The first target after stop moves the view at once, also after a pan.
	QCOMPARE_LE(offInPixels(rig.view, first), 0.01);
	QVERIFY2(!rig.camera.running(), "the first target leaves no timer running");

	// A snap during a catch-up ends it.
	rig.pan(-200);
	rig.camera.follow(first + QPointF(5.0, 0.0), false);
	QVERIFY2(rig.camera.running(), "a catch-up is under way before the snap");
	const QPointF c(1250.0, 1000.0);
	rig.camera.follow(c, true);
	// A snap moves the view at once during a catch-up.
	QCOMPARE_LE(offInPixels(rig.view, c), 0.01);
	QVERIFY2(!rig.camera.running(), "a snap ends the catch-up");
}

void TestFollowCamera::cutsToFarTarget() {
	Rig rig;
	rig.camera.follow(QPointF(1000.0, 1000.0), true);
	// a seek of the replay or a train that left the view: further than 1.5 times the larger visible side
	const QPointF seek(3000.0, 1000.0);
	rig.camera.follow(seek, false);
	// A target far outside the visible scene moves the view at once.
	QCOMPARE_LE(offInPixels(rig.view, seek), 0.01);
	QVERIFY2(!rig.camera.running(), "a far target leaves no timer running");

	rig.pan(-150);
	const QPointF back(1000.0, 1000.0);
	rig.camera.follow(back, false);
	// A far target after a pan is a cut, not a catch-up.
	QCOMPARE_LE(offInPixels(rig.view, back), 0.01);
	QVERIFY(!rig.camera.running());
}

// After a pan the view keeps the distance from the target that the user left and closes it over catchUpMs, easing in
// and out, while the target moves on.
void TestFollowCamera::catchesUpAfterPan() {
	Rig rig;
	const qreal pixel = pixelOf(rig.view);
	QPointF target(1300.0, 1000.0);
	rig.camera.follow(target, true);
	rig.pan(-200);
	const QPointF panned = centerOf(rig.view);
	const QPointF left = panned - target;

	qreal previousOff = std::numeric_limits<qreal>::max();
	bool closing = true;
	qreal firstMove = -1.0;
	const qint64 start = rig.now;
	while (rig.now - start < FollowCamera::catchUpMs + 100) {
		rig.now += 16;
		target += QPointF(2.0 * pixel, 0.0);
		rig.camera.follow(target, false);
		const QPointF off = centerOf(rig.view) - target;
		const qreal offPixels = std::hypot(off.x(), off.y()) / pixel;
		if (firstMove < 0.0)
			firstMove = distance(off, left) / pixel;
		closing &= offPixels <= previousOff + 0.01;
		previousOff = offPixels;
		if (rig.now - start == 224) {
			const qreal fraction = std::hypot(off.x(), off.y()) / std::hypot(left.x(), left.y());
			// Half way through the catch-up about half of the distance is left.
			QCOMPARE_GT(fraction, 0.35);
			QCOMPARE_LT(fraction, 0.6);
		}
	}
	// The catch-up starts from where the pan left the view.
	QCOMPARE_GE(firstMove, 0.0);
	QCOMPARE_LT(firstMove, 5.0);
	QVERIFY2(closing, "the distance to the target never grows during the catch-up");
	// The catch-up ends on the target and stops its timer.
	QCOMPARE_LE(previousOff, 0.01);
	QVERIFY(!rig.camera.running());
}

// A catch-up after a pan also ends while the target stands, as when the train waits at a platform.
void TestFollowCamera::catchUpEndsWhileTargetStands() {
	Rig rig;
	const QPointF target(1300.0, 1000.0);
	rig.camera.follow(target, true);
	rig.pan(150);
	rig.camera.follow(target, false);
	QVERIFY2(rig.camera.running(), "a target after a pan starts the timer of the catch-up");
	int ticks = 0;
	while (rig.camera.running() && ticks < 100) {
		rig.tick();
		++ticks;
	}
	const int expected = FollowCamera::catchUpMs / FollowCamera::tickIntervalMs;
	// The timer ends the catch-up in catchUpMs without a new target.
	QVERIFY(!rig.camera.running());
	QCOMPARE_GE(ticks, expected - 1);
	QCOMPARE_LE(ticks, expected + 1);
	// The view is back on the target that stands.
	QCOMPARE_LE(offInPixels(rig.view, target), 0.01);
}

// Without a new target, as in a paused run, the view stays where the user left it.
void TestFollowCamera::staysWhereUserPannedWithoutTarget() {
	Rig rig;
	rig.camera.follow(QPointF(1300.0, 1000.0), true);
	rig.pan(-120);
	const QPointF panned = centerOf(rig.view);
	for (int i = 0; i < 40; ++i)
		rig.tick();
	// A pan without a new target leaves the view where the user left it.
	QVERIFY(!rig.camera.running());
	QCOMPARE_LE(distance(centerOf(rig.view), panned), 0.01 * pixelOf(rig.view));
}

void TestFollowCamera::keepsZoomAnchor() {
	Rig rig;
	const QPointF target(2000.0, 1000.0);
	rig.camera.follow(target, true);

	rig.view.zoomBy(2.0, QPointF(0.0, 0.0));
	const QPointF zoomed = centerOf(rig.view);
	// Zooming around a corner moves the view centre.
	QCOMPARE_GT(distance(zoomed, target), 20.0);
	rig.now += 16;
	rig.camera.follow(target, false);
	const QPointF after = centerOf(rig.view);
	// The next target does not undo the zoom anchor.
	QCOMPARE_LT(distance(after, zoomed), 0.1 * distance(zoomed, target));
	QVERIFY2(rig.camera.running(), "the next target starts a catch-up");
	for (int i = 0; i < 40 && rig.camera.running(); ++i)
		rig.tick();
	// The view reaches the target after the zoom.
	QVERIFY(!rig.camera.running());
	QCOMPARE_LE(offInPixels(rig.view, target), 0.01);
}

// Where the edge of the scene holds an axis, the view stays at the edge on that axis, without a timer, and follows
// the target exactly on the other axis.
void TestFollowCamera::holdsAxisAtEdge() {
	Rig rig;
	rig.camera.follow(QPointF(3600.0, 1000.0), true);
	QPointF target(3990.0, 1000.0);
	rig.camera.follow(target, false);
	QVERIFY2(!rig.camera.running(), "a target outside what the view can show starts no timer");
	const QRectF visible = rig.view.mapToScene(rig.view.viewport()->rect()).boundingRect();
	// The view stopped at the edge of the scene, short of the target.
	QCOMPARE_GT(target.x() - centerOf(rig.view).x(), 50.0);
	QCOMPARE_GE(visible.right(), 4000.0 - 2.0 * pixelOf(rig.view));

	// The target now moves along the free axis. The held axis does not move at all, and the free one follows.
	const qreal heldX = centerOf(rig.view).x();
	bool held = true;
	qreal largest = 0.0;
	for (int step = 0; step < 50; ++step) {
		target += QPointF(0.13 * pixelOf(rig.view), 0.71 * pixelOf(rig.view));
		rig.camera.follow(target, false);
		held &= centerOf(rig.view).x() == heldX;
		largest = std::max(largest, std::abs(centerOf(rig.view).y() - target.y()) / pixelOf(rig.view));
	}
	QVERIFY2(held, "the axis at the edge of the scene does not move while the target moves along it");
	// The free axis follows the target next to the held axis.
	QCOMPARE_LE(largest, 0.01);
	QVERIFY2(!rig.camera.running(), "the edge needs no timer");
}

void TestFollowCamera::staysAtFit() {
	Rig rig(1.0);
	rig.camera.follow(QPointF(2000.0, 1000.0), true);
	const QPointF fitted = centerOf(rig.view);
	rig.camera.follow(QPointF(1000.0, 500.0), false);
	// At Fit the view does not move and no timer runs.
	QVERIFY(!rig.camera.running());
	QCOMPARE(centerOf(rig.view), fitted);
}

void TestFollowCamera::settles() {
	Rig rig;
	rig.camera.settle();
	QVERIFY2(!rig.camera.running(), "settle without a target does nothing");

	const QPointF target(1300.0, 1100.0);
	rig.camera.follow(QPointF(1250.0, 1100.0), true);
	rig.pan(-150);
	rig.camera.follow(target, false);
	QVERIFY2(rig.camera.running(), "a catch-up is under way before settle");
	rig.camera.settle();
	// Settle moves the view to the target.
	QCOMPARE_LE(offInPixels(rig.view, target), 0.01);
	QVERIFY2(!rig.camera.running(), "settle ends the catch-up");
}

void TestFollowCamera::reportsMovesOfCamera() {
	Rig rig;
	int byCamera = 0;
	int byUser = 0;
	QObject::connect(&rig.view, &NetworkView::viewportChanged, [&]() {
		if (rig.camera.movingView())
			++byCamera;
		else
			++byUser;
	});
	QVERIFY2(!rig.camera.movingView(), "an idle camera is not moving the view");

	rig.camera.follow(QPointF(1000.0, 1000.0), true);
	// A snap is reported as a move of the camera.
	QCOMPARE(byCamera, 1);
	QCOMPARE(byUser, 0);
	rig.camera.follow(QPointF(1060.0, 1000.0), false);
	// A target is reported as a move of the camera.
	QCOMPARE(byCamera, 2);
	QCOMPARE(byUser, 0);
	rig.pan(-50);
	// A pan of the user is not reported as a move of the camera.
	QCOMPARE(byCamera, 2);
	QCOMPARE(byUser, 1);
	rig.camera.follow(QPointF(1061.0, 1000.0), false);
	const int beforeTick = byCamera;
	rig.tick();
	// A tick of a catch-up is reported as a move of the camera.
	QCOMPARE_GT(byCamera, beforeTick);
	QCOMPARE(byUser, 1);
	const int beforeSettle = byCamera;
	rig.camera.settle();
	// Settle is reported as a move of the camera.
	QCOMPARE(byCamera, beforeSettle + 1);
	QCOMPARE(byUser, 1);
	QVERIFY2(!rig.camera.movingView(), "the camera has stopped moving the view when a call returns");

	rig.view.zoomBy(1.5);
	// A zoom of the user is not reported as a move of the camera.
	QCOMPARE(byCamera, beforeSettle + 1);
	QCOMPARE(byUser, 2);
}

// While the user holds a drag on the view or scrolls it, the view stays where the user puts it and a catch-up ends.
// The first target after the user has finished starts a catch-up from there.
void TestFollowCamera::holdsForUser() {
	Rig rig;
	const qreal pixel = pixelOf(rig.view);
	QPointF target(1300.0, 1000.0);
	rig.camera.follow(target, true);
	rig.pan(-100);
	rig.camera.follow(target, false);
	QVERIFY2(rig.camera.running(), "a catch-up is under way before the drag");

	sendLeftButton(rig.view, QEvent::MouseButtonPress);
	QVERIFY2(rig.view.interacting(), "a held left button on the view is an interaction");
	const QPointF pressed = centerOf(rig.view);
	rig.tick();
	// A tick while the button is held does not move the view and ends the catch-up.
	QCOMPARE_LE(distance(centerOf(rig.view), pressed), 0.01 * pixel);
	QVERIFY(!rig.camera.running());
	// what the drag does to the view
	rig.pan(-80);
	const QPointF dragged = centerOf(rig.view);
	for (int i = 0; i < 20; ++i) {
		rig.now += 16;
		target += QPointF(2.0 * pixel, 0.0);
		rig.camera.follow(target, false);
	}
	// New targets while the button is held do not move the view.
	QCOMPARE_LE(distance(centerOf(rig.view), dragged), 0.01 * pixel);
	QVERIFY(!rig.camera.running());

	sendLeftButton(rig.view, QEvent::MouseButtonRelease);
	QVERIFY2(!rig.view.interacting(), "releasing the button ends the interaction");
	const QPointF left = dragged - target;
	rig.now += 16;
	target += QPointF(2.0 * pixel, 0.0);
	rig.camera.follow(target, false);
	const qreal firstMove = distance(centerOf(rig.view) - target, left) / pixel;
	// The first target after the drag starts a catch-up from where the user left the view.
	QVERIFY(rig.camera.running());
	QCOMPARE_LT(firstMove, 5.0);
	for (int i = 0; i < 40 && rig.camera.running(); ++i)
		rig.tick();
	// The catch-up after the drag ends on the target.
	QVERIFY(!rig.camera.running());
	QCOMPARE_LE(offInPixels(rig.view, target), 0.01);

	// A scroll holds the view until interactionQuietMs after its last event.
	const QPoint at = rig.view.viewport()->rect().center();
	QWheelEvent wheel(QPointF(at), QPointF(rig.view.viewport()->mapToGlobal(at)), QPoint(60, 0), QPoint(0, 0), Qt::NoButton,
		Qt::NoModifier, Qt::ScrollUpdate, false);
	QElapsedTimer sinceScroll;
	sinceScroll.start();
	QApplication::sendEvent(rig.view.viewport(), &wheel);
	const QPointF scrolled = centerOf(rig.view);
	// The scroll moved the view and is an interaction.
	QCOMPARE_GT(offInPixels(rig.view, target), 30.0);
	QVERIFY(rig.view.interacting());
	rig.now += 16;
	target += QPointF(2.0 * pixel, 0.0);
	rig.camera.follow(target, false);
	if (sinceScroll.elapsed() < NetworkView::interactionQuietMs) {
		// A target right after a scroll does not move the view.
		QCOMPARE_LE(distance(centerOf(rig.view), scrolled), 0.01 * pixel);
		QVERIFY(!rig.camera.running());
	}
	while (rig.view.interacting())
		QThread::msleep(10);
	// The scroll ends interactionQuietMs after its last event.
	QCOMPARE_GE(sinceScroll.elapsed(), qint64(NetworkView::interactionQuietMs));
	const QPointF scrolledLeft = scrolled - target;
	rig.now += 16;
	target += QPointF(2.0 * pixel, 0.0);
	rig.camera.follow(target, false);
	const qreal afterScroll = distance(centerOf(rig.view) - target, scrolledLeft) / pixel;
	// The first target after the scroll starts a catch-up from where it left the view.
	QVERIFY(rig.camera.running());
	QCOMPARE_LT(afterScroll, 5.0);

	// A snap while the user holds the view, as when Follow is turned on, cuts at the first target after the user has
	// finished. A move of the pointer without the button ends a drag whose release went elsewhere.
	sendLeftButton(rig.view, QEvent::MouseButtonPress);
	target += QPointF(200.0, 0.0);
	rig.camera.follow(target, true);
	// A snap while the button is held does not move the view.
	QCOMPARE_GT(offInPixels(rig.view, target), 30.0);
	QVERIFY(!rig.camera.running());
	const QPoint inView = rig.view.viewport()->rect().center();
	QMouseEvent move(QEvent::MouseMove, QPointF(inView), QPointF(rig.view.viewport()->mapToGlobal(inView)), Qt::NoButton,
		Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(rig.view.viewport(), &move);
	QVERIFY2(!rig.view.interacting(), "a move of the pointer without the button ends the drag");
	rig.now += 16;
	target += QPointF(2.0 * pixel, 0.0);
	rig.camera.follow(target, false);
	// The first target after the drag cuts to it for the snap.
	QVERIFY(!rig.camera.running());
	QCOMPARE_LE(offInPixels(rig.view, target), 0.01);
}

void TestFollowCamera::stopsWithView() {
	QGraphicsScene scene;
	auto view = std::make_unique<NetworkView>();
	view->setScene(&scene);
	view->resize(800, 600);
	view->show();
	view->fitToBounds(QRectF(0.0, 0.0, 4000.0, 2000.0));
	view->zoomBy(8.0);
	QApplication::processEvents();

	FollowCamera camera(view.get());
	qint64 now = 0;
	camera.setClock([&now]() { return now; });
	camera.follow(QPointF(1000.0, 1000.0), true);
	view->horizontalScrollBar()->setValue(view->horizontalScrollBar()->value() - 100);
	camera.follow(QPointF(1010.0, 1000.0), false);
	QVERIFY2(camera.running(), "a catch-up is under way before the view is destroyed");

	view.reset();
	QVERIFY2(!camera.running(), "destroying the view stops the timer");
	now += 16;
	camera.tick();
	camera.follow(QPointF(1500.0, 1000.0), false);
	camera.follow(QPointF(1600.0, 1000.0), true);
	camera.settle();
	QVERIFY2(!camera.running(), "a camera without a view stays idle");
}

QTEST_MAIN(TestFollowCamera)
#include "test_followcamera.moc"
