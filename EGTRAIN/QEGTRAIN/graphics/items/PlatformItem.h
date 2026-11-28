#ifndef PLATFORMITEM_H
#define PLATFORMITEM_H

#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <string>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

#include "graphics/items/PassengerItem.h"
#include "simulation/Infrastructure.h"

/// The bar of a platform next to a station node, drawn for every platform and shown with the station layer when the
/// view is zoomed in. In a run with passengers it can show as a gauge of the crowding of the platform, and it holds the
/// figures of the passengers waiting on it. It draws over track lines, connections and nodes.
class PlatformItem : public QGraphicsRectItem {
public:
	/// A bar in rect, in item coordinates, without a platform.
	PlatformItem(const QRectF& rect, QGraphicsItem* parent = nullptr);
	~PlatformItem() override;

	/// Draws the bar with the pen and the brush of the item, or as a gauge when setGauge() has turned it on.
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
	/// The rectangle of the bar, with the margin that the end cap of a gauge needs while the gauge is on.
	QRectF boundingRect() const override;

	/// Turns the gauge on or off. A bar with the gauge on is a dark trough with a fill from its start for the passengers
	/// waiting, and an end cap while the platform is over its capacity. Off, the bar is plain.
	void setGauge(bool enabled);
	/// Whether the bar draws as a gauge.
	bool gauge() const { return m_gauge; }
	/// Sets how many passengers wait on a platform that holds capacity of them, the occupancy rate above which it
	/// counts as crowded, and how many passengers are held at the station entrance. A capacity of 0 or less leaves the
	/// trough empty. The bar is drawn again only when something changes.
	void setCrowding(int waiting, int capacity, double threshold, int held = 0);
	/// The number of passengers that setCrowding() was given.
	int waiting() const { return m_waiting; }
	/// The capacity that setCrowding() was given, 0 or more.
	int capacity() const { return m_capacity; }
	/// The number of passengers held at the entrance that setCrowding() was given, 0 or more.
	int held() const { return m_held; }
	/// The fraction of the bar that the fill covers: waiting over capacity up to 1, 0 without a capacity.
	qreal fillFraction() const;
	/// The fraction of the bar length that the held-at-entrance overflow past capacity covers, 0 when none are held or
	/// without a capacity.
	qreal heldFraction() const;
	/// How crowded the platform is: Over above its capacity or with passengers held at the entrance, Busy above its
	/// threshold, else Normal; Normal without a capacity.
	PlatformCrowding crowding() const;

	/// The node of the run beside which the bar lies, which belongs to the run; null in the preview.
	const Node* node;
	/// The id of the station of the platform; empty when the run has no such platform or no passengers, and in the
	/// preview.
	std::string stationId;
	/// The id of the platform; empty when the run has no such platform or no passengers, and in the preview.
	std::string platformId;
	/// The label with the number of passengers on the platform, shown at a fixed size on the screen while the view is
	/// not zoomed in far enough for the figures; the scene owns it. Null in a run without passengers and in the preview.
	QGraphicsTextItem* textIcon;

	/// The "+N" chip after the figures for the N passengers on the platform who have no figure, which the scene owns;
	/// null while every passenger has a figure.
	QGraphicsPathItem* moreIcon = nullptr;
	/// The text of the chip, a child of the chip; null when the chip is null.
	QGraphicsSimpleTextItem* moreLabel = nullptr;
	/// The "+N held" chip past the end of the bar for passengers held at the entrance while figures show, which the scene
	/// owns; null while none are held.
	QGraphicsPathItem* heldIcon = nullptr;
	/// The text of the held chip, a child of heldIcon; null when heldIcon is null.
	QGraphicsSimpleTextItem* heldLabel = nullptr;

	/// The figure of each passenger on the platform that the snapshot lists and that fits along the bar, in its order.
	/// The scene owns them.
	QList<PassengerItem*> passengerIcons;

private:
	bool m_gauge = false;
	int m_waiting = 0;
	int m_capacity = 0;
	int m_held = 0;
	double m_threshold = 0.0;

public:
	enum { Type = UserType + 9 /**< The item type for qgraphicsitem_cast. */ };
	/// Returns Type.
	int type() const override {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // PLATFORMITEM_H
