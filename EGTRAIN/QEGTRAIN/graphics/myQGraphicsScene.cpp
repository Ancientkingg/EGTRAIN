#include "graphics/myQGraphicsScene.h"

myQGraphicsScene::myQGraphicsScene(QObject* parent)
	: QGraphicsScene(parent) {
}

myQGraphicsScene::~myQGraphicsScene() {
}

// handles the click on graphical items
void myQGraphicsScene::mousePressEvent(QGraphicsSceneMouseEvent* mouseEvent) {
	// emit signal according to clicked item (mouse left button pressed)
	if (mouseEvent->button() == Qt::LeftButton) {
		// used to disable highlight if no item is clicked
		bool itemClicked = false;

		// clicked item
		QGraphicsItem* item = itemAt(mouseEvent->scenePos(), QTransform());

		// filter nodes
		myQGraphicsEllipseItem* Node = qgraphicsitem_cast<myQGraphicsEllipseItem*>(item);

		if (Node) {
			// display info of clicked Node (sends Node info on pointer)
			emit MousePressedOnNode(Node);
			itemClicked = true;
		}

		// filter station nodes
		myQGraphicsRectItem* stationNode = qgraphicsitem_cast<myQGraphicsRectItem*>(item);

		if (stationNode) {
			// display info of clicked Node (sends Node info on pointer)
			emit MousePressedOnStationNode(stationNode);
			itemClicked = true;
		}

		// filter arcs (Arc line items)
		myQGraphicsLineItem* Arc = qgraphicsitem_cast<myQGraphicsLineItem*>(item);

		if (Arc) {
			// display info of clicked Arc (sends Arc info on pointer)
			emit MousePressedOnArc(Arc);
			itemClicked = true;
		}

		// filter connections (connection line items)
		connectionQGraphicsLineItem* connection = qgraphicsitem_cast<connectionQGraphicsLineItem*>(item);

		if (connection) {
			// display info of clicked connection (sends connection info on pointer)
			emit MousePressedOnConnection(connection);
			itemClicked = true;
		}

		// filter signals (signalling group items)
		signallingQGraphicsEllipseItem* signal = qgraphicsitem_cast<signallingQGraphicsEllipseItem*>(item);

		if (signal) {
			// display info of clicked signal plate (sends signal info on pointer)
			emit MousePressedOnSignal(signal);
			itemClicked = true;
		}

		// filter trains (train polygon items (wagons))
		trainQGraphicsPolygonItem* train = qgraphicsitem_cast<trainQGraphicsPolygonItem*>(item);

		if (train) {
			// display info of clicked train (sends train info on pointer)
			emit MousePressedOnTrain(train);
			itemClicked = true;
		}

		// filter passengers (individual pax items)
		passengerQGraphicsPixmapItem* passenger = qgraphicsitem_cast<passengerQGraphicsPixmapItem*>(item);

		if (passenger) {
			// display info of clicked passenger (sends pax info on pointer)
			emit MousePressedOnPassenger(passenger);
			itemClicked = true;
		}
	}

	// send signal
	emit MousePressedOnScene();

	// default implementation
	QGraphicsScene::mousePressEvent(mouseEvent);
}
