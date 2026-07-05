#include "graphics/items/TrainItemGroup.h"

TrainItemGroup::TrainItemGroup(QGraphicsItem* parent)
	: QGraphicsItemGroup(parent) {
	setZValue(4); // draw on top of everything

	train = nullptr;
	index = -1;
	trainPolygonItemList = nullptr;
	paxInfoItem = nullptr;
}

TrainItemGroup::~TrainItemGroup() {
}
