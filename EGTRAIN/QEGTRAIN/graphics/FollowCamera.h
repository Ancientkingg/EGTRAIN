#ifndef FOLLOWCAMERA_H
#define FOLLOWCAMERA_H

#include "graphics/NetworkView.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QTimer>
#include <functional>

/// Moves the centre of a NetworkView towards a target scene position. It owns neither a
/// train nor a snapshot: the caller passes positions in and the controller moves the view.
///
/// A glide follows the target with a timer of its own. The view moves at a constant speed and
/// reaches the newest target one estimated update interval after that target arrived, and never
/// sooner than two timer steps. The estimate starts at 500 ms, and each interval between targets of
/// up to 2000 ms moves it halfway there. It is about one update behind a train that moves at constant
/// speed, and moves at one speed between updates and across them. The distance to the target is
/// capped to 30 percent of the smaller side of the visible scene, and the timer stops when the target
/// is reached or the view cannot get closer to it. A snap moves the view at once.
///
/// The controller reads the real centre back after every move to detect a clamped axis, and
/// leaves a clamped axis out of the distance, the step and the lag cap of the free axis. It
/// adopts the centre after a pan, a zoom or a resize by the user as the new starting point, so
/// the next glide returns from there.
class FollowCamera : public QObject {
	Q_OBJECT

public:
	/// A clock that returns a time in milliseconds.
	using Clock = std::function<qint64()>;

	/// The interval of the glide timer in milliseconds.
	static constexpr int tickIntervalMs = 33;

	/// Moves view, which the camera does not own. A pan, a zoom or a resize of the view sets a new starting
	/// point, and the deletion of the view stops the camera.
	explicit FollowCamera(NetworkView* view, QObject* parent = nullptr);

	/// Sets the position to follow, in scene coordinates. A snap moves the view there at once. The first target
	/// after stop() and a target further away than 1.5 times the larger side of the visible scene also
	/// move the view at once.
	void follow(const QPointF& target, bool snap);
	/// Moves the view to the target at once and stops the timer.
	void settle();
	/// Forgets the target and stops the timer.
	void stop();
	/// One step of the glide. The timer calls it; tests call it with a clock set by setClock().
	void tick();

	/// The glide timer runs.
	bool running() const { return m_timer.isActive(); }
	/// A target is set and not forgotten by stop().
	bool hasTarget() const { return m_hasTarget; }
	/// True while the controller itself moves the view, so that a slot of viewportChanged() can
	/// tell this move from a pan, a zoom or a resize by the user.
	bool movingView() const { return m_moving; }
	/// Replaces the millisecond clock. An empty function restores the real clock.
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
	qint64 glideDurationMs() const;
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
	qint64 m_arrivalMs = 0;
};

#endif // FOLLOWCAMERA_H
