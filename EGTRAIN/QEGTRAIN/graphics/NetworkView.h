#ifndef NETWORKVIEW_H
#define NETWORKVIEW_H

#include <QGraphicsView>
#include <QMouseEvent>
#include <QGraphicsSceneMouseEvent>
#include <QEvent>
#include <QPoint>
#include <QGraphicsItem>
#include <QGraphicsEllipseItem>
#include <QBrush>
#include <QTransform>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QDebug>
#include <QScrollBar>
#include <iostream>

class NetworkView : public QGraphicsView {
	Q_OBJECT

public:
	NetworkView(QWidget* parent = 0);
	~NetworkView();

	void mousePressEvent(QMouseEvent* mouseEvent) override;
	void scale(qreal sx, qreal sy);

protected:
	void wheelEvent(QWheelEvent* event) override;
	void drawBackground(QPainter* painter, const QRectF& rect) override;
	void resizeEvent(QResizeEvent* event) override;
	void scrollContentsBy(int dx, int dy) override;

private:
signals:
	void MousePressedOnView();
	void viewportChanged();
};

#endif // NETWORKVIEW_H
