#ifndef DIAGRAMWINDOW_H
#define DIAGRAMWINDOW_H

#include <QDialog>
#include <QHash>
#include <QPen>
#include <QBrush>
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
	void setChart(QChart* chart);                                  // takes ownership
	// Presentation only: keeps technical qualifications outside the plot and title.
	void setPresentation(const QString& heading, const QString& context,
		const QString& technicalNotes = QString());
	void setRollingStockSubject(bool on);
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
		const char* artifactKind, const std::string& artifactBytes)> writer);

signals:
	void trainSelected(const QString& trainId);  // scene linkage on click

protected:
	bool eventFilter(QObject* obj, QEvent* ev) override;           // track mouse for hover

private slots:
	void exportPng();
	void exportCsv();
	void resetZoom();
	void applyTrainVisibility();
	void clearPin();

private:
	struct SeriesGroup {
		QString trainId;
		QVector<QAbstractSeries*> members;
	};

	void applyChartStyle(QChart* chart);
	void applyTimeAxis();
	QAbstractSeries* inspectAt(const QPoint& viewportPosition);
	void clearTooltip();
	void navigate(double factor, const QPointF& chartPosition, const QPointF& pan = {});
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
	QPointer<QLabel> m_contextLabel;
	QPointer<QWidget> m_detailsPanel;
	QPointer<QLabel> m_detailsLabel;
	QPointer<QPushButton> m_detailsButton;
	bool m_timeAxis = false;
	Qt::Orientation m_timeOrientation = Qt::Horizontal;
	QPointer<QLabel> m_tooltip;
	QHash<QAbstractAxis*, QPair<double, double>> m_fullBounds;
	long long m_startOffset = 0;
	QVector<SeriesGroup> m_groups;
	QHash<QAbstractSeries*, QPen> m_basePens;
	QHash<QAbstractSeries*, QBrush> m_baseBrushes;
	QPoint m_pressPosition;
	QPointer<QAbstractAxis> m_clockAxis;
	QPointer<QValueAxis> m_numericAxis; // detached while its clock axis is displayed
	QString m_pinnedTrainId;
	std::function<std::string(const QStringList&)> m_csvProvider;
	QString m_csvSuggestedName;
	std::function<bool(const QString&, const char*, const std::string&)> m_provenanceWriter;
};

#endif // DIAGRAMWINDOW_H
