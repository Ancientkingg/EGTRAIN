#ifndef NETWORKSCENE_H
#define NETWORKSCENE_H

#include <QGraphicsScene>
#include <QGraphicsSceneContextMenuEvent>
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

#include "graphics/VisualPolish.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/NodeItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/PassengerItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"
#include <QGraphicsTextItem>

/// The item data roles of the items of the preview. They identify an item by the ids of the scene model;
/// no model or native pointer is stored, and a click resolves the current data. Roles 0 to 3 belong to the
/// signal decoration (graphics/SignalItemRoles.h).
namespace PreviewGraphics {
/// An item data role of the preview.
enum Role { Kind = 100, ///< The kind of item: "track", "arc", "connection", "node", "station-node", "station" or "signal".
	Id,					///< The id of the element of the scene model: track, arc, connection, node, station or section.
	StationId,			///< On a station node: the id of its station.
	PlatformId,			///< On a station node: the id of its platform, empty without one.
	TrackId,			///< On a signal head: the id of its track.
	Reversed /**< On a signal head: true for the head of the reversed direction. */ };
}

using namespace std;

/// The scene of the network canvas. It picks the item under a click by what it stands for rather than by
/// the stacking order alone, and tells the window what was clicked through its signals.
class NetworkScene : public QGraphicsScene {
	Q_OBJECT

public:
	/// A scene with parent as its QObject parent.
	NetworkScene(QObject* parent);
	~NetworkScene() override;

	/// A left click selects the item under it as select() does. Every press then emits MousePressedOnScene().
	void mousePressEvent(QGraphicsSceneMouseEvent* mouseEvent) override;
	/// A left click on empty canvas emits DisableHighlight(). A drag that starts there pans the view and keeps
	/// the selection.
	void mouseReleaseEvent(QGraphicsSceneMouseEvent* mouseEvent) override;
	/// Emits ContextMenuRequested() with the item that a left click at the same point would select, or null. A request
	/// by key goes to requestKeyboardContextMenu() instead.
	void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

	/// Selects the item as a left click on it does: emits the MousePressedOn signal of its kind. Null clears the selection
	/// without a signal; an item of another kind emits nothing.
	void select(QGraphicsItem* item);

	/// The item that select() chose last, or null when none was chosen, the selection was cleared or the item has left
	/// the scene.
	QGraphicsItem* selectedItem() const;

	/// Emits ContextMenuRequested() for a key: with selectedItem() and the centre of the item as the point, or with null and
	/// the centre of the view when nothing is selected. widget is the view that shows the scene; null takes its first view.
	void requestKeyboardContextMenu(QWidget* widget = nullptr);

	/// The style of the station marks of the scene; StationMarkStyle::MapPin until setStationMarkStyle().
	StationMarkStyle stationMarkStyle() const { return m_stationMarkStyle; }
	/// Draws the station marks of the scene in style, and those that stations attach later
	/// (StationOverlayItem::attachArtwork()). The next view scale that the stations apply places them by the anchor of
	/// style.
	void setStationMarkStyle(StationMarkStyle style);

signals:
	/// A mouse button was pressed on the scene.
	void MousePressedOnScene();
	/// An item of the preview was selected.
	void MousePressedOnPreview(QGraphicsItem* item);
	/// A node of a run was selected.
	void MousePressedOnNode(NodeItem* Node);
	/// A station node of a run was selected.
	void MousePressedOnStationNode(StationNodeItem* Node);
	/// A track line of a run was selected.
	void MousePressedOnArc(TrackLineItem* Arc);
	/// A connection of a run was selected.
	void MousePressedOnConnection(ConnectionItem* connection);
	/// A signal head of a run was selected.
	void MousePressedOnSignal(SignalItem* signal);
	/// A body of a train was selected.
	void MousePressedOnTrain(TrainBodyItem* train);
	/// A passenger icon was selected.
	void MousePressedOnPassenger(PassengerItem* passenger);
	/// A click on empty canvas clears the selection.
	void DisableHighlight();
	/// A context menu is asked for at scenePos in scene coordinates and screenPos in screen pixels. item is
	/// the item there or null; keyboard is true when a key, not the mouse, asked for the menu.
	void ContextMenuRequested(QGraphicsItem* item, const QPointF& scenePos, const QPoint& screenPos, bool keyboard);

private:
	QTransform viewTransformFor(QWidget* widget) const;
	QGraphicsItem* semanticItemAt(const QPointF& scenePos, QWidget* widget) const;

	bool m_pressedOnEmptyCanvas = false;
	// The item that select() chose last. Items leave the scene without notice, so selectedItem() checks it.
	QGraphicsItem* m_selectedItem = nullptr;
	StationMarkStyle m_stationMarkStyle = StationMarkStyle::MapPin;
};

#endif // NETWORKSCENE_H
