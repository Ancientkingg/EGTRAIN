#ifndef DIAGRAMWINDOW_H
#define DIAGRAMWINDOW_H

#include <QDialog>
#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/DiagramKey.h"
#include "diagrams/PositionScale.h"
#include "telemetry/TelemetryOperation.h"
#include <QHash>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QImage>
#include <QPointer>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtCharts/QAbstractSeries>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QCategoryAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QXYSeries>

#include <functional>
#include <vector>

class QGraphicsObject;
class QKeyEvent;
class QLabel;
class QPainter;
class QPushButton;
class ResultFrame;
class TrainKeyList;

/// A station along the distance axis of a route chart: its position on that axis, the plain-text labels it can
/// have, from the longest to the shortest, and the number of calls that the chart shows there.
struct StationTick {
	double position = 0.0; ///< On the distance axis, in the unit of that axis
	QStringList spellings; ///< Plain-text labels, longest first
	int calls = 0;		   ///< Calls of the trains of the chart at the station
};

/// A column that an exported figure draws at the right of the chart, such as the trains and the key of the chart.
struct FigureColumn {
	/// In pixels of the window; 0 for no column.
	double width = 0;
	/// Paints the column in its rectangle, in pixels of the window.
	std::function<void(QPainter&, const QRectF&)> paint;
};

