#include "graphics/items/StationCalloutItem.h"

#include "graphics/items/PassengerItem.h"
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace {
constexpr qreal kPadding = 12.0;
constexpr qreal kRowHeight = 17.0;
constexpr qreal kBarHeight = 7.0;
constexpr qreal kNameWidth = 108.0;
constexpr qreal kBarWidth = 150.0;

const QColor kText(0xd7, 0xdd, 0xe4);
const QColor kMuted(0x8b, 0x96, 0xa3);

QFont pixelFont(int pixels, bool bold = false) {
	QFont font;
	font.setPixelSize(pixels);
	font.setBold(bold);
	return font;
}

QFont sectionFont() {
	QFont font = pixelFont(10);
	font.setLetterSpacing(QFont::PercentageSpacing, 106.0);
	return font;
}
} // namespace

StationCalloutItem::StationCalloutItem(QGraphicsItem* parent)
	: QGraphicsItem(parent) {
	setAcceptedMouseButtons(Qt::LeftButton);
	setZValue(CardZValue);
	setVisible(false);
	layout();
}

void StationCalloutItem::setContent(const StationCalloutContent& content) {
	if (content.title == m_content.title && content.subtitle == m_content.subtitle && content.nobodyWaiting == m_content.nobodyWaiting
		&& content.destinationHeader == m_content.destinationHeader && content.moreStations == m_content.moreStations
		&& content.moreCount == m_content.moreCount && content.destinations.size() == m_content.destinations.size()
		&& content.calls.size() == m_content.calls.size()) {
		bool same = true;
		for (qsizetype i = 0; same && i < content.destinations.size(); ++i)
			same = content.destinations[i].name == m_content.destinations[i].name && content.destinations[i].count == m_content.destinations[i].count;
		for (qsizetype i = 0; same && i < content.calls.size(); ++i) {
			const auto& a = content.calls[i];
			const auto& b = m_content.calls[i];
			same = a.time == b.time && a.train == b.train && a.boarded == b.boarded && a.alighted == b.alighted && a.leftBehind == b.leftBehind
				&& a.load == b.load && a.anyLeft == b.anyLeft;
		}
		if (same)
			return;
	}
	prepareGeometryChange();
	m_content = content;
	layout();
	update();
}

void StationCalloutItem::setPixelScale(qreal scale) {
	if (scale <= 0.0)
		scale = 1.0;
	if (scale == m_pixelScale)
		return;
	// the extent in the scene changes with the transform, which Qt tells the index of
	m_pixelScale = scale;
	setTransform(QTransform::fromScale(1.0 / scale, 1.0 / scale));
}

void StationCalloutItem::setShift(qreal shift) {
	const qreal limit = CardWidth / 2.0 - 16.0;
	shift = std::clamp(shift, -limit, limit);
	if (shift == m_shift)
		return;
	prepareGeometryChange();
	m_shift = shift;
	update();
}

void StationCalloutItem::setBelow(bool below) {
	if (below == m_below)
		return;
	prepareGeometryChange();
	m_below = below;
	update();
}

// The height of the card: the title and the subtitle, the waiting section and the calls section.
void StationCalloutItem::layout() {
	qreal height = 10.0 + 18.0 + 16.0 + 6.0;
	if (m_content.nobodyWaiting || !m_content.destinationHeader.isEmpty())
		height += 18.0 + kRowHeight * (m_content.nobodyWaiting ? 1 : static_cast<qreal>(m_content.destinations.size()) + (m_content.moreStations > 0 ? 1 : 0));
	height += 8.0 + 18.0; // the heading of the calls
	height += m_content.calls.isEmpty() ? kRowHeight : kRowHeight * (1 + static_cast<qreal>(m_content.calls.size()));
	m_height = height + 10.0;
}

QRectF StationCalloutItem::cardRect() const {
	return QRectF(-CardWidth / 2.0 + m_shift, m_below ? Gap : -(Gap + m_height), CardWidth, m_height);
}

