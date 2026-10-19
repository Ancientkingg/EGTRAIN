#ifndef PLATFORMITEM_H
#define PLATFORMITEM_H

#include <QGraphicsRectItem>
#include <string>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

#include "graphics/items/PassengerItem.h"

/// The bar of a platform next to a station node, with the icons of the passengers waiting on it. It draws over
/// track lines, connections and nodes.
class PlatformItem : public QGraphicsRectItem {
public:
	/// A bar in rect, in scene coordinates, without a platform.
	PlatformItem(const QRectF& rect, QGraphicsItem* parent = nullptr);
	~PlatformItem() override;

	/// Draws the bar with the pen and the brush of the item.
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

	/// The id of the station of the platform; empty when the run has no such platform and in the preview.
	std::string stationId;
	/// The id of the platform; empty when the run has no such platform and in the preview.
	std::string platformId;
	/// The largest number of passengers on the platform when it was drawn; 0 when the run has no such platform.
	int maxVolume;

	/// The label with the number of passengers on the platform, which the scene owns; null in the preview.
	QGraphicsTextItem* textIcon;

	/// The icon of each passenger on the platform, in the order of the snapshot. The scene owns them.
	QList<PassengerItem*> passengerIcons;

	enum { Type = UserType + 9 /**< The item type for qgraphicsitem_cast. */ };
	/// Returns Type.
	int type() const override {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // PLATFORMITEM_H
