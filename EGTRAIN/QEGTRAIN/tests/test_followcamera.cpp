#include "graphics/FollowCamera.h"
#include "graphics/NetworkView.h"

#include <QApplication>
#include <QGraphicsScene>
#include <QLineF>
#include <QScrollBar>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// The measured numbers go with the message, so that a failure on another platform can be read from its log.
static bool expect(bool condition, const char* message, const std::string& detail = std::string()) {
	if (!condition)
		std::cerr << "failed: " << message << (detail.empty() ? "" : " (" + detail + ")") << "\n";
	return condition;
}

static std::string measured(std::initializer_list<std::pair<const char*, qreal>> values) {
	std::ostringstream text;
	text << std::fixed << std::setprecision(2);
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

// Where the targets of a stream lie along y.
static const qreal kStreamY = 1000.0;

// The view and the target along x after one tick of the camera, and whether its timer still runs.
struct TickRecord {
	qint64 ms;
	qreal center;
	qreal target;
	bool running;
};

// Delivers a stream of updates the way the window and the timer of the camera do. The target moves
// along x to the right by a step given in device pixels, so that the rounding of the view to whole
// pixels is the same small part of the step in every stream. The timer ticks every tickIntervalMs from
// the moment it starts, and an update goes before a tick on the same millisecond. The event loop does
// not run: only the clock of the rig drives the ticks.
struct Stream {
	Rig& rig;
	const qreal pixel;
	qreal target;
	qint64 nextTickMs = 0;
	std::vector<qint64> updateMs;
	std::vector<TickRecord> ticks;

	Stream(Rig& source, qreal startX)
		: rig(source), pixel(pixelOf(source.view)), target(startX) {
		rig.camera.stop();
		rig.now = 0;
		rig.camera.follow(QPointF(target, kStreamY), true);
		updateMs.push_back(0);
	}

	void tickOnce() {
		rig.now = nextTickMs;
		rig.camera.tick();
		nextTickMs += FollowCamera::tickIntervalMs;
		ticks.push_back({rig.now, centerOf(rig.view).x(), target, rig.camera.running()});
	}

	// Runs the ticks that fall before the given time while the timer runs.
	void tickBefore(qint64 ms) {
		while (rig.camera.running() && nextTickMs < ms)
			tickOnce();
	}

	// Runs the ticks until the timer stops. Returns false when it does not stop.
	bool runOut(int limit = 2000) {
		for (int i = 0; i < limit && rig.camera.running(); ++i)
			tickOnce();
		return !rig.camera.running();
	}

	// An update gapMs after the previous one that moves the target by stepPx.
	void update(qint64 gapMs, qreal stepPx) {
		const qint64 ms = updateMs.back() + gapMs;
		tickBefore(ms);
		rig.now = ms;
		target += stepPx * pixel;
		const bool wasRunning = rig.camera.running();
		rig.camera.follow(QPointF(target, kStreamY), false);
		if (!wasRunning && rig.camera.running())
			nextTickMs = ms + FollowCamera::tickIntervalMs;
		updateMs.push_back(ms);
	}

	// The moves of the view per tick in device pixels, for the ticks from fromMs up to toMs.
	std::vector<qreal> moves(qint64 fromMs, qint64 toMs) const {
		std::vector<qreal> result;
		for (std::size_t i = 1; i < ticks.size(); ++i) {
			if (ticks[i].ms >= fromMs && ticks[i].ms < toMs)
				result.push_back((ticks[i].center - ticks[i - 1].center) / pixel);
		}
		return result;
	}

	// The time of the first tick from afterMs on that leaves the timer stopped, or -1.
	qint64 stopMs(qint64 afterMs) const {
		for (const TickRecord& tick : ticks) {
			if (tick.ms >= afterMs && !tick.running)
				return tick.ms;
		}
		return -1;
	}

	// The largest distance the view lags behind the target, in device pixels.
	qreal largestLag() const {
		qreal lag = 0.0;
		for (const TickRecord& tick : ticks)
			lag = std::max(lag, (tick.target - tick.center) / pixel);
		return lag;
	}

	// The largest distance the view is beyond the target, in device pixels.
	qreal largestOvershoot() const {
		qreal overshoot = 0.0;
		for (const TickRecord& tick : ticks)
			overshoot = std::max(overshoot, (tick.center - tick.target) / pixel);
		return overshoot;
	}

	// How far the view is from the last target, in device pixels.
	qreal offTarget() const {
		return distance(centerOf(rig.view), QPointF(target, kStreamY)) / pixel;
	}
};

struct MoveStats {
	int count = 0;
	qreal smallest = 0.0;
	qreal largest = 0.0;
	qreal mean = 0.0;

	qreal ratio() const { return smallest > 0.0 ? largest / smallest : std::numeric_limits<qreal>::infinity(); }
};

static MoveStats statsOf(const std::vector<qreal>& moves) {
	MoveStats stats;
	if (moves.empty())
		return stats;
	stats.count = int(moves.size());
	stats.smallest = *std::min_element(moves.begin(), moves.end());
	stats.largest = *std::max_element(moves.begin(), moves.end());
	stats.mean = std::accumulate(moves.begin(), moves.end(), 0.0) / qreal(moves.size());
	return stats;
}

static std::string describe(const MoveStats& stats) {
	return measured({{"ticks", qreal(stats.count)}, {"smallest", stats.smallest}, {"largest", stats.largest}, {"ratio", stats.ratio()}, {"mean", stats.mean}});
}

// The view stays behind the target by no more than the lag cap, and does not pass it.
static bool expectBehindTarget(const Stream& stream, const std::string& label) {
	const qreal lag = stream.largestLag();
	const qreal cap = maxLagOf(stream.rig.view) / stream.pixel;
	bool ok = expect(lag <= cap + 3.0, "the view stays within the lag cap of the target", label + " " + measured({{"lag", lag}, {"cap", cap}}));
	const qreal past = stream.largestOvershoot();
	ok &= expect(past <= 2.0, "the view does not pass the target", label + " " + measured({{"overshoot", past}}));
	return ok;
}

static bool expectOnTarget(const Stream& stream, const char* message, const std::string& label) {
	const qreal off = stream.offTarget();
	return expect(off <= 3.0, message, label + " " + measured({{"off", off}}));
}

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

// Regular updates of a train at constant speed move the view at one speed, between the updates and
// across them. The first updates are skipped: the estimate of the update interval starts at a default.
static bool checkEvenGlideOf(Rig& rig, qint64 gapMs, qreal stepPx, int updates, int skipped, qint64 minSpanMs) {
	bool ok = true;
	Stream stream(rig, 500.0);
	for (int i = 1; i < updates; ++i)
		stream.update(gapMs, stepPx);
	const qint64 from = stream.updateMs[std::size_t(skipped)];
	const MoveStats moves = statsOf(stream.moves(from, stream.updateMs.back()));
	const std::string label = "gap " + std::to_string(gapMs) + " ms, " + describe(moves);
	ok &= expect(stream.updateMs.back() - from >= minSpanMs, "the measured part of the stream is long enough", label);
	ok &= expect(moves.count > 0 && moves.smallest > 0.0, "the view moves in every tick between and across updates", label);
	ok &= expect(moves.largest <= 1.25 * moves.smallest, "the largest move per tick is at most 1.25 times the smallest", label);
	ok &= expectBehindTarget(stream, label);
	return ok;
}

static bool checkEvenGlide() {
	Rig rig;
	bool ok = checkEvenGlideOf(rig, 500, 120.0, 17, 3, 5000);
	ok &= checkEvenGlideOf(rig, 40, 21.0, 51, 12, 1000);
	return ok;
}

// After the last update the view reaches the target one glide later, and the timer stops. The glide takes
// the gap between updates, and at least two timer steps. The last update meets different offsets of the
// tick grid with the number of updates.
static bool checkEndOfUpdatesOf(Rig& rig, qint64 gapMs, qreal stepPx, int updates) {
	Stream stream(rig, 500.0);
	for (int i = 1; i < updates; ++i)
		stream.update(gapMs, stepPx);
	const bool stopped = stream.runOut();
	const qint64 last = stream.updateMs.back();
	const qint64 glideMs = std::max<qint64>(gapMs, 2 * FollowCamera::tickIntervalMs);
	const qint64 delay = stream.stopMs(last) - last;
	const qint64 earliest = glideMs - 2;
	const qint64 latest = glideMs + FollowCamera::tickIntervalMs + 1;
	const std::string label = "gap " + std::to_string(gapMs) + " ms, " + std::to_string(updates) + " updates, the timer stopped " + std::to_string(delay)
		+ " ms after the last update, expected " + std::to_string(earliest) + " to " + std::to_string(latest);

	bool ok = true;
	ok &= expect(stopped && delay >= earliest && delay <= latest, "the timer stops one glide after the last update", label);
	ok &= expectOnTarget(stream, "the view ends on the last target", label);
	ok &= expect(stream.largestOvershoot() <= 2.0, "the view does not pass the target", label);
	return ok;
}

static bool checkEndOfUpdates() {
	Rig rig;
	bool ok = true;
	for (int updates = 24; updates <= 32; ++updates) {
		ok &= checkEndOfUpdatesOf(rig, 500, 120.0, updates);
		ok &= checkEndOfUpdatesOf(rig, 100, 120.0, updates);
		ok &= checkEndOfUpdatesOf(rig, 40, 21.0, updates);
	}
	return ok;
}

// A change of the step takes effect at once: the glide of the update with the new step covers the new
// distance in the same time. The even move per tick follows from the step and the gap of 500 ms.
static bool checkSpeedChangeOf(const char* name, qreal firstStepPx, qreal secondStepPx) {
	bool ok = true;
	Rig rig;
	Stream stream(rig, 500.0);
	for (int i = 0; i < 12; ++i)
		stream.update(500, firstStepPx);
	const std::size_t change = stream.updateMs.size();
	for (int i = 0; i < 8; ++i)
		stream.update(500, secondStepPx);
	stream.runOut();

	const qint64 from = stream.updateMs[change];
	const qreal even = secondStepPx * FollowCamera::tickIntervalMs / 500.0;
	const std::vector<qreal> interval = stream.moves(from, from + 500);
	const MoveStats first = statsOf(interval);
	const MoveStats later = statsOf(std::vector<qreal>(interval.begin() + (interval.empty() ? 0 : 1), interval.end()));
	const std::string label = std::string(name) + ": " + measured({{"even", even}}) + ", " + describe(first) + ", from the second tick "
		+ measured({{"mean", later.mean}});
	ok &= expect(first.count > 2, "the interval of the new step has ticks", label);
	ok &= expect(first.largest <= 1.3 * even + 1.0, "no tick after the change moves the view much more than the new even move", label);
	ok &= expect(std::abs(later.mean - even) <= 0.15 * even, "the mean move after the change is the new even move", label);
	ok &= expect(stream.largestOvershoot() <= 2.0, "the view does not pass the target", label);
	return ok;
}

// After the last moving update the updates go on with an unchanged target. The view finishes with moves
// of a pixel or less and then stands.
static bool checkTrainStands() {
	bool ok = true;
	Rig rig;
	Stream stream(rig, 500.0);
	for (int i = 0; i < 12; ++i)
		stream.update(500, 80.0);
	const qint64 lastMoving = stream.updateMs.back();
	for (int i = 0; i < 8; ++i)
		stream.update(500, 0.0);
	stream.runOut();

	const qint64 end = std::numeric_limits<qint64>::max();
	const MoveStats small = statsOf(stream.moves(lastMoving + 500 + 2 * FollowCamera::tickIntervalMs, end));
	const MoveStats still = statsOf(stream.moves(lastMoving + 1500 + 2 * FollowCamera::tickIntervalMs, end));
	const std::string label = "from one interval on " + describe(small) + "; from three intervals on " + describe(still);
	ok &= expect(stream.largestOvershoot() <= 2.0, "a train that stands is not passed by the view", label);
	ok &= expect(small.largest <= 1.01 && small.smallest >= -1.01, "one interval after the last move no tick moves the view by more than a pixel", label);
	ok &= expect(still.largest < 0.01 && still.smallest > -0.01, "three intervals after the last move the view does not move", label);
	ok &= expectOnTarget(stream, "the view is on the target of the train that stands", label);
	return ok;
}

static bool checkSpeedChange() {
	bool ok = checkSpeedChangeOf("a step that doubles", 80.0, 160.0);
	ok &= checkSpeedChangeOf("a step that halves", 160.0, 80.0);
	ok &= checkTrainStands();
	return ok;
}

// Updates that come a little early or late do not make the view surge: the largest move per tick stays
// close to the mean, and the view never passes the target.
static bool checkIrregularGaps() {
	bool ok = true;
	Rig rig;
	Stream stream(rig, 500.0);
	for (int i = 1; i < 25; ++i)
		stream.update(i % 2 == 1 ? 450 : 550, 120.0);
	const MoveStats moves = statsOf(stream.moves(stream.updateMs[4], stream.updateMs.back()));
	const std::string label = describe(moves);
	ok &= expect(moves.count > 0 && moves.largest <= 1.4 * moves.mean, "the largest move per tick is at most 1.4 times the mean", label);
	ok &= expect(moves.smallest >= 0.0, "the view never moves backwards", label);
	ok &= expectBehindTarget(stream, label);
	return ok;
}

// A pause between updates is not an update interval: the first update after it moves nothing by itself
// and the view reaches the new target about one update interval later.
static bool checkPauseThenUpdate() {
	bool ok = true;
	Rig rig;
	Stream stream(rig, 500.0);
	for (int i = 0; i < 10; ++i)
		stream.update(100, 120.0);
	stream.tickBefore(stream.updateMs.back() + 5000);
	ok &= expect(!rig.camera.running(), "the timer stops while no update comes");

	const qreal before = centerOf(rig.view).x();
	stream.update(5000, 120.0);
	const qreal moved = std::abs(centerOf(rig.view).x() - before) / stream.pixel;
	ok &= expect(moved < 0.5, "the first update after a pause does not move the view", measured({{"moved", moved}}));
	ok &= expect(rig.camera.running(), "the first update after a pause starts the timer");

	const bool stopped = stream.runOut();
	const qint64 last = stream.updateMs.back();
	const qint64 delay = stream.stopMs(last) - last;
	const qint64 earliest = 100 - FollowCamera::tickIntervalMs;
	const qint64 latest = 100 + 2 * FollowCamera::tickIntervalMs;
	const std::string label = "the timer stopped " + std::to_string(delay) + " ms after the update, expected " + std::to_string(earliest) + " to "
		+ std::to_string(latest);
	ok &= expect(stopped && delay >= earliest && delay <= latest, "the pause does not become the update interval", label);
	ok &= expectOnTarget(stream, "the view ends on the target after the pause", label);
	return ok;
}

// A pan of the user late in a glide is adopted as the new starting point, and the view returns to the
// target over one update interval from there.
static bool checkPanLateInGlide() {
	bool ok = true;
	Rig rig;
	Stream stream(rig, 1000.0);
	for (int i = 0; i < 5; ++i)
		stream.update(500, 120.0);
	const qint64 last = stream.updateMs.back();
	stream.tickBefore(last + 450);
	stream.tickOnce();
	ok &= expect(rig.camera.running(), "the glide is still under way when the user pans");

	const qint64 panMs = rig.now;
	QScrollBar* bar = rig.view.horizontalScrollBar();
	bar->setValue(bar->value() - 450);
	const qreal panned = centerOf(rig.view).x();
	const qreal toTarget = stream.target - panned;
	stream.tickOnce();
	const qreal fraction = (centerOf(rig.view).x() - panned) / toTarget;
	ok &= expect(fraction > 0.03 && fraction < 0.10, "the next tick covers one step of a glide over an update interval", measured({{"fraction", fraction}}));

	const bool stopped = stream.runOut();
	const qint64 delay = stream.stopMs(panMs + 1) - panMs;
	const std::string label = "the timer stopped " + std::to_string(delay) + " ms after the pan";
	ok &= expect(stopped && delay <= 500 + 2 * FollowCamera::tickIntervalMs, "the view is back on the target one update interval after the pan", label);
	ok &= expectOnTarget(stream, "the view ends on the target after the pan", label);
	return ok;
}

// A tick long after the last one takes a step of at most 250 ms. A tick after the arrival moves the view
// onto the target and not past it.
static bool checkStalledTick() {
	bool ok = true;
	Rig rig;
	const QPointF a(2000.0, 1000.0);
	const QPointF target = a + QPointF(80.0, 0.0);
	rig.camera.follow(a, true);
	rig.camera.follow(target, false);
	rig.now = 1000;
	rig.camera.tick();
	const qreal fraction = (centerOf(rig.view).x() - a.x()) / 80.0;
	ok &= expect(fraction > 0.45 && fraction < 0.55, "a tick after a stalled event loop takes a step of 250 ms", measured({{"fraction", fraction}}));

	rig.now = 1033;
	rig.camera.tick();
	const qreal off = distance(centerOf(rig.view), target) / pixelOf(rig.view);
	ok &= expect(off <= 3.0, "a tick after the arrival moves the view onto the target", measured({{"off", off}}));
	ok &= expect(!rig.camera.running(), "the timer stops when the target is reached");
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
	rig.tick();
	ok &= expect(byCamera == 2 && byUser == 0, "a tick is reported as a move of the camera");
	rig.camera.settle();
	ok &= expect(byCamera == 3 && byUser == 0, "settle is reported as a move of the camera");
	ok &= expect(!rig.camera.movingView(), "the camera has stopped moving the view when a call returns");

	QScrollBar* bar = rig.view.horizontalScrollBar();
	bar->setValue(bar->value() - 50);
	ok &= expect(byCamera == 3 && byUser > 0, "a pan of the user is not reported as a move of the camera");
	const int afterPan = byUser;
	rig.view.zoomBy(1.5);
	ok &= expect(byCamera == 3 && byUser > afterPan, "a zoom of the user is not reported as a move of the camera");
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
	ok &= checkEvenGlide();
	ok &= checkEndOfUpdates();
	ok &= checkSpeedChange();
	ok &= checkIrregularGaps();
	ok &= checkPauseThenUpdate();
	ok &= checkPanLateInGlide();
	ok &= checkStalledTick();
	ok &= checkSeek();
	ok &= checkManualPan();
	ok &= checkZoom();
	ok &= checkEdge();
	ok &= checkEdgeFreeAxis();
	ok &= checkFit();
	ok &= checkSettle();
	ok &= checkMovingView();
	ok &= checkViewDestroyed();
	return ok ? 0 : 1;
}
