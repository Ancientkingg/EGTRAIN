#ifndef PLATFORMITEM_H
#define PLATFORMITEM_H

#include <QGraphicsRectItem>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

#include "simulation/Infrastructure.h"
#include "graphics/items/PassengerItem.h"

class PlatformItem : public QGraphicsRectItem {
	// Q_OBJECT

public:
	PlatformItem(const QRectF& rect, QGraphicsItem* parent = 0);
	~PlatformItem();

	// reimplemented functions
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget);

	// StationPlatform pointer
	StationPlatform* platform;

	// text item pointer
	QGraphicsTextItem* textIcon;

	// collection of passenger icons
	QList<PassengerItem*> passengerIcons;

	// to allow cast
	enum { Type = UserType + 9 };
	int type() const {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // PLATFORMITEM_H