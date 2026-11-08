#ifndef SERIESMETA_H
#define SERIESMETA_H

#include <QPen>
#include <QString>
#include <QStringList>

#include <optional>

QT_BEGIN_NAMESPACE
class QAbstractAxis;
class QAbstractSeries;
QT_END_NAMESPACE

/// What a chart builder tells DiagramWindow about a series, beyond what the series itself holds. A series that was not
/// described has the default of every field.
struct SeriesMeta {
	/// The train that the series belongs to, which groups the series of a train; empty groups the series by its own name.
	QString trainId;
	/// The series is planned, whatever its name: the window draws it dashed, with open circles at its points.
	bool planned = false;
	/// The kind that the Key lists the series under while `keyKind` is not set; empty makes it Planned or Simulated.
	QString layer;
	/// The kind that the Key lists the series under, in the place of `layer`; empty leaves the series out of the Key.
	std::optional<QString> keyKind;
	/// The pen of the sample of a box in the Key, so that it looks alike for every train; not set for the pen of the series.
	std::optional<QPen> keyPen;
	/// A kind of its own in the Key for the points of a series that shows them, such as "Stop"; empty for none.
	QString keyMarker;
	bool keyUpright = false; ///< The Key draws the line upright, as for a dwell.
	bool ownColour = false;	 ///< The series keeps the colours of its builder and does not take the colour of its train.
	/// Plain-text context for each appended point, including empty entries.
	QStringList inspectionPoints;
	QString inspectionTitle;	///< The first line of the hover of a box, such as "IC1723-2 on 12-B0"; empty for none.
	QString inspectionInterval; ///< Plain-text context shared by the line segments of the series; empty for none.
	/// A closed line series is a box: the window hits its interior, and it keeps the width of its outline.
	bool inspectionFilled = false;
};

/// What a chart builder tells DiagramWindow about an axis.
struct AxisMeta {
	bool exactRange = false;	 ///< setChart keeps the range of the numeric axis and still gives it round ticks.
	bool readoutStation = false; ///< The readout names the station nearest the pointer, on the distance axis of a route chart.
};

/// Attaches `meta` to `series`, in the place of what it had.
void describeSeries(QAbstractSeries* series, const SeriesMeta& meta);
/// What describeSeries attached to `series`, or the defaults when it was not described.
SeriesMeta seriesMeta(const QAbstractSeries* series);

/// Attaches `meta` to `axis`, in the place of what it had.
void describeAxis(QAbstractAxis* axis, const AxisMeta& meta);
/// What describeAxis attached to `axis`, or the defaults when it was not described.
AxisMeta axisMeta(const QAbstractAxis* axis);

#endif // SERIESMETA_H
