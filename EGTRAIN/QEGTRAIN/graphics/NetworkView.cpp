#include "graphics/NetworkView.h"

NetworkView::NetworkView(QWidget* parent)
	: QGraphicsView(parent) {
	sizePolicy().setHorizontalPolicy(QSizePolicy::Preferred);
	sizePolicy().setVerticalPolicy(QSizePolicy::Preferred);

	sizePolicy().setHorizontalStretch(0);
	sizePolicy().setVerticalStretch(0);

	setMouseTracking(true);
	setSceneRect(QRectF(0, 0, 0, 0));
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

void NetworkView::wheelEvent(QWheelEvent* event) {
	// press CTRL to use scroll bars
	if (event->modifiers() & Qt::ControlModifier)
		QGraphicsView::wheelEvent(event);
}
