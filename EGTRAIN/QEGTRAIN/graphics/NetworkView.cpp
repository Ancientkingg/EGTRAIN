#include "graphics/NetworkView.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/VirtualArcItem.h"

#include <algorithm>
#include <cmath>
#include <limits>

NetworkView::NetworkView(QWidget* parent)
	: QGraphicsView(parent) {
	sizePolicy().setHorizontalPolicy(QSizePolicy::Preferred);
	sizePolicy().setVerticalPolicy(QSizePolicy::Preferred);

	sizePolicy().setHorizontalStretch(0);
	sizePolicy().setVerticalStretch(0);

	setMouseTracking(true);
	setSceneRect(QRectF(0, 0, 0, 0));
	setBackgroundBrush(QColor("#101A22"));
	setRenderHints(QPainter::Antialiasing | QPainter::HighQualityAntialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
	setDragMode(QGraphicsView::ScrollHandDrag);
	setTransformationAnchor(QGraphicsView::AnchorViewCenter);
	setResizeAnchor(QGraphicsView::AnchorViewCenter);
}

NetworkView::~NetworkView() {
}

void NetworkView::mousePressEvent(QMouseEvent* mouseEvent) {
	emit MousePressedOnView();
	QGraphicsView::mousePressEvent(mouseEvent);
}

QRectF NetworkView::paintedTopologyBounds(bool* hasBounds) const {
	QRectF bounds;
	bool found = false;
	const auto addPoint = [&bounds, &found](const QPointF& point) {
		if (!found) {
			bounds = QRectF(point, QSizeF(0.0, 0.0));
			found = true;
			return;
		}
		bounds.setLeft(std::min(bounds.left(), point.x()));
		bounds.setRight(std::max(bounds.right(), point.x()));
		bounds.setTop(std::min(bounds.top(), point.y()));
		bounds.setBottom(std::max(bounds.bottom(), point.y()));
	};
	if (scene()) {
		for (QGraphicsItem* item : scene()->items()) {
			if (auto* virtualArc = dynamic_cast<VirtualArcItem*>(item)) {
				addPoint(virtualArc->mapToScene(virtualArc->line().p1()));
				addPoint(virtualArc->mapToScene(virtualArc->line().p2()));
				addPoint(virtualArc->mapToScene(virtualArc->secondPaintedLine().p1()));
				addPoint(virtualArc->mapToScene(virtualArc->secondPaintedLine().p2()));
				continue;
			}
			if (auto* track = dynamic_cast<TrackLineItem*>(item)) {
				addPoint(track->mapToScene(track->line().p1()));
				addPoint(track->mapToScene(track->line().p2()));
				continue;
			}
			if (auto* connection = dynamic_cast<ConnectionItem*>(item)) {
				addPoint(connection->mapToScene(connection->line().p1()));
				addPoint(connection->mapToScene(connection->line().p2()));
			}
		}
	}
	if (hasBounds)
		*hasBounds = found;
	return bounds;
}

qreal NetworkView::calculateFittedScale() const {
	if (!m_hasTopologyBounds)
		return 1.0;
	const qreal availableWidth = qMax<qreal>(1.0, viewport()->width() - 50.0);
	const qreal availableHeight = qMax<qreal>(1.0, viewport()->height() - 50.0);
	const qreal topologyWidth = qMax<qreal>(1.0, m_topologyBounds.width());
	const qreal topologyHeight = qMax<qreal>(1.0, m_topologyBounds.height());
	return qMax<qreal>(std::numeric_limits<qreal>::epsilon(),
		qMin(availableWidth / topologyWidth, availableHeight / topologyHeight));
}

qreal NetworkView::zoomRatio() const {
	if (m_fittedScale <= 0.0)
		return 1.0;
	return qBound<qreal>(1.0, qAbs(transform().m11()) / m_fittedScale, 12.0);
}

qreal NetworkView::fittedScale() const {
	return m_fittedScale;
}

QRectF NetworkView::topologyBounds() const {
	return m_topologyBounds;
}

bool NetworkView::atFit() const {
	return zoomRatio() <= 1.0 + 1e-6;
}

QString NetworkView::zoomLabel() const {
	const qreal ratio = zoomRatio();
	if (ratio <= 1.0 + 1e-6)
		return QStringLiteral("Fit");
	const qreal integral = qRound(ratio);
	if (qAbs(ratio - integral) < 0.05)
		return QStringLiteral("%1x").arg(integral);
	return QStringLiteral("%1x").arg(ratio, 0, 'f', 1);
}

void NetworkView::fitToTopology() {
	bool hasBounds = false;
	const QRectF bounds = paintedTopologyBounds(&hasBounds);
	m_topologyBounds = bounds;
	m_hasTopologyBounds = hasBounds;
	const bool wasSuppressed = m_suppressViewportChanged;
	m_suppressViewportChanged = true;
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	if (m_hasTopologyBounds) {
		setSceneRect(m_topologyBounds);
		m_fittedScale = calculateFittedScale();
		setTransform(QTransform::fromScale(m_fittedScale, m_fittedScale));
		centerOn(m_topologyBounds.center());
		m_viewCenter = mapToScene(viewport()->rect().center());
		m_hasViewCenter = true;
	} else {
		m_fittedScale = 1.0;
		setSceneRect(QRectF());
		setTransform(QTransform());
		centerOn(QPointF(0.0, 0.0));
		m_viewCenter = mapToScene(viewport()->rect().center());
		m_hasViewCenter = true;
	}
	m_suppressViewportChanged = wasSuppressed;
	if (!wasSuppressed)
		emit viewportChanged();
}

bool NetworkView::zoomBy(qreal factor, const QPointF& viewportAnchor) {
	if (!std::isfinite(factor) || factor <= 0.0 || m_fittedScale <= 0.0)
		return false;
	const qreal currentRatio = zoomRatio();
	const qreal targetRatio = qBound<qreal>(1.0, currentRatio * factor, 12.0);
	if (qAbs(targetRatio - currentRatio) <= 1e-9)
		return false;

	const QPoint viewportCenter = this->viewport()->rect().center();
	const bool hasAnchor = viewportAnchor.x() >= 0.0 && viewportAnchor.y() >= 0.0;
	const QPoint anchorPoint = hasAnchor ? viewportAnchor.toPoint() : viewportCenter;
	const QPointF anchorScene = mapToScene(anchorPoint);
	const QPointF centerScene = m_hasViewCenter ? m_viewCenter : mapToScene(viewportCenter);

	const bool wasSuppressed = m_suppressViewportChanged;
	m_suppressViewportChanged = true;
	if (targetRatio <= 1.0 + 1e-6) {
		setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	}
	setTransform(QTransform::fromScale(m_fittedScale * targetRatio, m_fittedScale * targetRatio));
	centerOn(centerScene);
	if (hasAnchor) {
		const QPointF newAnchorScene = mapToScene(anchorPoint);
		centerOn(centerScene + anchorScene - newAnchorScene);
	}
	m_viewCenter = mapToScene(viewportCenter);
	m_hasViewCenter = true;
	m_suppressViewportChanged = wasSuppressed;
	if (!wasSuppressed)
		emit viewportChanged();
	return true;
}

void NetworkView::wheelEvent(QWheelEvent* event) {
	if (event->modifiers() & Qt::ControlModifier) {
		QGraphicsView::wheelEvent(event);
	} else if (event->angleDelta().y() != 0) {
		const qreal factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
		zoomBy(factor, event->position());
		event->accept();
	} else {
		QGraphicsView::wheelEvent(event);
	}
}

void NetworkView::drawBackground(QPainter* painter, const QRectF& rect) {
	painter->fillRect(rect, QColor("#101A22"));

	const qreal viewScale = qAbs(transform().m11());
	if (viewScale <= 0.0)
		return;

	qreal spacing = 80.0;
	while (spacing * viewScale < 24.0)
		spacing *= 2.0;
	while (spacing * viewScale > 96.0)
		spacing /= 2.0;

	QPen gridPen(QColor("#1B2A35"));
	gridPen.setCosmetic(true);
	gridPen.setWidth(0);
	painter->setPen(gridPen);
	const qreal left = std::floor(rect.left() / spacing) * spacing;
	const qreal top = std::floor(rect.top() / spacing) * spacing;
	for (qreal x = left; x <= rect.right(); x += spacing)
		painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
	for (qreal y = top; y <= rect.bottom(); y += spacing)
		painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
}

void NetworkView::resizeEvent(QResizeEvent* event) {
	const bool preserveFit = atFit();
	const qreal previousRatio = zoomRatio();
	const QPoint viewportCenter = viewport()->rect().center();
	const QPointF previousCenter = m_hasViewCenter ? m_viewCenter : mapToScene(viewportCenter);
	const bool wasSuppressed = m_suppressViewportChanged;
	m_suppressViewportChanged = true;
	QGraphicsView::resizeEvent(event);
	if (m_hasTopologyBounds) {
		m_fittedScale = calculateFittedScale();
		const qreal ratio = preserveFit ? 1.0 : qBound<qreal>(1.0, previousRatio, 12.0);
		if (preserveFit) {
			setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
			setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		}
		setTransform(QTransform::fromScale(m_fittedScale * ratio, m_fittedScale * ratio));
		centerOn(preserveFit ? m_topologyBounds.center() : previousCenter);
		m_viewCenter = mapToScene(viewport()->rect().center());
		m_hasViewCenter = true;
	}
	m_suppressViewportChanged = wasSuppressed;
	if (!wasSuppressed)
		emit viewportChanged();
}

void NetworkView::scrollContentsBy(int dx, int dy) {
	QGraphicsView::scrollContentsBy(dx, dy);
	if (!m_suppressViewportChanged) {
		m_viewCenter = mapToScene(viewport()->rect().center());
		m_hasViewCenter = true;
		emit viewportChanged();
	}
}