QString StationCalloutItem::shownSubtitle() const {
	return QFontMetricsF(pixelFont(11)).elidedText(m_content.subtitle, Qt::ElideRight, CardWidth - 2.0 * kPadding);
}

QRectF StationCalloutItem::closeRect() const {
	const QRectF card = cardRect();
	return QRectF(card.right() - 28.0, card.top() + 4.0, 24.0, 24.0);
}

QRectF StationCalloutItem::boundingRect() const {
	return cardRect().united(QRectF(-6.0, -6.0, 12.0, 12.0)).adjusted(-1.0, -1.0, 1.0, 1.0);
}

QPainterPath StationCalloutItem::shape() const {
	QPainterPath path;
	path.addRoundedRect(cardRect(), 8.0, 8.0);
	return path;
}

void StationCalloutItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
	painter->setRenderHint(QPainter::Antialiasing);
	const QRectF card = cardRect();

	// the pointer, from the card to the origin
	QPainterPath pointer;
	const qreal edge = m_below ? card.top() : card.bottom();
	pointer.moveTo(-6.0, edge);
	pointer.lineTo(0.0, 0.0);
	pointer.lineTo(6.0, edge);
	pointer.closeSubpath();
	painter->setPen(QPen(edgeColour(), 1.0));
	painter->setBrush(cardColour());
	painter->drawPath(pointer);
	painter->drawRoundedRect(card, 8.0, 8.0);
	// hide the line of the card where the pointer joins it
	painter->setPen(Qt::NoPen);
	painter->drawRect(QRectF(-5.0, edge + (m_below ? -0.5 : -1.0), 10.0, 1.5));

	const qreal left = card.left() + kPadding;
	const qreal right = card.right() - kPadding;
	qreal y = card.top() + 10.0;

	painter->setFont(pixelFont(13, true));
	painter->setPen(Qt::white);
	const QFontMetricsF titleMetrics(painter->font());
	painter->drawText(QRectF(left, y, right - left - 22.0, 18.0), Qt::AlignVCenter | Qt::AlignLeft,
		titleMetrics.elidedText(m_content.title, Qt::ElideRight, right - left - 22.0));
	painter->setFont(pixelFont(15));
	painter->setPen(kMuted);
	painter->drawText(closeRect(), Qt::AlignCenter, QString(QChar(0x00d7)));
	y += 18.0;
	painter->setFont(pixelFont(11));
	painter->drawText(QRectF(left, y, right - left, 16.0), Qt::AlignVCenter | Qt::AlignLeft, shownSubtitle());
	y += 16.0 + 6.0;

	painter->setFont(sectionFont());
	painter->setPen(kMuted);
	if (m_content.nobodyWaiting || !m_content.destinationHeader.isEmpty()) {
		painter->drawText(QRectF(left, y, right - left, 18.0), Qt::AlignVCenter | Qt::AlignLeft,
			(m_content.destinationHeader.isEmpty() ? QStringLiteral("Waiting, by destination") : m_content.destinationHeader).toUpper());
		y += 18.0;
	}
	painter->setFont(pixelFont(12));
	if (m_content.nobodyWaiting) {
		painter->setPen(kMuted);
		painter->drawText(QRectF(left, y, right - left, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Nobody waiting"));
		y += kRowHeight;
	} else if (!m_content.destinationHeader.isEmpty()) {
		int largest = 1;
		for (const auto& destination : m_content.destinations)
			largest = std::max(largest, destination.count);
		const QFontMetricsF metrics(painter->font());
		for (const auto& destination : m_content.destinations) {
			painter->setPen(kText);
			painter->drawText(QRectF(left, y, kNameWidth, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft,
				metrics.elidedText(destination.name, Qt::ElideRight, kNameWidth - 4.0));
			const qreal width = std::max(3.0, kBarWidth * destination.count / largest);
			painter->setPen(Qt::NoPen);
			painter->setBrush(PassengerItem::crowdingColour(PlatformCrowding::Normal));
			painter->drawRoundedRect(QRectF(left + kNameWidth, y + (kRowHeight - kBarHeight) / 2.0, width, kBarHeight), 3.5, 3.5);
			painter->setPen(kText);
			painter->drawText(QRectF(left, y, right - left, kRowHeight), Qt::AlignVCenter | Qt::AlignRight, QString::number(destination.count));
			y += kRowHeight;
		}
		if (m_content.moreStations > 0) {
			painter->setPen(kMuted);
			painter->drawText(QRectF(left, y, kNameWidth + kBarWidth, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft,
				QStringLiteral("%1 more station%2").arg(m_content.moreStations).arg(m_content.moreStations == 1 ? "" : "s"));
			painter->drawText(QRectF(left, y, right - left, kRowHeight), Qt::AlignVCenter | Qt::AlignRight, QString::number(m_content.moreCount));
			y += kRowHeight;
		}
	}

	y += 8.0;
	painter->setFont(sectionFont());
	painter->setPen(kMuted);
	painter->drawText(QRectF(left, y, right - left, 18.0), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("LATEST CALLS"));
	y += 18.0;
	painter->setFont(pixelFont(12));
	if (m_content.calls.isEmpty()) {
		painter->drawText(QRectF(left, y, right - left, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("No calls yet"));
		return;
	}
	// Time and Train are left aligned from their columns; the counts are right aligned to theirs.
	const qreal trainLeft = left + 62.0;
	const qreal onRight = left + 212.0;
	const qreal offRight = left + 244.0;
	const qreal leftRight = left + 276.0;
	const auto cell = [&painter](qreal x, qreal width, qreal top, const QString& text, Qt::Alignment align) {
		painter->drawText(QRectF(x, top, width, kRowHeight), Qt::AlignVCenter | align, text);
	};
	painter->setFont(pixelFont(10));
	painter->setPen(kMuted);
	cell(left, 60.0, y, QStringLiteral("Time"), Qt::AlignLeft);
	cell(trainLeft, 60.0, y, QStringLiteral("Train"), Qt::AlignLeft);
	cell(onRight - 30.0, 30.0, y, QStringLiteral("On"), Qt::AlignRight);
	cell(offRight - 30.0, 30.0, y, QStringLiteral("Off"), Qt::AlignRight);
	cell(leftRight - 30.0, 30.0, y, QStringLiteral("Left"), Qt::AlignRight);
	cell(right - 36.0, 36.0, y, QStringLiteral("Load"), Qt::AlignRight);
	y += kRowHeight;
	painter->setFont(pixelFont(12));
	const QFontMetricsF metrics(painter->font());
	for (const auto& call : m_content.calls) {
		painter->setPen(kText);
		cell(left, 60.0, y, call.time, Qt::AlignLeft);
		// the middle of a long name goes, as the ends tell the trains of a line apart
		cell(trainLeft, onRight - 30.0 - trainLeft - 4.0, y, metrics.elidedText(call.train, Qt::ElideMiddle, onRight - 30.0 - trainLeft - 4.0), Qt::AlignLeft);
		cell(onRight - 30.0, 30.0, y, call.boarded, Qt::AlignRight);
		cell(offRight - 30.0, 30.0, y, call.alighted, Qt::AlignRight);
		painter->setPen(call.anyLeft ? leftBehindColour() : kText);
		cell(leftRight - 30.0, 30.0, y, call.leftBehind, Qt::AlignRight);
		painter->setPen(kText);
		cell(right - 36.0, 36.0, y, call.load, Qt::AlignRight);
		y += kRowHeight;
	}
}

void StationCalloutItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
	event->accept();
	if (event->button() == Qt::LeftButton && closeRect().contains(event->pos()) && onClose)
		onClose();
}
