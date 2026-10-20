#include "graphics/items/NodeItem.h"

NodeItem::NodeItem(const QRectF& rect, QGraphicsItem* parent)
	: QGraphicsEllipseItem(rect, parent), track(-1), node(nullptr) {
	setZValue(1); // draw over arcs and connections (which have z = 0)
	// A transparent device-space child preserves the existing Fit hit target,
	// but only the parent scene-sized dot paints.
	auto* target = new QGraphicsEllipseItem(QRectF(-1.5, -1.5, 3.0, 3.0), this);
	target->setPos(rect.center());
	target->setFlag(QGraphicsItem::ItemIgnoresTransformations);
	target->setPen(Qt::NoPen);
	target->setBrush(Qt::NoBrush);
	target->setAcceptedMouseButtons(Qt::NoButton);
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
