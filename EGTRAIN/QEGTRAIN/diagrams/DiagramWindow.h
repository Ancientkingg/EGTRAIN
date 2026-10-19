#ifndef DIAGRAMWINDOW_H
#define DIAGRAMWINDOW_H

#include <QDialog>
#include "diagrams/BlockingTimeDiagram.h"
#include "telemetry/TelemetryOperation.h"
#include <QHash>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QPointer>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtCharts/QAbstractSeries>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QCategoryAxis>
#include <QtCharts/QValueAxis>

#include <functional>
#include <vector>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
QT_CHARTS_USE_NAMESPACE
#endif

class QGraphicsObject;
class QLabel;
class QPushButton;
class TrainFilterButton;

/// A station along the distance axis of a route chart: its position on that axis and the plain-text labels it can
/// have, from the longest to the shortest.
struct StationTick {
	double position = 0.0; ///< On the distance axis, in the unit of that axis
	QStringList spellings; ///< Plain-text labels, longest first
};

/// Reusable non-modal window that shows a chart with a train visibility
/// dropdown, hover and click identification, rubber-band zoom with reset,
/// PNG export, and optional CSV export.
///
/// Series are grouped by the dynamic property "trainId" when the caller sets it
/// (the same train can own several series, such as planned and simulated lines);
/// series without that property are grouped by their own name.
/// Optional inspection metadata: "inspectionPoints" is a QStringList with one
/// plain-text context entry per appended point (including empty entries).
/// "inspectionInterval" is plain-text context shared by a line segment;
/// "inspectionFilled" explicitly opts a closed QLineSeries into interior hits.
/// A line hit always reports its nearest actual plotted sample, not an
/// interpolated value at the cursor.
class DiagramWindow : public QDialog {
	Q_OBJECT
public:
	/// An empty window with the given title.
	explicit DiagramWindow(const QString& title, QWidget* parent = nullptr);
	/// Shows the chart and takes ownership of it. The navigation help is built from the series the chart has when it
	/// is set.
	void setChart(QChart* chart);
	/// Sets the window title to `heading` and shows `context` and `warning` in labels of their own; an empty text hides
	/// its label.
	void setPresentation(const QString& heading, const QString& context,
		const QString& warning = QString());
	/// True for a chart of rolling stock input, which has no trains: hides the train filter and the selection.
	void setRollingStockSubject(bool on);
	/// Paints the series of each listed train in its colour: the pen colour of a
	/// line series, the brush colour (alpha kept) and pen colour of an area series.
	/// Width and dash pattern stay as the chart builder set them. Train ids are the
	/// group ids (the "trainId" property, or the series name); a train that is not
	/// listed, or has an invalid colour, keeps its chart colours. The colours become
	/// the base look of the series, so selecting and clearing a train keeps them,
	/// and the train filter swatch shows them. Set before or after setChart. A later
	/// call does not undo colours an earlier call applied and resets the train
	/// filter to all trains visible.
	void setTrainColors(const QHash<QString, QColor>& colors);
	/// Shows the colour of each listed train in the train filter only and leaves
	/// the series as the chart builder painted them, for a chart whose series
	/// colours have a meaning of their own.
	void setTrainListColors(const QHash<QString, QColor>& colors);
	/// Labels the Y axis as clock time HH:MM:SS: the axis value in seconds plus `startOffsetSeconds`, the clock time
	/// of second 0 in seconds since midnight. False restores numeric labels. Set before or after setChart. Bounds and
	/// reversal come from the caller; reset restores the bounds at setChart.
	void setTimeAxisY(bool on, long long startOffsetSeconds = 0);
	/// As setTimeAxisY, for the X axis.
	void setTimeAxisX(bool on, long long startOffsetSeconds = 0);

