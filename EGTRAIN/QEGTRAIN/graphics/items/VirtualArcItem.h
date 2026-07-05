#ifndef VIRTUALARCITEM_H
#define VIRTUALARCITEM_H

#include "graphics/items/TrackLineItem.h"

class VirtualArcItem : public TrackLineItem {
	// Q_OBJECT

public:
	VirtualArcItem(const QLineF& firstLine, const QLineF& secondLine, QGraphicsItem* parent = 0);
	~VirtualArcItem();

	// reimplemented functions
	QPainterPath shape() const;
	QRectF boundingRect() const;
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget);

private:
	QLineF secondLine; // second part of the virtual Arc

	// used to highlight the line when clicked
	void createSelectionPolygon();
};

#endif // VIRTUALARCITEM_H