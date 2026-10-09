#include "graphics/items/StationOverlayItem.h"

#include <QGraphicsSceneHoverEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr qreal kSymbolPixels = 16.0;
constexpr qreal kLabelPixels = 11.0;
constexpr qreal kLabelGap = 8.0;
constexpr qreal kFitCollisionStep = 20.0;
constexpr qreal kNameCollisionGap = 2.0;
// A name meets the pictogram of a neighbour at the same height along the artwork line.
// It covers that pictogram only when they share more than this many pixels both ways.
constexpr qreal kPictogramOverlapPixels = 2.0;

// Enlarges item by factor about pivot in one change. The item transform acts after the
// item scale, so pivot is in the coordinates of the scaled item (its parent without pos).
void enlargeAbout(QGraphicsItem* item, const QPointF& pivot, qreal factor) {
	item->setTransform(QTransform().translate(pivot.x(), pivot.y()).scale(factor, factor).translate(-pivot.x(), -pivot.y()));
}

QList<QPointF> fitCollisionCandidates() {
	return {
		QPointF(),
		QPointF(kFitCollisionStep, 0.0),
		QPointF(-kFitCollisionStep, 0.0),
		QPointF(0.0, kFitCollisionStep),
		QPointF(0.0, -kFitCollisionStep),
		QPointF(kFitCollisionStep, kFitCollisionStep),
		QPointF(-kFitCollisionStep, kFitCollisionStep),
		QPointF(kFitCollisionStep, -kFitCollisionStep),
		QPointF(-kFitCollisionStep, -kFitCollisionStep),
		QPointF(2.0 * kFitCollisionStep, 0.0),
		QPointF(-2.0 * kFitCollisionStep, 0.0),
		QPointF(0.0, 2.0 * kFitCollisionStep),
		QPointF(0.0, -2.0 * kFitCollisionStep),
	};
}
}

StationOverlayItem::StationOverlayItem(const QString& stationName, const QPointF& stableAnchor,
	const StationVisual& visual, int degree, QGraphicsItem* parent)
	: QGraphicsItem(parent), m_stationName(stationName), m_stableAnchor(stableAnchor),
	  m_visual(visual), m_degree(std::max(0, degree)), m_endpoint(degree == 1) {
	setFlag(QGraphicsItem::ItemIgnoresTransformations);
	setFlag(QGraphicsItem::ItemIsSelectable);
	setAcceptHoverEvents(true);
	setAcceptedMouseButtons(Qt::NoButton);
	setZValue(3.5);
	m_labelFont.setPixelSize(static_cast<int>(kLabelPixels));
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
	QString spaced;
	spaced.reserve(stationName.size() + 4);
	for (int i = 0; i < stationName.size(); ++i) {
		const QChar current = stationName.at(i);
		if (i > 0 && current.isUpper()) {
			const QChar previous = stationName.at(i - 1);
			const bool startsWord = previous.isLower();
			int precedingUppercaseRun = 0;
			for (int j = i - 1; j >= 0 && stationName.at(j).isUpper(); --j)
				++precedingUppercaseRun;
			const bool endsAcronym = precedingUppercaseRun >= 2
				&& i + 1 < stationName.size() && stationName.at(i + 1).isLower();
			if (startsWord || endsAcronym)
				spaced.append(QLatin1Char(' '));
		}
		spaced.append(current);
	}
	return spaced;
}

QString StationOverlayItem::displayName(const std::string& stationName) {
	return displayName(QString::fromStdString(stationName));
}

qreal StationOverlayItem::labelScale() const {
	return m_labelScale;
}

void StationOverlayItem::setLabelScale(qreal scale) {
	scale = std::clamp(scale, 1.0, 3.0);
	const int pixelSize = qRound(kLabelPixels * m_visualScale * scale);
	if (qFuzzyCompare(m_labelScale, scale) && m_labelFont.pixelSize() == pixelSize)
		return;
	prepareGeometryChange();
	m_labelScale = scale;
	m_labelFont.setPixelSize(pixelSize);
	rebuildGeometry();
	update();
}

