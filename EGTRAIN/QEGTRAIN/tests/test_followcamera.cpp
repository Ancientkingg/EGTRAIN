#include "graphics/FollowCamera.h"
#include "graphics/NetworkView.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QGraphicsScene>
#include <QLineF>
#include <QMouseEvent>
#include <QScrollBar>
#include <QThread>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

// The measured numbers go with the message, so that a failure on another platform can be read from its log.
static bool expect(bool condition, const char* message, const std::string& detail = std::string()) {
	if (!condition)
		std::cerr << "failed: " << message << (detail.empty() ? "" : " (" + detail + ")") << "\n";
	return condition;
}

static std::string measured(std::initializer_list<std::pair<const char*, qreal>> values) {
	std::ostringstream text;
	text << std::fixed << std::setprecision(3);
	const char* separator = "";
	for (const auto& value : values) {
		text << separator << value.first << "=" << value.second;
		separator = " ";
	}
	return text.str();
}

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

static bool checkIdle() {
	bool ok = true;
	Rig rig;
	ok &= expect(!rig.camera.running() && !rig.camera.hasTarget(), "a new camera is idle");
	rig.camera.follow(QPointF(std::nan(""), 1000.0), true);
	ok &= expect(!rig.camera.hasTarget(), "a target that is not finite is ignored");
	rig.camera.follow(QPointF(1000.0, 1000.0), true);
	rig.camera.follow(QPointF(1010.0, 1000.0), false);
	ok &= expect(rig.camera.hasTarget() && !rig.camera.running(), "following a target needs no timer");
	rig.pan(-100);
	rig.camera.follow(QPointF(1020.0, 1000.0), false);
	ok &= expect(rig.camera.running(), "a target after a pan starts a catch-up");
	rig.camera.stop();
	ok &= expect(!rig.camera.running() && !rig.camera.hasTarget(), "stop ends the catch-up and forgets the target");
	return ok;
}

// The window passes the drawn centre of the followed train at every step of its glide. The view is on it at every
// step, to a fraction of a pixel, also between whole pixels.
static bool checkFollow() {
	bool ok = true;
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
		ok &= !rig.camera.running();
	}
	ok &= expect(largest <= 0.01, "the view is on the target after every step, to a hundredth of a pixel",
		measured({{"largest", largest}}));
	ok &= expect(!rig.camera.running(), "following a moving target needs no timer");
	return ok;
}

static bool checkSnap() {
	bool ok = true;
	Rig rig;
	const QPointF target(1500.0, 900.0);
	rig.camera.follow(target, true);
	ok &= expect(offInPixels(rig.view, target) <= 0.01, "a snap moves the view at once");
	ok &= expect(!rig.camera.running(), "a snap leaves no timer running");

	rig.camera.stop();
	rig.pan(-200);
	const QPointF first(1200.0, 1000.0);
	rig.camera.follow(first, false);
	ok &= expect(offInPixels(rig.view, first) <= 0.01, "the first target after stop moves the view at once, also after a pan");
	ok &= expect(!rig.camera.running(), "the first target leaves no timer running");

	// A snap during a catch-up ends it.
	rig.pan(-200);
	rig.camera.follow(first + QPointF(5.0, 0.0), false);
	ok &= expect(rig.camera.running(), "a catch-up is under way before the snap");
	const QPointF c(1250.0, 1000.0);
	rig.camera.follow(c, true);
	ok &= expect(offInPixels(rig.view, c) <= 0.01, "a snap moves the view at once during a catch-up");
	ok &= expect(!rig.camera.running(), "a snap ends the catch-up");
	return ok;
}

static bool checkJump() {
	bool ok = true;
	Rig rig;
	rig.camera.follow(QPointF(1000.0, 1000.0), true);
	// a seek of the replay or a train that left the view: further than 1.5 times the larger visible side
	const QPointF seek(3000.0, 1000.0);
	rig.camera.follow(seek, false);
	ok &= expect(offInPixels(rig.view, seek) <= 0.01, "a target far outside the visible scene moves the view at once");
	ok &= expect(!rig.camera.running(), "a far target leaves no timer running");

	rig.pan(-150);
	const QPointF back(1000.0, 1000.0);
	rig.camera.follow(back, false);
	ok &= expect(offInPixels(rig.view, back) <= 0.01 && !rig.camera.running(), "a far target after a pan is a cut, not a catch-up");
	return ok;
}

