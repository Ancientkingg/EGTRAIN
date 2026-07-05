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
#include <QDebug>
#include <QScrollBar>
#include <iostream>

class NetworkView : public QGraphicsView {
	Q_OBJECT

public:
	NetworkView(QWidget* parent = 0);
	~NetworkView();

	void mousePressEvent(QMouseEvent* mouseEvent);

protected:
	virtual void wheelEvent(QWheelEvent* event);

private:
	void customizeScrollBars();

signals:
	void MousePressedOnView();
};

#endif // NETWORKVIEW_H