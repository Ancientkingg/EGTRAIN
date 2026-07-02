#include "myQGraphicsPixmapItem.h"

myQGraphicsPixmapItem::myQGraphicsPixmapItem(const QPixmap& pixmap, QGraphicsItem* parent)
	: QGraphicsPixmapItem(pixmap, parent) {
	setZValue(2); // draw over arcs and connections (z = 0) and nodes (z = 1)
}

myQGraphicsPixmapItem::~myQGraphicsPixmapItem() {
}
