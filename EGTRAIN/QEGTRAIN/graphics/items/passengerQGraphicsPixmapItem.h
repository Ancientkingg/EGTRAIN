#ifndef PASSENGERQGRAPHICSPIXMAPITEM_H
#define PASSENGERQGRAPHICSPIXMAPITEM_H

#include <QGraphicsPixmapItem>

#include "simulation/Passengers.h"

class passengerQGraphicsPixmapItem : public QGraphicsPixmapItem {
	// Q_OBJECT

public:
	passengerQGraphicsPixmapItem(const QPixmap& pixmap, QGraphicsItem* parent = 0);
	~passengerQGraphicsPixmapItem();

	// pointer to passenger object
	Passenger* passenger;

	// to allow cast
	enum { Type = UserType + 10 };
	int type() const {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // PASSENGERQGRAPHICSPIXMAPITEM_H