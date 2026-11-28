#pragma once

#include "diagrams/PassengerComparison.h"
#include "diagrams/RunResults.h"
#include "telemetry/TelemetryOperation.h"
#include "widgets/UserPrompts.h"

#include <QDialog>
#include <QString>

#include <utility>

class QPushButton;
class QTableWidget;
class ResultFrame;

/// Non-modal result window comparing the passengers of a completed run against a baseline run.
/// Shows a summary table (baseline, this run and difference) and the origin-destination pairs that changed most
/// (comparePassengersAgainstBaseline), or one line that says that no pair changed.
class PassengerComparisonWindow : public QDialog {
public:
	/// Opens a comparison of `scenario` against `baseline`. Holds both snapshots by value so the window outlives the runs.
	PassengerComparisonWindow(DelayRunSnapshot baseline, DelayRunSnapshot scenario, QWidget* parent = nullptr);

	/// Sets the function that gives the telemetry observation of an export; empty for none.
	void setTelemetryCapture(telemetry::CaptureOperation capture) { m_telemetryCapture = std::move(capture); }
	/// Sets the prompts of the export, which ask whether to replace a file and show what went wrong. Null restores the
	/// prompts of the window, which show every message.
	void setPrompts(UserPrompts* prompts) { m_prompts = prompts ? prompts : &m_ownPrompts; }
	/// The file name that the export offers: "passenger_comparison.csv".
	QString suggestedFileName() const { return QStringLiteral("passenger_comparison.csv"); }
	/// Writes the comparison CSV and the provenance sidecar for both runs to `path`. Returns true on success.
	bool exportCsvTo(const QString& path) const;
	/// The computed comparison shown in the window.
	const PassengerBaselineComparison& comparison() const { return m_comparison; }
	/// The width of the window at which the four columns of the summary fit beside the panel without a horizontal scroll
	/// bar. The window opens at this width, at least 960 px as the Delay analysis window, and at most its maximum width.
	int summaryFitWidth() const;

private:
	void fillSummaryTable();
	void fillOdTable();
	QString writeCsv(const QString& path, const std::string& content) const;
	void exportCsv();

	DelayRunSnapshot m_baseline;
	DelayRunSnapshot m_scenario;
	PassengerBaselineComparison m_comparison;
	ResultFrame* m_frame = nullptr; ///< The frame of the window, with the tables in its main area and the figures in its panel.
	QTableWidget* m_summaryTable = nullptr;
	QTableWidget* m_odTable = nullptr;
	QPushButton* m_exportButton = nullptr;
	telemetry::CaptureOperation m_telemetryCapture;
	UserPrompts m_ownPrompts;
	UserPrompts* m_prompts = &m_ownPrompts;
};
