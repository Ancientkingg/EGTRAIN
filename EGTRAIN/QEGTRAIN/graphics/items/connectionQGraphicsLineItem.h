#ifndef CONNECTIONQGRAPHICSLINEITEM_H
#define CONNECTIONQGRAPHICSLINEITEM_H

#include <QGraphicsLineItem>

#include <QRectF>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPen>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

#include "simulation/Infrastructure.h"

class connectionQGraphicsLineItem : public QGraphicsLineItem {
	// Q_OBJECT

public:
	connectionQGraphicsLineItem(const QLineF& line, QGraphicsItem* parent = 0);
	~connectionQGraphicsLineItem();

	// reimplemented functions
	QPainterPath shape() const;
	QRectF boundingRect() const;
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget);

	// connection pointer
	Connections* connection;

	// to allow cast
	enum { Type = UserType + 4 };
	int type() const {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}

private:
	// used to highlight the line when clicked
	const qreal selectionOffset;
	QPolygonF selectionPolygon;
	void createSelectionPolygon();
};

#endif // CONNECTIONQGRAPHICSLINEITEM_H