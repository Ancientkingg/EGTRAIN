#ifndef NETWORKSCENE_H
#define NETWORKSCENE_H

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

#include "graphics/items/TrackLineItem.h"
#include "graphics/items/NodeItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/PassengerItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"
#include <QGraphicsTextItem>

using namespace std;

class NetworkScene : public QGraphicsScene {
	Q_OBJECT

public:
	// NetworkScene(QWidget *parent);
	NetworkScene(QObject* parent);
	~NetworkScene();

	void mousePressEvent(QGraphicsSceneMouseEvent* mouseEvent);

signals:
	void MousePressedOnScene();
	void MousePressedOnNode(NodeItem* Node);
	void MousePressedOnStationNode(StationNodeItem* Node);
	void MousePressedOnArc(TrackLineItem* Arc);
	void MousePressedOnConnection(ConnectionItem* connection);
	void MousePressedOnSignal(SignalItem* signal);
	void MousePressedOnTrain(TrainBodyItem* train);
	void MousePressedOnPassenger(PassengerItem* passenger);
	void DisableHighlight();
};

#endif // NETWORKSCENE_H
