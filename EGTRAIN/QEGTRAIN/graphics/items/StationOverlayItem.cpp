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
	  m_visual(visual), m_degree(std::max(0, degree)), m_originalVisualKind(visual.kind),
	  m_interchange(visual.kind == StationVisualKind::Interchange), m_endpoint(degree == 1) {
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

StationOverlayItem::ViewportPlacement StationOverlayItem::placementForSide(
	LabelSide side, const QPointF& deviceAnchor, const QRectF& viewportInset) const {
	const QRectF rawSymbol = m_symbolRect.translated(deviceAnchor);
	const QRectF rawLabel = labelRectForSide(side).translated(deviceAnchor);
	const QRectF rawCombined = rawSymbol.united(rawLabel);
	const auto overflowFor = [&viewportInset](const QRectF& rect) {
		return std::max<qreal>(0.0, viewportInset.left() - rect.left())
			+ std::max<qreal>(0.0, rect.right() - viewportInset.right())
			+ std::max<qreal>(0.0, viewportInset.top() - rect.top())
			+ std::max<qreal>(0.0, rect.bottom() - viewportInset.bottom());
	};
	QPointF offset;
	if (rawCombined.left() < viewportInset.left())
		offset.rx() += viewportInset.left() - rawCombined.left();
	else if (rawCombined.right() > viewportInset.right())
		offset.rx() += viewportInset.right() - rawCombined.right();
	if (rawCombined.top() < viewportInset.top())
		offset.ry() += viewportInset.top() - rawCombined.top();
	else if (rawCombined.bottom() > viewportInset.bottom())
		offset.ry() += viewportInset.bottom() - rawCombined.bottom();
	ViewportPlacement placement;
	placement.side = side;
	placement.offset = offset;
	placement.symbolRect = rawSymbol.translated(offset);
	placement.labelRect = rawLabel.translated(offset);
	placement.combinedRect = rawCombined.translated(offset);
	placement.overflow = overflowFor(rawCombined);
	placement.fitsBeforeClamp = viewportInset.contains(rawCombined);
	placement.fits = viewportInset.contains(placement.combinedRect);
	return placement;
}

StationOverlayItem::ViewportPlacement StationOverlayItem::preferredViewportPlacement(
	const QPointF& deviceAnchor, const QRectF& viewportInset) const {
	const ViewportPlacement right = placementForSide(LabelSide::Right, deviceAnchor, viewportInset);
	const ViewportPlacement left = placementForSide(LabelSide::Left, deviceAnchor, viewportInset);
	if (right.fitsBeforeClamp || (!left.fitsBeforeClamp && right.overflow <= left.overflow))
		return right;
	return left;
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
	return (m_layoutVisible && !m_collisionBlocked) || m_hovered || isSelected();
}

void StationOverlayItem::setCollisionBlocked(bool blocked) {
	if (m_collisionBlocked == blocked)
		return;
	m_collisionBlocked = blocked;
	update();
}

void StationOverlayItem::setFollowed(bool followed) {
	if (m_followed == followed)
		return;
	m_followed = followed;
	update();
}

void StationOverlayItem::setDegree(int degree) {
	setNetworkDegree(degree, degree >= 3, degree == 1);
}

void StationOverlayItem::setNetworkDegree(int degree, bool interchange, bool endpoint) {
	degree = std::max(0, degree);
	const bool nextInterchange = interchange || degree >= 3
		|| m_originalVisualKind == StationVisualKind::Interchange;
	const bool nextEndpoint = endpoint || degree == 1;
	if (m_degree == degree && m_interchange == nextInterchange && m_endpoint == nextEndpoint)
		return;
	m_degree = degree;
	m_interchange = nextInterchange;
	m_endpoint = nextEndpoint;
	m_visual = classifyStation(m_originalVisualKind == StationVisualKind::Platform,
		m_interchange ? 3 : m_degree);
	const QPixmap sourceSymbol(m_visual.iconResource);
	m_symbol = sourceSymbol.isNull() ? QPixmap() : sourceSymbol.scaled(QSize(static_cast<int>(kSymbolPixels),
		static_cast<int>(kSymbolPixels)), Qt::KeepAspectRatio, Qt::SmoothTransformation);
	update();
}

bool StationOverlayItem::isInterchange() const {
	return m_interchange || m_degree >= 3 || m_originalVisualKind == StationVisualKind::Interchange;
}

bool StationOverlayItem::isEndpoint() const {
	return m_endpoint || m_degree == 1;
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
	const int leftClass = classPriority(left.m_originalVisualKind);
	const int rightClass = classPriority(right.m_originalVisualKind);
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
	m_labelRect = labelRectForSide(m_labelSide);
}

QRectF StationOverlayItem::labelRectForSide(LabelSide side) const {
	const QFontMetricsF metrics(m_labelFont);
	const qreal width = metrics.horizontalAdvance(m_displayName);
	const qreal height = metrics.height();
	if (side == LabelSide::Right)
		return QRectF(m_symbolRect.right() + kLabelGap, -height / 2.0, width, height);
	if (side == LabelSide::Left)
		return QRectF(m_symbolRect.left() - kLabelGap - width, -height / 2.0, width, height);
	if (side == LabelSide::Above)
		return QRectF(-width / 2.0, m_symbolRect.top() - kLabelGap - height, width, height);
	return QRectF(-width / 2.0, m_symbolRect.bottom() + kLabelGap, width, height);
}
