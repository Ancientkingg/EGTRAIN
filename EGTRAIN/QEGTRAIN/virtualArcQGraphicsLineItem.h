#ifndef VIRTUALARCQGRAPHICSLINEITEM_H
#define VIRTUALARCQGRAPHICSLINEITEM_H

#include "myQGraphicsLineItem.h"

class virtualArcQGraphicsLineItem : public myQGraphicsLineItem {
	// Q_OBJECT

public:
	virtualArcQGraphicsLineItem(const QLineF& firstLine, const QLineF& secondLine, QGraphicsItem* parent = 0);
	~virtualArcQGraphicsLineItem();

	// reimplemented functions
	QPainterPath shape() const;
	QRectF boundingRect() const;
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget);

private:
	QLineF secondLine; // second part of the virtual Arc

	// used to highlight the line when clicked
	void createSelectionPolygon();
};

#endif // VIRTUALARCQGRAPHICSLINEITEM_H