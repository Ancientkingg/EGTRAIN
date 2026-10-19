#ifndef SIGNALGEOMETRY_H
#define SIGNALGEOMETRY_H

#include <QLineF>
#include <QPointF>

/// The two heads of a signal, one for each direction, with their posts and bases, in scene coordinates.
/// The reversed head is on the side opposite the normal, the forward head on the side of the normal.
struct SignalGeometry {
	QLineF reversedBase;  ///< The base of the reversed head, across the track.
	QLineF reversedPost;  ///< The post of the reversed head, along the track towards before.
	QPointF reversedHead; ///< The centre of the reversed head, at the end of its post.
	QLineF forwardBase;	  ///< The base of the forward head, across the track.
	QLineF forwardPost;	  ///< The post of the forward head, along the track towards after.
	QPointF forwardHead;  ///< The centre of the forward head, at the end of its post.
};

/// The geometry of a signal at center on the track. before and after are the track points on either side of
/// it, which the callers take 8 metres away, and normal is the unit normal of the track. A base runs across
/// the track from 0.10 to 0.30 separation away from it; a post runs at 0.20 separation from the signal to the
/// point before or after it, where its head is. separation is the distance in scene units between two display
/// levels of track lines. Preview and playback take the points from the same authored track interpolation.
inline SignalGeometry signalGeometry(const QPointF& center, const QPointF& before,
	const QPointF& after, const QPointF& normal, qreal separation) {
	SignalGeometry geometry;
	geometry.reversedBase = QLineF(center - normal * (0.10 * separation),
		center - normal * (0.30 * separation));
	geometry.reversedPost = QLineF(center - normal * (0.20 * separation),
		before - normal * (0.20 * separation));
	geometry.reversedHead = geometry.reversedPost.p2();
	geometry.forwardBase = QLineF(center + normal * (0.10 * separation),
		center + normal * (0.30 * separation));
	geometry.forwardPost = QLineF(center + normal * (0.20 * separation),
		after + normal * (0.20 * separation));
	geometry.forwardHead = geometry.forwardPost.p2();
	return geometry;
}

#endif // SIGNALGEOMETRY_H