	/// Supply raw source data for CSV export. The provider receives the ids of the
	/// trains currently visible so it can export only what the user is looking at.
	/// `trainIds` adds to the train filter the trains that have CSV rows but no plotted series.
	/// An empty return means there is nothing to export. The CSV button is shown only while a provider is set.
	void setCsvProvider(std::function<std::string(const QStringList& visibleTrainIds)> provider,
		const QString& suggestedFileName, const QStringList& trainIds = {});
	/// Draws the block sections of the reference route over the chart, with positions in km. Set after setChart. A
	/// chart-owned physical layer, not train series. Chart replacement clears it.
	void setBlockSections(std::vector<BlockSectionExtent> sections, const QString& referenceRouteId);
	/// Set after setChart. `axis` is a category axis of the chart that runs along the distance axis and has its own
	/// range. The window gives it the stations that are in its range and whose labels fit above the plot side by
	/// side, each with the longest spelling that fits, so the chart never has to shorten a label. A resize or a
	/// zoom chooses again. The labels are plain text; the chart reads them as rich text, so they are escaped.
	void setStationAxis(QCategoryAxis* axis, std::vector<StationTick> stations);
	/// The same provider/filter snapshot that Export CSV writes, without opening a file dialog.
	std::string csvContent() const;
	/// Sets the function that writes an export and its provenance sidecar; `artifactKind` is "png" or "csv". Without
	/// it the export is written alone.
	void setProvenanceWriter(std::function<bool(const QString& artifactPath,
			const char* artifactKind, const std::string& artifactBytes)>
			writer);

signals:
	void trainSelected(const QString& trainId); ///< A click on a series selected this train

protected:
	/// Hover, click selection, wheel and pinch zoom, and wheel pan on the chart view.
	bool eventFilter(QObject* obj, QEvent* ev) override;

public:
	/// Sets the function that gives the telemetry observation of an export; empty for none.
	void setTelemetryCapture(telemetry::CaptureOperation capture) { m_telemetryCapture = std::move(capture); }

private slots:
	void exportPng();
	void exportCsv();
	void resetZoom();
	void applyTrainVisibility();
	void clearPin();

private:
	telemetry::CaptureOperation m_telemetryCapture;
	struct SeriesGroup {
		QString trainId;
		QVector<QAbstractSeries*> members;
	};

	void applyChartStyle(QChart* chart);
	void applyTimeAxis();
	void scheduleStationLayout();
	void layoutStationAxis();
	QString elidedAxisTextAt(const QPointF& chartPosition) const;
	QAbstractSeries* inspectAt(const QPoint& viewportPosition);
	void clearTooltip();
	void showTooltip(const QPoint& viewportPosition);
	void navigate(double factor, const QPointF& chartPosition, const QPointF& pan = {});
	void applyTrainColors(const QHash<QString, QColor>& colors, bool paintSeries);
	void rebuildFilterGroups();
	void pinTrain(const QString& trainId);
	void refreshEmphasis();
	QStringList visibleTrainIds() const;
	QString groupIdForSeries(QAbstractSeries* series) const;
	void updateReadout(const QPointF& value, const QString& seriesName);
	void refreshNavigationHelp();

	QPointer<QChartView> m_view;
	QPointer<QLabel> m_readout;
	QPointer<QLabel> m_pinLabel;
	QPointer<TrainFilterButton> m_trainsButton;
	QPointer<QPushButton> m_csvButton;
	QPointer<QPushButton> m_clearPinButton;
	QPointer<QLabel> m_warningLabel;
	QPointer<QLabel> m_contextLabel;
	bool m_rollingStockSubject = false;
	bool m_timeAxis = false;
	Qt::Orientation m_timeOrientation = Qt::Horizontal;
	QPointer<QLabel> m_tooltip;
	QPointer<QGraphicsObject> m_sectionOverlay;
	QHash<QAbstractAxis*, QPair<double, double>> m_fullBounds;
	long long m_startOffset = 0;
	QVector<SeriesGroup> m_groups;
	QHash<QAbstractSeries*, QPen> m_basePens;
	QHash<QAbstractSeries*, QBrush> m_baseBrushes;
	QHash<QString, QColor> m_trainColors;
	bool m_paintTrainSeries = true;
	QPoint m_pressPosition;
	QPointer<QCategoryAxis> m_stationAxis;
	std::vector<StationTick> m_stations;
	bool m_stationLayoutPending = false;
	QPointer<QAbstractAxis> m_clockAxis;
	QPointer<QValueAxis> m_numericAxis; // detached while its clock axis is displayed
	QString m_pinnedTrainId;
	std::function<std::string(const QStringList&)> m_csvProvider;
	QString m_csvSuggestedName;
	QStringList m_csvTrainIds;
	std::function<bool(const QString&, const char*, const std::string&)> m_provenanceWriter;
};

#endif // DIAGRAMWINDOW_H
