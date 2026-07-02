#include "myQGraphicsRectItem.h"

myQGraphicsRectItem::myQGraphicsRectItem(const QRectF& rect, QGraphicsItem* parent)
	: QGraphicsRectItem(rect, parent), track(-1), Node(nullptr) {
	setZValue(1); // draw over arcs and connections (which have z = 0)
}

myQGraphicsRectItem::~myQGraphicsRectItem() {
}

void myQGraphicsRectItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
	Q_UNUSED(option);
	Q_UNUSED(widget);

	QPen effectPen = pen();
	effectPen.setColor(Qt::blue);

	// change line color when selected
	if (graphicsEffect()) {
		painter->setPen(effectPen);
		painter->setBrush(effectPen.color());
	} else {
		painter->setPen(pen());
		painter->setBrush(brush());
	}

	painter->drawRect(rect());
}