#include "graphics/items/TrainBodyItem.h"

TrainBodyItem::TrainBodyItem(const QPolygonF& polygon, QGraphicsItem* parent)
	: QGraphicsPolygonItem(polygon, parent) {
	setZValue(3); // draw on top of everything (except TrainItemGroup)

	train = nullptr;
	index = -1;
}

TrainBodyItem::~TrainBodyItem() {
}

void TrainBodyItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
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

	painter->drawPolygon(polygon());
}