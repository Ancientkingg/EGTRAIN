#include "graphics/items/TrainBadgeItem.h"

TrainBadgeItem::TrainBadgeItem(QGraphicsItem* parent)
	: QGraphicsItem(parent), m_visual(classifyTrainType("", "")), m_icon(m_visual.iconResource), m_reversed(false), m_compact(false) {
	setFlag(QGraphicsItem::ItemIgnoresTransformations);
	setAcceptedMouseButtons(Qt::NoButton);
	setZValue(5);
	updateToolTip();
}

void TrainBadgeItem::setIdentifier(const QString& identifier) {
	if (m_identifier == identifier)
		return;
	m_identifier = identifier;
	updateToolTip();
	update();
}

void TrainBadgeItem::setSpeedText(const QString& speedText) {
	if (m_speedText == speedText)
		return;
	m_speedText = speedText;
	updateToolTip();
	update();
}

void TrainBadgeItem::setSpeedVisible(bool visible) {
	if (m_speedVisible == visible)
		return;
	m_speedVisible = visible;
	updateToolTip();
	update();
}

void TrainBadgeItem::setTrainVisual(const TrainVisual& visual) {
	m_visual = visual;
	m_icon = QPixmap(m_visual.iconResource);
	update();
}

void TrainBadgeItem::setReversed(bool reversed) {
	if (m_reversed == reversed)
		return;
	m_reversed = reversed;
	update();
}

void TrainBadgeItem::setCompact(bool compact) {
	if (m_compact == compact)
		return;
	prepareGeometryChange();
	m_compact = compact;
	update();
}

QRectF TrainBadgeItem::badgeRect() const {
	return QRectF(0.0, 0.0, m_compact ? 92.0 : 156.0, m_compact ? 24.0 : 32.0);
}

QRectF TrainBadgeItem::boundingRect() const {
	return badgeRect();
}

void TrainBadgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
	Q_UNUSED(option);
	Q_UNUSED(widget);

	const QRectF body = badgeRect().adjusted(1.0, 1.0, -1.0, -1.0);
	QPen outline(m_visual.outline);
	outline.setWidthF(1.2);
	painter->setPen(outline);
	painter->setBrush(badgeSurfaceColor());
	const qreal radius = trainBadgeCornerRadius(m_visual.shape);
	painter->drawRoundedRect(body, radius, radius);

	QPolygonF direction;
	const qreal centerY = body.center().y();
	if (m_reversed) {
		direction << QPointF(body.left() + 5.0, centerY)
				  << QPointF(body.left() + 11.0, centerY - 4.0)
				  << QPointF(body.left() + 11.0, centerY + 4.0);
	} else {
		direction << QPointF(body.right() - 5.0, centerY)
				  << QPointF(body.right() - 11.0, centerY - 4.0)
				  << QPointF(body.right() - 11.0, centerY + 4.0);
	}
	painter->setPen(Qt::NoPen);
	painter->setBrush(badgePrimaryTextColor());
	painter->drawPolygon(direction);

	QPen plateOutline(m_visual.outline);
	plateOutline.setWidthF(0.8);
	painter->setPen(plateOutline);
	painter->setBrush(m_visual.fill);
	painter->drawRoundedRect(iconPlateRect(body, m_reversed), 2.0, 2.0);

	painter->setPen(badgePrimaryTextColor());
	painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
	painter->drawPixmap(iconRect(body, m_reversed), m_icon, m_icon.rect());
	QFont font = painter->font();
	font.setPointSize(m_compact ? 9 : 10);
	font.setBold(true);
	painter->setFont(font);
	const QFontMetricsF metrics(font);
	const QString visibleSpeedText = m_speedVisible ? m_speedText : QString();
	const QRectF identifierRect = identifierTextRect(body, m_compact, m_reversed, metrics, visibleSpeedText);
	const QRectF speedRect = speedTextRect(body, m_compact, m_reversed, metrics, visibleSpeedText);
	painter->drawText(identifierRect, Qt::AlignVCenter | Qt::AlignLeft,
		elidedIdentifier(m_identifier, metrics, identifierRect));
	if (!m_compact) {
		QFont speedFont = font;
		speedFont.setPointSize(8);
		speedFont.setBold(false);
		painter->setFont(speedFont);
		painter->setPen(badgeSecondaryTextColor());
		painter->drawText(speedRect, Qt::AlignVCenter | Qt::AlignRight, visibleSpeedText);
	}
}

void TrainBadgeItem::updateToolTip() {
	QString tooltip;
	if (!m_identifier.isEmpty())
		tooltip = QStringLiteral("Train: %1").arg(m_identifier);
	if (!m_speedText.isEmpty()) {
		if (!tooltip.isEmpty())
			tooltip += QLatin1Char('\n');
		tooltip += QStringLiteral("Speed: %1").arg(m_speedText);
	}
	setToolTip(tooltip);
}
