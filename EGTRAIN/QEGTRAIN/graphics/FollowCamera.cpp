#include "graphics/FollowCamera.h"

#include <QLineF>
#include <QSizeF>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
// A target further away than this fraction of the larger side is a jump, not a glide.
constexpr qreal kSnapFraction = 1.5;
}

FollowCamera::FollowCamera(NetworkView* view, QObject* parent)
	: QObject(parent), m_view(view) {
	m_elapsed.start();
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

// Without a target the next target cuts to it anyway.
void FollowCamera::adoptViewCenter() {
	if (m_moving || !m_view || !m_hasTarget)
		return;
	m_timer.stop();
	m_adopted = true;
}

// The part of the catch-up distance that is left at now. It eases in and out and ends the catch-up when it is
// closed.
QPointF FollowCamera::catchUpOffset(qint64 now) {
	if (!m_timer.isActive())
		return QPointF();
	const qreal t = qreal(now - m_catchUpStartMs) / catchUpMs;
	if (t >= 1.0) {
		m_timer.stop();
		return QPointF();
	}
	const qreal done = std::clamp<qreal>(t, 0.0, 1.0);
	return m_catchUp * (1.0 - done * done * (3.0 - 2.0 * done));
}

void FollowCamera::moveTo(const QPointF& desired) {
	m_moving = true;
	m_view->centerOnExactly(desired);
	m_moving = false;
}

void FollowCamera::cut() {
	m_timer.stop();
	m_adopted = false;
	moveTo(m_target);
}

void FollowCamera::follow(const QPointF& target, bool snap) {
	if (!m_view || !std::isfinite(target.x()) || !std::isfinite(target.y()))
		return;
	m_target = target;
	m_hasTarget = true;

	const QPointF center = viewCenter();
	const QSizeF visible = m_view->mapToScene(m_view->viewport()->rect()).boundingRect().size();
	const bool jump = QLineF(center, target).length() > kSnapFraction * std::max(visible.width(), visible.height());
	if (snap || jump) {
		cut();
		return;
	}
	const qint64 now = nowMs();
	if (m_adopted) {
		m_adopted = false;
		m_catchUp = center - target;
		m_catchUpStartMs = now;
		m_timer.start();
	}
	moveTo(target + catchUpOffset(now));
}

void FollowCamera::settle() {
	if (!m_view || !m_hasTarget)
		return;
	cut();
}

void FollowCamera::stop() {
	m_timer.stop();
	m_hasTarget = false;
	m_adopted = false;
}

void FollowCamera::tick() {
	if (!m_view || !m_hasTarget || !m_timer.isActive()) {
		m_timer.stop();
		return;
	}
	moveTo(m_target + catchUpOffset(nowMs()));
}
