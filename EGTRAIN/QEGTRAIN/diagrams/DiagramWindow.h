#ifndef DIAGRAMWINDOW_H
#define DIAGRAMWINDOW_H

#include <QDialog>
#include <QHash>
#include <QPen>
#include <QPointer>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtCharts/QAbstractSeries>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>

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
class DiagramWindow : public QDialog {
	Q_OBJECT
public:
	explicit DiagramWindow(const QString& title, QWidget* parent = nullptr);
	void setChart(QChart* chart);                                  // takes ownership
	void setTimeAxisX(bool on, long long startOffsetSeconds = 0);  // label X axis and hover as HH:MM:SS

	// Supply raw source data for CSV export. The provider receives the ids of the
	// trains currently visible so it can export only what the user is looking at.
	// An empty return means there is nothing to export. Enables the CSV button.
	void setCsvProvider(std::function<std::string(const QStringList& visibleTrainIds)> provider,
						const QString& suggestedFileName);

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
	bool m_timeAxis = false;
	bool m_timeAxisApplied = false;
	long long m_startOffset = 0;
	QVector<SeriesGroup> m_groups;
	QHash<QAbstractSeries*, QPen> m_basePens;
	QString m_hoverName;
	QString m_pinnedTrainId;
	std::function<std::string(const QStringList&)> m_csvProvider;
	QString m_csvSuggestedName;
};

#endif // DIAGRAMWINDOW_H
