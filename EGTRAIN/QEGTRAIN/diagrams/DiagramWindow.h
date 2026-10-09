#ifndef DIAGRAMWINDOW_H
#define DIAGRAMWINDOW_H

#include <QDialog>
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
#include <QtCharts/QValueAxis>

#include <functional>

QT_CHARTS_USE_NAMESPACE

class QLabel;
class QPushButton;
class TrainFilterButton;

// Reusable non-modal window that shows a chart with a train visibility
// dropdown, hover and click identification, rubber-band zoom with reset,
// PNG export, and optional CSV export.
//
// Series are grouped by the dynamic property "trainId" when the caller sets it
// (the same train can own several series, such as planned and simulated lines);
// series without that property are grouped by their own name.
// Optional inspection metadata: "inspectionPoints" is a QStringList with one
// plain-text context entry per appended point (including empty entries).
// "inspectionInterval" is plain-text context shared by a line segment;
// "inspectionFilled" explicitly opts a closed QLineSeries into interior hits.
// A line hit always reports its nearest actual plotted sample, not an
// interpolated value at the cursor.
class DiagramWindow : public QDialog {
	Q_OBJECT
public:
	explicit DiagramWindow(const QString& title, QWidget* parent = nullptr);
	void setChart(QChart* chart); // takes ownership
	// Presentation only: concise warning separate from bounded subject/run context.
	void setPresentation(const QString& heading, const QString& context,
		const QString& warning = QString());
	void setRollingStockSubject(bool on);
	// Paints the series of each listed train in its colour: the pen colour of a
	// line series, the brush colour (alpha kept) and pen colour of an area series.
	// Width and dash pattern stay as the chart builder set them. Train ids are the
	// group ids (the "trainId" property, or the series name); a train that is not
	// listed, or has an invalid colour, keeps its chart colours. The colours become
	// the base look of the series, so selecting and clearing a train keeps them,
	// and the train filter swatch shows them. Set before or after setChart. A later
	// call does not undo colours an earlier call applied and resets the train
	// filter to all trains visible.
	void setTrainColors(const QHash<QString, QColor>& colors);
	// Shows the colour of each listed train in the train filter only and leaves
	// the series as the chart builder painted them, for a chart whose series
	// colours have a meaning of their own.
	void setTrainListColors(const QHash<QString, QColor>& colors);
	// Format one axis as HH:MM:SS with an offset; true selects X or Y, false
	// restores numeric formatting. Set before or after setChart. Bounds and
	// reversal come from the caller; reset restores the bounds at setChart.
	void setTimeAxisY(bool on, long long startOffsetSeconds = 0);
	void setTimeAxisX(bool on, long long startOffsetSeconds = 0);

	// Supply raw source data for CSV export. The provider receives the ids of the
	// trains currently visible so it can export only what the user is looking at.
	// An empty return means there is nothing to export. Enables the CSV button.
	void setCsvProvider(std::function<std::string(const QStringList& visibleTrainIds)> provider,
		const QString& suggestedFileName);
	void setProvenanceWriter(std::function<bool(const QString& artifactPath,
			const char* artifactKind, const std::string& artifactBytes)>
			writer);

signals:
	void trainSelected(const QString& trainId); // scene linkage on click

protected:
	bool eventFilter(QObject* obj, QEvent* ev) override; // track mouse for hover

public:
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
	QAbstractSeries* inspectAt(const QPoint& viewportPosition);
	void clearTooltip();
	void navigate(double factor, const QPointF& chartPosition, const QPointF& pan = {});
	void applyTrainColors(const QHash<QString, QColor>& colors, bool paintSeries);
	void rebuildFilterGroups();
	void pinTrain(const QString& trainId);
	void refreshEmphasis();
	QStringList visibleTrainIds() const;
	QString groupIdForSeries(QAbstractSeries* series) const;
	void updateReadout(const QPointF& value, const QString& seriesName);

	QPointer<QChartView> m_view;
	QPointer<QLabel> m_readout;
	QPointer<QLabel> m_pinLabel;
	QPointer<TrainFilterButton> m_trainsButton;
	QPointer<QPushButton> m_csvButton;
	QPointer<QPushButton> m_clearPinButton;
	QPointer<QLabel> m_warningLabel;
	QPointer<QLabel> m_contextLabel;
	bool m_timeAxis = false;
	Qt::Orientation m_timeOrientation = Qt::Horizontal;
	QPointer<QLabel> m_tooltip;
	QHash<QAbstractAxis*, QPair<double, double>> m_fullBounds;
	long long m_startOffset = 0;
	QVector<SeriesGroup> m_groups;
	QHash<QAbstractSeries*, QPen> m_basePens;
	QHash<QAbstractSeries*, QBrush> m_baseBrushes;
	QHash<QString, QColor> m_trainColors;
	bool m_paintTrainSeries = true;
	QPoint m_pressPosition;
	QPointer<QAbstractAxis> m_clockAxis;
	QPointer<QValueAxis> m_numericAxis; // detached while its clock axis is displayed
	QString m_pinnedTrainId;
	std::function<std::string(const QStringList&)> m_csvProvider;
	QString m_csvSuggestedName;
	std::function<bool(const QString&, const char*, const std::string&)> m_provenanceWriter;
};

#endif // DIAGRAMWINDOW_H
