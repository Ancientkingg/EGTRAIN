#ifndef TRAINITEMGROUP_H
#define TRAINITEMGROUP_H

#include <QColor>
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
	// fill and outline of the train bodies; serviceId names the service that
	// supplies a custom colour (empty for the default colour)
	QColor fillColor;
	QColor outlineColor;
	std::string serviceId;
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

	// QGraphicsItemGroup caches its child bounds. Notify the scene before a
	// train body polygon changes so the group's scene index remains current.
	void prepareForChildGeometryChange();
	QRectF boundingRect() const override;

	// to allow cast
	enum { Type = UserType + 8 };
	int type() const override {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // TRAINITEMGROUP_H
