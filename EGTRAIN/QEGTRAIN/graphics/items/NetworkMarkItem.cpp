#include "graphics/items/NetworkMarkItem.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QLineF>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr int kCasingAlpha = 200;

// A cosmetic pen with round caps and joins.
QPen markPen(const QColor& color, qreal width) {
	QPen pen(color, width);
	pen.setCosmetic(true);
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	return pen;
}

// The rectangle of the artwork of a shape item in the scene, else its bounds.
QRectF ownRect(const QGraphicsItem* item) {
	if (const auto* ellipse = dynamic_cast<const QGraphicsEllipseItem*>(item))
		return item->sceneTransform().mapRect(ellipse->rect());
	if (const auto* rectangle = dynamic_cast<const QGraphicsRectItem*>(item))
		return item->sceneTransform().mapRect(rectangle->rect());
	return item->sceneBoundingRect();
}

// The closed outline of a rectangle item in the scene, else of its bounds, turned with the item.
QPolygonF outlineOf(const QGraphicsItem* item) {
	const auto* rectangle = dynamic_cast<const QGraphicsRectItem*>(item);
	QPolygonF outline = item->mapToScene(rectangle ? rectangle->rect() : item->boundingRect());
	if (!outline.isEmpty())
		outline.append(outline.first());
	return outline;
}

// The point of the segment from a to b nearest to point.
QPointF nearestOnSegment(const QPointF& a, const QPointF& b, const QPointF& point) {
	const QPointF segment = b - a;
	const qreal lengthSquared = QPointF::dotProduct(segment, segment);
	if (!(lengthSquared > 0.0))
		return a;
	const qreal ratio = std::clamp(QPointF::dotProduct(point - a, segment) / lengthSquared, 0.0, 1.0);
	return a + ratio * segment;
}

// The segment from a to b has a point in the rectangle.
bool segmentMeets(const QPointF& a, const QPointF& b, const QRectF& rect) {
	if (rect.contains(a) || rect.contains(b))
		return true;
	const QLineF segment(a, b);
	const QPointF corners[] = {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()};
	for (int corner = 0; corner < 4; ++corner)
		if (segment.intersects(QLineF(corners[corner], corners[(corner + 1) % 4]), nullptr) == QLineF::BoundedIntersection)
			return true;
	return false;
}

// The polylines without the empty ones.
QList<QPolygonF> withoutEmpty(const QList<QPolygonF>& lines) {
	QList<QPolygonF> kept;
	for (const QPolygonF& line : lines)
		if (!line.isEmpty())
			kept.append(line);
	return kept;
}

// The smallest rectangle that holds the points so far. QRectF::united() drops a rectangle without area, such as the one of
// a single point, so the corners are kept apart.
struct Extent {
	qreal left = std::numeric_limits<qreal>::max();
	qreal top = std::numeric_limits<qreal>::max();
	qreal right = std::numeric_limits<qreal>::lowest();
	qreal bottom = std::numeric_limits<qreal>::lowest();

	void include(const QPointF& point) {
		left = std::min(left, point.x());
		top = std::min(top, point.y());
		right = std::max(right, point.x());
		bottom = std::max(bottom, point.y());
	}
	void include(const QRectF& rect) {
		include(rect.topLeft());
		include(rect.bottomRight());
	}
	bool empty() const { return left > right; }
	QRectF rect() const { return empty() ? QRectF() : QRectF(QPointF(left, top), QPointF(right, bottom)); }
};
} // namespace

NetworkMarkItem::NetworkMarkItem() {
	setZValue(ZValue);
	setAcceptedMouseButtons(Qt::NoButton);
}

void NetworkMarkItem::setStretches(const QList<QPolygonF>& stretches) {
	m_stretches = withoutEmpty(stretches);
	sync();
}

void NetworkMarkItem::setEmphasis(const QList<QPolygonF>& emphasis) {
	m_emphasis = withoutEmpty(emphasis);
	sync();
}

void NetworkMarkItem::setDirected(bool directed) {
	m_directed = directed;
	sync();
}

void NetworkMarkItem::setRingTargets(const QList<const QGraphicsItem*>& targets) {
	m_ringTargets = targets;
	sync();
}

void NetworkMarkItem::setHaloTargets(const QList<const QGraphicsItem*>& targets) {
	m_haloTargets = targets;
	sync();
}

bool NetworkMarkItem::isEmpty() const {
	return m_stretches.isEmpty() && m_emphasis.isEmpty() && m_ringTargets.isEmpty() && m_haloTargets.isEmpty();
}

void NetworkMarkItem::setViewScale(qreal scale) {
	if (!(scale > 0.0) || scale == m_scale)
		return;
	m_scale = scale;
	sync();
}

void NetworkMarkItem::sync() {
	prepareGeometryChange();
	rebuild();
	update();
}

