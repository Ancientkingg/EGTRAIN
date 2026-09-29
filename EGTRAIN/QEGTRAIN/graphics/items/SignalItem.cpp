#include "graphics/items/SignalItem.h"

#include "graphics/VisualPolish.h"

#include <algorithm>

SignalItem::SignalItem(const QRectF& rect, QGraphicsItem* parent)
	: QGraphicsEllipseItem(rect, parent), m_aspectCode(-1), m_lampColor(QColor(128, 128, 128)) {
	setZValue(2); // draw over arcs and connections (which have z = 0), and nodes (z = 1)
	setFlag(QGraphicsItem::ItemIgnoresTransformations);

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

	// Preserve each distinct aspect, including Stop, with equal-sized sectors.
	// The central count distinguishes even a same-aspect cluster from a lone plate.
	painter->setPen(QPen(graphicsEffect() ? QColor(110, 170, 255) : Qt::white, 1.0));
	if (m_groupedSignals.size() > 1) {
		QVector<QColor> colors;
		for (const int code : {0, 75, 180, -1}) {
			for (const auto& member : m_groupedSignals) {
				if (classifySignalCue(member.first) == classifySignalCue(code)) {
					colors.append(classifySignalAspect(member.first).lamp);
					break;
				}
			}
		}
		for (int i = 0; i < colors.size(); ++i) {
			painter->setBrush(colors.at(i));
			painter->drawPie(rect(), 90 * 16 - i * 360 * 16 / colors.size(),
				-360 * 16 / colors.size());
		}
		painter->setPen(QPen(Qt::white, 1.0));
		painter->setBrush(QColor(0, 0, 0, 200));
		const QRectF countRect = rect().adjusted(rect().width() * 0.19, rect().height() * 0.19,
			-rect().width() * 0.19, -rect().height() * 0.19);
		painter->drawEllipse(countRect);
		QFont font = painter->font();
		font.setPixelSize(qMax(6, qRound(8.0 / scale())));
		font.setBold(true);
		painter->setFont(font);
		painter->drawText(countRect, Qt::AlignCenter,
			m_groupedSignals.size() <= 9 ? QString::number(m_groupedSignals.size()) : QStringLiteral("+"));
	} else {
		painter->setBrush(m_lampColor);
		painter->drawEllipse(rect());
	}
	painter->setPen(QPen(Qt::white, 1.0));
	const QPointF center = rect().center();
	for (const bool reversed : {true, false}) {
		if (m_groupedSignals.size() > 1) {
			bool present = false;
			for (const auto& member : m_groupedSignals)
				present |= member.second == reversed;
			if (!present)
				continue;
		} else if (reversedDirection != reversed) {
			continue;
		}
		const qreal direction = reversed ? -1.0 : 1.0;
		painter->drawLine(center + QPointF(direction * rect().width() * 0.55, -2.0),
			center + QPointF(direction * rect().width() * 0.55, 2.0));
	}
}
