#include "graphics/NetworkScene.h"

#include <QGraphicsView>
#include <limits>

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
	SignalItem* nearestSignal = nullptr;
	qreal nearestDistance = std::numeric_limits<qreal>::max();
	NodeItem* paddedNode = nullptr;
	const QTransform device = viewTransformFor(widget);
	const QPointF click = device.map(scenePos);
	for (QGraphicsItem* item : hitItems) {
		for (QGraphicsItem* candidate = item; candidate; candidate = candidate->parentItem()) {
			// Artwork owns its station identity even when its structural parent
			// has a different last-wins station membership.
			if (candidate->data(PreviewGraphics::Kind).toString() == "station")
				return candidate;
			if (auto* signal = qgraphicsitem_cast<SignalItem*>(candidate)) {
				const QPointF local = signal->mapFromScene(scenePos);
				if (signal->QGraphicsEllipseItem::shape().contains(local))
					return signal;
				const qreal distance = QLineF(click, device.map(signal->sceneBoundingRect().center())).length();
				if (distance < nearestDistance) {
					nearestDistance = distance;
					nearestSignal = signal;
				}
				break;
			}
			if (auto* node = qgraphicsitem_cast<NodeItem*>(candidate)) {
				if (node->shape().contains(node->mapFromScene(scenePos)))
					return node;
				if (!paddedNode)
					paddedNode = node;
				break;
			}
			// Node targets sit above the broad track/connection selection shapes.
			// Keep looking for painted signals or trains before falling back to the node.
			if (paddedNode && (qgraphicsitem_cast<TrackLineItem*>(candidate) || qgraphicsitem_cast<ConnectionItem*>(candidate)))
				break;
			if (qgraphicsitem_cast<StationNodeItem*>(candidate)
				|| qgraphicsitem_cast<TrackLineItem*>(candidate)
				|| qgraphicsitem_cast<ConnectionItem*>(candidate)
				|| qgraphicsitem_cast<TrainBodyItem*>(candidate)
				|| qgraphicsitem_cast<PassengerItem*>(candidate))
				return candidate;
		}
	}
	return paddedNode ? static_cast<QGraphicsItem*>(paddedNode) : nearestSignal;
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
		if (item && item->data(PreviewGraphics::Kind).isValid())
			emit MousePressedOnPreview(item);
		else if (NodeItem* node = qgraphicsitem_cast<NodeItem*>(item))
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
