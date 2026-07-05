#include "graphics/myQGraphicsView.h"

myQGraphicsView::myQGraphicsView(QWidget* parent)
	: QGraphicsView(parent) {
	sizePolicy().setHorizontalPolicy(QSizePolicy::Preferred);
	sizePolicy().setVerticalPolicy(QSizePolicy::Preferred);

	sizePolicy().setHorizontalStretch(0);
	sizePolicy().setVerticalStretch(0);

	setMouseTracking(true);

	setStyleSheet(QString("background-color: rgb(0, 0, 0);"));

	setSceneRect(QRectF(0, 0, 0, 0));

	setRenderHints(QPainter::Antialiasing | QPainter::HighQualityAntialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);

	setDragMode(QGraphicsView::ScrollHandDrag);

	setTransformationAnchor(QGraphicsView::AnchorViewCenter);

	// use center of view as anchor when resizing window
	setResizeAnchor(QGraphicsView::AnchorViewCenter);

	// customize scroll bars
	customizeScrollBars();
}

myQGraphicsView::~myQGraphicsView() {
}

// reimplemented mousePressEvent
void myQGraphicsView::mousePressEvent(QMouseEvent* mouseEvent) {
	// send signal
	emit MousePressedOnView();

	// call default method
	QGraphicsView::mousePressEvent(mouseEvent);
}

// reimplemented wheelEvent
void myQGraphicsView::wheelEvent(QWheelEvent* event) {
	// press CTRL to use scroll bars
	if (event->modifiers() & Qt::ControlModifier) {
		QGraphicsView::wheelEvent(event);
	}
}

// custom scroll bars
void myQGraphicsView::customizeScrollBars() {
	// define styles
	QString verticalStyle = "QScrollBar:vertical{"
							"    border: 1px solid #999999;"
							"    background: white;"
							"    width: 15px;"
							"    margin: 0px 0px 0px 0px;"
							"}"
							"QScrollBar::handle:vertical{"
							"    background:#999999;"
							"    min-height: 0px;"
							"    border-width: 1px;"
							"}"
							"QScrollBar::sub-line:vertical{"
							"	width: 0px;"
							"	height: 0px;"
							"}"
							"QScrollBar::add-line:vertical{"
							"	width: 0px;"
							"	height: 0px;"
							"}";

	QString horizontalStyle = "QScrollBar:horizontal{"
							  "    border: 1px solid #999999;"
							  "    background: white;"
							  "    height: 15px;"
							  "    margin: 0px 0px 0px 0px;"
							  "}"
							  "QScrollBar::handle:horizontal{"
							  "    background:#999999;"
							  "    min-width: 0px;"
							  "    border-width: 1px;"
							  "}"
							  "QScrollBar::sub-line:horizontal{"
							  "	width: 0px;"
							  "	height: 0px;"
							  "}"
							  "QScrollBar::add-line:horizontal{"
							  "	width: 0px;"
							  "	height: 0px;"
							  "}";

	// set style of vertical scroll bar
	verticalScrollBar()->setStyleSheet(verticalStyle);

	// set style of horizontal scroll bar
	horizontalScrollBar()->setStyleSheet(horizontalStyle);
}
