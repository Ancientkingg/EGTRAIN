#include "graphics/FollowCamera.h"
#include "graphics/NetworkView.h"

#include <QApplication>
#include <QGraphicsScene>
#include <QLineF>
#include <QScrollBar>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static QPointF centerOf(const NetworkView& view) {
	return view.mapToScene(view.viewport()->rect().center());
}

static qreal distance(const QPointF& from, const QPointF& to) {
	return QLineF(from, to).length();
}

// One device pixel in scene units.
static qreal pixelOf(const NetworkView& view) {
	return 1.0 / std::abs(view.transform().m11());
}

static qreal maxLagOf(const NetworkView& view) {
	const QSizeF visible = view.mapToScene(view.viewport()->rect()).boundingRect().size();
	return 0.3 * std::min(visible.width(), visible.height());
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

	// Runs ticks until the timer stops. Returns the number of ticks, or -1 when it did not stop.
	int tickUntilStopped(int limit = 200) {
		for (int i = 1; i <= limit; ++i) {
			tick();
			if (!camera.running())
				return i;
		}
		return -1;
	}
};

static bool checkIdle() {
	bool ok = true;
	Rig rig;
	ok &= expect(!rig.camera.running() && !rig.camera.hasTarget(), "a new camera is idle");

	const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
	rig.camera.follow(QPointF(nan, 10.0), false);
	rig.camera.follow(QPointF(10.0, std::numeric_limits<qreal>::infinity()), true);
	ok &= expect(!rig.camera.hasTarget(), "a target that is not finite is ignored");

	rig.camera.follow(QPointF(1000.0, 1000.0), true);
	rig.camera.follow(QPointF(1100.0, 1000.0), false);
	ok &= expect(rig.camera.running(), "a glide starts the timer");
	rig.camera.stop();
	ok &= expect(!rig.camera.running() && !rig.camera.hasTarget(), "stop ends the timer and forgets the target");
	return ok;
}

static bool checkSnap() {
	bool ok = true;
	Rig rig;
	const QPointF target(1234.0, 876.0);
	rig.camera.follow(target, true);
	ok &= expect(distance(centerOf(rig.view), target) <= 3.0 * pixelOf(rig.view), "a snap moves the view at once");
	ok &= expect(!rig.camera.running(), "a snap leaves no timer running");

	rig.camera.stop();
	const QPointF first(2500.0, 1200.0);
	rig.camera.follow(first, false);
	ok &= expect(distance(centerOf(rig.view), first) <= 3.0 * pixelOf(rig.view), "the first target after stop moves the view at once");
	ok &= expect(!rig.camera.running(), "the first target leaves no timer running");

	// A snap to a target well inside the visible scene (consecutive replay frames) does not glide.
	rig.camera.stop();
	const QPointF a(2000.0, 1000.0);
	rig.camera.follow(a, true);
	rig.camera.follow(a + QPointF(80.0, 0.0), false);
	ok &= expect(rig.camera.running(), "a near target starts a glide");
	const QPointF c = a + QPointF(0.0, 100.0);
	rig.camera.follow(c, true);
	ok &= expect(distance(centerOf(rig.view), c) <= 3.0 * pixelOf(rig.view), "a snap to a near target moves the view at once");
	ok &= expect(!rig.camera.running(), "a snap to a near target stops the glide");
	return ok;
}

static bool checkApproach() {
	bool ok = true;
	Rig rig;
	const QPointF start(2000.0, 1000.0);
	const QPointF target(2080.0, 1040.0);
	rig.camera.follow(start, true);
	rig.camera.follow(target, false);
	ok &= expect(rig.camera.running(), "the timer runs while the target is not reached");

	qreal previous = distance(centerOf(rig.view), target);
	const qreal initial = previous;
	bool monotonic = true;
	int ticks = 0;
	for (; ticks < 200 && rig.camera.running(); ++ticks) {
		rig.tick();
		const qreal current = distance(centerOf(rig.view), target);
		if (current > previous + 1e-6)
			monotonic = false;
		if (ticks == 0)
			ok &= expect(current < initial && current > 0.5 * pixelOf(rig.view), "the first tick moves part of the way");
		previous = current;
	}
	ok &= expect(monotonic, "the distance to the target never grows");
	ok &= expect(!rig.camera.running() && ticks > 3 && ticks < 100, "the timer stops after a bounded number of ticks");
	ok &= expect(previous <= 3.0 * pixelOf(rig.view), "the view reaches the target");
	return ok;
}

static bool checkLagCap() {
	bool ok = true;
	Rig rig;
	const QPointF start(2000.0, 1000.0);
	const QPointF target(2300.0, 1000.0);
	rig.camera.follow(start, true);
	rig.camera.follow(target, false);
	rig.tick();
	const QPointF moved = centerOf(rig.view);
	ok &= expect(moved.x() > start.x() + 10.0, "a target beyond the lag cap pulls the view along");
	ok &= expect(distance(moved, target) <= maxLagOf(rig.view) + 3.0 * pixelOf(rig.view), "the view stays within the lag cap of the target");
	ok &= expect(rig.tickUntilStopped() > 0, "the timer stops");
	ok &= expect(distance(centerOf(rig.view), target) <= 3.0 * pixelOf(rig.view), "the view reaches the target");
	return ok;
}

