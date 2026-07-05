#ifndef TRAINQGRAPHICSITEMGROUP_H
#define TRAINQGRAPHICSITEMGROUP_H

#include <QGraphicsItemGroup>

#include "simulation/RollingStock.h"

#include "graphics/items/trainQGraphicsPolygonItem.h"

class trainQGraphicsItemGroup : public QGraphicsItemGroup {
	// Q_OBJECT

public:
	trainQGraphicsItemGroup(QGraphicsItem* parent = 0);
	~trainQGraphicsItemGroup();

	// train pointer
	Train* train;

	// train index on list of trains (train ID is not unique, index is)
	int index;

	// pax info group icon pointer
	QGraphicsItemGroup* paxInfoItem;

	// pointer to list of polygons
	QList<trainQGraphicsPolygonItem*>* trainPolygonItemList;

	// to allow cast
	enum { Type = UserType + 8 };
	int type() const {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // TRAINQGRAPHICSITEMGROUP_H
