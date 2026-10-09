#ifndef FOLLOWCAMERA_H
#define FOLLOWCAMERA_H

#include "graphics/NetworkView.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QTimer>
#include <functional>

// Moves the centre of a NetworkView towards a target scene position. It owns neither a
// train nor a snapshot: the caller passes positions in and the controller moves the view.
//
// A glide follows the target with an exponential approach driven by a timer of its own.
// The time constant comes from the interval between targets, the distance to the target is
// capped to a fraction of the visible scene, and the timer stops when the target is reached
// or the view cannot get closer to it. A snap moves the view at once.
//
// The controller reads the real centre back after every move to detect a clamped axis, and
// leaves a clamped axis out of the distance, the step and the lag cap of the free axis. It
// adopts the centre after a pan, a zoom or a resize by the user as the new starting point, so
// the next glide returns from there.
class FollowCamera : public QObject {
	Q_OBJECT

public:
	using Clock = std::function<qint64()>;

	static constexpr int tickIntervalMs = 33;

	explicit FollowCamera(NetworkView* view, QObject* parent = nullptr);

	// Sets the position to follow. A snap moves the view there at once. The first target
	// after stop() and a target far outside the visible scene also move the view at once.
	void follow(const QPointF& target, bool snap);
	// Moves the view to the target at once and stops the timer.
	void settle();
	// Forgets the target and stops the timer.
	void stop();
	// One step of the glide. The timer calls it; tests call it with a clock set by setClock().
	void tick();

	bool running() const { return m_timer.isActive(); }
	bool hasTarget() const { return m_hasTarget; }
	// True while the controller itself moves the view, so that a slot of viewportChanged() can
	// tell this move from a pan, a zoom or a resize by the user.
	bool movingView() const { return m_moving; }
	// Replaces the millisecond clock. An empty function restores the real clock.
	void setClock(Clock clock);

private:
	struct Stall {
		bool x = false;
		bool y = false;
	};

	qint64 nowMs() const;
	QPointF viewCenter() const;
	qreal pixelSize() const;
	Stall moveTo(const QPointF& desired);
	qreal distanceToTarget(const Stall& stall) const;
	void adoptViewCenter();

	QPointer<NetworkView> m_view;
	QTimer m_timer;
	QElapsedTimer m_elapsed;
	Clock m_clock;
	QPointF m_position;
	QPointF m_target;
	Stall m_stall;
	bool m_hasTarget = false;
	bool m_withinLag = true;
	bool m_moving = false;
	qreal m_intervalMs;
	qint64 m_lastTargetMs = 0;
	qint64 m_lastTickMs = 0;
};

#endif // FOLLOWCAMERA_H