/// Reusable non-modal window that shows a chart in a ResultFrame: a header row with the context and the zoom, fit and
/// panel buttons over the chart, and a panel beside it with the sections Trains (the train filter and colour key in one
/// list, a TrainKeyList), Key (the kinds of mark in view, a DiagramKey), Selected and Export, and a readout of the value
/// under the pointer. The chart has hover and click identification and rubber-band zoom, and exports a PNG and,
/// optionally, a CSV.
///
/// The Key lists each kind of mark that a shown series draws, with the pen and brush of the series: the "keyKind" property
/// of a series names its kind, and an empty one leaves the series out; without it the "layer" property names the kind,
/// else the series is Planned or Simulated. A closed outline whose "inspectionFilled" property is set is a box, filled
/// with the brush of an area series of the same kind; another series is a line in the text colour, since its colour
/// belongs to its train, with a dot when it shows its points, and upright when its "keyUpright" property is true, as for a
/// dwell. The "keyMarker" property of a series that shows its points names a kind of its own for its points, such as
/// "Stop". The chart draws the points of a planned series as open circles and those of another series as dots, and the
/// Key draws them alike: a planned line with an open circle, a kind of points as a dot while a series that is not planned
/// has them. Simulated is left out while it is the only kind of line. The section strip adds Station section, Switch
/// section and Block boundary. The Key follows the trains that are shown. A box takes the pen of the "keyPen" property
/// of its outline when the outline has one, so that it looks alike for every train. A chart of rolling stock input
/// lists its series by name, in their own colours.
///
/// Series are grouped by the dynamic property "trainId" when the caller sets it
/// (the same train can own several series, such as planned and simulated lines);
/// series without that property are grouped by their own name.
/// Optional inspection metadata: "inspectionPoints" is a QStringList with one
/// plain-text context entry per appended point (including empty entries).
/// "inspectionInterval" is plain-text context shared by a line segment;
/// "inspectionFilled" explicitly opts a closed QLineSeries into interior hits, and
/// "inspectionTitle" is the first line of such a box, such as "IC1723-2 on 12-B0".
/// Inside a box a click selects a line only within 4 px of it, and otherwise the box.
/// A line hit always reports its nearest actual plotted sample, not an
/// interpolated value at the cursor.
///
/// One colour rule paints the trains, by the trains that the Trains list shows (trainColourPlan): a colour of the palette
/// for each train while six trains or fewer are shown, else one for each service while those trains run in six services
/// or fewer (setTrainServices), else grey with the selected train in the first colour of the palette. The colour of a
/// service in the case wins over the palette (setTrainColors). Showing, hiding and selecting trains apply the rule again
/// and keep which trains are checked. A series takes the colour of its train in its pen, and an area series also in its
/// brush, with the alpha of the brush kept; widths and dash patterns stay as the chart builder set them. A series whose
/// "ownColour" property is true keeps the colours of its builder, as do the series of a chart of rolling stock input.
/// The line sample of a train in the Trains list has its colour, and that of a hidden train is light grey. The selected
/// train is drawn over the other trains.
///
/// A chart without series shows the text of setEmptyText in the middle of its plot, and the panel leaves out the
/// sections that have nothing to show, Trains included.
class DiagramWindow : public QDialog {
	Q_OBJECT
public:
	/// An empty window with the given title.
	explicit DiagramWindow(const QString& title, QWidget* parent = nullptr);
	/// Shows the chart and takes ownership of it. The chart loses its title, since the window title and the exported
	/// figure name the chart. Numeric axes that keep the ticks Qt gives them get ticks on round values, and their range
	/// widens to the first round tick past each end; a range that starts at zero keeps that start. An axis whose
	/// "exactRange" property is true keeps its range and still gets round ticks. A series is planned when its "planned"
	/// property is true or its name holds "planned"; a planned series is drawn dashed. A line narrower than 2 px is drawn
	/// 2 px wide, and the outline of a box ("inspectionFilled") keeps its width. A new chart ends the selection.
	void setChart(QChart* chart);
	/// Sets the window title to `heading`, shows `context` in one line in the header, elided at the right, and `warning`
	/// above the chart with the look of a warning of the table windows, which the exported figure keeps; an empty text
	/// hides its label. The tooltip of the context has the whole context, with `contextToolTip` on the lines below it when
	/// given.
	void setPresentation(const QString& heading, const QString& context,
		const QString& warning = QString(), const QString& contextToolTip = QString());
	/// The line that the plot shows while the chart has no series, such as "No arrival delays at stations of route8";
	/// empty for none. Set before or after setChart.
	void setEmptyText(const QString& text);
	/// The file name that the PNG export suggests, such as "delays.png", in the place of the name of the CSV.
	void setFigureFileName(const QString& fileName);
	/// True for a chart of rolling stock input, which has no trains: hides the train filter and the selection, and the
	/// curves of the next chart keep their colours.
	void setRollingStockSubject(bool on);
	/// The colour that the case gives the service of each train, by its group id (trainColorsForRun), which wins over the
	/// palette of the colour rule. Set before or after setChart. Keeps which trains are shown and which is selected.
	void setTrainColors(const QHash<QString, QColor>& colors);
	/// The service of each train, by its group id, which groups the Trains list by service when it has more than six
	/// trains in six services or fewer and gives the trains their colours by service when more than six are shown. A
	/// train without a service is a service of its own. Keeps which trains are shown.
	void setTrainServices(const QHash<QString, QString>& services);
	/// The train that each copied train copies, by group id, such as the first train of a capacity cycle for its copy one
	/// period later. A copy takes the colour of its train (trainColourPlan). Keeps which trains are shown.
	void setTrainCopies(const QHash<QString, QString>& copyOf);
	/// Labels the Y axis as clock time, titled "Time (hh:mm)": the axis value in seconds plus `startOffsetSeconds`, the
	/// clock time of second 0 in seconds since midnight, with ticks on round clock times and one at second 0 while it is
	/// in range. Ticks on whole minutes read HH:MM while the ticks are at least a minute apart; a tick off a whole minute,
	/// such as second 0 at 07:10:40, and every tick of a shorter step read HH:MM:SS, so no two ticks read the same.
	/// False restores numeric labels. Set before or after setChart. Bounds and reversal come from the caller, without
	/// the round widening of setChart; reset restores those bounds. The numeric axis is hidden while the clock axis takes
	/// its place, so that the chart draws one axis on that side. Tooltips and the readout keep HH:MM:SS.
	void setTimeAxisY(bool on, long long startOffsetSeconds = 0);
	/// As setTimeAxisY, for the X axis.
	void setTimeAxisX(bool on, long long startOffsetSeconds = 0);
	/// Labels the bottom axis, a route distance in km, as the geo X of `scale`, titled "Position (km)": labels on round
	/// geo X values at their route distance, which count down where the route runs against the scene coordinate, and a
	/// break mark on the bottom edge of the plot at each jump of the scale, whose hover gives both geo X. The plot keeps
	/// the route distance. The labels follow every zoom, pan, fit and resize, and lie at least 60 px apart. The readout and
	/// the tooltip of a sample give the geo X. Set after setChart, not with setTimeAxisX; an empty scale restores the
	/// numeric axis. The numeric axis is hidden while the position axis takes its place, as with setTimeAxisY.
	void setPositionAxis(PositionScale scale);

