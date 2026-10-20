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
	TrainBodyItem(const QPolygonF& polygon, QGraphicsItem* parent = nullptr);
	~TrainBodyItem() override;

	// Narrowest on-screen size, in logical pixels, of a body that is still drawn as a mark.
	static constexpr qreal MinPixels = 8.0;

	// A body keeps its shape and fill at every zoom. When the view shows it, with its outline, thinner than
	// MinPixels, it is stroked in its fill colour to that thickness, and the stroke is part of the bounding
	// rect and the shape that a click hits. viewScale is the pixels per scene unit of the view.
	void setViewScale(qreal viewScale);

	// reimplemented functions
	QRectF boundingRect() const override;
	QPainterPath shape() const override;
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

	// train index on list of trains (train ID is not unique, index is)
	int index;

	// to allow cast
	enum { Type = UserType + 7 };
	int type() const override {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}

private:
	// Width in scene units of the stroke that enlarges the body at viewScale, 0 when the body is thick enough.
	qreal markWidth(qreal viewScale) const;

	qreal m_viewScale = 0.0;
};

#endif // TRAINBODYITEM_H
