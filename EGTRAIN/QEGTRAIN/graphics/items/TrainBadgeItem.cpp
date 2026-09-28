#include "graphics/items/TrainBadgeItem.h"

#include <QFontMetricsF>
#include <QPainterPath>
#include <QStringList>

namespace {
constexpr qreal kAnchorOffsetX = 8.0;
constexpr qreal kAnchorGapY = 8.0;
constexpr qreal kNoseWidth = 3.0;
constexpr qreal kTextGap = 4.0;
constexpr qreal kSpeedGap = 6.0;
constexpr qreal kSidePadding = 3.0;
}

TrainBadgeItem::TrainBadgeItem(QGraphicsItem* parent)
	: QGraphicsItem(parent), m_visual(classifyTrainType("", "")), m_icon(m_visual.iconResource) {
	setFlag(QGraphicsItem::ItemIgnoresTransformations);
	setAcceptedMouseButtons(Qt::NoButton);
	setZValue(5.0);
	updateToolTip();
}

void TrainBadgeItem::setIdentifier(const QString& value) {
	if (m_identifier == value) return;
	prepareGeometryChange(); m_identifier = value; updateToolTip(); update();
}
void TrainBadgeItem::setTooltipDetails(const QString& description, const QString& operatingCode,
		const QString& trainType) {
	if (m_description == description && m_operatingCode == operatingCode && m_trainType == trainType)
		return;
	m_description = description;
	m_operatingCode = operatingCode;
	m_trainType = trainType;
	updateToolTip();
}
void TrainBadgeItem::setSpeedText(const QString& value) {
	if (m_speedText == value) return;
	prepareGeometryChange(); m_speedText = value; updateToolTip(); update();
}
void TrainBadgeItem::setSpeedVisible(bool visible) {
	if (m_speedVisible == visible) return;
	prepareGeometryChange(); m_speedVisible = visible; update();
}
void TrainBadgeItem::setTrainVisual(const TrainVisual& visual) {
	m_visual = visual; m_icon = QPixmap(m_visual.iconResource); update();
}
void TrainBadgeItem::setReversed(bool reversed) {
	if (m_reversed == reversed) return;
	prepareGeometryChange();
	m_reversed = reversed; update();
}
void TrainBadgeItem::setPresentation(Presentation presentation) {
	if (m_presentation == presentation) return;
	prepareGeometryChange(); m_presentation = presentation; update();
}
void TrainBadgeItem::setPromoted(bool promoted) {
	if (m_promoted == promoted) return;
	m_promoted = promoted; setZValue(promoted ? 6.0 : 5.0); update();
}

TrainBadgeItem::Presentation TrainBadgeItem::presentationForZoom(qreal zoom, bool promoted) {
	if (promoted || zoom >= detailedZoomThreshold())
		return Presentation::Detailed;
	return zoom >= identityZoomThreshold() ? Presentation::Identity : Presentation::Overview;
}

QFont TrainBadgeItem::identifierFont() const {
	QFont font; font.setPointSize(m_presentation == Presentation::Identity ? 8 : 9); font.setBold(true); return font;
}
QFont TrainBadgeItem::speedFont() const {
	QFont font = identifierFont(); font.setPointSize(8); font.setBold(false); return font;
}

qreal TrainBadgeItem::badgeWidth() const {
	if (m_presentation == Presentation::Overview) return markerSize().width();
	qreal width = kSidePadding + (m_reversed ? kNoseWidth : 0.0) + 14.0 + kTextGap
		+ QFontMetricsF(identifierFont()).horizontalAdvance(m_identifier) + kSidePadding
		+ (!m_reversed ? kNoseWidth : 0.0);
	if (showsSpeed()) width += kSpeedGap + QFontMetricsF(speedFont()).horizontalAdvance(m_speedText);
	return qMin(maximumWidth(m_presentation), qMax<qreal>(44.0, width));
}

