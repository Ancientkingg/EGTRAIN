#ifndef SIGNALITEM_H
#define SIGNALITEM_H

#include <QGraphicsEllipseItem>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

#include "simulation/Signalling.h"

class SignalItem : public QGraphicsEllipseItem {
	// Q_OBJECT

public:
	SignalItem(const QRectF& rect, QGraphicsItem* parent = 0);
	~SignalItem();

	// reimplemented functions
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget);

	// trackline to which signal belongs
	int trackID;

	// location of the signal on X axis
	double X;

	// pointers to block sections (ahead/behind signalling_block_sections depends on reversedDirection variable)
	Section* sectionAhead;
	Section* sectionBehind;

	// distinguishes signals at same location (indicates for which direction this signal is used)
	bool reversedDirection;

	// to allow cast
	enum { Type = UserType + 6 };
	int type() const {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // SIGNALITEM_H