	/// Supply raw source data for CSV export. The provider receives the ids of the
	/// trains currently visible so it can export only what the user is looking at.
	/// `trainIds` adds to the train filter the trains that have CSV rows but no plotted series.
	/// An empty return means there is nothing to export. The CSV button is shown only while a provider is set. A save
	/// names the file in the status bar of the main window that is the parent of this window. The PNG export suggests
	/// the name of setFigureFileName, else `suggestedFileName` with the suffix png, or diagram.png without a name. After
	/// an export the window is raised and active again, saved or cancelled (KeepInFront).
	void setCsvProvider(std::function<std::string(const QStringList& visibleTrainIds)> provider,
		const QString& suggestedFileName, const QStringList& trainIds = {});
	/// Draws the block sections of the reference route over the chart, with positions in km. Set after setChart. A
	/// chart-owned physical layer, not train series, with its entries in the Key. Chart replacement clears it. The strip
	/// above the plot names each section by its short id (shortBlockId), centred in its cell, in one row
	/// when every id fits at the full view of the chart and otherwise in two, where an id that collides in the first row
	/// tries the second. When the ids in route order leave one out, those of critical and conflict sections, the first and
	/// last section, then switch and station sections are placed first. A switch section is hatched, and `marks`, one per
	/// section or empty, add a dark top bar for a station, an amber bar for a critical block and a red one for a conflict.
	/// The hover of a cell or a boundary gives the id, the label from `labels`, the kind, "Section N of M" with M
	/// `routeSectionCount` (the number of sections when 0), the geo extent and the route distance.
	void setBlockSections(std::vector<BlockSectionExtent> sections, const QString& referenceRouteId,
		const SceneSectionLabels* labels = nullptr, std::vector<BlockSectionMarks> marks = {}, int routeSectionCount = 0);
	/// Set after setChart. `axis` is a category axis of the chart that runs along the distance axis and has its own
	/// range. The window gives it the stations that are in its range and whose labels fit above the plot side by
	/// side, each with the longest spelling that fits, so the chart never has to shorten a label. When not all of them
	/// fit, the stations with the most calls are named first. A resize or a zoom chooses again. The labels are plain
	/// text; the chart reads them as rich text, so they are escaped. When the distance axis has the dynamic property
	/// "readoutStation", the readout names by its shortest spelling the station at the nearest position where a shown
	/// series has a sample, as in "2.5 min at Treviglio", and gives only the value of the left axis when no shown series
	/// has one.
	void setStationAxis(QCategoryAxis* axis, std::vector<StationTick> stations);
	/// The same provider/filter snapshot that the CSV export writes, without opening a file dialog.
	std::string csvContent() const;
	/// A figure at two image pixels per pixel of the window: the window title as its heading, the context below it, then
	/// the chart as the chart view shows it, without the rubber band, the tooltip, the readout or the outline of a
	/// selected blocking time, and `column` at the right of the chart.
	QImage figureImage(const FigureColumn& column = {}) const;
	/// The figure that the PNG export saves and Copy puts on the clipboard: figureImage with the Trains and Key of the
	/// panel in a column at the right, the shown trains with their line samples and without check boxes.
	QImage exportedFigure() const;
	/// Sets the function that writes an export and its provenance sidecar; `artifactKind` is "png" or "csv". Without
	/// it the export is written alone.
	void setProvenanceWriter(std::function<bool(const QString& artifactPath,
			const char* artifactKind, const std::string& artifactBytes)>
			writer);

signals:
	void trainSelected(const QString& trainId); ///< A click on a series selected this train

protected:
	/// Hover, click selection, the readout, wheel and pinch zoom, and wheel pan on the chart view. The readout fills when
	/// the pointer comes into the chart and follows the zoom of a dragged rectangle. A new size of the chart, as when the
	/// panel shows or hides or the window resizes, clears the tooltip and the readout.
	bool eventFilter(QObject* obj, QEvent* ev) override;
	/// Escape ends a selection, and closes the window only while nothing is selected.
	void keyPressEvent(QKeyEvent* event) override;

public:
	/// Sets the function that gives the telemetry observation of an export; empty for none.
	void setTelemetryCapture(telemetry::CaptureOperation capture) { m_telemetryCapture = std::move(capture); }

private slots:
	void exportPng();
	void exportCsv();
	void copyFigure();
	void resetZoom();
	void applyTrainVisibility();
	void clearPin();

private:
	telemetry::CaptureOperation m_telemetryCapture;
	struct SeriesGroup {
		QString trainId;
		QVector<QAbstractSeries*> members;
	};
	// What the chart shows at a point of the chart view: the lines of its tooltip and the series of a sample.
	struct ChartHit {
		QXYSeries* series = nullptr; // the series of the sample, or null
		bool inside = false;		 // the point lies inside the filled outline `series`, not on a line
		QStringList lines;			 // one fact per line; empty where the chart shows nothing
	};

