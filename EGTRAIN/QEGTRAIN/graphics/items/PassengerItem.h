#ifndef PASSENGERITEM_H
#define PASSENGERITEM_H

#include <QGraphicsPixmapItem>
#include <string>

class PassengerItem : public QGraphicsPixmapItem {
	// Q_OBJECT

public:
	PassengerItem(const QPixmap& pixmap, QGraphicsItem* parent = 0);
	~PassengerItem();

	std::string passengerId;

	// to allow cast
	enum { Type = UserType + 10 };
	int type() const {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // PASSENGERITEM_H
