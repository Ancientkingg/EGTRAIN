#ifndef NODEITEM_H
#define NODEITEM_H

#include <QGraphicsEllipseItem>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

#include "simulation/Infrastructure.h"

class NodeItem : public QGraphicsEllipseItem {
	// Q_OBJECT

public:
	NodeItem(const QRectF& rect, QGraphicsItem* parent = nullptr);
	~NodeItem() override;

	// reimplemented functions
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

	// track to which Node belongs
	int track;

	// Node pointer
	Node* node;

	// to allow cast
	enum { Type = UserType + 2 };
	int type() const override {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // NODEITEM_H
