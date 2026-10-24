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

	painter->setPen(pen());
	painter->setBrush(brush());

	painter->drawEllipse(rect());
}
