#ifndef MYQGRAPHICSELLIPSEITEM_H
#define MYQGRAPHICSELLIPSEITEM_H

#include <QGraphicsEllipseItem>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

#include "simulation/Infrastructure.h"

class myQGraphicsEllipseItem : public QGraphicsEllipseItem {
	// Q_OBJECT

public:
	myQGraphicsEllipseItem(const QRectF& rect, QGraphicsItem* parent = 0);
	~myQGraphicsEllipseItem();

	// reimplemented functions
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget);

	// track to which Node belongs
	int track;

	// Node pointer
	Node* Node;

	// to allow cast
	enum { Type = UserType + 2 };
	int type() const {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // MYQGRAPHICSELLIPSEITEM_H