	void applyChartStyle(QChart* chart);
	void applyTimeAxis();
	void applyPositionAxis();
	void schedulePositionLayout();
	void layoutPositionAxis();
	void reserveStripRoom();
	void scheduleStationLayout();
	void layoutStationAxis();
	QString elidedAxisTextAt(const QPointF& chartPosition) const;
	ChartHit hitAt(const QPoint& viewportPosition) const;
	ChartHit inspectAt(const QPoint& viewportPosition);
	void clearTooltip();
	void showTooltip(const QPoint& viewportPosition);
	void navigate(double factor, const QPointF& chartPosition, const QPointF& pan = {});
	void applyColourPlan();
	void rebuildFilterGroups();
	void refreshTrainList();
	void select(const ChartHit& hit);
	void raiseTrain(const QString& trainId);
	void refreshTrainsSection();
	void refreshEmptyNote();
	void refreshSelected();
	void refreshEmphasis();
	QStringList visibleTrainIds() const;
	QString groupIdForSeries(QAbstractSeries* series) const;
	QStringList sampleLines(const QPointF& value, const QString& seriesName) const;
	void updateCursorReadout(const QPointF& chartPosition);
	QVector<DiagramKeyEntry> keyEntries() const;
	void refreshKey();
	FigureColumn panelColumn() const;

	QPointer<QChartView> m_view;
	QPointer<ResultFrame> m_frame;
	QPointer<QWidget> m_trainsSection;
	QPointer<QWidget> m_keySection;
	QPointer<QWidget> m_selectedSection;
	QPointer<QLabel> m_readout;	 // the value under the pointer, at the bottom of the panel
	QPointer<QLabel> m_pinTitle; // the first line of the selected item, in bold
	QPointer<QLabel> m_pinLabel; // the other lines of the selected item
	QPointer<TrainKeyList> m_trainList;
	QPointer<DiagramKey> m_key;
	QPointer<QPushButton> m_csvButton;
	QPointer<QPushButton> m_clearPinButton;
	QPointer<QWidget> m_warningArea; // the margins around the warning label
	QPointer<QLabel> m_warningLabel;
	QPointer<QLabel> m_contextLabel;
	bool m_rollingStockSubject = false;
	bool m_timeAxis = false;
	Qt::Orientation m_timeOrientation = Qt::Horizontal;
	QPointer<QLabel> m_tooltip;
	QPointer<QGraphicsObject> m_sectionOverlay;
	QPointer<QGraphicsObject> m_selectionOutline; // around the selected blocking time
	QPointer<QGraphicsObject> m_emptyNote;		  // the text of a chart without series
	QString m_emptyText;
	QHash<QAbstractAxis*, QPair<double, double>> m_fullBounds;
	QHash<QAbstractAxis*, QPair<double, double>> m_builderBounds; // ranges of the widened axes as the chart builder set them
	long long m_startOffset = 0;
	QVector<SeriesGroup> m_groups;
	QHash<QAbstractSeries*, QPen> m_basePens;
	QHash<QAbstractSeries*, QBrush> m_baseBrushes;
	QHash<QString, QColor> m_serviceColours; // the colour of the service of a train in the case, by group id
	QHash<QString, QColor> m_planColours;	 // the colour that the colour rule gave each train last, by group id
	QHash<QString, QString> m_trainServices;
	QHash<QString, QString> m_trainCopies; // the train that a copied train copies, by group id
	QPoint m_pressPosition;
	QPointer<QCategoryAxis> m_stationAxis;
	std::vector<StationTick> m_stations;
	bool m_stationLayoutPending = false;
	QPointer<QAbstractAxis> m_clockAxis;
	QPointer<QValueAxis> m_numericAxis; // detached while its clock axis is displayed
	PositionScale m_positionScale;
	QPointer<QCategoryAxis> m_positionAxis;
	QPointer<QValueAxis> m_positionNumericAxis; // detached while the position axis is displayed
	bool m_positionLayoutPending = false;
	QString m_pinnedTrainId;		   // the selected train, or the train of the selected blocking time
	QPointer<QXYSeries> m_selectedBox; // the outline of the selected blocking time; null while a train is selected
	QStringList m_selectedLines;	   // what Selected shows: the tooltip lines of the click
	std::function<std::string(const QStringList&)> m_csvProvider;
	QString m_csvSuggestedName;
	QString m_figureFileName;
	QStringList m_csvTrainIds;
	std::function<bool(const QString&, const char*, const std::string&)> m_provenanceWriter;
};

#endif // DIAGRAMWINDOW_H