// After a pan the view keeps the distance from the target that the user left and closes it over catchUpMs, easing in
// and out, while the target moves on.
static bool checkManualPan() {
	bool ok = true;
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
			ok &= expect(fraction > 0.35 && fraction < 0.6, "half way through the catch-up about half of the distance is left",
				measured({{"fraction", fraction}}));
		}
	}
	ok &= expect(firstMove >= 0.0 && firstMove < 5.0, "the catch-up starts from where the pan left the view",
		measured({{"firstMove", firstMove}}));
	ok &= expect(closing, "the distance to the target never grows during the catch-up");
	ok &= expect(previousOff <= 0.01 && !rig.camera.running(), "the catch-up ends on the target and stops its timer",
		measured({{"off", previousOff}}));
	return ok;
}

// A catch-up after a pan also ends while the target stands, as when the train waits at a platform.
static bool checkCatchUpWhileStanding() {
	bool ok = true;
	Rig rig;
	const QPointF target(1300.0, 1000.0);
	rig.camera.follow(target, true);
	rig.pan(150);
	rig.camera.follow(target, false);
	ok &= expect(rig.camera.running(), "a target after a pan starts the timer of the catch-up");
	int ticks = 0;
	while (rig.camera.running() && ticks < 100) {
		rig.tick();
		++ticks;
	}
	const int expected = FollowCamera::catchUpMs / FollowCamera::tickIntervalMs;
	ok &= expect(!rig.camera.running() && ticks >= expected - 1 && ticks <= expected + 1,
		"the timer ends the catch-up in catchUpMs without a new target", measured({{"ticks", ticks}}));
	ok &= expect(offInPixels(rig.view, target) <= 0.01, "the view is back on the target that stands");
	return ok;
}

// Without a new target, as in a paused run, the view stays where the user left it.
static bool checkPausedPan() {
	bool ok = true;
	Rig rig;
	rig.camera.follow(QPointF(1300.0, 1000.0), true);
	rig.pan(-120);
	const QPointF panned = centerOf(rig.view);
	for (int i = 0; i < 40; ++i)
		rig.tick();
	ok &= expect(!rig.camera.running() && distance(centerOf(rig.view), panned) <= 0.01 * pixelOf(rig.view),
		"a pan without a new target leaves the view where the user left it");
	return ok;
}

static bool checkZoom() {
	bool ok = true;
	Rig rig;
	const QPointF target(2000.0, 1000.0);
	rig.camera.follow(target, true);

	rig.view.zoomBy(2.0, QPointF(0.0, 0.0));
	const QPointF zoomed = centerOf(rig.view);
	ok &= expect(distance(zoomed, target) > 20.0, "zooming around a corner moves the view centre");
	rig.now += 16;
	rig.camera.follow(target, false);
	const QPointF after = centerOf(rig.view);
	ok &= expect(distance(after, zoomed) < 0.1 * distance(zoomed, target), "the next target does not undo the zoom anchor");
	ok &= expect(rig.camera.running(), "the next target starts a catch-up");
	for (int i = 0; i < 40 && rig.camera.running(); ++i)
		rig.tick();
	ok &= expect(!rig.camera.running() && offInPixels(rig.view, target) <= 0.01, "the view reaches the target after the zoom");
	return ok;
}

// Where the edge of the scene holds an axis, the view stays at the edge on that axis, without a timer, and follows
// the target exactly on the other axis.
static bool checkEdge() {
	bool ok = true;
	Rig rig;
	rig.camera.follow(QPointF(3600.0, 1000.0), true);
	QPointF target(3990.0, 1000.0);
	rig.camera.follow(target, false);
	ok &= expect(!rig.camera.running(), "a target outside what the view can show starts no timer");
	const QRectF visible = rig.view.mapToScene(rig.view.viewport()->rect()).boundingRect();
	ok &= expect(target.x() - centerOf(rig.view).x() > 50.0 && visible.right() >= 4000.0 - 2.0 * pixelOf(rig.view),
		"the view stopped at the edge of the scene, short of the target");

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
	ok &= expect(held, "the axis at the edge of the scene does not move while the target moves along it");
	ok &= expect(largest <= 0.01, "the free axis follows the target next to the held axis", measured({{"largest", largest}}));
	ok &= expect(!rig.camera.running(), "the edge needs no timer");
	return ok;
}

