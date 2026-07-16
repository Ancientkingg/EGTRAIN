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
		bool itemClicked = false;
		QTransform viewTransform;
		for (QGraphicsView* view : views()) {
			if (view->viewport() == mouseEvent->widget()) {
				viewTransform = view->viewportTransform();
				break;
			}
		}

		const QList<QGraphicsItem*> hitItems = items(
			mouseEvent->scenePos(), Qt::IntersectsItemShape, Qt::DescendingOrder, viewTransform);
		for (QGraphicsItem* item : hitItems) {
			for (QGraphicsItem* candidate = item; candidate; candidate = candidate->parentItem()) {
				if (NodeItem* node = qgraphicsitem_cast<NodeItem*>(candidate)) {
					emit MousePressedOnNode(node);
					itemClicked = true;
				} else if (StationNodeItem* stationNode = qgraphicsitem_cast<StationNodeItem*>(candidate)) {
					emit MousePressedOnStationNode(stationNode);
					itemClicked = true;
				} else if (TrackLineItem* arc = qgraphicsitem_cast<TrackLineItem*>(candidate)) {
					emit MousePressedOnArc(arc);
					itemClicked = true;
				} else if (ConnectionItem* connection = qgraphicsitem_cast<ConnectionItem*>(candidate)) {
					emit MousePressedOnConnection(connection);
					itemClicked = true;
				} else if (SignalItem* signal = qgraphicsitem_cast<SignalItem*>(candidate)) {
					emit MousePressedOnSignal(signal);
					itemClicked = true;
				} else if (TrainBodyItem* train = qgraphicsitem_cast<TrainBodyItem*>(candidate)) {
					emit MousePressedOnTrain(train);
					itemClicked = true;
				} else if (PassengerItem* passenger = qgraphicsitem_cast<PassengerItem*>(candidate)) {
					emit MousePressedOnPassenger(passenger);
					itemClicked = true;
				}

				if (itemClicked)
					break;
			}
			if (itemClicked)
				break;
		}

		if (!itemClicked)
			emit DisableHighlight();
	}

	// send signal
	emit MousePressedOnScene();

	// default implementation
	QGraphicsScene::mousePressEvent(mouseEvent);
}
