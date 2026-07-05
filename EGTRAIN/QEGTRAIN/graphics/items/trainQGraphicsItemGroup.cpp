#include "graphics/items/trainQGraphicsItemGroup.h"

trainQGraphicsItemGroup::trainQGraphicsItemGroup(QGraphicsItem* parent)
	: QGraphicsItemGroup(parent) {
	setZValue(4); // draw on top of everything

	train = nullptr;
	index = -1;
	trainPolygonItemList = nullptr;
	paxInfoItem = nullptr;
}

trainQGraphicsItemGroup::~trainQGraphicsItemGroup() {
}