static bool checkFit() {
	bool ok = true;
	Rig rig(1.0);
	rig.camera.follow(QPointF(2000.0, 1000.0), true);
	const QPointF fitted = centerOf(rig.view);
	rig.camera.follow(QPointF(1000.0, 500.0), false);
	ok &= expect(!rig.camera.running() && centerOf(rig.view) == fitted, "at Fit the view does not move and no timer runs");
	return ok;
}

static bool checkSettle() {
	bool ok = true;
	Rig rig;
	rig.camera.settle();
	ok &= expect(!rig.camera.running(), "settle without a target does nothing");

	const QPointF target(1300.0, 1100.0);
	rig.camera.follow(QPointF(1250.0, 1100.0), true);
	rig.pan(-150);
	rig.camera.follow(target, false);
	ok &= expect(rig.camera.running(), "a catch-up is under way before settle");
	rig.camera.settle();
	ok &= expect(offInPixels(rig.view, target) <= 0.01, "settle moves the view to the target");
	ok &= expect(!rig.camera.running(), "settle ends the catch-up");
	return ok;
}

static bool checkMovingView() {
	bool ok = true;
	Rig rig;
	int byCamera = 0;
	int byUser = 0;
	QObject::connect(&rig.view, &NetworkView::viewportChanged, [&]() {
		if (rig.camera.movingView())
			++byCamera;
		else
			++byUser;
	});
	ok &= expect(!rig.camera.movingView(), "an idle camera is not moving the view");

	rig.camera.follow(QPointF(1000.0, 1000.0), true);
	ok &= expect(byCamera == 1 && byUser == 0, "a snap is reported as a move of the camera");
	rig.camera.follow(QPointF(1060.0, 1000.0), false);
	ok &= expect(byCamera == 2 && byUser == 0, "a target is reported as a move of the camera");
	rig.pan(-50);
	ok &= expect(byCamera == 2 && byUser == 1, "a pan of the user is not reported as a move of the camera");
	rig.camera.follow(QPointF(1061.0, 1000.0), false);
	const int beforeTick = byCamera;
	rig.tick();
	ok &= expect(byCamera > beforeTick && byUser == 1, "a tick of a catch-up is reported as a move of the camera");
	const int beforeSettle = byCamera;
	rig.camera.settle();
	ok &= expect(byCamera == beforeSettle + 1 && byUser == 1, "settle is reported as a move of the camera");
	ok &= expect(!rig.camera.movingView(), "the camera has stopped moving the view when a call returns");

	rig.view.zoomBy(1.5);
	ok &= expect(byCamera == beforeSettle + 1 && byUser == 2, "a zoom of the user is not reported as a move of the camera");
	return ok;
}

