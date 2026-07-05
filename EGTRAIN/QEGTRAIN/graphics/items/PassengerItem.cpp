#include "graphics/items/PassengerItem.h"

PassengerItem::PassengerItem(const QPixmap& pixmap, QGraphicsItem* parent)
	: QGraphicsPixmapItem(pixmap, parent), passenger(nullptr) {
	setZValue(3); // draw over platforms
}

PassengerItem::~PassengerItem() {
}
