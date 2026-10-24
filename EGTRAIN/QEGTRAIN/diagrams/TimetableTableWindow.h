#ifndef TIMETABLETABLEWINDOW_H
#define TIMETABLETABLEWINDOW_H

#include "diagrams/RunResults.h"

#include <QColor>
#include <QDialog>
#include <QHash>
#include <QImage>
#include "telemetry/TelemetryOperation.h"
#include <QPointer>
#include <QStringList>

#include <functional>
#include <vector>

class QTableWidget;
class ResultFrame;
class TrainFilterButton;

/// Planned versus simulated timetable as a filterable, sortable table in the frame of the chart windows: the table at
/// the left and a panel with the train filter and the exports at the right.
/// The CSV button is shown only when the constructor gets a CSV provider.
class TimetableTableWindow : public QDialog {
	Q_OBJECT
public:
	/// Shows `rows` with their times as clock times: second 0 is `startOffsetSeconds` after midnight. `csvProvider`
	/// gives the CSV for the visible train ids; empty for no CSV export.
	TimetableTableWindow(std::vector<TimetableResultRow> rows,
		long long startOffsetSeconds,
		std::function<std::string(const QStringList&)> csvProvider,
		QWidget* parent = nullptr);
	/// Writes `provenance` as a sidecar with every export and shows its case and scenario as the context.
	void setRunProvenance(RunProvenance provenance);
	/// Sets the window title to `heading` and shows `context` in the header in one line; an empty context hides it.
	void setPresentation(const QString& heading, const QString& context);
	/// The picture that the PNG export saves: every column of the table and the rows of the visible trains.
	QImage tableImage() const;
	/// Shows a swatch of the given colour next to each listed train in the train
	/// filter; trains without an entry, or with an invalid colour, have none. Resets
	/// the filter to all trains visible.
	void setTrainColors(const QHash<QString, QColor>& colors);

public:
	/// Sets the function that gives the telemetry observation of an export; empty for none.
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
	ResultFrame* m_frame = nullptr;
	QPointer<TrainFilterButton> m_trainsButton;
	RunProvenance m_runProvenance;
	bool m_hasRunProvenance = false;
};

#endif // TIMETABLETABLEWINDOW_H