// While the user holds a drag on the view or scrolls it, the view stays where the user puts it and a catch-up ends.
// The first target after the user has finished starts a catch-up from there.
static bool checkHoldForUser() {
	bool ok = true;
	Rig rig;
	const qreal pixel = pixelOf(rig.view);
	QPointF target(1300.0, 1000.0);
	rig.camera.follow(target, true);
	rig.pan(-100);
	rig.camera.follow(target, false);
	ok &= expect(rig.camera.running(), "a catch-up is under way before the drag");

	sendLeftButton(rig.view, QEvent::MouseButtonPress);
	ok &= expect(rig.view.interacting(), "a held left button on the view is an interaction");
	const QPointF pressed = centerOf(rig.view);
	rig.tick();
	ok &= expect(distance(centerOf(rig.view), pressed) <= 0.01 * pixel && !rig.camera.running(),
		"a tick while the button is held does not move the view and ends the catch-up");
	// what the drag does to the view
	rig.pan(-80);
	const QPointF dragged = centerOf(rig.view);
	for (int i = 0; i < 20; ++i) {
		rig.now += 16;
		target += QPointF(2.0 * pixel, 0.0);
		rig.camera.follow(target, false);
	}
	ok &= expect(distance(centerOf(rig.view), dragged) <= 0.01 * pixel && !rig.camera.running(),
		"new targets while the button is held do not move the view", measured({{"moved", distance(centerOf(rig.view), dragged) / pixel}}));

	sendLeftButton(rig.view, QEvent::MouseButtonRelease);
	ok &= expect(!rig.view.interacting(), "releasing the button ends the interaction");
	const QPointF left = dragged - target;
	rig.now += 16;
	target += QPointF(2.0 * pixel, 0.0);
	rig.camera.follow(target, false);
	const qreal firstMove = distance(centerOf(rig.view) - target, left) / pixel;
	ok &= expect(rig.camera.running() && firstMove < 5.0, "the first target after the drag starts a catch-up from where the user left the view",
		measured({{"firstMove", firstMove}}));
	for (int i = 0; i < 40 && rig.camera.running(); ++i)
		rig.tick();
	ok &= expect(!rig.camera.running() && offInPixels(rig.view, target) <= 0.01, "the catch-up after the drag ends on the target");

	// A scroll holds the view until interactionQuietMs after its last event.
	const QPoint at = rig.view.viewport()->rect().center();
	QWheelEvent wheel(QPointF(at), QPointF(rig.view.viewport()->mapToGlobal(at)), QPoint(60, 0), QPoint(0, 0), Qt::NoButton,
		Qt::NoModifier, Qt::ScrollUpdate, false);
	QElapsedTimer sinceScroll;
	sinceScroll.start();
	QApplication::sendEvent(rig.view.viewport(), &wheel);
	const QPointF scrolled = centerOf(rig.view);
	ok &= expect(offInPixels(rig.view, target) > 30.0 && rig.view.interacting(), "the scroll moved the view and is an interaction");
	rig.now += 16;
	target += QPointF(2.0 * pixel, 0.0);
	rig.camera.follow(target, false);
	if (sinceScroll.elapsed() < NetworkView::interactionQuietMs)
		ok &= expect(distance(centerOf(rig.view), scrolled) <= 0.01 * pixel && !rig.camera.running(),
			"a target right after a scroll does not move the view");
	while (rig.view.interacting())
		QThread::msleep(10);
	ok &= expect(sinceScroll.elapsed() >= NetworkView::interactionQuietMs, "the scroll ends interactionQuietMs after its last event",
		measured({{"elapsed", qreal(sinceScroll.elapsed())}}));
	const QPointF scrolledLeft = scrolled - target;
	rig.now += 16;
	target += QPointF(2.0 * pixel, 0.0);
	rig.camera.follow(target, false);
	const qreal afterScroll = distance(centerOf(rig.view) - target, scrolledLeft) / pixel;
	ok &= expect(rig.camera.running() && afterScroll < 5.0, "the first target after the scroll starts a catch-up from where it left the view",
		measured({{"firstMove", afterScroll}}));

	// A snap while the user holds the view, as when Follow is turned on, cuts at the first target after the user has
	// finished. A move of the pointer without the button ends a drag whose release went elsewhere.
	sendLeftButton(rig.view, QEvent::MouseButtonPress);
	target += QPointF(200.0, 0.0);
	rig.camera.follow(target, true);
	ok &= expect(offInPixels(rig.view, target) > 30.0 && !rig.camera.running(), "a snap while the button is held does not move the view");
	const QPoint inView = rig.view.viewport()->rect().center();
	QMouseEvent move(QEvent::MouseMove, QPointF(inView), QPointF(rig.view.viewport()->mapToGlobal(inView)), Qt::NoButton,
		Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(rig.view.viewport(), &move);
	ok &= expect(!rig.view.interacting(), "a move of the pointer without the button ends the drag");
	rig.now += 16;
	target += QPointF(2.0 * pixel, 0.0);
	rig.camera.follow(target, false);
	ok &= expect(!rig.camera.running() && offInPixels(rig.view, target) <= 0.01, "the first target after the drag cuts to it for the snap");
	return ok;
}

static bool checkViewDestroyed() {
	bool ok = true;
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
	ok &= expect(camera.running(), "a catch-up is under way before the view is destroyed");

	view.reset();
	ok &= expect(!camera.running(), "destroying the view stops the timer");
	now += 16;
	camera.tick();
	camera.follow(QPointF(1500.0, 1000.0), false);
	camera.follow(QPointF(1600.0, 1000.0), true);
	camera.settle();
	ok &= expect(!camera.running(), "a camera without a view stays idle");
	return ok;
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = true;
	ok &= checkIdle();
	ok &= checkFollow();
	ok &= checkSnap();
	ok &= checkJump();
	ok &= checkManualPan();
	ok &= checkCatchUpWhileStanding();
	ok &= checkPausedPan();
	ok &= checkZoom();
	ok &= checkEdge();
	ok &= checkFit();
	ok &= checkSettle();
	ok &= checkMovingView();
	ok &= checkHoldForUser();
	ok &= checkViewDestroyed();
	return ok ? 0 : 1;
}
