#ifndef TRAINITEMGROUP_H
#define TRAINITEMGROUP_H

#include <QColor>
#include <QGraphicsItemGroup>
#include <string>

#include "graphics/items/TrainBodyItem.h"

/// A train on the canvas: the group of the bodies of its wagons, with the data that the inspector and the
/// passenger information read. It draws over the track, the signals, the stations, the platforms and the
/// passengers.
class TrainItemGroup : public QGraphicsItemGroup {
public:
	/// An empty group with train index -1 and no bodies.
	TrainItemGroup(QGraphicsItem* parent = nullptr);
	/// Deletes the list of bodies; the bodies are children and go with the group.
	~TrainItemGroup() override;

	/// The index of the train in the run; -1 until it is set. A train ID is not unique, the index is.
	int index;
	/// The description of the train.
	std::string trainDescription;
	/// The type of the train.
	std::string trainType;
	/// The fill of the bodies.
	QColor fillColor;
	/// The outline of the bodies.
	QColor outlineColor;
	/// The service that gives the bodies their colour; empty for the default colour.
	std::string serviceId;
	/// The ID of the train.
	double trainId;
	/// The length of the train in metres.
	double trainLength;
	/// The number of wagons; the group has one body more.
	int wagonCount;
	/// The number of passengers on board.
	int currentOnboardPassengers;
	/// The passenger capacity of the train, from its number of wagons.
	int maxOnboardPassengers;
	/// The train has left the network.
	bool outOfSimulation;

	/// The body items of the train, from the head. The bodies are children of the group; the group owns
	/// the list and deletes it in its destructor. A caller that deletes the list itself
	/// sets this pointer to null first.
	QList<TrainBodyItem*>* trainPolygonItemList;

	/// QGraphicsItemGroup caches its child bounds. Notify the scene before a
	/// train body polygon changes so the group's scene index remains current.
	void prepareForChildGeometryChange();
	/// The bounds of the bodies as they are now.
	QRectF boundingRect() const override;

	/// Gives every body its minimum size on screen for a view of viewScale logical pixels per scene unit.
	void applyViewScale(qreal viewScale);

	enum { Type = UserType + 8 /**< The item type for qgraphicsitem_cast. */ };
	/// Returns Type.
	int type() const override {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}

private:
	qreal m_appliedViewScale = 0.0;
};

#endif // TRAINITEMGROUP_H