QRectF TrainBadgeItem::badgeRect() const {
	const qreal height = m_presentation == Presentation::Overview ? 16.0
		: m_presentation == Presentation::Identity ? 22.0 : 26.0;
	return QRectF(kAnchorOffsetX, -kAnchorGapY - height, badgeWidth(), height);
}
QRectF TrainBadgeItem::boundingRect() const { return badgeRect().adjusted(-1.0, -1.0, 1.0, 1.0); }
QRectF TrainBadgeItem::iconRect() const {
	const QRectF body = badgeRect();
	const qreal size = m_presentation == Presentation::Overview ? 12.0 : 14.0;
	return QRectF(body.left() + kSidePadding + (m_reversed ? kNoseWidth : 0.0),
		body.center().y() - size / 2.0, size, size);
}
QRectF TrainBadgeItem::speedTextRect() const {
	if (!showsSpeed()) return QRectF();
	const QRectF body = badgeRect();
	const qreal right = body.right() - kSidePadding - (!m_reversed ? kNoseWidth : 0.0);
	const qreal width = qMin(QFontMetricsF(speedFont()).horizontalAdvance(m_speedText), body.width() / 2.0);
	return QRectF(right - width, body.top(), width, body.height());
}
QRectF TrainBadgeItem::identifierTextRect() const {
	if (!showsIdentifier()) return QRectF();
	const QRectF body = badgeRect();
	const qreal left = iconRect().right() + kTextGap;
	const QRectF speed = speedTextRect();
	const qreal right = speed.isEmpty() ? body.right() - kSidePadding - (!m_reversed ? kNoseWidth : 0.0)
		: speed.left() - kSpeedGap;
	return QRectF(left, body.top(), qMax<qreal>(0.0, right - left), body.height());
}
QString TrainBadgeItem::displayedIdentifier() const {
	return QFontMetricsF(identifierFont()).elidedText(m_identifier, Qt::ElideMiddle,
		qMax<qreal>(0.0, identifierTextRect().width()));
}
QPolygonF TrainBadgeItem::directionNose() const {
	const QRectF body = badgeRect().adjusted(0.75, 0.75, -0.75, -0.75);
	const qreal halfHeight = qMin<qreal>(4.0, body.height() / 3.0);
	QPolygonF nose;
	if (m_reversed)
		nose << QPointF(body.left(), body.center().y())
			<< QPointF(body.left() + kNoseWidth + 0.5, body.center().y() - halfHeight)
			<< QPointF(body.left() + kNoseWidth + 0.5, body.center().y() + halfHeight);
	else
		nose << QPointF(body.right(), body.center().y())
			<< QPointF(body.right() - kNoseWidth - 0.5, body.center().y() - halfHeight)
			<< QPointF(body.right() - kNoseWidth - 0.5, body.center().y() + halfHeight);
	return nose;
}

void TrainBadgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
	Q_UNUSED(option); Q_UNUSED(widget);
	painter->setRenderHint(QPainter::Antialiasing, true);
	const QRectF body = badgeRect().adjusted(0.75, 0.75, -0.75, -0.75);
	const QRectF shell = m_reversed ? body.adjusted(kNoseWidth, 0.0, 0.0, 0.0)
		: body.adjusted(0.0, 0.0, -kNoseWidth, 0.0);
	QPainterPath shape; shape.addRoundedRect(shell,
		m_presentation == Presentation::Overview ? 2.0 : 4.0,
		m_presentation == Presentation::Overview ? 2.0 : 4.0);
	QPainterPath nose; nose.addPolygon(directionNose()); shape = shape.united(nose);
	QPen outline(m_promoted ? promotedBorderColor() : m_visual.outline);
	outline.setWidthF(m_promoted ? 1.6 : 1.0);
	painter->setPen(outline); painter->setBrush(badgeSurfaceColor()); painter->drawPath(shape);

	const QRectF icon = iconRect();
	QPen plateOutline(m_visual.outline); plateOutline.setWidthF(0.7);
	painter->setPen(plateOutline); painter->setBrush(m_visual.fill);
	const qreal radius = qMin<qreal>(2.5, trainBadgeCornerRadius(m_visual.shape));
	painter->drawRoundedRect(icon.adjusted(-0.5, -0.5, 0.5, 0.5), radius, radius);
	painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
	painter->drawPixmap(icon, m_icon, m_icon.rect());
	if (!showsIdentifier()) return;
	painter->setFont(identifierFont()); painter->setPen(badgePrimaryTextColor());
	painter->drawText(identifierTextRect(), Qt::AlignVCenter | Qt::AlignLeft, displayedIdentifier());
	if (showsSpeed()) {
		painter->setFont(speedFont()); painter->setPen(badgeSecondaryTextColor());
		painter->drawText(speedTextRect(), Qt::AlignVCenter | Qt::AlignRight, m_speedText);
	}
}

void TrainBadgeItem::updateToolTip() {
	QStringList lines;
	if (!m_description.isEmpty()) lines << QStringLiteral("Train: %1").arg(m_description);
	else if (!m_identifier.isEmpty()) lines << QStringLiteral("Train: %1").arg(m_identifier);
	if (!m_operatingCode.isEmpty()) lines << QStringLiteral("Operating code: %1").arg(m_operatingCode);
	if (!m_speedText.isEmpty()) lines << QStringLiteral("Speed: %1").arg(m_speedText);
	if (!m_trainType.isEmpty()) lines << QStringLiteral("Type: %1").arg(m_trainType);
	setToolTip(lines.join(QLatin1Char('\n')));
}
