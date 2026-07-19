#include "graphics/items/StationOverlayItem.h"

#include <QGraphicsSceneHoverEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace {
constexpr qreal kSymbolPixels = 24.0;
constexpr qreal kLabelPixels = 11.0;
constexpr qreal kLabelGap = 8.0;

int classPriority(StationVisualKind kind) {
	return kind == StationVisualKind::Platform ? 1 : 0;
}
}

StationOverlayItem::StationOverlayItem(const QString& stationName, const QPointF& stableAnchor,
	const StationVisual& visual, int degree, QGraphicsItem* parent)
	: QGraphicsItem(parent), m_stationName(stationName), m_stableAnchor(stableAnchor),
	  m_visual(visual), m_degree(std::max(0, degree)) {
	setFlag(QGraphicsItem::ItemIgnoresTransformations);
	setFlag(QGraphicsItem::ItemIsSelectable);
	setAcceptHoverEvents(true);
	setAcceptedMouseButtons(Qt::NoButton);
	setZValue(3.0);
	m_labelFont.setPixelSize(static_cast<int>(kLabelPixels));
	const QPixmap sourceSymbol(m_visual.iconResource);
	if (!sourceSymbol.isNull())
		m_symbol = sourceSymbol.scaled(QSize(static_cast<int>(kSymbolPixels),
			static_cast<int>(kSymbolPixels)), Qt::KeepAspectRatio, Qt::SmoothTransformation);
	m_displayName = displayName(m_stationName);
	m_symbolRect = QRectF(-kSymbolPixels / 2.0, -kSymbolPixels / 2.0, kSymbolPixels, kSymbolPixels);
	m_labelSide = LabelSide::Right;
	rebuildGeometry();
	setPos(m_stableAnchor);
}

StationOverlayItem::~StationOverlayItem() = default;

QString StationOverlayItem::displayName() const {
	return m_displayName;
}

QString StationOverlayItem::displayName(const QString& stationName) {
	QString spaced = stationName;
	for (int i = 1; i < spaced.size(); ++i) {
		const auto character = static_cast<unsigned char>(spaced.at(i).toLatin1());
		if (std::isupper(character)) {
			spaced.insert(i, QLatin1Char(' '));
			++i;
		}
	}
	return spaced;
}

QString StationOverlayItem::displayName(const std::string& stationName) {
	return displayName(QString::fromStdString(stationName));
}

void StationOverlayItem::setStableAnchor(const QPointF& anchor) {
	m_stableAnchor = anchor;
	setPos(anchor);
}

void StationOverlayItem::setViewportOffset(const QPointF& offset) {
	if (m_viewportOffset == offset)
		return;
	prepareGeometryChange();
	m_viewportOffset = offset;
	update();
}

void StationOverlayItem::setLabelSide(LabelSide side) {
	if (m_labelSide == side)
		return;
	prepareGeometryChange();
	m_labelSide = side;
	rebuildGeometry();
	update();
}

QRectF StationOverlayItem::combinedRect() const {
	return translated(m_symbolRect).united(translated(m_labelRect));
}

QRectF StationOverlayItem::translated(const QRectF& rect) const {
	return rect.translated(m_viewportOffset);
}

QRectF StationOverlayItem::boundingRect() const {
	return combinedRect();
}

QPainterPath StationOverlayItem::shape() const {
	QPainterPath path;
	path.addRect(translated(m_symbolRect));
	if (isLabelVisible())
		path.addRect(translated(m_labelRect));
	return path;
}

void StationOverlayItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
	Q_UNUSED(option);
	Q_UNUSED(widget);
	painter->save();
	painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
	painter->translate(m_viewportOffset);
	if (!m_symbol.isNull()) {
		painter->drawPixmap(m_symbolRect, m_symbol, m_symbol.rect());
	} else {
		painter->setPen(QPen(m_visual.outline, 1.0));
		painter->setBrush(m_visual.fill);
		painter->drawEllipse(m_symbolRect);
	}
	if (isLabelVisible()) {
		painter->setPen(Qt::white);
		painter->setFont(m_labelFont);
		painter->drawText(m_labelRect, Qt::AlignVCenter | Qt::AlignLeft, m_displayName);
	}
	painter->restore();
}

