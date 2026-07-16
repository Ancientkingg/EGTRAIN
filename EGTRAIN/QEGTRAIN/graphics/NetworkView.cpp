#include "graphics/NetworkView.h"

#include <cmath>

NetworkView::NetworkView(QWidget* parent)
	: QGraphicsView(parent) {
	sizePolicy().setHorizontalPolicy(QSizePolicy::Preferred);
	sizePolicy().setVerticalPolicy(QSizePolicy::Preferred);

	sizePolicy().setHorizontalStretch(0);
	sizePolicy().setVerticalStretch(0);

	setMouseTracking(true);
	setSceneRect(QRectF(0, 0, 0, 0));
	setBackgroundBrush(QColor("#171d24"));
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

void NetworkView::scale(qreal sx, qreal sy) {
	QGraphicsView::scale(sx, sy);
	emit viewportChanged();
}

void NetworkView::wheelEvent(QWheelEvent* event) {
	// press CTRL to use scroll bars
	if (event->modifiers() & Qt::ControlModifier) {
		QGraphicsView::wheelEvent(event);
	} else if (event->angleDelta().y() != 0) {
		const qreal factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
		QGraphicsView::scale(factor, factor);
		event->accept();
	}
	emit viewportChanged();
}

void NetworkView::drawBackground(QPainter* painter, const QRectF& rect) {
	painter->fillRect(rect, QColor("#171d24"));

	const qreal viewScale = qAbs(transform().m11());
	if (viewScale <= 0.0)
		return;

	qreal spacing = 80.0;
	while (spacing * viewScale < 24.0)
		spacing *= 2.0;
	while (spacing * viewScale > 96.0)
		spacing /= 2.0;

	QPen gridPen(QColor("#2a3743"));
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
	QGraphicsView::resizeEvent(event);
	emit viewportChanged();
}

void NetworkView::scrollContentsBy(int dx, int dy) {
	QGraphicsView::scrollContentsBy(dx, dy);
	emit viewportChanged();
}
