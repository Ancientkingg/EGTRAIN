#include "graphics/items/TrainItemGroup.h"

TrainItemGroup::TrainItemGroup(QGraphicsItem* parent)
	: QGraphicsItemGroup(parent) {
	setZValue(4); // draw on top of everything

	index = -1;
	trainId = 0.0;
	trainLength = 0.0;
	wagonCount = 0;
	currentOnboardPassengers = 0;
	maxOnboardPassengers = 0;
	outOfSimulation = false;
	trainPolygonItemList = nullptr;
	paxInfoItem = nullptr;
}

TrainItemGroup::~TrainItemGroup() {
}
