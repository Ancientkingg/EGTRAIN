#ifndef DELAYANALYSISWINDOW_H
#define DELAYANALYSISWINDOW_H

#include "diagrams/DelayAnalysis.h"
#include "diagrams/RunResults.h"
#include "telemetry/TelemetryOperation.h"

#include <QDialog>
#include <QString>

#include <optional>
#include <string>
#include <utility>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QTabWidget;
class QTableWidget;
class TrainFilterButton;

// The window of the delay analysis (diagrams/DelayAnalysis.h). It shows the delay of each call against the
// authored plan, a summary with counts and thresholds, and the additional delay against a baseline run.
// A value that does not exist is shown as "-" and is counted next to the figures, never shown as 0.

/// The words of the window and of delay_analysis.csv for a status, such as "Not reached".
QString delayStatusText(TimetableValueStatus status);
/// The words of the window and of delay_analysis.csv for a delay class, such as "On time".
QString delayClassText(DelayClass delayClass);
/// The words of the window and of delay_analysis.csv for a change against the baseline, such as "Later".
QString delayChangeText(DelayChange change);
/// Why a baseline cannot be used, in a sentence. The diagnostic names the occurrence for the occurrence kinds.
QString delayDiagnosticText(const DelayDiagnostic& diagnostic);

// The text of delay_analysis.csv for each tab. Times and delays are in seconds and a value that does not
// exist is an empty field. Empty when the tab has no row.

/// The CSV text of the Calls tab: one record for each event.
std::string delayCallsCsv(const std::vector<DelayEvent>& events);
/// The CSV text of the Summary tab: one station record for each station and one network record. The share columns
/// come in pairs, the count and the percentage, for each threshold of the summary.
std::string delaySummaryCsv(const DelaySummary& summary, DelayEventKind kind);
/// The CSV text of the Against baseline tab: an occurrence record for each occurrence (the final arrival, the input
/// delay, the evidence and the secondary delay), a call record for each call and one total record. Empty when the
/// comparison is not compatible.
std::string delayBaselineCsv(const DelayBaselineComparison& comparison);

/// The result window. Not modal. It holds the runs as values, so it can outlive them.
class DelayAnalysisWindow : public QDialog {
public:
	/// `baseline` is the baseline run when one is set. The Against baseline tab compares it with `scenario`.
	DelayAnalysisWindow(DelayRunSnapshot scenario, std::optional<DelayRunSnapshot> baseline, QWidget* parent = nullptr);
	/// Sets the function that gives the telemetry observation of an export; empty for none.
	void setTelemetryCapture(telemetry::CaptureOperation capture) { m_telemetryCapture = std::move(capture); }
	/// Shows the Against baseline tab. The window opens on the Calls tab.
	void showBaselineTab();
	/// The file name that the export offers for the visible tab: delay_comparison.csv for Against baseline,
	/// else delay_analysis.csv.
	QString suggestedFileName() const;
	/// Writes the visible tab, for the occurrences, stations and events that are selected, and the provenance sidecar.
	/// The Against baseline tab writes the provenance of both runs. False when the tab has no row or a write fails.
	bool exportCsvTo(const QString& path) const;

private:
	void refresh();
	void fillCalls();
	void fillSummary();
	void fillBaseline();
	void updateExport();
	std::string currentCsv() const;
	void exportCsv();

	DelayRunSnapshot m_scenario;
	std::optional<DelayRunSnapshot> m_baseline;
	std::vector<DelayEvent> m_events;
	DelayBaselineComparison m_comparison;
	int m_occurrenceCount = 0;
	int m_stationCount = 0;

	// What the tabs show for the selection.
	std::vector<DelayEvent> m_shownEvents;
	DelaySummary m_shownSummary;
	DelayEventKind m_shownKind = DelayEventKind::Arrival;
	DelayBaselineComparison m_shownBaseline;

	TrainFilterButton* m_occurrences = nullptr;
	TrainFilterButton* m_stations = nullptr;
	QComboBox* m_kind = nullptr;
	QTabWidget* m_tabs = nullptr;
	QTableWidget* m_callsTable = nullptr;
	QTableWidget* m_summaryTable = nullptr;
	QLabel* m_baselineNote = nullptr;
	QWidget* m_baselineResults = nullptr;
	QLabel* m_baselineTotals = nullptr;
	QTableWidget* m_baselineOccurrences = nullptr;
	QTableWidget* m_baselineCalls = nullptr;
	QTableWidget* m_baselineDiagnostics = nullptr;
	QPushButton* m_exportButton = nullptr;
	telemetry::CaptureOperation m_telemetryCapture;
};

#endif // DELAYANALYSISWINDOW_H
