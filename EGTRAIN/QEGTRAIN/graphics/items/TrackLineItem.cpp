#include "graphics/items/TrackLineItem.h"

// adjust value of selectionOffset to control the margin used when clicking on the line item
TrackLineItem::TrackLineItem(const QLineF& line, QGraphicsItem* parent)
	: QGraphicsLineItem(line, parent), track(-1), arc(nullptr), selectionOffset(20), m_operationalState(TrackOperationalState::Free),
	  m_occupied(false) {
	createSelectionPolygon();
}

TrackLineItem::~TrackLineItem() {
}

void TrackLineItem::setOperationalState(TrackOperationalState state, bool occupied) {
	occupied = occupied || state == TrackOperationalState::Occupied;
	if (m_operationalState == state && m_occupied == occupied)
		return;
	m_operationalState = state;
	m_occupied = occupied;
	update();
}

TrackOperationalState TrackLineItem::operationalState() const {
	return m_operationalState;
}

bool TrackLineItem::isOccupied() const {
	return m_occupied;
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
	selectionPolygon = std::move(nPolygon);
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

	// While a train occupies the track, red takes the place of the speed colour at the same width.
	QPen stroke = pen();
	if (m_occupied)
		stroke.setColor(occupiedTrackColor());
	painter->setPen(stroke);
	painter->drawLine(line());
}
