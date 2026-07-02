#include "platformQGraphicsRectItem.h"

platformQGraphicsRectItem::platformQGraphicsRectItem(const QRectF& rect, QGraphicsItem* parent)
	: QGraphicsRectItem(rect, parent), platform(nullptr), textIcon(nullptr) {
	setZValue(2); // draw over arcs and connections (which have z = 0) and nodes (z = 1)
}

platformQGraphicsRectItem::~platformQGraphicsRectItem() {
}

void platformQGraphicsRectItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
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