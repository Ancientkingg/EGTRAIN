#include "passengerQGraphicsPixmapItem.h"

passengerQGraphicsPixmapItem::passengerQGraphicsPixmapItem(const QPixmap& pixmap, QGraphicsItem* parent)
	: QGraphicsPixmapItem(pixmap, parent), passenger(nullptr) {
	setZValue(3); // draw over platforms
}

passengerQGraphicsPixmapItem::~passengerQGraphicsPixmapItem() {
}