static bool checkFastStream() {
	bool ok = true;
	Rig rig;
	QPointF target(500.0, 1000.0);
	rig.camera.follow(target, true);
	const qreal lag = maxLagOf(rig.view) + 3.0 * pixelOf(rig.view);
	bool withinLag = true;
	bool forward = true;
	qreal previousX = centerOf(rig.view).x();
	// A target every 54 ms and a tick every 27 ms.
	for (int i = 0; i < 40; ++i) {
		rig.now += 27;
		if (i % 2 == 0) {
			target.rx() += 155.0;
			rig.camera.follow(target, false);
		}
		rig.camera.tick();
		const QPointF center = centerOf(rig.view);
		withinLag &= distance(center, target) <= lag;
		forward &= center.x() >= previousX - 1e-6;
		previousX = center.x();
	}
	ok &= expect(withinLag, "a fast stream of targets never leaves the view further behind than the lag cap");
	ok &= expect(forward, "the view only moves forward along the stream");
	ok &= expect(rig.tickUntilStopped() > 0, "the timer stops after the stream ends");
	ok &= expect(distance(centerOf(rig.view), target) <= 3.0 * pixelOf(rig.view), "the view reaches the last target");
	return ok;
}

// How far the first tick moves the view towards a target 60 units away, as a fraction of that
// distance, after the targets arrived with the given gaps. The gaps add up to more than the
// longest step a tick takes, so the tick always takes that step and the fraction tells the
// time constant of the approach.
static qreal firstTickFraction(const std::vector<qint64>& gaps, qint64 tickAfter) {
	Rig rig;
	const QPointF start(2000.0, 1000.0);
	rig.camera.follow(start, true);
	const int updates = int(gaps.size());
	for (int i = 0; i < updates; ++i) {
		rig.now += gaps[i];
		rig.camera.follow(start + QPointF(60.0 * (i + 1) / updates, 0.0), false);
	}
	rig.now += tickAfter;
	rig.camera.tick();
	return (centerOf(rig.view).x() - start.x()) / 60.0;
}

static bool checkUpdateInterval() {
	bool ok = true;
	// A step of 250 ms with a time constant of 250 ms covers 63 percent, with 60 ms 98 percent.
	const qreal slow = firstTickFraction(std::vector<qint64>(6, 500), 250);
	const qreal fast = firstTickFraction(std::vector<qint64>(6, 100), 250);
	ok &= expect(slow > 0.58 && slow < 0.68, "targets every 500 ms give the slowest approach");
	ok &= expect(fast > 0.93, "targets every 100 ms give a faster approach");

	std::vector<qint64> paused(6, 100);
	paused.push_back(5000);
	ok &= expect(firstTickFraction(paused, 250) > 0.93, "a pause between targets does not count as an update interval");

	// One target 33 ms after the first gives a time constant of 133 ms: a step of 250 ms covers
	// 85 percent, the whole 1000 ms would cover all of it.
	const qreal clamped = firstTickFraction(std::vector<qint64>(1, 33), 1000);
	ok &= expect(clamped > 0.78 && clamped < 0.92, "a tick after a stalled event loop takes a step of 250 ms");
	return ok;
}

static bool checkSeek() {
	bool ok = true;
	Rig rig;
	rig.camera.follow(QPointF(500.0, 1000.0), true);
	rig.camera.follow(QPointF(700.0, 1000.0), false);
	rig.tick();
	ok &= expect(rig.camera.running(), "a glide is under way before the seek");

	const QPointF seek(3500.0, 1500.0);
	rig.camera.follow(seek, true);
	ok &= expect(distance(centerOf(rig.view), seek) <= 3.0 * pixelOf(rig.view), "a snap to a far target leaves no glide");
	ok &= expect(!rig.camera.running(), "a snap stops the glide that was under way");

	const QPointF back(300.0, 400.0);
	rig.camera.follow(back, false);
	ok &= expect(distance(centerOf(rig.view), back) <= 3.0 * pixelOf(rig.view), "a glide towards a target far outside the visible scene moves at once");
	ok &= expect(!rig.camera.running(), "a far target leaves no timer running");
	return ok;
}

static bool checkManualPan() {
	bool ok = true;
	Rig rig;
	const QPointF target(1300.0, 1000.0);
	rig.camera.follow(QPointF(1000.0, 1000.0), true);
	rig.camera.follow(target, false);
	for (int i = 0; i < 3; ++i)
		rig.tick();

	QScrollBar* bar = rig.view.horizontalScrollBar();
	bar->setValue(bar->value() - 450);
	const QPointF panned = centerOf(rig.view);
	const qreal pannedDistance = distance(panned, target);
	rig.tick();
	const QPointF after = centerOf(rig.view);
	ok &= expect(rig.camera.running(), "the glide continues after a manual pan");
	ok &= expect(distance(after, target) < pannedDistance, "Follow returns towards the target after a manual pan");
	ok &= expect(distance(after, panned) < 0.5 * pannedDistance, "Follow returns from where the pan left the view");
	ok &= expect(rig.tickUntilStopped() > 0, "the timer stops");
	ok &= expect(distance(centerOf(rig.view), target) <= 3.0 * pixelOf(rig.view), "the view reaches the target after the pan");
	return ok;
}

