#ifndef SIGNALITEM_H
#define SIGNALITEM_H

#include <QGraphicsEllipseItem>
#include <QPixmap>
#include <string>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

#include "graphics/VisualPolish.h"

class SignalItem : public QGraphicsEllipseItem {
	// Q_OBJECT

public:
	SignalItem(const QRectF& rect, QGraphicsItem* parent = 0);
	~SignalItem();

	void setAspectCode(int code);
	int aspectCode() const;
	void setReversedDirection(bool reversed);
	void setCompact(bool compact);

	// reimplemented functions
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget);

	// trackline to which signal belongs
	int trackID;

	// location of the signal on X axis
	double X;

	std::string sectionAheadId;
	std::string sectionBehindId;
	double sectionAheadLength;
	double sectionBehindLength;
	int sectionAheadTrackId;
	int sectionBehindTrackId;

	// distinguishes signals at same location (indicates for which direction this signal is used)
	bool reversedDirection;

	// to allow cast
	enum { Type = UserType + 6 };
	int type() const {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}

private:
	int m_aspectCode;
	QPixmap m_aspectIcon;
	QColor m_lampColor;
	bool m_compact;
};

#endif // SIGNALITEM_H
