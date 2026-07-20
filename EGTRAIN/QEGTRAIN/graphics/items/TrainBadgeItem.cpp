#include "graphics/items/TrainBadgeItem.h"

TrainBadgeItem::TrainBadgeItem(QGraphicsItem* parent)
	: QGraphicsItem(parent), m_visual(classifyTrainType("", "")), m_icon(m_visual.iconResource), m_reversed(false), m_compact(false) {
	setFlag(QGraphicsItem::ItemIgnoresTransformations);
	setZValue(5);
}

void TrainBadgeItem::setIdentifier(const QString& identifier) {
	if (m_identifier == identifier)
		return;
	m_identifier = identifier;
	update();
}

void TrainBadgeItem::setSpeedText(const QString& speedText) {
	if (m_speedText == speedText)
		return;
	m_speedText = speedText;
	update();
}

void TrainBadgeItem::setSpeedVisible(bool visible) {
	if (m_speedVisible == visible)
		return;
	m_speedVisible = visible;
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
	return QRectF(0.0, 0.0, m_compact ? 76.0 : 132.0, m_compact ? 20.0 : 28.0);
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
	painter->setBrush(m_visual.fill);
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
	painter->setBrush(m_visual.outline);
	painter->drawPolygon(direction);

	const QColor textColor = m_visual.fill.lightness() < 150 ? QColor(Qt::white) : QColor("#16202a");
	painter->setPen(textColor);
	painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
	painter->drawPixmap(iconRect(body, m_reversed), m_icon, m_icon.rect());
	QFont font = painter->font();
	font.setPointSize(m_compact ? 8 : 9);
	font.setBold(true);
	painter->setFont(font);
	const QFontMetricsF metrics(font);
	const QString visibleSpeedText = m_speedVisible ? m_speedText : QString();
	const QRectF identifierRect = identifierTextRect(body, m_compact, m_reversed, metrics, visibleSpeedText);
	painter->drawText(identifierRect, Qt::AlignVCenter | Qt::AlignLeft,
		elidedIdentifier(m_identifier, metrics, identifierRect));
	if (!m_compact)
		painter->drawText(speedTextRect(body, m_compact, m_reversed, metrics, visibleSpeedText),
			Qt::AlignVCenter | Qt::AlignRight, visibleSpeedText);
}