void StationOverlayItem::setVisualScale(qreal scale) {
	scale = std::clamp(scale, 0.1, 1.0);
	if (qFuzzyCompare(m_visualScale, scale))
		return;
	prepareGeometryChange();
	m_visualScale = scale;
	m_labelFont.setPixelSize(qRound(kLabelPixels * m_visualScale * m_labelScale));
	m_symbolRect = QRectF(-kSymbolPixels * m_visualScale / 2.0,
		-kSymbolPixels * m_visualScale / 2.0,
		kSymbolPixels * m_visualScale, kSymbolPixels * m_visualScale);
	rebuildGeometry();
	update();
}

void StationOverlayItem::setViewportOffset(const QPointF& offset) {
	if (m_viewportOffset == offset)
		return;
	prepareGeometryChange();
	m_viewportOffset = offset;
	update();
}

void StationOverlayItem::setFitCollisionOffset(const QPointF& offset) {
	if (m_fitCollisionOffset == offset)
		return;
	prepareGeometryChange();
	m_fitCollisionOffset = offset;
	setAcceptedMouseButtons(!m_sceneDecoration && !offset.isNull() && m_fitSymbolVisible && m_displacedClickHandler
			? Qt::LeftButton
			: Qt::NoButton);
	update();
}

void StationOverlayItem::setFitSymbolVisible(bool visible) {
	if (m_fitSymbolVisible == visible)
		return;
	prepareGeometryChange();
	m_fitSymbolVisible = visible;
	setAcceptedMouseButtons(!m_sceneDecoration && visible && !m_fitCollisionOffset.isNull() && m_displacedClickHandler
			? Qt::LeftButton
			: Qt::NoButton);
	update();
}

void StationOverlayItem::setDisplacedClickHandler(std::function<void(const QString&)> handler) {
	m_displacedClickHandler = std::move(handler);
	setAcceptedMouseButtons(!m_sceneDecoration && m_fitSymbolVisible && !m_fitCollisionOffset.isNull() && m_displacedClickHandler
			? Qt::LeftButton
			: Qt::NoButton);
}

void StationOverlayItem::setSourceIdentities(const QList<SourceIdentity>& identities) {
	m_sourceIdentities = identities;
}

void StationOverlayItem::clearSourceIdentities() {
	m_sourceIdentities.clear();
}

bool StationOverlayItem::matchesSourceIdentity(double nodeId, int track) const {
	return std::any_of(m_sourceIdentities.cbegin(), m_sourceIdentities.cend(),
		[nodeId, track](const SourceIdentity& identity) {
			return identity.nodeId == nodeId && identity.track == track;
		});
}

double StationOverlayItem::sourceNodeId() const {
	return m_sourceIdentities.isEmpty() ? 0.0 : m_sourceIdentities.first().nodeId;
}

int StationOverlayItem::sourceTrack() const {
	return m_sourceIdentities.isEmpty() ? -1 : m_sourceIdentities.first().track;
}

QPointF StationOverlayItem::firstFitCollisionOffset(const QRectF& symbolRect,
	const QRectF& viewportInset, const QList<QRectF>& occupiedSymbols,
	const QList<QRectF>& blockedRects, bool* found) {
	for (const QPointF& offset : fitCollisionCandidates()) {
		const QRectF candidate = symbolRect.translated(offset);
		if (!viewportInset.contains(candidate))
			continue;
		const bool occupied = std::any_of(occupiedSymbols.cbegin(), occupiedSymbols.cend(),
			[&candidate](const QRectF& other) { return candidate.intersects(other); });
		if (occupied)
			continue;
		const bool blocked = std::any_of(blockedRects.cbegin(), blockedRects.cend(),
			[&candidate](const QRectF& other) { return candidate.intersects(other); });
		if (blocked)
			continue;
		if (found)
			*found = true;
		return offset;
	}
	if (found)
		*found = false;
	return QPointF();
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
	const QRectF clampReach = viewportInset.adjusted(-rawCombined.width(), -rawCombined.height(),
		rawCombined.width(), rawCombined.height());
	const auto overflowFor = [&viewportInset](const QRectF& rect) {
		return std::max<qreal>(0.0, viewportInset.left() - rect.left())
			+ std::max<qreal>(0.0, rect.right() - viewportInset.right())
			+ std::max<qreal>(0.0, viewportInset.top() - rect.top())
			+ std::max<qreal>(0.0, rect.bottom() - viewportInset.bottom());
	};
	QPointF offset;
	if (clampReach.contains(deviceAnchor)) {
		if (rawCombined.left() < viewportInset.left())
			offset.rx() += viewportInset.left() - rawCombined.left();
		else if (rawCombined.right() > viewportInset.right())
			offset.rx() += viewportInset.right() - rawCombined.right();
		if (rawCombined.top() < viewportInset.top())
			offset.ry() += viewportInset.top() - rawCombined.top();
		else if (rawCombined.bottom() > viewportInset.bottom())
			offset.ry() += viewportInset.bottom() - rawCombined.bottom();
	}
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
	QRectF bounds = translated(m_labelRect);
	if (m_fitSymbolVisible)
		bounds = bounds.united(translatedSymbol(m_symbolRect));
	return bounds;
}

