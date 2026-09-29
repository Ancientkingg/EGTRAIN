#include "graphics/items/NodeItem.h"

NodeItem::NodeItem(const QRectF& rect, QGraphicsItem* parent)
	: QGraphicsEllipseItem(rect, parent), track(-1), node(nullptr) {
	setZValue(1); // draw over arcs and connections (which have z = 0)
	// A separate transformation-independent child owns the Fit-sized geometry,
	// so painting, culling and semantic hits agree on its actual bounds.
	auto* fitDot = new QGraphicsEllipseItem(QRectF(-1.5, -1.5, 3.0, 3.0), this);
	fitDot->setPos(rect.center());
	fitDot->setFlag(QGraphicsItem::ItemIgnoresTransformations);
	fitDot->setPen(Qt::NoPen);
	fitDot->setBrush(Qt::lightGray);
	fitDot->setAcceptedMouseButtons(Qt::NoButton);
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