void StationOverlayItem::setLayoutVisible(bool visible) {
	if (m_layoutVisible == visible)
		return;
	m_layoutVisible = visible;
	update();
}

bool StationOverlayItem::isLabelVisible() const {
	return m_layoutVisible || m_hovered || isSelected();
}

void StationOverlayItem::setFollowed(bool followed) {
	if (m_followed == followed)
		return;
	m_followed = followed;
	update();
}

void StationOverlayItem::setDegree(int degree) {
	degree = std::max(0, degree);
	if (m_degree == degree)
		return;
	m_degree = degree;
	const bool hasPlatform = m_visual.kind != StationVisualKind::StopMarker;
	m_visual = classifyStation(hasPlatform, m_degree);
	const QPixmap sourceSymbol(m_visual.iconResource);
	m_symbol = sourceSymbol.isNull() ? QPixmap() : sourceSymbol.scaled(QSize(static_cast<int>(kSymbolPixels),
		static_cast<int>(kSymbolPixels)), Qt::KeepAspectRatio, Qt::SmoothTransformation);
	update();
}

bool StationOverlayItem::isInterchange() const {
	return m_degree >= 3 || m_visual.kind == StationVisualKind::Interchange;
}

bool StationOverlayItem::isEndpoint() const {
	return m_degree == 1;
}

bool StationOverlayItem::priorityLess(const StationOverlayItem& left, const StationOverlayItem& right,
	const QPointF& viewportCenter) {
	if (left.isSelected() != right.isSelected())
		return left.isSelected();
	if (left.isFollowed() != right.isFollowed())
		return left.isFollowed();
	if (left.isInterchange() != right.isInterchange())
		return left.isInterchange();
	if (left.isEndpoint() != right.isEndpoint())
		return left.isEndpoint();
	const int leftClass = classPriority(left.m_visual.kind);
	const int rightClass = classPriority(right.m_visual.kind);
	if (leftClass != rightClass)
		return leftClass > rightClass;
	const QPointF leftDelta = left.m_stableAnchor - viewportCenter;
	const QPointF rightDelta = right.m_stableAnchor - viewportCenter;
	const qreal leftDistance = leftDelta.x() * leftDelta.x() + leftDelta.y() * leftDelta.y();
	const qreal rightDistance = rightDelta.x() * rightDelta.x() + rightDelta.y() * rightDelta.y();
	if (!qFuzzyCompare(leftDistance + 1.0, rightDistance + 1.0))
		return leftDistance < rightDistance;
	const int nameOrder = QString::compare(left.m_stationName, right.m_stationName, Qt::CaseSensitive);
	if (nameOrder != 0)
		return nameOrder < 0;
	if (!qFuzzyCompare(left.m_stableAnchor.x() + 1.0, right.m_stableAnchor.x() + 1.0))
		return left.m_stableAnchor.x() < right.m_stableAnchor.x();
	return left.m_stableAnchor.y() < right.m_stableAnchor.y();
}

void StationOverlayItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
	m_hovered = true;
	update();
	QGraphicsItem::hoverEnterEvent(event);
}

void StationOverlayItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
	m_hovered = false;
	update();
	QGraphicsItem::hoverLeaveEvent(event);
}

void StationOverlayItem::rebuildGeometry() {
	const QFontMetricsF metrics(m_labelFont);
	const qreal width = metrics.horizontalAdvance(m_displayName);
	const qreal height = metrics.height();
	if (m_labelSide == LabelSide::Right)
		m_labelRect = QRectF(m_symbolRect.right() + kLabelGap, -height / 2.0, width, height);
	else
		m_labelRect = QRectF(m_symbolRect.left() - kLabelGap - width, -height / 2.0, width, height);
}
