#ifndef SIGNALITEM_H
#define SIGNALITEM_H

#include <QGraphicsEllipseItem>
#include <string>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QVector>
#include <QPair>
#include <QtMath>

class SignalItem : public QGraphicsEllipseItem {
	// Q_OBJECT

public:
	SignalItem(const QRectF& rect, QGraphicsItem* parent = 0);
	~SignalItem();

	void setAspectCode(int code);
	int aspectCode() const;
	void setReversedDirection(bool reversed);
	void setGroupedSignals(const QVector<QPair<int, bool>>& aspects);
	int groupedSignalCount() const { return m_groupedSignals.size(); }
	void setInspectionIdentity(const QString& identity) { m_inspectionIdentity = identity; }
	QString inspectionIdentity() const;
	QRectF boundingRect() const override;

	// reimplemented functions
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

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
	int type() const override {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}

private:
	int m_aspectCode;
	QColor m_lampColor;
	QVector<QPair<int, bool>> m_groupedSignals;
	QString m_inspectionIdentity;
};

#endif // SIGNALITEM_H
