#include "graphics/NetworkScene.h"

#include <QGraphicsView>

NetworkScene::NetworkScene(QObject* parent)
	: QGraphicsScene(parent) {
}

NetworkScene::~NetworkScene() {
}

QTransform NetworkScene::viewTransformFor(QWidget* widget) const {
	for (QGraphicsView* view : views()) {
		if (view->viewport() == widget)
			return view->viewportTransform();
	}
	return QTransform();
}

QGraphicsItem* NetworkScene::semanticItemAt(const QPointF& scenePos, QWidget* widget) const {
	const QList<QGraphicsItem*> hitItems = items(
		scenePos, Qt::IntersectsItemShape, Qt::DescendingOrder, viewTransformFor(widget));
	for (QGraphicsItem* item : hitItems) {
		for (QGraphicsItem* candidate = item; candidate; candidate = candidate->parentItem()) {
			if (qgraphicsitem_cast<NodeItem*>(candidate)
				|| qgraphicsitem_cast<StationNodeItem*>(candidate)
				|| qgraphicsitem_cast<TrackLineItem*>(candidate)
				|| qgraphicsitem_cast<ConnectionItem*>(candidate)
				|| qgraphicsitem_cast<SignalItem*>(candidate)
				|| qgraphicsitem_cast<TrainBodyItem*>(candidate)
				|| qgraphicsitem_cast<PassengerItem*>(candidate))
				return candidate;
		}
	}
	return nullptr;
}

// handles the click on graphical items
void NetworkScene::mousePressEvent(QGraphicsSceneMouseEvent* mouseEvent) {
	QGraphicsItem* item = nullptr;
	if (mouseEvent->button() == Qt::LeftButton)
		item = semanticItemAt(mouseEvent->scenePos(), mouseEvent->widget());

	// Let Qt finish its built-in selection handling before application selection
	// highlights are applied by the semantic handlers below.
	QGraphicsScene::mousePressEvent(mouseEvent);

	if (mouseEvent->button() == Qt::LeftButton) {
		if (NodeItem* node = qgraphicsitem_cast<NodeItem*>(item))
			emit MousePressedOnNode(node);
		else if (StationNodeItem* stationNode = qgraphicsitem_cast<StationNodeItem*>(item))
			emit MousePressedOnStationNode(stationNode);
		else if (TrackLineItem* arc = qgraphicsitem_cast<TrackLineItem*>(item))
			emit MousePressedOnArc(arc);
		else if (ConnectionItem* connection = qgraphicsitem_cast<ConnectionItem*>(item))
			emit MousePressedOnConnection(connection);
		else if (SignalItem* signal = qgraphicsitem_cast<SignalItem*>(item))
			emit MousePressedOnSignal(signal);
		else if (TrainBodyItem* train = qgraphicsitem_cast<TrainBodyItem*>(item))
			emit MousePressedOnTrain(train);
		else if (PassengerItem* passenger = qgraphicsitem_cast<PassengerItem*>(item))
			emit MousePressedOnPassenger(passenger);

		if (!item)
			emit DisableHighlight();
	}

	emit MousePressedOnScene();
}

void NetworkScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
	QGraphicsItem* item = semanticItemAt(event->scenePos(), event->widget());
	emit ContextMenuRequested(item, event->scenePos(), event->screenPos(),
		event->reason() == QGraphicsSceneContextMenuEvent::Keyboard);
	event->accept();
}
