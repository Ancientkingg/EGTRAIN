#include "graphics/items/TrainBodyItem.h"

#include <QLineF>
#include <QPainterPathStroker>

TrainBodyItem::TrainBodyItem(const QPolygonF& polygon, QGraphicsItem* parent)
	: QGraphicsPolygonItem(polygon, parent) {
	setZValue(3); // draw on top of everything (except TrainItemGroup)

	index = -1;
}

TrainBodyItem::~TrainBodyItem() {
}

void TrainBodyItem::setViewScale(qreal viewScale) {
	if (markWidth(viewScale) != markWidth(m_viewScale))
		prepareGeometryChange();
	m_viewScale = viewScale;
}

qreal TrainBodyItem::markWidth(qreal viewScale) const {
	if (viewScale <= 0.0 || polygon().size() < 2)
		return 0.0;
	// The first and the last point are the two corners of one end of the body.
	const qreal thickness = QLineF(polygon().first(), polygon().last()).length();
	const qreal width = MinPixels / viewScale - thickness;
	return width > pen().widthF() ? width : 0.0;
}

QRectF TrainBodyItem::boundingRect() const {
	const qreal width = markWidth(m_viewScale);
	if (width <= 0.0)
		return QGraphicsPolygonItem::boundingRect();
	// The round stroke is wider than the pen, so it alone sets the bounds.
	const qreal half = width / 2.0;
	return polygon().boundingRect().adjusted(-half, -half, half, half);
}

QPainterPath TrainBodyItem::shape() const {
	const qreal width = markWidth(m_viewScale);
	if (width <= 0.0)
		return QGraphicsPolygonItem::shape();
	// The polygon with the stroke that paint() draws around it, as Qt builds the shape of a shape item.
	QPainterPath outline;
	outline.addPolygon(polygon());
	outline.closeSubpath();
	QPainterPathStroker stroker;
	stroker.setWidth(width);
	stroker.setCapStyle(Qt::RoundCap);
	stroker.setJoinStyle(Qt::RoundJoin);
	QPainterPath path = stroker.createStroke(outline);
	path.addPath(outline);
	return path;
}

void TrainBodyItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
	Q_UNUSED(option);
	Q_UNUSED(widget);

	painter->setBrush(brush());
	const qreal width = markWidth(m_viewScale);
	if (width > 0.0) {
		painter->setPen(QPen(brush().color(), width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter->drawPolygon(polygon());
	}
	painter->setPen(pen());
	painter->drawPolygon(polygon());
}