// The rings, the halos and the direction in scene units, and the bounds; pixel sizes are divided by the view scale. A
// target is drawn when it is shown in the scene of the mark and has bounds; a station mark has none in its near look.
void NetworkMarkItem::rebuild() {
	const auto shown = [this](const QGraphicsItem* target) {
		return target && target->isVisible() && target->scene() == scene() && !target->boundingRect().isEmpty();
	};
	m_rings.clear();
	for (const QGraphicsItem* target : std::as_const(m_ringTargets)) {
		if (!shown(target))
			continue;
		const QRectF rect = ownRect(target);
		const qreal radius = std::max(RingRadiusPixels / m_scale,
			std::hypot(rect.width(), rect.height()) / 2.0 + RingPaddingPixels / m_scale);
		m_rings.append(QRectF(rect.center() - QPointF(radius, radius), QSizeF(2.0 * radius, 2.0 * radius)));
	}
	m_halos.clear();
	for (const QGraphicsItem* target : std::as_const(m_haloTargets))
		if (shown(target))
			m_halos.append(outlineOf(target));

	m_direction = QPainterPath();
	if (m_directed && !m_stretches.isEmpty()) {
		m_direction.addEllipse(m_stretches.first().first(), StartPixels / m_scale, StartPixels / m_scale);
		// The arrowhead points along the end of the last stretch, from the last point at least a pixel before its end.
		const QPolygonF& last = m_stretches.last();
		const QPointF end = last.last();
		for (qsizetype index = last.size() - 2; index >= 0; --index) {
			const QLineF back(last[index], end);
			if (back.length() * m_scale < 1.0)
				continue;
			const QPointF along = (end - last[index]) / back.length();
			const QPointF across(-along.y(), along.x());
			const qreal half = ArrowPixels / 2.0 / m_scale;
			QPolygonF arrow({end + along * half, end - along * half + across * half, end - along * half - across * half});
			arrow.append(arrow.first());
			m_direction.addPolygon(arrow);
			break;
		}
	}

	Extent extent;
	for (const QList<QPolygonF>* lines : {&m_stretches, &m_emphasis})
		for (const QPolygonF& line : *lines)
			for (const QPointF& point : line)
				extent.include(point);
	for (const QRectF& ring : std::as_const(m_rings))
		extent.include(ring);
	for (const QPolygonF& halo : std::as_const(m_halos))
		extent.include(halo.boundingRect());
	if (!m_direction.isEmpty())
		extent.include(m_direction.boundingRect());
	const qreal reach = std::max({EmphasisPixels / 2.0, BandPixels / 2.0, HaloPixels, RingPixels / 2.0});
	const qreal margin = (reach + CasingPixels + 1.0) / m_scale;
	m_bounds = extent.empty() ? QRectF() : extent.rect().adjusted(-margin, -margin, margin, margin);
}

bool NetworkMarkItem::intersects(const QRectF& rect) const {
	for (const QList<QPolygonF>* lines : {&m_stretches, &m_emphasis})
		for (const QPolygonF& line : *lines) {
			if (line.size() == 1 && rect.contains(line.first()))
				return true;
			for (qsizetype index = 1; index < line.size(); ++index)
				if (segmentMeets(line[index - 1], line[index], rect))
					return true;
		}
	for (const QRectF& ring : m_rings)
		if (ring.intersects(rect))
			return true;
	for (const QPolygonF& halo : m_halos)
		if (halo.boundingRect().intersects(rect))
			return true;
	return false;
}

bool NetworkMarkItem::nearestPoint(const QPointF& point, QPointF* nearest) const {
	bool found = false;
	qreal best = std::numeric_limits<qreal>::max();
	const auto consider = [&](const QPointF& candidate) {
		const QPointF delta = candidate - point;
		const qreal distance = QPointF::dotProduct(delta, delta);
		if (distance < best) {
			best = distance;
			*nearest = candidate;
			found = true;
		}
	};
	for (const QList<QPolygonF>* lines : {&m_stretches, &m_emphasis})
		for (const QPolygonF& line : *lines) {
			if (line.size() == 1)
				consider(line.first());
			for (qsizetype index = 1; index < line.size(); ++index)
				consider(nearestOnSegment(line[index - 1], line[index], point));
		}
	for (const QRectF& ring : m_rings)
		consider(ring.center());
	for (const QPolygonF& halo : m_halos)
		consider(halo.boundingRect().center());
	return found;
}

QRectF NetworkMarkItem::boundingRect() const {
	return m_bounds;
}

QPainterPath NetworkMarkItem::shape() const {
	return QPainterPath();
}

void NetworkMarkItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setBrush(Qt::NoBrush);
	const QColor casing(0, 0, 0, kCasingAlpha);
	// A polyline of one point is a dot of the width of the pen.
	const auto drawLines = [painter](const QList<QPolygonF>& lines, const QColor& color, qreal width) {
		painter->setPen(markPen(color, width));
		for (const QPolygonF& line : lines) {
			if (line.size() == 1)
				painter->drawPoint(line.first());
			else
				painter->drawPolyline(line);
		}
	};
	drawLines(m_stretches, casing, BandPixels + 2.0 * CasingPixels);
	drawLines(m_stretches, kNetworkMarkColor, BandPixels);
	drawLines(m_emphasis, casing, EmphasisPixels + 2.0 * CasingPixels);
	drawLines(m_emphasis, kNetworkMarkEmphasisColor, EmphasisPixels);
	if (!m_direction.isEmpty()) {
		painter->setPen(markPen(casing, 2.0 * CasingPixels));
		painter->setBrush(kNetworkMarkColor);
		painter->drawPath(m_direction);
		painter->setBrush(Qt::NoBrush);
	}
	// A halo is a wide line along the outline; the target, stacked over it, covers its inner half.
	drawLines(m_halos, casing, 2.0 * (HaloPixels + CasingPixels));
	drawLines(m_halos, kNetworkMarkColor, 2.0 * HaloPixels);
	for (const QRectF& ring : std::as_const(m_rings)) {
		painter->setPen(markPen(casing, RingPixels + 2.0 * CasingPixels));
		painter->drawEllipse(ring);
		painter->setPen(markPen(kNetworkMarkColor, RingPixels));
		painter->drawEllipse(ring);
	}
}
