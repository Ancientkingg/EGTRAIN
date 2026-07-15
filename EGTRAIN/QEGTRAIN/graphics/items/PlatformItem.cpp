#include "graphics/items/PlatformItem.h"

PlatformItem::PlatformItem(const QRectF& rect, QGraphicsItem* parent)
	: QGraphicsRectItem(rect, parent), textIcon(nullptr), maxVolume(0) {
	setZValue(2); // draw over arcs and connections (which have z = 0) and nodes (z = 1)
}

PlatformItem::~PlatformItem() {
}

void PlatformItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
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
