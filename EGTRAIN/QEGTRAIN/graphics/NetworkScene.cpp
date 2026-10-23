#include "graphics/NetworkScene.h"

#include <QApplication>
#include <QGraphicsView>
#include <QPainterPath>
#include <limits>

namespace {
// Whether scenePos, on a view with the transform device, is in the pick circle of item, a node dot or a signal head:
// PickPixels across on screen around the centre of its rect. As the scene tests an item that ignores the view transform,
// the point is in the square of the circle and the square of one pixel from the point meets the circle. Other items
// have no pick circle.
bool inPickCircle(const QGraphicsItem* item, const QPointF& scenePos, const QTransform& device) {
	const QGraphicsEllipseItem* ellipse = qgraphicsitem_cast<const NodeItem*>(item);
	qreal pixels = NodeItem::PickPixels;
	if (!ellipse) {
		ellipse = qgraphicsitem_cast<const SignalItem*>(item);
		pixels = SignalItem::PickPixels;
	}
	if (!ellipse)
		return false;
	const QPointF local = device.map(scenePos) - device.map(ellipse->mapToScene(ellipse->rect().center()));
	const QRectF square(-pixels / 2.0, -pixels / 2.0, pixels, pixels);
	if (!square.contains(local))
		return false;
	QPainterPath circle;
	circle.addEllipse(square);
	return circle.intersects(QRectF(local, QSizeF(1.0, 1.0)));
}
} // namespace

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
	const QTransform device = viewTransformFor(widget);
	const QPointF click = device.map(scenePos);
	// The items under the point, with every node dot and signal head whose pick circle holds it, from the top down. The
	// square around the point that the widest pick circle reaches holds every item that the point hits too, so the items
	// in it give the order.
	const QList<QGraphicsItem*> pointHits = items(scenePos, Qt::IntersectsItemShape, Qt::DescendingOrder, device);
	const qreal reach = SignalItem::PickPixels / 2.0 + 1.0;
	const QRectF around = device.inverted().mapRect(QRectF(click - QPointF(reach, reach), click + QPointF(reach, reach)));
	QList<QGraphicsItem*> hitItems;
	for (QGraphicsItem* item : items(around, Qt::IntersectsItemBoundingRect, Qt::DescendingOrder, device))
		if (pointHits.contains(item) || inPickCircle(item, scenePos, device))
			hitItems.append(item);
	SignalItem* nearestSignal = nullptr;
	qreal nearestDistance = std::numeric_limits<qreal>::max();
	NodeItem* paddedNode = nullptr;
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
			// The pick circles of nodes and signals sit above the broad track/connection selection shapes.
			// Keep looking for painted signals or trains before falling back to the node or the padded signal.
			if ((paddedNode || nearestSignal) && (qgraphicsitem_cast<TrackLineItem*>(candidate) || qgraphicsitem_cast<ConnectionItem*>(candidate)))
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

void NetworkScene::select(QGraphicsItem* item) {
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
		select(item);
		m_pressedOnEmptyCanvas = !item;
	}

	emit MousePressedOnScene();
}

// A click on empty canvas clears the selection. A drag that starts there pans the view and keeps it.
void NetworkScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* mouseEvent) {
	QGraphicsScene::mouseReleaseEvent(mouseEvent);
	if (mouseEvent->button() != Qt::LeftButton || !m_pressedOnEmptyCanvas)
		return;
	m_pressedOnEmptyCanvas = false;
	const QPoint moved = mouseEvent->screenPos() - mouseEvent->buttonDownScreenPos(Qt::LeftButton);
	if (moved.manhattanLength() < QApplication::startDragDistance())
		emit DisableHighlight();
}

void NetworkScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
	QGraphicsItem* item = semanticItemAt(event->scenePos(), event->widget());
	emit ContextMenuRequested(item, event->scenePos(), event->screenPos(),
		event->reason() == QGraphicsSceneContextMenuEvent::Keyboard);
	event->accept();
}
