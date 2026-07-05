#include "graphics/items/virtualArcQGraphicsLineItem.h"

virtualArcQGraphicsLineItem::virtualArcQGraphicsLineItem(const QLineF& firstLine, const QLineF& secondLine, QGraphicsItem* parent)
	: myQGraphicsLineItem(firstLine, parent), secondLine(secondLine) {
	createSelectionPolygon();
}

virtualArcQGraphicsLineItem::~virtualArcQGraphicsLineItem() {
}

QRectF virtualArcQGraphicsLineItem::boundingRect() const {
	return selectionPolygon.boundingRect();
}

QPainterPath virtualArcQGraphicsLineItem::shape() const {
	QPainterPath ret;
	ret.addPolygon(selectionPolygon);
	return ret;
}

void virtualArcQGraphicsLineItem::createSelectionPolygon() {
	QPolygonF nPolygon;

	// main line (line())
	qreal mainRadAngle = line().angle() * M_PI / 180;
	qreal mainDx = selectionOffset * sin(mainRadAngle);
	qreal mainDy = selectionOffset * cos(mainRadAngle);
	QPointF mainOffset1 = QPointF(mainDx, mainDy);
	QPointF mainOffset2 = QPointF(-mainDx, -mainDy);

	// secondLine
	qreal secRadAngle = secondLine.angle() * M_PI / 180;
	qreal secDx = selectionOffset * sin(secRadAngle);
	qreal secDy = selectionOffset * cos(secRadAngle);
	QPointF secOffset1 = QPointF(secDx, secDy);
	QPointF secOffset2 = QPointF(-secDx, -secDy);

	// avg p1+off1 to draw polygon
	QPointF avgMiddleOff1;
	avgMiddleOff1 = line().p2() + mainOffset1 + secondLine.p1() + secOffset1;
	avgMiddleOff1 /= 2;

	// avg p1+off2 to draw polygon
	QPointF avgMiddleOff2;
	avgMiddleOff2 = line().p2() + mainOffset2 + secondLine.p1() + secOffset2;
	avgMiddleOff2 /= 2;

	// draw polygon covering the two lines (using avg points around point where lines connect)
	nPolygon << line().p1() + mainOffset1
			 << line().p1() + mainOffset2
			 << avgMiddleOff2
			 << secondLine.p2() + secOffset2
			 << secondLine.p2() + secOffset1
			 << avgMiddleOff1;
	selectionPolygon = nPolygon;
	update();
}

void virtualArcQGraphicsLineItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
	Q_UNUSED(option);
	Q_UNUSED(widget);

	QPen effectPen = pen();
	effectPen.setColor(Qt::blue);

	// change line color when selected
	if (graphicsEffect()) {
		painter->setPen(effectPen);
	} else {
		painter->setPen(pen());
	}

	painter->drawLine(line());
	painter->drawLine(secondLine);
}