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

/// A signal head: the lamp of one direction of a signal, in rect in scene coordinates. Its colour, and a mark for
/// any state but Proceed, show the state of the aspect code. A transparent child of 12 by 12 logical pixels at
/// its centre keeps it clickable when the head is smaller on screen.
class SignalItem : public QGraphicsEllipseItem {
public:
	/// A head in rect with aspect code 180, on no track line and at no position.
	SignalItem(const QRectF& rect, QGraphicsItem* parent = nullptr);
	~SignalItem() override;

	/// Sets the aspect code of the head, which gives the colour of its lamp through classifySignalAspect().
	void setAspectCode(int code);
	/// The aspect code of the head.
	int aspectCode() const;
	/// A failed signal is a red lamp with a cross, whatever its code is.
	void setFailed(bool failed);
	/// The signal has failed.
	bool failed() const { return m_failed; }
	/// Sets the direction that the head is for.
	void setReversedDirection(bool reversed);
	/// Sets the aspect codes and directions of the heads grouped with this one. paint() does not draw them;
	/// the window clears the list.
	void setGroupedSignals(const QVector<QPair<int, bool>>& aspects);
	/// The number of heads grouped with this one.
	int groupedSignalCount() const { return static_cast<int>(m_groupedSignals.size()); }
	/// Sets the text that names the head in its tooltip, the inspector and Find.
	void setInspectionIdentity(const QString& identity) { m_inspectionIdentity = identity; }
	/// That text, or without one the track line and the position in kilometres: "Track B0 at 0.23 km". The track is
	/// named by sceneTrackId, or by its number without one.
	QString inspectionIdentity() const;
	/// The head with a margin of 2 scene units.
	QRectF boundingRect() const override;

	/// Draws the lamp and, when the head is at least 6 device pixels wide, the mark of a failed, Stop, Caution or
	/// unavailable head; Proceed has no mark.
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

	/// The index of the track line of the signal in the run; -1 in the preview.
	int trackID;
	/// The id in the scene of that track line; empty without one.
	std::string sceneTrackId;

	/// The position of the signal along its track line in kilometres; -1 until it is set.
	double X;

	/// The id of the block section ahead of the head in its direction; empty without one.
	std::string sectionAheadId;
	/// The id of the block section behind the head; empty without one.
	std::string sectionBehindId;
	/// The label of the section ahead, as sceneSectionLabels gives it; empty without one.
	std::string sectionAheadLabel;
	/// The label of the section behind; empty without one.
	std::string sectionBehindLabel;
	/// The length of the section ahead as the run gives it, a difference of positions in kilometres; 0 without one.
	double sectionAheadLength;
	/// The length of the section behind, as for sectionAheadLength; 0 without one.
	double sectionBehindLength;
	/// The index of the track line of the section ahead; -1 without one.
	int sectionAheadTrackId;
	/// The index of the track line of the section behind; -1 without one.
	int sectionBehindTrackId;

	/// True for the head of the reversed direction. The two heads of a signal have the same X.
	bool reversedDirection;

	enum { Type = UserType + 6 /**< The item type for qgraphicsitem_cast. */ };
	/// Returns Type.
	int type() const override {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}

private:
	int m_aspectCode;
	bool m_failed = false;
	QColor m_lampColor;
	QVector<QPair<int, bool>> m_groupedSignals;
	QString m_inspectionIdentity;
};

/// The state of a signal head in tooltips, the inspector and the scripted runs: "Failed", "Stop", "Caution",
/// "Proceed" or "Unavailable".
QString signalAspectLabel(const SignalItem* signal);

#endif // SIGNALITEM_H
