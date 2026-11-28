#include "graphics/items/StationCalloutItem.h"

#include "graphics/items/PassengerItem.h"
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneWheelEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace {
constexpr qreal kPadding = 12.0;
constexpr qreal kTopPadding = 10.0;
constexpr qreal kBottomPadding = 10.0;
constexpr qreal kTitleHeight = 18.0;
constexpr qreal kSubtitleHeight = 16.0;
constexpr qreal kHeaderGap = 6.0;
constexpr qreal kSectionHeadingHeight = 18.0;
constexpr qreal kSectionGap = 8.0;
constexpr qreal kRowHeight = 17.0;
constexpr qreal kBarHeight = 7.0;
constexpr qreal kNameWidth = 108.0;
constexpr qreal kBarWidth = 150.0;
constexpr qreal kScrollBarWidth = 4.0;
constexpr qreal kScrollBarInset = 4.0;
constexpr qreal kMinThumbHeight = 16.0;
constexpr qreal kWheelRowsPerNotch = 3.0;

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
	if (content == m_content)
		return;
	prepareGeometryChange();
	const bool sameStation = content.title == m_content.title;
	m_content = content;
	if (!sameStation)
		m_scrollOffset = 0.0;
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

void StationCalloutItem::setMaxHeight(qreal maxHeight) {
	if (maxHeight <= 0.0)
		maxHeight = 0.0;
	if (maxHeight == m_maxHeight)
		return;
	const qreal oldHeight = m_height;
	const qreal oldScroll = m_scrollOffset;
	const qreal oldMaxScroll = m_maxScrollOffset;
	m_maxHeight = maxHeight;
	if (fixedHeight() + layoutDestinationsHeight() != oldHeight)
		prepareGeometryChange();
	layout();
	if (m_height != oldHeight || m_scrollOffset != oldScroll || m_maxScrollOffset != oldMaxScroll)
		update();
}

qreal StationCalloutItem::fullHeight() const {
	return fixedHeight() + fullDestinationsHeight();
}

void StationCalloutItem::setScrollOffset(qreal offset) {
	offset = std::clamp(offset, 0.0, m_maxScrollOffset);
	if (offset == m_scrollOffset)
		return;
	m_scrollOffset = offset;
	update();
}

qreal StationCalloutItem::fixedHeight() const {
	qreal height = kTopPadding + kTitleHeight + kSubtitleHeight + (m_content.held.isEmpty() ? 0.0 : kSubtitleHeight) + kHeaderGap;
	if (m_content.nobodyWaiting || m_content.destinationHeader) {
		height += kSectionHeadingHeight;
		if (m_content.nobodyWaiting)
			height += kRowHeight;
	}
	height += kSectionGap + kSectionHeadingHeight;
	height += m_content.calls.isEmpty() ? kRowHeight : kRowHeight * (1 + static_cast<qreal>(m_content.calls.size()));
	return height + kBottomPadding;
}

qreal StationCalloutItem::fullDestinationsHeight() const {
	if (m_content.nobodyWaiting || !m_content.destinationHeader)
		return 0.0;
	return kRowHeight * static_cast<qreal>(m_content.destinations.size());
}

qreal StationCalloutItem::layoutDestinationsHeight() const {
	const qreal fullDest = fullDestinationsHeight();
	if (fullDest <= 0.0)
		return 0.0;
	const qreal fixed = fixedHeight();
	if (m_maxHeight > 0.0 && fixed + fullDest > m_maxHeight)
		return std::min(fullDest, std::max(kRowHeight, m_maxHeight - fixed));
	return fullDest;
}

// The height of the card: the title and the subtitle, the waiting section and the calls section.
void StationCalloutItem::layout() {
	const qreal fixed = fixedHeight();
	const qreal fullDest = fullDestinationsHeight();
	m_destinationsHeight = layoutDestinationsHeight();
	m_maxScrollOffset = std::max<qreal>(0.0, fullDest - m_destinationsHeight);
	m_scrollOffset = std::clamp(m_scrollOffset, 0.0, m_maxScrollOffset);
	if (!canScroll())
		m_draggingScrollBar = false;
	m_height = fixed + m_destinationsHeight;
}

QRectF StationCalloutItem::cardRect() const {
	return QRectF(-CardWidth / 2.0 + m_shift, m_below ? Gap : -(Gap + m_height), CardWidth, m_height);
}

QRectF StationCalloutItem::destinationsRect() const {
	if (m_destinationsHeight <= 0.0)
		return {};
	const QRectF card = cardRect();
	const qreal top = card.top() + kTopPadding + kTitleHeight + kSubtitleHeight
		+ (m_content.held.isEmpty() ? 0.0 : kSubtitleHeight) + kHeaderGap + kSectionHeadingHeight;
	return QRectF(card.left(), top, CardWidth, m_destinationsHeight);
}

QRectF StationCalloutItem::scrollBarRect() const {
	if (!canScroll())
		return {};
	const QRectF area = destinationsRect();
	return QRectF(area.right() - kScrollBarInset - kScrollBarWidth, area.top(), kScrollBarWidth, area.height());
}

QRectF StationCalloutItem::scrollThumbRect() const {
	if (!canScroll())
		return {};
	const QRectF track = scrollBarRect();
	const qreal fullDest = fullDestinationsHeight();
	const qreal thumbHeight = std::clamp(track.height() * (m_destinationsHeight / fullDest),
		std::min(kMinThumbHeight, track.height()), track.height());
	const qreal travel = track.height() - thumbHeight;
	const qreal thumbTop = track.top() + (travel > 0.0 ? travel * (m_scrollOffset / m_maxScrollOffset) : 0.0);
	return QRectF(track.left(), thumbTop, track.width(), thumbHeight);
}

QRectF StationCalloutItem::scrollBarHitRect() const {
	const QRectF track = scrollBarRect();
	return track.isEmpty() ? track : track.adjusted(-4.0, 0.0, 4.0, 0.0);
}

void StationCalloutItem::scrollToThumbTop(qreal thumbTop) {
	if (!canScroll())
		return;
	const QRectF track = scrollBarRect();
	const QRectF thumb = scrollThumbRect();
	const qreal travel = track.height() - thumb.height();
	if (travel <= 0.0)
		return;
	const qreal clamped = std::clamp(thumbTop - track.top(), 0.0, travel);
	setScrollOffset(clamped / travel * m_maxScrollOffset);
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
	qreal y = card.top() + kTopPadding;

	painter->setFont(pixelFont(13, true));
	painter->setPen(Qt::white);
	const QFontMetricsF titleMetrics(painter->font());
	painter->drawText(QRectF(left, y, right - left - 22.0, kTitleHeight), Qt::AlignVCenter | Qt::AlignLeft,
		titleMetrics.elidedText(m_content.title, Qt::ElideRight, right - left - 22.0));
	painter->setFont(pixelFont(15));
	painter->setPen(kMuted);
	painter->drawText(closeRect(), Qt::AlignCenter, QString(QChar(0x00d7)));
	y += kTitleHeight;
	painter->setFont(pixelFont(11));
	painter->drawText(QRectF(left, y, right - left, kSubtitleHeight), Qt::AlignVCenter | Qt::AlignLeft, shownSubtitle());
	y += kSubtitleHeight;
	if (!m_content.held.isEmpty()) {
		painter->setFont(pixelFont(11, true));
		painter->setPen(PassengerItem::crowdingColour(PlatformCrowding::Over));
		painter->drawText(QRectF(left, y, right - left, kSubtitleHeight), Qt::AlignVCenter | Qt::AlignLeft, m_content.held);
		y += kSubtitleHeight;
	}
	y += kHeaderGap;

	painter->setFont(sectionFont());
	painter->setPen(kMuted);
	if (m_content.nobodyWaiting || m_content.destinationHeader) {
		painter->drawText(QRectF(left, y, right - left, kSectionHeadingHeight), Qt::AlignVCenter | Qt::AlignLeft,
			QStringLiteral("Waiting, by destination").toUpper());
		y += kSectionHeadingHeight;
	}
	painter->setFont(pixelFont(12));
	if (m_content.nobodyWaiting) {
		painter->setPen(kMuted);
		painter->drawText(QRectF(left, y, right - left, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Nobody waiting"));
		y += kRowHeight;
	} else if (m_content.destinationHeader) {
		int largest = 1;
		for (const auto& destination : m_content.destinations)
			largest = std::max(largest, destination.count);
		const QFontMetricsF metrics(painter->font());
		const QRectF area = destinationsRect();
		painter->save();
		painter->setClipRect(area);
		qreal rowY = area.top() - m_scrollOffset;
		for (const auto& destination : m_content.destinations) {
			if (rowY + kRowHeight >= area.top() && rowY <= area.bottom()) {
				painter->setPen(kText);
				painter->drawText(QRectF(left, rowY, kNameWidth, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft,
					metrics.elidedText(destination.name, Qt::ElideRight, kNameWidth - 4.0));
				const qreal width = std::max(3.0, kBarWidth * destination.count / largest);
				painter->setPen(Qt::NoPen);
				painter->setBrush(PassengerItem::crowdingColour(PlatformCrowding::Normal));
				painter->drawRoundedRect(QRectF(left + kNameWidth, rowY + (kRowHeight - kBarHeight) / 2.0, width, kBarHeight), 3.5, 3.5);
				painter->setPen(kText);
				painter->drawText(QRectF(left, rowY, right - left, kRowHeight), Qt::AlignVCenter | Qt::AlignRight, QString::number(destination.count));
			}
			rowY += kRowHeight;
		}
		painter->restore();
		if (canScroll()) {
			const qreal radius = kScrollBarWidth / 2.0;
			painter->setPen(Qt::NoPen);
			painter->setBrush(edgeColour());
			painter->drawRoundedRect(scrollBarRect(), radius, radius);
			painter->setBrush(m_draggingScrollBar ? kText : kMuted);
			painter->drawRoundedRect(scrollThumbRect(), radius, radius);
		}
		y += m_destinationsHeight;
	}

	y += kSectionGap;
	painter->setFont(sectionFont());
	painter->setPen(kMuted);
	painter->drawText(QRectF(left, y, right - left, kSectionHeadingHeight), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("LATEST CALLS"));
	y += kSectionHeadingHeight;
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
	if (event->button() != Qt::LeftButton)
		return;
	if (closeRect().contains(event->pos()) && onClose) {
		onClose();
		return;
	}
	if (canScroll() && scrollBarHitRect().contains(event->pos())) {
		m_draggingScrollBar = true;
		const QRectF thumb = scrollThumbRect();
		if (event->pos().y() >= thumb.top() && event->pos().y() <= thumb.bottom())
			m_dragThumbOffset = event->pos().y() - thumb.top();
		else {
			m_dragThumbOffset = thumb.height() / 2.0;
			scrollToThumbTop(event->pos().y() - m_dragThumbOffset);
		}
		update();
	}
}

void StationCalloutItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
	if (m_draggingScrollBar && canScroll()) {
		scrollToThumbTop(event->pos().y() - m_dragThumbOffset);
		event->accept();
		return;
	}
	QGraphicsItem::mouseMoveEvent(event);
}

void StationCalloutItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
	if (m_draggingScrollBar) {
		m_draggingScrollBar = false;
		update();
		event->accept();
		return;
	}
	QGraphicsItem::mouseReleaseEvent(event);
}

void StationCalloutItem::wheelEvent(QGraphicsSceneWheelEvent* event) {
	if (!canScroll() || !destinationsRect().contains(event->pos())
		|| (event->modifiers() & Qt::ControlModifier)) {
		event->ignore();
		return;
	}
	qreal step = 0.0;
	if (!event->pixelDelta().isNull()) {
		if (event->pixelDelta().y() == 0) {
			event->ignore();
			return;
		}
		step = -static_cast<qreal>(event->pixelDelta().y());
	} else if (event->orientation() == Qt::Vertical && event->delta() != 0) {
		step = -static_cast<qreal>(event->delta()) / 120.0 * (kWheelRowsPerNotch * kRowHeight);
	} else {
		event->ignore();
		return;
	}
	setScrollOffset(m_scrollOffset + step);
	event->accept();
}
