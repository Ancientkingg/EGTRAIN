#ifndef PLATFORMQGRAPHICSRECTITEM_H
#define PLATFORMQGRAPHICSRECTITEM_H

#include <QGraphicsRectItem>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

#include "simulation/Infrastructure.h"
#include "graphics/items/passengerQGraphicsPixmapItem.h"

class platformQGraphicsRectItem : public QGraphicsRectItem {
	// Q_OBJECT

public:
	platformQGraphicsRectItem(const QRectF& rect, QGraphicsItem* parent = 0);
	~platformQGraphicsRectItem();

	// reimplemented functions
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget);

	// StationPlatform pointer
	StationPlatform* platform;

	// text item pointer
	QGraphicsTextItem* textIcon;

	// collection of passenger icons
	QList<passengerQGraphicsPixmapItem*> passengerIcons;

	// to allow cast
	enum { Type = UserType + 9 };
	int type() const {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // PLATFORMQGRAPHICSRECTITEM_H