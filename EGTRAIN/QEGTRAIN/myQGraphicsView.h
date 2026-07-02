#ifndef MYQGRAPHICSVIEW_H
#define MYQGRAPHICSVIEW_H

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

class myQGraphicsView : public QGraphicsView {
	Q_OBJECT

public:
	myQGraphicsView(QWidget* parent = 0);
	~myQGraphicsView();

	void mousePressEvent(QMouseEvent* mouseEvent);

protected:
	virtual void wheelEvent(QWheelEvent* event);

private:
	void customizeScrollBars();

signals:
	void MousePressedOnView();
};

#endif // MYQGRAPHICSVIEW_H