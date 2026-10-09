#include "graphics/FollowCamera.h"

#include <QLineF>
#include <QSizeF>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
// The time constant of the approach is half the interval between targets, within these bounds.
constexpr qreal kMinTauMs = 60.0;
constexpr qreal kMaxTauMs = 250.0;
constexpr qreal kTauPerInterval = 0.5;
constexpr qreal kDefaultIntervalMs = 500.0;
// A longer gap between targets (a pause) is not an update interval.
constexpr qint64 kMaxIntervalMs = 2000;
// A step longer than this (a stalled event loop) is treated as this long.
constexpr qint64 kMaxStepMs = 250;
// The target stays within this fraction of the smaller side of the visible scene.
constexpr qreal kMaxLagFraction = 0.30;
// A target further away than this fraction of the larger side is a jump, not a glide.
constexpr qreal kSnapFraction = 1.5;
// Closer than this many device pixels counts as reached.
constexpr qreal kReachedPixels = 0.25;
// The view positions itself on whole device pixels. A larger difference between the position
// asked for and the real centre means the view clamped the move.
constexpr qreal kReadbackPixels = 2.0;

qreal distanceBetween(const QPointF& from, const QPointF& to) {
	return QLineF(from, to).length();
}
}

FollowCamera::FollowCamera(NetworkView* view, QObject* parent)
	: QObject(parent), m_view(view), m_intervalMs(kDefaultIntervalMs) {
	m_elapsed.start();
	m_timer.setTimerType(Qt::CoarseTimer);
	m_timer.setInterval(tickIntervalMs);
	connect(&m_timer, &QTimer::timeout, this, &FollowCamera::tick);
	if (view) {
		connect(view, &NetworkView::viewportChanged, this, &FollowCamera::adoptViewCenter);
		connect(view, &QObject::destroyed, this, &FollowCamera::stop);
	}
}

void FollowCamera::setClock(Clock clock) {
	m_clock = std::move(clock);
}

qint64 FollowCamera::nowMs() const {
	return m_clock ? m_clock() : m_elapsed.elapsed();
}

QPointF FollowCamera::viewCenter() const {
	return m_view->mapToScene(m_view->viewport()->rect().center());
}

qreal FollowCamera::pixelSize() const {
	const qreal scale = std::abs(m_view->transform().m11());
	return scale > 0.0 ? 1.0 / scale : 1.0;
}

void FollowCamera::adoptViewCenter() {
	if (m_moving || !m_view)
		return;
	m_position = viewCenter();
	m_withinLag = false;
}

FollowCamera::Stall FollowCamera::moveTo(const QPointF& desired) {
	m_moving = true;
	m_view->centerOn(desired);
	m_moving = false;
	const QPointF real = viewCenter();
	const qreal tolerance = kReadbackPixels * pixelSize();
	Stall stall;
	stall.x = std::abs(real.x() - desired.x()) > tolerance;
	stall.y = std::abs(real.y() - desired.y()) > tolerance;
	m_position = QPointF(stall.x ? real.x() : desired.x(), stall.y ? real.y() : desired.y());
	return stall;
}

void FollowCamera::follow(const QPointF& target, bool snap) {
	if (!m_view || !std::isfinite(target.x()) || !std::isfinite(target.y()))
		return;
	const qint64 now = nowMs();
	const bool first = !m_hasTarget;
	const qint64 interval = now - m_lastTargetMs;
	m_target = target;
	m_hasTarget = true;
	m_lastTargetMs = now;

	const QSizeF visible = m_view->mapToScene(m_view->viewport()->rect()).boundingRect().size();
	const bool jump = !first && distanceBetween(m_position, target) > kSnapFraction * std::max(visible.width(), visible.height());
	if (snap || first || jump) {
		moveTo(target);
		m_withinLag = true;
		m_timer.stop();
		return;
	}
	if (interval > 0 && interval <= kMaxIntervalMs)
		m_intervalMs = 0.5 * (m_intervalMs + interval);
	if (!m_timer.isActive()) {
		m_lastTickMs = now;
		m_timer.start();
	}
}

void FollowCamera::settle() {
	if (!m_view || !m_hasTarget)
		return;
	moveTo(m_target);
	m_withinLag = true;
	m_timer.stop();
}

void FollowCamera::stop() {
	m_timer.stop();
	m_hasTarget = false;
	m_withinLag = true;
	m_intervalMs = kDefaultIntervalMs;
}

void FollowCamera::tick() {
	if (!m_view || !m_hasTarget) {
		m_timer.stop();
		return;
	}
	const qint64 now = nowMs();
	const qreal stepMs = qreal(std::clamp<qint64>(now - m_lastTickMs, 0, kMaxStepMs));
	m_lastTickMs = now;

	const qreal reached = kReachedPixels * pixelSize();
	const QSizeF visible = m_view->mapToScene(m_view->viewport()->rect()).boundingRect().size();
	const qreal maxLag = kMaxLagFraction * std::min(visible.width(), visible.height());
	const QPointF delta = m_target - m_position;
	const qreal distance = distanceBetween(m_position, m_target);

	QPointF next = m_target;
	if (distance >= reached) {
		const qreal tau = std::clamp(kTauPerInterval * m_intervalMs, kMinTauMs, kMaxTauMs);
		next = m_position + delta * (1.0 - std::exp(-stepMs / tau));
		const qreal remaining = distanceBetween(next, m_target);
		if (m_withinLag && remaining > maxLag)
			next = m_target - delta * (maxLag / distance);
		else if (remaining < reached)
			next = m_target;
	}
	const Stall stall = moveTo(next);
	if (distanceBetween(m_position, m_target) <= maxLag)
		m_withinLag = true;

	const bool doneX = stall.x || std::abs(m_target.x() - m_position.x()) < reached;
	const bool doneY = stall.y || std::abs(m_target.y() - m_position.y()) < reached;
	if (doneX && doneY)
		m_timer.stop();
}
