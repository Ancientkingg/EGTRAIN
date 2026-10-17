#ifndef STATIONNODEITEM_H
#define STATIONNODEITEM_H

#include <QGraphicsRectItem>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

#include "simulation/Infrastructure.h"

class StationNodeItem : public QGraphicsRectItem {
	// Q_OBJECT

public:
	StationNodeItem(const QRectF& rect, QGraphicsItem* parent = nullptr);
	~StationNodeItem() override;

	// reimplemented functions
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

	// track to which Node belongs
	int track;

	// Node pointer
	Node* node;

	// to allow cast
	enum { Type = UserType + 5 };
	int type() const override {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // STATIONNODEITEM_H
