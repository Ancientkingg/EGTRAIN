#ifndef PASSENGERITEM_H
#define PASSENGERITEM_H

#include <QGraphicsPixmapItem>
#include <string>

/// The icon of a passenger waiting on a platform. It draws over the platforms.
class PassengerItem : public QGraphicsPixmapItem {
public:
	/// An icon that shows pixmap, one scene unit per pixel before the scale of the item.
	PassengerItem(const QPixmap& pixmap, QGraphicsItem* parent = nullptr);
	~PassengerItem() override;

	/// The id of the passenger.
	std::string passengerId;

	enum { Type = UserType + 10 /**< The item type for qgraphicsitem_cast. */ };
	/// Returns Type.
	int type() const override {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // PASSENGERITEM_H
