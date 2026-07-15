#ifndef TRAINITEMGROUP_H
#define TRAINITEMGROUP_H

#include <QGraphicsItemGroup>
#include <string>

#include "graphics/items/TrainBodyItem.h"

class TrainItemGroup : public QGraphicsItemGroup {
	// Q_OBJECT

public:
	TrainItemGroup(QGraphicsItem* parent = 0);
	~TrainItemGroup();

	// train index on list of trains (train ID is not unique, index is)
	int index;
	std::string trainDescription;
	std::string trainType;
	double trainId;
	double trainLength;
	int wagonCount;
	int currentOnboardPassengers;
	int maxOnboardPassengers;
	bool outOfSimulation;

	// pax info group icon pointer
	QGraphicsItemGroup* paxInfoItem;

	// pointer to list of polygons
	QList<TrainBodyItem*>* trainPolygonItemList;

	// to allow cast
	enum { Type = UserType + 8 };
	int type() const {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // TRAINITEMGROUP_H
