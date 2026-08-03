#include "graphics/items/SignalItem.h"

#include "graphics/VisualPolish.h"

SignalItem::SignalItem(const QRectF& rect, QGraphicsItem* parent)
	: QGraphicsEllipseItem(rect, parent), m_aspectCode(-1), m_lampColor(QColor(128, 128, 128)) {
	setZValue(2); // draw over arcs and connections (which have z = 0), and nodes (z = 1)

	// initialize parameters
	trackID = -1;
	X = -1;
	sectionAheadLength = sectionBehindLength = 0.0;
	sectionAheadTrackId = sectionBehindTrackId = -1;
	reversedDirection = false;
	setAspectCode(180);
}

SignalItem::~SignalItem() {
}

void SignalItem::setAspectCode(int code) {
	if (m_aspectCode == code)
		return;
	m_aspectCode = code;
	m_lampColor = classifySignalAspect(code).lamp;
	update();
}

int SignalItem::aspectCode() const {
	return m_aspectCode;
}

void SignalItem::setReversedDirection(bool reversed) {
	if (reversedDirection == reversed)
		return;
	reversedDirection = reversed;
	update();
}

void SignalItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
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
		painter->setBrush(m_lampColor);
	}

	painter->drawEllipse(rect());

	// restrained direction cue, scaled with the marker
	const QPointF center = rect().center();
	QPolygonF cue;
	if (reversedDirection) {
		cue << center + QPointF(-0.3 * rect().width(), 0.0)
		    << center + QPointF(0.1 * rect().width(), -0.25 * rect().height())
		    << center + QPointF(0.1 * rect().width(), 0.25 * rect().height());
	} else {
		cue << center + QPointF(0.3 * rect().width(), 0.0)
		    << center + QPointF(-0.1 * rect().width(), -0.25 * rect().height())
		    << center + QPointF(-0.1 * rect().width(), 0.25 * rect().height());
	}
	painter->setBrush(QColor("#0d131a"));
	painter->drawPolygon(cue);
}
