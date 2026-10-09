#include "graphics/items/SelectionCueItem.h"

#include "graphics/items/StationNodeItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"

#include <QAbstractGraphicsShapeItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPainter>
#include <QPainterPathStroker>

#include <algorithm>
#include <cmath>

namespace {
constexpr qreal kCornerPixels = 4.0;
constexpr int kCasingAlpha = 200;
// The ring is stacked just below the target: over the items under the target, under the target and everything over it.
constexpr qreal kBehindTarget = 0.01;

// The rectangle of the artwork of a shape item in the scene.
QRectF ownRect(const QGraphicsItem* item) {
	if (const auto* ellipse = dynamic_cast<const QGraphicsEllipseItem*>(item))
		return item->sceneTransform().mapRect(ellipse->rect());
	if (const auto* rectangle = dynamic_cast<const QGraphicsRectItem*>(item))
		return item->sceneTransform().mapRect(rectangle->rect());
	return item->sceneBoundingRect();
}

// The pictogram of a station node: a child in the preview, a scene item that the overlay of the
// station owns when a run is shown.
const QGraphicsItem* pictogramOf(const QGraphicsItem* item) {
	if (item->type() != StationNodeItem::Type)
		return nullptr;
	for (const QGraphicsItem* child : item->childItems())
		if (child->type() == QGraphicsPixmapItem::Type)
			return child;
	const auto* station = static_cast<const StationNodeItem*>(item);
	if (!station->node || !item->scene())
		return nullptr;
	for (const QGraphicsItem* other : item->scene()->items()) {
		if (other->type() != StationOverlayItem::Type)
			continue;
		const auto* overlay = static_cast<const StationOverlayItem*>(other);
		if (overlay->hasSourceIdentity() && overlay->matchesSourceIdentity(station->node->ID, station->track))
			return overlay->pictureItem();
	}
	return nullptr;
}
} // namespace

SelectionCueItem::SelectionCueItem(const QGraphicsItem* target)
	: m_target(target), m_pictogram(pictogramOf(target)) {
	stackBehindTarget();
	setAcceptedMouseButtons(Qt::NoButton);
	if (target->scene() && !target->scene()->views().isEmpty()) {
		const qreal scale = std::abs(target->scene()->views().first()->transform().m11());
		if (scale > 0.0)
			m_scale = scale;
	}
	readTarget(&m_isLine, &m_line, &m_rect);
	setVisible(target->isVisible());
	rebuild();
}

void SelectionCueItem::readTarget(bool* isLine, QLineF* line, QRectF* rect) const {
	*isLine = false;
	if (const auto* lineItem = dynamic_cast<const QGraphicsLineItem*>(m_target)) {
		*isLine = true;
		*line = m_target->sceneTransform().map(lineItem->line());
		return;
	}
	const QGraphicsItem* parent = m_target->parentItem();
	if (m_target->type() == QGraphicsPixmapItem::Type && parent && dynamic_cast<const QAbstractGraphicsShapeItem*>(parent)) {
		// The artwork of a station in the preview stands for the node it is attached to.
		*rect = ownRect(parent).united(m_target->sceneBoundingRect());
		return;
	}
	if (m_target->type() == TrainItemGroup::Type) {
		bool found = false;
		QRectF bodies;
		for (const QGraphicsItem* child : m_target->childItems()) {
			if (child->type() != TrainBodyItem::Type)
				continue;
			bodies = found ? bodies.united(child->sceneBoundingRect()) : child->sceneBoundingRect();
			found = true;
		}
		if (found) {
			*rect = bodies;
			return;
		}
	}
	*rect = ownRect(m_target);
	if (m_pictogram && m_pictogram->isVisible())
		*rect = rect->united(m_pictogram->sceneBoundingRect());
}

void SelectionCueItem::setViewScale(qreal scale) {
	if (!(scale > 0.0) || scale == m_scale)
		return;
	m_scale = scale;
	prepareGeometryChange();
	rebuild();
	update();
}

void SelectionCueItem::stackBehindTarget() {
	setZValue(m_target->topLevelItem()->zValue() - kBehindTarget);
}

void SelectionCueItem::sync() {
	stackBehindTarget();
	setVisible(m_target->isVisible() && m_target->scene() == scene());
	bool isLine = false;
	QLineF line;
	QRectF rect;
	readTarget(&isLine, &line, &rect);
	if (isLine == m_isLine && line == m_line && rect == m_rect)
		return;
	prepareGeometryChange();
	m_isLine = isLine;
	m_line = line;
	m_rect = rect;
	rebuild();
	update();
}

// The ring in scene units; pixel sizes are divided by the view scale.
void SelectionCueItem::rebuild() {
	m_ring = QPainterPath();
	if (m_isLine) {
		const qreal radius = HaloRadiusPixels / m_scale;
		if (m_line.length() * m_scale < 1.0) {
			const qreal round = std::max(HaloRadiusPixels, MinPixels / 2.0) / m_scale;
			m_ring.addEllipse(m_line.center(), round, round);
		} else {
			QPainterPath axis(m_line.p1());
			axis.lineTo(m_line.p2());
			QPainterPathStroker halo;
			halo.setWidth(2.0 * radius);
			halo.setCapStyle(Qt::RoundCap);
			halo.setJoinStyle(Qt::RoundJoin);
			m_ring = halo.createStroke(axis);
		}
	} else {
		const qreal width = std::max(MinPixels, m_rect.width() * m_scale + 2.0 * PaddingPixels);
		const qreal height = std::max(MinPixels, m_rect.height() * m_scale + 2.0 * PaddingPixels);
		QRectF ring(0.0, 0.0, width / m_scale, height / m_scale);
		ring.moveCenter(m_rect.center());
		m_ring.addRoundedRect(ring, kCornerPixels / m_scale, kCornerPixels / m_scale);
	}
	const qreal margin = (RingPixels / 2.0 + CasingPixels + 1.0) / m_scale;
	m_bounds = m_ring.boundingRect().adjusted(-margin, -margin, margin, margin);
}

QRectF SelectionCueItem::boundingRect() const {
	return m_bounds;
}

QPainterPath SelectionCueItem::shape() const {
	return QPainterPath();
}

void SelectionCueItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setBrush(Qt::NoBrush);
	QPen casing(QColor(0, 0, 0, kCasingAlpha), RingPixels + 2.0 * CasingPixels);
	casing.setCosmetic(true);
	casing.setJoinStyle(Qt::RoundJoin);
	painter->setPen(casing);
	painter->drawPath(m_ring);
	QPen ring(kSelectionCueColor, RingPixels);
	ring.setCosmetic(true);
	ring.setJoinStyle(Qt::RoundJoin);
	painter->setPen(ring);
	painter->drawPath(m_ring);
}
