#include "graphics/items/TrainBadgeItem.h"

#include <QFontMetricsF>
#include <QPainterPath>
#include <QStringList>

namespace {
constexpr qreal kAnchorOffsetX = 8.0;
constexpr qreal kAnchorGapY = 8.0;
// The outline of the speed reaches this far beyond its glyphs, in logical pixels.
constexpr qreal kOutlineWidth = 1.0;
// Over the trains (z 4) and under the notices of a virtual coupling (z 4.4), the station names
// (StationNameItem::NameZValue, 4.5), which stay readable, and the callouts of the trains and the details of a passenger
// (z 5). A promoted badge goes over the others.
constexpr qreal kBadgeZValue = 4.2;
constexpr qreal kPromotedBadgeZValue = 4.3;
}

TrainBadgeItem::TrainBadgeItem(QGraphicsItem* parent)
	: QGraphicsItem(parent), m_visual(classifyTrainType("", "")), m_icon(m_visual.iconResource) {
	setFlag(QGraphicsItem::ItemIgnoresTransformations);
	setAcceptedMouseButtons(Qt::NoButton);
	setZValue(kBadgeZValue);
	updateToolTip();
}

void TrainBadgeItem::setIdentifier(const QString& value) {
	if (m_identifier == value) return;
	m_identifier = value;
	updateToolTip();
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
	prepareGeometryChange();
	m_speedText = value;
	updateToolTip();
	update();
}
void TrainBadgeItem::setSpeedVisible(bool visible) {
	if (m_speedVisible == visible) return;
	prepareGeometryChange();
	m_speedVisible = visible;
	update();
}
void TrainBadgeItem::setTrainVisual(const TrainVisual& visual) {
	m_visual = visual;
	m_icon = QPixmap(m_visual.iconResource);
	update();
}
void TrainBadgeItem::setReversed(bool reversed) {
	m_reversed = reversed;
}
void TrainBadgeItem::setPresentation(Presentation presentation) {
	if (m_presentation == presentation) return;
	prepareGeometryChange();
	m_presentation = presentation;
	update();
}
void TrainBadgeItem::setPromoted(bool promoted) {
	if (m_promoted == promoted) return;
	m_promoted = promoted;
	setZValue(promoted ? kPromotedBadgeZValue : kBadgeZValue);
	update();
}

TrainBadgeItem::Presentation TrainBadgeItem::presentationForZoom(qreal zoom, bool promoted) {
	if (promoted || zoom >= detailedZoomThreshold())
		return Presentation::Detailed;
	return zoom >= identityZoomThreshold() ? Presentation::Identity : Presentation::Overview;
}

QFont TrainBadgeItem::speedFont() const {
	QFont font;
	font.setPointSize(8);
	return font;
}

// The text starts kAnchorOffsetX to the right of the position, on a line that ends kAnchorGapY above it.
QPointF TrainBadgeItem::speedBaseline() const {
	return QPointF(kAnchorOffsetX, -kAnchorGapY - QFontMetricsF(speedFont()).descent());
}
QRectF TrainBadgeItem::speedTextRect() const {
	if (!showsSpeed()) return QRectF();
	return QFontMetricsF(speedFont()).boundingRect(m_speedText).translated(speedBaseline());
}
QRectF TrainBadgeItem::boundingRect() const {
	if (!showsSpeed()) return QRectF();
	const qreal margin = kOutlineWidth + 1.0;
	return speedTextRect().adjusted(-margin, -margin, margin, margin);
}
QPainterPath TrainBadgeItem::shape() const { return QPainterPath(); }

void TrainBadgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
	Q_UNUSED(option);
	Q_UNUSED(widget);
	if (!showsSpeed()) return;
	// The bodies of the train show the train itself, so the badge adds only the speed. The outline keeps the
	// white text readable over the light grey tracks and over the trains.
	QPainterPath text;
	text.addText(speedBaseline(), speedFont(), m_speedText);
	painter->setRenderHint(QPainter::Antialiasing);
	painter->strokePath(text, QPen(canvasColor(), 2.0 * kOutlineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter->fillPath(text, QColor(Qt::white));
}

void TrainBadgeItem::updateToolTip() {
	setToolTip(QString());
}
