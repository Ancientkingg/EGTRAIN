#ifndef AREACAPACITYWINDOW_H
#define AREACAPACITYWINDOW_H

#include "diagrams/AreaCapacity.h"
#include "diagrams/CompletedRunData.h"
#include "diagrams/RunResults.h"
#include "telemetry/TelemetryOperation.h"

#include <QDialog>
#include <QString>

#include <string>
#include <utility>
#include <vector>

class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;

// The window of the area capacity analysis (diagrams/AreaCapacity.h). It reports a rate per resource, the
// governing resource and the selected chain. It states no trains per hour: the figures are occupation rates
// of the trains that the person selected, not the capacity of the line.

/// The analysis area and the traffic that the person chose. The area is the block range of one route and,
/// optionally, the block range of a second one.
struct AreaCapacityScope {
	int routeIndex = -1;					 ///< Index into CompletedRunData::routes
	std::vector<std::string> blockIds;		 ///< Range of the first route; its first block is the reference of each train
	int secondRouteIndex = -1;				 ///< -1 for no second route
	std::vector<std::string> secondBlockIds; ///< The range of the second route, empty for none
	double periodSeconds = 3600.0;			 ///< Analysis period in seconds
	std::vector<std::string> occurrenceIds;	 ///< The selected trains in chain order
	std::string cycleEndOccurrenceId;		 ///< Empty for no cycle-closing train
};

/// The input of analyzeAreaCapacity for the scope: the selected trains in the order of the scope and every
/// other train of the run as other traffic. Occupations are passed as the run holds them, so that the
/// analysis reports the unusable ones.
AreaCapacityInput areaCapacityInputFrom(const CompletedRunData& data, const AreaCapacityScope& scope);
/// The route and block range of the scope by block ids, as the export names the area.
QString areaCapacityLabel(const CompletedRunData& data, const AreaCapacityScope& scope);
/// The route and block range of the scope by the labels of the blocks, as the window names the area.
QString areaCapacityText(const CompletedRunData& data, const AreaCapacityScope& scope);

/// The words of the window and of area_capacity.csv for a note: a short name of the kind, such as "Invalid period".
QString areaCapacityStatusName(AreaCapacityNoteKind kind);
/// The words of the window and of area_capacity.csv for a note: a sentence that names its trains and resource.
QString areaCapacityStatusText(const AreaCapacityNote& note);

/// The text of area_capacity.csv. Every record has the area and whether the traffic is complete. The record types are
/// summary (period, the cycle of the chain, the legacy envelope; the leader and follower columns hold the first and
/// the cycle-closing train of the chain), resource (one for each resource of the area), pair (one for each pair of
/// adjacent trains of the chain) and note (one for each note). A figure that is not available is an empty field.
/// Empty when the result has no resource row.
std::string areaCapacityCsv(const AreaCapacityResult& result, const QString& areaLabel);

/// The options dialog. It lists the trains that use the area in the order of their reference time and checks
/// them all. The run must outlive the dialog.
class AreaCapacityScopeDialog : public QDialog {
public:
	/// Builds the dialog for the routes and trains of `data`.
	explicit AreaCapacityScopeDialog(const CompletedRunData& data, QWidget* parent = nullptr);
	/// The scope that the widgets hold now.
	AreaCapacityScope scope() const;

private:
	void refillBlocks(QComboBox* routes, QComboBox* first, QComboBox* last);
	void refillOccurrences();
	void validate();

	const CompletedRunData& m_data;
	QComboBox* m_route = nullptr;
	QComboBox* m_firstBlock = nullptr;
	QComboBox* m_lastBlock = nullptr;
	QComboBox* m_secondRoute = nullptr;
	QComboBox* m_secondFirstBlock = nullptr;
	QComboBox* m_secondLastBlock = nullptr;
	QDoubleSpinBox* m_period = nullptr;
	QComboBox* m_cycleEnd = nullptr;
	QListWidget* m_occurrences = nullptr;
	QLabel* m_status = nullptr;
	QDialogButtonBox* m_buttons = nullptr;
};

/// The result window. Not modal. It holds the result and the run identity, not the run, so it can outlive the run.
class AreaCapacityWindow : public QDialog {
public:
	/// Shows `result` for the area named `areaLabel` in the export. `provenance` goes into the export sidecar. The window
	/// names the area `areaText`, or `areaLabel` when it is empty, and gives the resources their labels in `labels`.
	AreaCapacityWindow(AreaCapacityResult result, const QString& areaLabel, RunProvenance provenance,
		QWidget* parent = nullptr, const QString& areaText = QString(), const SceneSectionLabels& labels = SceneSectionLabels());
	/// Sets the function that gives the telemetry observation of an export; empty for none.
	void setTelemetryCapture(telemetry::CaptureOperation capture) { m_telemetryCapture = std::move(capture); }
	/// Writes area_capacity.csv and the provenance sidecar. False when the result has no resource row or a write fails.
	bool exportCsvTo(const QString& path) const;

private:
	void exportCsv();

	AreaCapacityResult m_result;
	QString m_areaLabel;
	RunProvenance m_provenance;
	telemetry::CaptureOperation m_telemetryCapture;
};

/// Asks for the scope, analyses it and opens the window, which deletes itself on close. Null when the person cancels.
AreaCapacityWindow* openAreaCapacityWindow(QWidget* parent, const CompletedRunData& data,
	telemetry::CaptureOperation capture = {});

#endif // AREACAPACITYWINDOW_H
