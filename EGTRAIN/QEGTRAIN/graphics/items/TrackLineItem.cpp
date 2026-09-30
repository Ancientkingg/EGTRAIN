#include "graphics/items/TrackLineItem.h"

// adjust value of selectionOffset to control the margin used when clicking on the line item
TrackLineItem::TrackLineItem(const QLineF& line, QGraphicsItem* parent)
	: QGraphicsLineItem(line, parent), track(-1), arc(nullptr), selectionOffset(20), m_operationalState(TrackOperationalState::Free) {
	createSelectionPolygon();
}

TrackLineItem::~TrackLineItem() {
}

void TrackLineItem::setOperationalState(TrackOperationalState state) {
	if (m_operationalState == state)
		return;
	m_operationalState = state;
	update();
}

TrackOperationalState TrackLineItem::operationalState() const {
	return m_operationalState;
}

void TrackLineItem::createSelectionPolygon() {
	QPolygonF nPolygon;
	qreal radAngle = line().angle() * M_PI / 180;
	qreal dx = selectionOffset * sin(radAngle);
	qreal dy = selectionOffset * cos(radAngle);
	QPointF offset1 = QPointF(dx, dy);
	QPointF offset2 = QPointF(-dx, -dy);
	nPolygon << line().p1() + offset1
			 << line().p1() + offset2
			 << line().p2() + offset2
			 << line().p2() + offset1;
	selectionPolygon = nPolygon;
	update();
}

QRectF TrackLineItem::boundingRect() const {
	return selectionPolygon.boundingRect();
}

QPainterPath TrackLineItem::shape() const {
	QPainterPath ret;
	ret.addPolygon(selectionPolygon);
	return ret;
}

void TrackLineItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
	Q_UNUSED(option);
	Q_UNUSED(widget);

	QPen stroke = pen();
	if (graphicsEffect())
		stroke.setColor(Qt::blue);
	painter->setPen(stroke);
	painter->drawLine(line());
}
