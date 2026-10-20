#ifndef SIGNALGEOMETRY_H
#define SIGNALGEOMETRY_H

#include <QLineF>
#include <QPointF>

struct SignalGeometry {
	QLineF reversedBase;
	QLineF reversedPost;
	QPointF reversedHead;
	QLineF forwardBase;
	QLineF forwardPost;
	QPointF forwardHead;
};

// Coordinates come from the same authored track interpolation in preview and playback.
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
