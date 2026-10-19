#ifndef VIRTUALARCITEM_H
#define VIRTUALARCITEM_H

#include "graphics/items/TrackLineItem.h"

/// A track line drawn as two lines, firstLine and secondLine, with one click target over both.
class VirtualArcItem : public TrackLineItem {
public:
	/// The two lines in scene coordinates. The click target joins the end of the first to the start of the second.
	VirtualArcItem(const QLineF& firstLine, const QLineF& secondLine, QGraphicsItem* parent = nullptr);
	~VirtualArcItem() override;
	/// The second line; line() is the first.
	const QLineF& secondPaintedLine() const { return secondLine; }

	/// The click target over both lines, 20 scene units on each side of them.
	QPainterPath shape() const override;
	/// The bounds of the click target.
	QRectF boundingRect() const override;
	/// Draws both lines with the pen of the item.
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
	QLineF secondLine; // second part of the virtual Arc

	// Builds the click target over both lines.
	void createSelectionPolygon();
};

#endif // VIRTUALARCITEM_H
