#include "graphics/items/TrainBodyItem.h"

#include <QPainterPathStroker>
#include <cmath>

TrainBodyItem::TrainBodyItem(const QPolygonF& polygon, QGraphicsItem* parent)
	: QGraphicsPolygonItem(polygon, parent) {
	setZValue(3); // draw on top of everything (except TrainItemGroup)

	index = -1;
}

TrainBodyItem::~TrainBodyItem() {
}

void TrainBodyItem::setViewScale(qreal viewScale) {
	viewScale = std::abs(viewScale);
	const qreal needed = viewScale > 0.0 ? MinPixels / viewScale : 0.0;
	const qreal minWidth = needed > pen().widthF() ? needed : 0.0;
	if (minWidth == m_minWidth)
		return;
	prepareGeometryChange();
	m_minWidth = minWidth;
}

QRectF TrainBodyItem::boundingRect() const {
	const QRectF rect = QGraphicsPolygonItem::boundingRect();
	if (m_minWidth <= 0.0 || polygon().isEmpty())
		return rect;
	return rect.adjusted(-m_minWidth / 2.0, -m_minWidth / 2.0, m_minWidth / 2.0, m_minWidth / 2.0);
}

QPainterPath TrainBodyItem::shape() const {
	QPainterPath path = QGraphicsPolygonItem::shape();
	if (m_minWidth <= 0.0 || polygon().isEmpty())
		return path;
	QPainterPath outline;
	outline.addPolygon(polygon());
	outline.closeSubpath();
	QPainterPathStroker stroker;
	stroker.setWidth(m_minWidth);
	stroker.setCapStyle(Qt::RoundCap);
	stroker.setJoinStyle(Qt::RoundJoin);
	path.setFillRule(Qt::WindingFill);
	path.addPath(stroker.createStroke(outline));
	return path;
}

void TrainBodyItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
	Q_UNUSED(option);
	Q_UNUSED(widget);

	painter->setBrush(brush());
	if (m_minWidth > 0.0) {
		painter->setPen(QPen(brush().color(), m_minWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter->drawPolygon(polygon());
	}
	painter->setPen(pen());
	painter->drawPolygon(polygon());
}
