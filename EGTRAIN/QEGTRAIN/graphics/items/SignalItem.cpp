#include "graphics/items/SignalItem.h"

SignalItem::SignalItem(const QRectF& rect, QGraphicsItem* parent)
	: QGraphicsEllipseItem(rect, parent), m_aspectCode(-1), m_aspectIcon(":/icons/signal-neutral.svg") {
	setFlag(QGraphicsItem::ItemIgnoresTransformations);
	setZValue(2); // draw over arcs and connections (which have z = 0), and nodes (z = 1)
	QRectF fixedHousing = rect;
	fixedHousing.setSize(QSizeF(12.0, 16.0));
	fixedHousing.moveCenter(rect.center());
	setRect(fixedHousing);

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
	m_aspectIcon = QPixmap(classifySignalAspect(code).iconResource);
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

	QPen housingPen(QColor("#0d131a"));
	housingPen.setWidthF(1.0);
	painter->setPen(housingPen);
	painter->setBrush(QColor("#26313b"));
	painter->drawRoundedRect(rect(), 3.0, 3.0);

	painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
	const QRectF iconRect(rect().left(), rect().top(), rect().width(), rect().width());
	painter->drawPixmap(iconRect, m_aspectIcon, m_aspectIcon.rect());

	const qreal y = rect().bottom() - 3.0;
	QPolygonF cue;
	if (reversedDirection) {
		cue << QPointF(rect().left() + 3.0, y) << QPointF(rect().left() + 7.0, y - 2.5) << QPointF(rect().left() + 7.0, y + 2.5);
	} else {
		cue << QPointF(rect().right() - 3.0, y) << QPointF(rect().right() - 7.0, y - 2.5) << QPointF(rect().right() - 7.0, y + 2.5);
	}
	painter->setBrush(QColor("#f2f5f7"));
	painter->drawPolygon(cue);
}
