#ifndef MYQGRAPHICSRECTITEM_H
#define MYQGRAPHICSRECTITEM_H

#include <QGraphicsRectItem>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

#include "simulation/Infrastructure.h"

class myQGraphicsRectItem : public QGraphicsRectItem {
	// Q_OBJECT

public:
	myQGraphicsRectItem(const QRectF& rect, QGraphicsItem* parent = 0);
	~myQGraphicsRectItem();

	// reimplemented functions
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget);

	// track to which Node belongs
	int track;

	// Node pointer
	Node* Node;

	// to allow cast
	enum { Type = UserType + 5 };
	int type() const {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // MYQGRAPHICSRECTITEM_H