#ifndef TRACKLINEITEM_H
#define TRACKLINEITEM_H

#include <QGraphicsLineItem>

#include <string>
#include <sstream>
#include <QRectF>
#include <QLineF>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPen>
#include <iostream>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

#include "simulation/Infrastructure.h"

using namespace std;

class TrackLineItem : public QGraphicsLineItem {
	// Q_OBJECT

public:
	TrackLineItem(const QLineF& line, QGraphicsItem* parent = 0);
	~TrackLineItem();

	// reimplemented functions
	QPainterPath shape() const;
	QRectF boundingRect() const;
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget);

	// track to which Arc belongs
	int track;

	// Arc pointer
	Arc* Arc;

	// to allow cast
	enum { Type = UserType + 1 };
	int type() const {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}

protected:
	// used to highlight the line when clicked
	const qreal selectionOffset;
	QPolygonF selectionPolygon;
	void createSelectionPolygon();
};

#endif // TRACKLINEITEM_H