static bool checkZoom() {
	bool ok = true;
	Rig rig;
	const QPointF before(2000.0, 1000.0);
	const QPointF target = before + QPointF(60.0, 0.0);
	rig.camera.follow(before, true);
	rig.camera.follow(target, false);
	ok &= expect(rig.camera.running(), "the glide is under way before the zoom");

	rig.view.zoomBy(2.0, QPointF(0.0, 0.0));
	const QPointF zoomed = centerOf(rig.view);
	ok &= expect(distance(zoomed, before) > 20.0, "zooming around a corner moves the view centre");
	rig.tick();
	const QPointF after = centerOf(rig.view);
	ok &= expect(distance(after, zoomed) < 0.5 * distance(zoomed, before), "the next tick does not undo the zoom anchor");
	ok &= expect(distance(after, target) < distance(zoomed, target), "the next tick moves towards the target");
	ok &= expect(rig.tickUntilStopped() > 0, "the timer stops");
	ok &= expect(distance(centerOf(rig.view), target) <= 3.0 * pixelOf(rig.view), "the view reaches the target after the zoom");
	return ok;
}

static bool checkEdge() {
	bool ok = true;
	Rig rig;
	rig.camera.follow(QPointF(3600.0, 1000.0), true);
	const QPointF target(3990.0, 1000.0);
	rig.camera.follow(target, false);
	const int ticks = rig.tickUntilStopped(10);
	ok &= expect(ticks > 0, "the timer stops although the target lies outside the scene");

	const QPointF stopped = centerOf(rig.view);
	rig.view.centerOn(target);
	const QPointF limit = centerOf(rig.view);
	ok &= expect(distance(limit, target) > 50.0, "the target is outside what the view can show");
	ok &= expect(distance(stopped, limit) <= 3.0 * pixelOf(rig.view), "the view stopped at the edge of the scene");
	return ok;
}

static bool checkEdgeFreeAxis() {
	bool ok = true;
	Rig rig;
	rig.camera.follow(QPointF(3600.0, 1000.0), true);
	rig.camera.follow(QPointF(3990.0, 1000.0), false);
	ok &= expect(rig.tickUntilStopped(10) > 0, "the timer stops at the right edge");

	// The target now also moves along the free axis. The clamped axis does not hold it back.
	rig.camera.follow(QPointF(3990.0, 1060.0), false);
	ok &= expect(rig.camera.running(), "a target that moves along the free axis restarts the timer");
	rig.tick();
	const qreal fraction = (centerOf(rig.view).y() - 1000.0) / 60.0;
	ok &= expect(fraction > 0.05 && fraction < 0.3, "the free axis approaches smoothly next to a clamped axis");
	ok &= expect(rig.tickUntilStopped(100) > 0, "the timer stops");
	ok &= expect(std::abs(centerOf(rig.view).y() - 1060.0) <= 3.0 * pixelOf(rig.view), "the free axis reaches the target");
	return ok;
}

static bool checkFit() {
	bool ok = true;
	Rig rig(1.0);
	rig.camera.follow(QPointF(2000.0, 1000.0), true);
	rig.camera.follow(QPointF(1000.0, 500.0), false);
	ok &= expect(rig.tickUntilStopped(10) > 0, "the timer stops at Fit, where the view cannot move");
	return ok;
}

static bool checkSettle() {
	bool ok = true;
	Rig rig;
	rig.camera.settle();
	ok &= expect(!rig.camera.running(), "settle without a target does nothing");

	const QPointF target(1300.0, 1100.0);
	rig.camera.follow(QPointF(1000.0, 1000.0), true);
	rig.camera.follow(target, false);
	ok &= expect(rig.camera.running(), "the glide is under way before settle");
	rig.camera.settle();
	ok &= expect(distance(centerOf(rig.view), target) <= 3.0 * pixelOf(rig.view), "settle moves the view to the target");
	ok &= expect(!rig.camera.running(), "settle stops the timer");
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
	camera.follow(QPointF(1300.0, 1000.0), false);
	ok &= expect(camera.running(), "the glide is under way before the view is destroyed");

	view.reset();
	ok &= expect(!camera.running(), "destroying the view stops the timer");
	now += 33;
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
	ok &= checkSnap();
	ok &= checkApproach();
	ok &= checkLagCap();
	ok &= checkFastStream();
	ok &= checkUpdateInterval();
	ok &= checkSeek();
	ok &= checkManualPan();
	ok &= checkZoom();
	ok &= checkEdge();
	ok &= checkEdgeFreeAxis();
	ok &= checkFit();
	ok &= checkSettle();
	ok &= checkViewDestroyed();
	return ok ? 0 : 1;
}
