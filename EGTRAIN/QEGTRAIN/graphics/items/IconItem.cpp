#include "graphics/items/IconItem.h"

IconItem::IconItem(const QPixmap& pixmap, QGraphicsItem* parent)
	: QGraphicsPixmapItem(pixmap, parent) {
	setZValue(2); // draw over arcs and connections (z = 0) and nodes (z = 1)
}

IconItem::~IconItem() {
}
