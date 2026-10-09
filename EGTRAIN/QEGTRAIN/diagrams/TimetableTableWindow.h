#ifndef TIMETABLETABLEWINDOW_H
#define TIMETABLETABLEWINDOW_H

#include "diagrams/RunResults.h"

#include <QColor>
#include <QDialog>
#include <QHash>
#include "telemetry/TelemetryOperation.h"
#include <QPointer>
#include <QStringList>

#include <functional>
#include <vector>

class QTableWidget;
class QLabel;
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
	void setRunProvenance(RunProvenance provenance);
	void setPresentation(const QString& heading, const QString& context);
	// Shows a swatch of the given colour next to each listed train in the train
	// filter; trains without an entry, or with an invalid colour, have none. Resets
	// the filter to all trains visible.
	void setTrainColors(const QHash<QString, QColor>& colors);

public:
	void setTelemetryCapture(telemetry::CaptureOperation capture) { m_telemetryCapture = std::move(capture); }

private slots:
	void applyTrainVisibility();
	void exportCsv();
	void exportPng();

private:
	telemetry::CaptureOperation m_telemetryCapture;
	void fillTable();
	void fillTrainFilter();

	std::vector<TimetableResultRow> m_rows;
	long long m_startOffset = 0;
	QStringList m_trainOrder;
	QHash<QString, QColor> m_trainColors;
	std::function<std::string(const QStringList&)> m_csvProvider;
	QPointer<QTableWidget> m_table;
	QPointer<QLabel> m_contextLabel;
	QPointer<TrainFilterButton> m_trainsButton;
	RunProvenance m_runProvenance;
	bool m_hasRunProvenance = false;
};

#endif // TIMETABLETABLEWINDOW_H
