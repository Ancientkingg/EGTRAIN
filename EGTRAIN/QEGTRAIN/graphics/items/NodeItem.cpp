#include "graphics/items/NodeItem.h"

NodeItem::NodeItem(const QRectF& rect, QGraphicsItem* parent)
	: QGraphicsEllipseItem(rect, parent), track(-1), node(nullptr) {
	setZValue(1); // draw over arcs and connections (which have z = 0)
}

NodeItem::~NodeItem() {
}

void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
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

	painter->drawEllipse(rect());
}
