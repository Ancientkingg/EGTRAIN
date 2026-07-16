#include "graphics/items/ConnectionItem.h"

// adjust value of selectionOffset to control the margin used when clicking on the line item
ConnectionItem::ConnectionItem(const QLineF& line, QGraphicsItem* parent)
	: QGraphicsLineItem(line, parent), selectionOffset(20) {
	createSelectionPolygon();
}

ConnectionItem::~ConnectionItem() {
}

void ConnectionItem::createSelectionPolygon() {
	QPolygonF nPolygon;
	qreal radAngle = line().angle() * M_PI / 180;
	qreal dx = selectionOffset * sin(radAngle);
	qreal dy = selectionOffset * cos(radAngle);
	QPointF offset1 = QPointF(dx, dy);
	QPointF offset2 = QPointF(-dx, -dy);
	nPolygon << line().p1() + offset1
			 << line().p1() + offset2
			 << line().p2() + offset2
			 << line().p2() + offset1;
	selectionPolygon = nPolygon;
	update();
}

QRectF ConnectionItem::boundingRect() const {
	return selectionPolygon.boundingRect();
}

QPainterPath ConnectionItem::shape() const {
	QPainterPath ret;
	ret.addPolygon(selectionPolygon);
	return ret;
}

void ConnectionItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
	Q_UNUSED(option);
	Q_UNUSED(widget);

	painter->setPen(pen());
	painter->drawLine(line());
}
