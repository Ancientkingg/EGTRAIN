#include "graphics/items/signallingQGraphicsEllipseItem.h"

signallingQGraphicsEllipseItem::signallingQGraphicsEllipseItem(const QRectF& rect, QGraphicsItem* parent)
	: QGraphicsEllipseItem(rect, parent) {
	setZValue(2); // draw over arcs and connections (which have z = 0), and nodes (z = 1)

	// initialize parameters
	trackID = -1;
	X = -1;
	sectionAhead = sectionBehind = nullptr;
	reversedDirection = false;
}

signallingQGraphicsEllipseItem::~signallingQGraphicsEllipseItem() {
}

void signallingQGraphicsEllipseItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
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