#include "graphics/NetworkScene.h"

#include <QGraphicsView>

NetworkScene::NetworkScene(QObject* parent)
	: QGraphicsScene(parent) {
}

NetworkScene::~NetworkScene() {
}

// handles the click on graphical items
void NetworkScene::mousePressEvent(QGraphicsSceneMouseEvent* mouseEvent) {
	// emit signal according to clicked item (mouse left button pressed)
	if (mouseEvent->button() == Qt::LeftButton) {
		// used to disable highlight if no item is clicked
		bool itemClicked = false;

		// clicked item
		QTransform viewTransform;
		for (QGraphicsView* view : views()) {
			if (view->viewport() == mouseEvent->widget()) {
				viewTransform = view->transform();
				break;
			}
		}
		QGraphicsItem* item = itemAt(mouseEvent->scenePos(), viewTransform);

		// filter nodes
		NodeItem* Node = qgraphicsitem_cast<NodeItem*>(item);

		if (Node) {
			// display info of clicked Node (sends Node info on pointer)
			emit MousePressedOnNode(Node);
			itemClicked = true;
		}

		// filter station nodes
		StationNodeItem* stationNode = qgraphicsitem_cast<StationNodeItem*>(item);

		if (stationNode) {
			// display info of clicked Node (sends Node info on pointer)
			emit MousePressedOnStationNode(stationNode);
			itemClicked = true;
		}

		// filter arcs (Arc line items)
		TrackLineItem* Arc = qgraphicsitem_cast<TrackLineItem*>(item);

		if (Arc) {
			// display info of clicked Arc (sends Arc info on pointer)
			emit MousePressedOnArc(Arc);
			itemClicked = true;
		}

		// filter connections (connection line items)
		ConnectionItem* connection = qgraphicsitem_cast<ConnectionItem*>(item);

		if (connection) {
			// display info of clicked connection (sends connection info on pointer)
			emit MousePressedOnConnection(connection);
			itemClicked = true;
		}

		// filter signals (signalling group items)
		SignalItem* signal = qgraphicsitem_cast<SignalItem*>(item);

		if (signal) {
			// display info of clicked signal plate (sends signal info on pointer)
			emit MousePressedOnSignal(signal);
			itemClicked = true;
		}

		// filter trains (train polygon items (wagons))
		TrainBodyItem* train = qgraphicsitem_cast<TrainBodyItem*>(item);

		if (train) {
			// display info of clicked train (sends train info on pointer)
			emit MousePressedOnTrain(train);
			itemClicked = true;
		}

		// filter passengers (individual pax items)
		PassengerItem* passenger = qgraphicsitem_cast<PassengerItem*>(item);

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
