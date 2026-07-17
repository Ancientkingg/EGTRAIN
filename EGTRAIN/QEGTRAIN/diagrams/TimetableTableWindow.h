#ifndef TIMETABLETABLEWINDOW_H
#define TIMETABLETABLEWINDOW_H

#include "diagrams/RunResults.h"

#include <QDialog>
#include <QPointer>
#include <QStringList>

#include <functional>
#include <vector>

class QTableWidget;
class TrainFilterButton;

// Planned versus simulated timetable as a filterable, sortable table with the
// same train dropdown and export pair the chart windows use.
class TimetableTableWindow : public QDialog {
	Q_OBJECT
public:
	TimetableTableWindow(std::vector<TimetableResultRow> rows,
						 long long startOffsetSeconds,
						 std::function<std::string(const QStringList&)> csvProvider,
						 QWidget* parent = nullptr);

private slots:
	void applyTrainVisibility();
	void exportCsv();
	void exportPng();

private:
	void fillTable();

	std::vector<TimetableResultRow> m_rows;
	long long m_startOffset = 0;
	std::function<std::string(const QStringList&)> m_csvProvider;
	QPointer<QTableWidget> m_table;
	QPointer<TrainFilterButton> m_trainsButton;
};

#endif // TIMETABLETABLEWINDOW_H
