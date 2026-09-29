#include "graphics/items/SignalItem.h"

#include "graphics/VisualPolish.h"

#include <algorithm>

SignalItem::SignalItem(const QRectF& rect, QGraphicsItem* parent)
	: QGraphicsEllipseItem(rect, parent), m_aspectCode(-1), m_lampColor(QColor(128, 128, 128)) {
	setZValue(2); // draw over arcs and connections (which have z = 0), and nodes (z = 1)
	// Invisible picking geometry stays device-sized while the actual head scales
	// with the authored scene. NetworkScene resolves child hits to this signal.
	auto* target = new QGraphicsEllipseItem(QRectF(-6.0, -6.0, 12.0, 12.0), this);
	target->setPos(rect.center());
	target->setFlag(QGraphicsItem::ItemIgnoresTransformations);
	target->setPen(Qt::NoPen);
	target->setBrush(Qt::NoBrush);
	target->setAcceptedMouseButtons(Qt::NoButton);

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

void SignalItem::setGroupedSignals(const QVector<QPair<int, bool>>& aspects) {
	if (std::equal(m_groupedSignals.cbegin(), m_groupedSignals.cend(), aspects.cbegin(), aspects.cend()))
		return;
	m_groupedSignals = aspects;
	update();
}

QString SignalItem::inspectionIdentity() const {
	if (!m_inspectionIdentity.isEmpty())
		return m_inspectionIdentity;
	return QStringLiteral("Track %1 at %2 km; ahead %3; behind %4")
		.arg(trackID).arg(X, 0, 'g', 10)
		.arg(QString::fromStdString(sectionAheadId.empty() ? "none" : sectionAheadId))
		.arg(QString::fromStdString(sectionBehindId.empty() ? "none" : sectionBehindId));
}

QRectF SignalItem::boundingRect() const {
	return QGraphicsEllipseItem::boundingRect().adjusted(-2.0, -2.0, 2.0, 2.0);
}

void SignalItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
	Q_UNUSED(option);
	Q_UNUSED(widget);

	painter->setPen(pen());
	painter->setBrush(m_lampColor);
	painter->drawEllipse(rect());
}
