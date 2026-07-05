#include "graphics/items/StationNodeItem.h"

StationNodeItem::StationNodeItem(const QRectF& rect, QGraphicsItem* parent)
	: QGraphicsRectItem(rect, parent), track(-1), node(nullptr) {
	setZValue(1); // draw over arcs and connections (which have z = 0)
}

StationNodeItem::~StationNodeItem() {
}

void StationNodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
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
