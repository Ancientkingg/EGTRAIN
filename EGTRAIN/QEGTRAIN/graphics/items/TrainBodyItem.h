#ifndef TRAINBODYITEM_H
#define TRAINBODYITEM_H

#include <QGraphicsPolygonItem>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

class TrainBodyItem : public QGraphicsPolygonItem {
	// Q_OBJECT

public:
	TrainBodyItem(const QPolygonF& polygon, QGraphicsItem* parent = 0);
	~TrainBodyItem();

	// reimplemented functions
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget);

	// train index on list of trains (train ID is not unique, index is)
	int index;

	// to allow cast
	enum { Type = UserType + 7 };
	int type() const {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // TRAINBODYITEM_H
