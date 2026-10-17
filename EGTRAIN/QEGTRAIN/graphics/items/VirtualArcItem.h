#ifndef VIRTUALARCITEM_H
#define VIRTUALARCITEM_H

#include "graphics/items/TrackLineItem.h"

class VirtualArcItem : public TrackLineItem {
	// Q_OBJECT

public:
	VirtualArcItem(const QLineF& firstLine, const QLineF& secondLine, QGraphicsItem* parent = nullptr);
	~VirtualArcItem() override;
	const QLineF& secondPaintedLine() const { return secondLine; }

	// reimplemented functions
	QPainterPath shape() const override;
	QRectF boundingRect() const override;
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
	QLineF secondLine; // second part of the virtual Arc

	// used to highlight the line when clicked
	void createSelectionPolygon();
};

#endif // VIRTUALARCITEM_H
