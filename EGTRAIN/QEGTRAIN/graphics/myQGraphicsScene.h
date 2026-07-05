#ifndef MYQGRAPHICSSCENE_H
#define MYQGRAPHICSSCENE_H

#include <QGraphicsScene>
#include <QMouseEvent>
#include <QGraphicsSceneMouseEvent>
#include <QEvent>
#include <QPoint>
#include <QGraphicsItem>
#include <QGraphicsEllipseItem>
#include <QBrush>
#include <QTransform>
#include <QDebug>
#include <QPen>

#include "graphics/items/myQGraphicsLineItem.h"
#include "graphics/items/myQGraphicsEllipseItem.h"
#include "graphics/items/myQGraphicsRectItem.h"
#include "graphics/items/connectionQGraphicsLineItem.h"
#include "graphics/items/passengerQGraphicsPixmapItem.h"
#include "graphics/items/signallingQGraphicsEllipseItem.h"
#include "graphics/items/trainQGraphicsPolygonItem.h"
#include "graphics/items/trainQGraphicsItemGroup.h"
#include <QGraphicsTextItem>

using namespace std;

class myQGraphicsScene : public QGraphicsScene {
	Q_OBJECT

public:
	// myQGraphicsScene(QWidget *parent);
	myQGraphicsScene(QObject* parent);
	~myQGraphicsScene();

	void mousePressEvent(QGraphicsSceneMouseEvent* mouseEvent);

signals:
	void MousePressedOnScene();
	void MousePressedOnNode(myQGraphicsEllipseItem* Node);
	void MousePressedOnStationNode(myQGraphicsRectItem* Node);
	void MousePressedOnArc(myQGraphicsLineItem* Arc);
	void MousePressedOnConnection(connectionQGraphicsLineItem* connection);
	void MousePressedOnSignal(signallingQGraphicsEllipseItem* signal);
	void MousePressedOnTrain(trainQGraphicsPolygonItem* train);
	void MousePressedOnPassenger(passengerQGraphicsPixmapItem* passenger);
	void DisableHighlight();
};

#endif // MYQGRAPHICSSCENE_H
