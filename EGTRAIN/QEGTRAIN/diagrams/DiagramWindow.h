#ifndef DIAGRAMWINDOW_H
#define DIAGRAMWINDOW_H

#include <QDialog>
#include <QHash>
#include <QPen>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtCharts/QAbstractSeries>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>

#include <functional>

QT_CHARTS_USE_NAMESPACE

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

// Reusable non-modal window that shows a chart with a train filter panel,
// hover and click identification, PNG export, and optional CSV export.
//
// Series are grouped by the dynamic property "trainId" when the caller sets it
// (the same train can own several series, such as planned and simulated lines);
// series without that property are grouped by their own name.
class DiagramWindow : public QDialog {
	Q_OBJECT
public:
	explicit DiagramWindow(const QString& title, QWidget* parent = nullptr);
	void setChart(QChart* chart);                                  // takes ownership
	void setTimeAxisX(bool on, long long startOffsetSeconds = 0);  // format X hover as HH:MM:SS

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
	void applyGroupFilter();       // apply the search box to the visible group list
	void selectAllGroups();
	void selectNoGroups();
	void clearPin();

private:
	struct SeriesGroup {
		QString trainId;
		QVector<QAbstractSeries*> members;
		QListWidgetItem* item = nullptr;
	};

	void rebuildFilterPanel();
	void setGroupChecked(const QString& trainId, bool checked);
	void onGroupItemChanged(QListWidgetItem* item);
	void pinTrain(const QString& trainId);
	void refreshEmphasis();
	QStringList visibleTrainIds() const;
	QString groupIdForSeries(QAbstractSeries* series) const;

	QPointer<QChartView> m_view;
	QPointer<QLabel> m_readout;
	QPointer<QLabel> m_pinLabel;
	QPointer<QLineEdit> m_search;
	QPointer<QListWidget> m_groupList;
	QPointer<QPushButton> m_csvButton;
	bool m_timeAxis = false;
	long long m_startOffset = 0;
	QVector<SeriesGroup> m_groups;
	QHash<QAbstractSeries*, QPen> m_basePens;
	QString m_hoverName;
	QString m_pinnedTrainId;
	std::function<std::string(const QStringList&)> m_csvProvider;
	QString m_csvSuggestedName;
};

#endif // DIAGRAMWINDOW_H