QRectF StationOverlayItem::translated(const QRectF& rect) const {
	return rect.translated(m_viewportOffset);
}

QRectF StationOverlayItem::translatedSymbol(const QRectF& rect) const {
	return rect.translated(m_viewportOffset + m_fitCollisionOffset);
}

QRectF StationOverlayItem::boundingRect() const {
	return combinedRect();
}

QPainterPath StationOverlayItem::shape() const {
	QPainterPath path;
	if (m_sceneDecoration)
		return path;
	if (m_fitSymbolVisible)
		path.addRect(translatedSymbol(m_symbolRect));
	if (isLabelVisible())
		path.addRect(translated(m_labelRect));
	return path;
}

void StationOverlayItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
	Q_UNUSED(option);
	Q_UNUSED(widget);
	if (m_sceneDecoration)
		return;
	painter->save();
	painter->translate(m_viewportOffset);
	const QColor markerColor(210, 215, 220);
	if (m_fitSymbolVisible) {
		painter->save();
		painter->translate(m_fitCollisionOffset);
		const QPixmap historicalSymbol(m_visual.iconResource);
		if (!historicalSymbol.isNull())
			painter->drawPixmap(m_symbolRect.toRect(), historicalSymbol);
		else {
			painter->setPen(QPen(markerColor, m_visualScale));
			painter->setBrush(markerColor);
			painter->drawEllipse(m_symbolRect);
		}
		painter->restore();
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

void StationOverlayItem::setNameVisible(bool visible) {
	if (m_nameVisible == visible)
		return;
	m_nameVisible = visible;
	update();
}

bool StationOverlayItem::isLabelVisible() const {
	return m_nameVisible && ((m_layoutVisible && !m_collisionBlocked) || m_hovered || isSelected());
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
	const bool nextInterchange = interchange || degree >= 3;
	const bool nextEndpoint = endpoint || degree == 1;
	if (m_degree == degree && m_interchange == nextInterchange && m_endpoint == nextEndpoint)
		return;
	m_degree = degree;
	m_interchange = nextInterchange;
	m_endpoint = nextEndpoint;
	update();
}

bool StationOverlayItem::isInterchange() const {
	return m_interchange || m_degree >= 3;
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
	if (left.m_platformCount != right.m_platformCount)
		return left.m_platformCount > right.m_platformCount;
	if (left.isInterchange() != right.isInterchange())
		return left.isInterchange();
	if (left.isEndpoint() != right.isEndpoint())
		return left.isEndpoint();
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

qreal StationOverlayItem::readableItemScale(qreal nativeSizePx, qreal presentationScale,
	qreal viewScale, qreal minPx) {
	if (nativeSizePx <= 0.0 || viewScale <= 0.0)
		return presentationScale;
	return std::max(presentationScale, minPx / (nativeSizePx * viewScale));
}

void StationOverlayItem::attachArtwork(QGraphicsPixmapItem* picture, QGraphicsTextItem* name,
	int platformCount) {
	m_picture = picture;
	m_name = name;
	m_platformCount = std::max(0, platformCount);
	m_appliedViewScale = 0.0;
	if (m_picture) {
		const QPixmap& pixmap = m_picture->pixmap();
		const QSizeF size = QSizeF(pixmap.size()) / pixmap.devicePixelRatio();
		m_pictureSceneScale = m_picture->scale() > 0.0 ? m_picture->scale() : 1.0;
		m_pictureNativePixels = size.height();
		m_pictureAnchor = m_picture->offset() + QPointF(size.width() / 2.0, size.height());
	}
	if (m_name) {
		const QFont font = m_name->font();
		m_nameSceneScale = m_name->scale() > 0.0 ? m_name->scale() : 1.0;
		m_nameNativePixels = font.pixelSize() > 0 ? font.pixelSize() : QFontMetricsF(font).height();
		m_nameAnchor = QPointF(m_name->boundingRect().width() / 2.0, 0.0);
	}
}

void StationOverlayItem::applyViewScale(qreal viewScale) {
	viewScale = std::abs(viewScale);
	if (viewScale <= 0.0 || viewScale == m_appliedViewScale)
		return;
	m_appliedViewScale = viewScale;
	if (m_picture) {
		const qreal scale = readableItemScale(m_pictureNativePixels, m_pictureSceneScale, viewScale, MinPictogramPixels);
		enlargeAbout(m_picture, m_pictureAnchor * m_pictureSceneScale, scale / m_pictureSceneScale);
	}
	if (m_name) {
		const qreal scale = readableItemScale(m_nameNativePixels, m_nameSceneScale, viewScale, MinNamePixels);
		enlargeAbout(m_name, m_nameAnchor * m_nameSceneScale, scale / m_nameSceneScale);
	}
}

void StationOverlayItem::resolveNameCollisions(const QList<StationOverlayItem*>& stations,
	const QTransform& sceneToViewport) {
	// Overlaps are tested in device pixels. The priority order uses a fixed reference point,
	// so panning cannot reorder it.
	const QTransform scaling(sceneToViewport.m11(), sceneToViewport.m12(),
		sceneToViewport.m21(), sceneToViewport.m22(), 0.0, 0.0);
	QList<StationOverlayItem*> ordered;
	QList<QPair<const StationOverlayItem*, QRectF>> pictograms;
	for (StationOverlayItem* station : stations) {
		if (!station)
			continue;
		if (station->m_name)
			ordered.append(station);
		if (station->m_picture && station->m_picture->isVisible())
			pictograms.append({station, scaling.mapRect(station->m_picture->sceneBoundingRect())});
	}
	std::stable_sort(ordered.begin(), ordered.end(),
		[](const StationOverlayItem* left, const StationOverlayItem* right) {
			return priorityLess(*left, *right, QPointF());
		});
	QList<QRectF> placed;
	for (StationOverlayItem* station : ordered) {
		const QRectF name = scaling.mapRect(station->m_name->sceneBoundingRect());
		const QRectF rect = name.adjusted(-kNameCollisionGap, -kNameCollisionGap, kNameCollisionGap, kNameCollisionGap);
		const bool keep = station->isSelected() || station->isFollowed();
		// A pictogram is never hidden, so a name gives way to the pictogram of any other
		// station that it covers, as well as to the name of a station that ranks higher.
		const bool hidden = !keep
			&& (std::any_of(placed.cbegin(), placed.cend(),
					[&rect](const QRectF& other) { return rect.intersects(other); })
				|| std::any_of(pictograms.cbegin(), pictograms.cend(),
					[&name, station](const QPair<const StationOverlayItem*, QRectF>& pictogram) {
						const QRectF shared = name.intersected(pictogram.second);
						return pictogram.first != station && shared.width() > kPictogramOverlapPixels
							&& shared.height() > kPictogramOverlapPixels;
					}));
		station->m_nameHiddenByCollision = hidden;
		if (!hidden)
			placed.append(rect);
	}
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

void StationOverlayItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
	if (event->button() == Qt::LeftButton && m_fitSymbolVisible
		&& !m_fitCollisionOffset.isNull() && translatedSymbol(m_symbolRect).contains(event->pos())
		&& m_displacedClickHandler) {
		m_displacedClickHandler(m_stationName);
		event->accept();
		return;
	}
	event->ignore();
}

void StationOverlayItem::rebuildGeometry() {
	m_labelRect = labelRectForSide(m_labelSide);
}

QRectF StationOverlayItem::labelRectForSide(LabelSide side) const {
	const QFontMetricsF metrics(m_labelFont);
	const qreal width = metrics.horizontalAdvance(m_displayName);
	const qreal height = metrics.height();
	const qreal gap = kLabelGap * m_visualScale;
	if (side == LabelSide::Right)
		return QRectF(m_symbolRect.right() + gap, -height / 2.0, width, height);
	if (side == LabelSide::Left)
		return QRectF(m_symbolRect.left() - gap - width, -height / 2.0, width, height);
	if (side == LabelSide::Above)
		return QRectF(-width / 2.0, m_symbolRect.top() - gap - height, width, height);
	return QRectF(-width / 2.0, m_symbolRect.bottom() + gap, width, height);
}
