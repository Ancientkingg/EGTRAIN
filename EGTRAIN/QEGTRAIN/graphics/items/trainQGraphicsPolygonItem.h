#ifndef TRAINQGRAPHICSPOLYGONITEM_H
#define TRAINQGRAPHICSPOLYGONITEM_H

#include <QGraphicsPolygonItem>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

#include "simulation/RollingStock.h"

class trainQGraphicsPolygonItem : public QGraphicsPolygonItem {
	// Q_OBJECT

public:
	trainQGraphicsPolygonItem(const QPolygonF& polygon, QGraphicsItem* parent = 0);
	~trainQGraphicsPolygonItem();

	// reimplemented functions
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget);

	// train pointer
	Train* train;

	// train index on list of trains (train ID is not unique, index is)
	int index;

	// to allow cast
	enum { Type = UserType + 7 };
	int type() const {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // TRAINQGRAPHICSPOLYGONITEM_H