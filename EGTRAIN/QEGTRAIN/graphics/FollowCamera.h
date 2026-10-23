#ifndef FOLLOWCAMERA_H
#define FOLLOWCAMERA_H

#include "graphics/NetworkView.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QTimer>
#include <functional>

/// Keeps the centre of a NetworkView on a target scene position. It owns neither a train nor a snapshot: the caller
/// passes positions in and the controller moves the view.
///
/// Every target moves the view to it at once, to a fraction of a pixel. The window passes the position at which a
/// followed train is drawn at every step of its motion, so the view moves with the train in the same frame. The first
/// target after stop(), a snap and a target further away than 1.5 times the larger side of the visible scene are a
/// cut, which also ends a catch-up. Where the edge of the scene holds an axis of the view, the view stays at the edge
/// on that axis.
///
/// While the user moves the view (NetworkView::interacting()), holding a drag, scrolling or pinching, the controller
/// does not move it: it keeps the target and ends a catch-up. It adopts the centre after a pan, a zoom or a resize by
/// the user, and also after a drag in which the view did not move. The first target after the user has finished then
/// starts a catch-up: the view keeps the distance from the target that the user left and closes it over catchUpMs,
/// easing in and out, with a timer of its own so that a catch-up also ends while the target stands still. Without a
/// new target, as in a paused run, the view stays where the user left it.
class FollowCamera : public QObject {
	Q_OBJECT

public:
	/// A clock that returns a time in milliseconds.
	using Clock = std::function<qint64()>;

	/// The interval of the timer of a catch-up in milliseconds.
	static constexpr int tickIntervalMs = 16;
	/// How long a catch-up takes in milliseconds.
	static constexpr int catchUpMs = 400;

	/// Moves view, which the camera does not own. A pan, a zoom or a resize of the view sets a new starting
	/// point, and the deletion of the view stops the camera.
	explicit FollowCamera(NetworkView* view, QObject* parent = nullptr);

	/// Sets the position to follow, in scene coordinates, and moves the view there, part of the way during a
	/// catch-up. A snap, the first target after stop() and a target further away than 1.5 times the larger side of
	/// the visible scene cut to the target. While the user moves the view, the view stays where it is.
	void follow(const QPointF& target, bool snap);
	/// Moves the view to the target at once and ends a catch-up.
	void settle();
	/// Forgets the target and ends a catch-up.
	void stop();
	/// One step of a catch-up. The timer calls it; tests call it with a clock set by setClock().
	void tick();

	/// A catch-up is under way, and its timer runs.
	bool running() const { return m_timer.isActive(); }
	/// A target is set and not forgotten by stop().
	bool hasTarget() const { return m_hasTarget; }
	/// True while the controller itself moves the view, so that a slot of viewportChanged() can
	/// tell this move from a pan, a zoom or a resize by the user.
	bool movingView() const { return m_moving; }
	/// Replaces the millisecond clock. An empty function restores the real clock.
	void setClock(Clock clock);

private:
	qint64 nowMs() const;
	QPointF viewCenter() const;
	QPointF catchUpOffset(qint64 now);
	void moveTo(const QPointF& desired);
	void cut();
	void adoptViewCenter();
	bool holdForUser();

	QPointer<NetworkView> m_view;
	QTimer m_timer;
	QElapsedTimer m_elapsed;
	Clock m_clock;
	QPointF m_target;
	bool m_hasTarget = false;
	bool m_moving = false;
	// The user moved the view, so the next target starts a catch-up from where the view is.
	bool m_adopted = false;
	// The distance from the target that a catch-up closes, and when it started.
	QPointF m_catchUp;
	qint64 m_catchUpStartMs = 0;
};

#endif // FOLLOWCAMERA_H
