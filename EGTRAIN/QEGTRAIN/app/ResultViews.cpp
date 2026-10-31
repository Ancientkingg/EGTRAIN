// RunController.h brings in SceneModel.h in the way that the member named signals needs, before the other Qt headers do.
#include "app/RunController.h"
#include "app/ResultViews.h"
#include "app/MainWindowSupport.h"
#include "app/MainWindowTestHooks.h"
#include "diagrams/AreaCapacityWindow.h"
#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/BlockingTimeScope.h"
#include "diagrams/CapacityAnalysis.h"
#include "diagrams/CapacityScope.h"
#include "diagrams/CapacityWindow.h"
#include "diagrams/DelayAnalysisWindow.h"
#include "diagrams/DiagramWindow.h"
#include "diagrams/PositionScale.h"
#include "diagrams/ResultFrame.h"
#include "diagrams/RouteDiagramCoordinates.h"
#include "diagrams/RouteCharts.h"
#include "diagrams/RouteReferenceChoice.h"
#include "diagrams/RunExports.h"
#include "diagrams/RunResults.h"
#include "diagrams/TimetableTableWindow.h"
#include "diagrams/TrainColors.h"
#include "util/TimeFormat.h"
#include "widgets/UserPrompts.h"
#include <QDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

void attachRunProvenance(DiagramWindow* window, RunProvenance provenance) {
	window->setProvenanceWriter(runProvenanceWriter(std::move(provenance)));
}

// The window title of each chart of the trajectories, in the order of TrajectoryChart.
constexpr const char* kTrajectoryTitles[] = {
	ResultViewName::kSpeedDistance,
	ResultViewName::kSpeedTime,
	ResultViewName::kTimeDistance,
	ResultViewName::kTractiveEffortDistance,
};

} // namespace

void saveCsvInteractive(QWidget* parent, const QString& suggestedName,
	const telemetry::OperationObservation& operation, const std::function<std::string()>& provider,
	const std::function<bool(const QString&, const std::string&)>& artifactWriter) {
	const std::string content = provider();
	if (content.empty()) {
		// The window comes back in front once the message closes.
		const KeepInFront front(parent);
		QMessageBox::information(parent, "Nothing to export", "There is no data to export.");
		return;
	}
	const auto csv = [&content]() -> const std::string& { return content; };
	const auto write = [&artifactWriter](const QString& path, const char*, const std::string& bytes) { return artifactWriter(path, bytes); };
	saveExportInteractive(parent, telemetry::ExportKind::Csv, "Export Data", suggestedName, operation, csv, write);
}

ResultViews::ResultViews(QWidget* window, RunController* runController, Host& host)
	: QObject(window), m_window(window), m_runController(runController), m_host(host) {}

void ResultViews::showWindow(QWidget* window) {
	m_windows.removeAll(QPointer<QWidget>());
	if (!m_windows.isEmpty()) {
		const QWidget* last = m_windows.last();
		const QRect available = last->screen()->availableGeometry();
		QRect frame(last->pos() + QPoint(32, 32), window->size() + last->frameSize() - last->size());
		if (!available.contains(frame))
			frame.moveTopLeft(available.topLeft());
		window->move(frame.topLeft());
	}
	window->show();
	m_windows.append(window);
}

void ResultViews::addCaseWindow(QWidget* window) {
	m_caseWindows.removeAll(QPointer<QWidget>());
	m_caseWindows.append(window);
}

void ResultViews::closeAll() {
	for (const QPointer<QWidget>& window : m_windows + m_caseWindows)
		if (window)
			window->close();
	m_windows.clear();
	m_caseWindows.clear();
}

// Choose a reference from routes that were actually used in this run.
int ResultViews::chooseRouteDiagramReference(const CompletedRunData& run, const QString& purpose) const {
	const QVector<RouteReferenceChoice> choices = buildRouteReferenceChoices(m_host.sceneModel(), usedRouteReferencesFrom(run));
	if (choices.isEmpty()) return -1;
	if (m_host.dialogsSuppressed()) return choices.first().runtimeIndex;
	RouteReferenceDialog dialog(choices, purpose, m_window);
	return dialog.exec() == QDialog::Accepted ? dialog.selectedRuntimeIndex() : -1;
}

void ResultViews::displayTrainPathDiagrams() {
	if (!m_runController->hasRunResults()) return;
	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	const int reference = chooseRouteDiagramReference(*run, "train paths");
	if (reference >= 0) buildRouteDiagram(run, false, reference);
}

// --- Per-train diagram slots ---
void ResultViews::showSpeedDistanceDiagram() {
	buildPerTrainDiagram(TrajectoryChart::SpeedDistance);
}
void ResultViews::showSpeedTimeDiagram() {
	buildPerTrainDiagram(TrajectoryChart::SpeedTime);
}
void ResultViews::showTimeDistanceDiagram() {
	buildPerTrainDiagram(TrajectoryChart::TimeDistance);
}

void ResultViews::showTractiveEffortDistanceDiagram() {
	buildPerTrainDiagram(TrajectoryChart::TractiveEffortDistance);
}

void ResultViews::buildPerTrainDiagram(int mode) {
	if (mode >= 0 && mode < static_cast<int>(std::size(kTrajectoryTitles)))
		buildPerTrainDiagram(static_cast<TrajectoryChart>(mode));
}

void ResultViews::buildPerTrainDiagram(TrajectoryChart kind) {
	// require a completed run
	if (!m_runController->hasRunResults()) {
		m_host.resultMessage("Run a simulation to open diagrams", 5000);
		return;
	}

	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	const QString heading = kTrajectoryTitles[static_cast<int>(kind)];
	const TrajectoryChartResult trajectories = buildTrajectoryChart(*run, kind);

	DiagramWindow* win = new DiagramWindow(heading, m_window);
	win->setPresentation(heading, completedRunContext(run->provenance));
	win->setProperty("manualSection", QStringLiteral("train-diagram"));
	win->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	win->setChart(trajectories.chart);
	win->setTrainColors(trainColorsForRun(run->results, m_host.sceneModel()));
	win->setTrainServices(trainServicesForRun(run->results));
	win->setCsvProvider(csvProviderFrom(run, &trajectoryCsvFrom), "trajectory.csv");
	attachRunProvenance(win, run->provenance);
	connect(win, &DiagramWindow::trainSelected, this, &ResultViews::trainSelected);
	win->setTimeAxisX(trajectories.timeAxis, run->settings.startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	showWindow(win);
}

void ResultViews::showDelayAnalysis() {
	if (QWidget* window = m_runController->openDelayAnalysis())
		showWindow(window);
}

void ResultViews::showDelayComparison() {
	if (DelayAnalysisWindow* window = m_runController->openDelayAnalysis()) {
		showWindow(window);
		window->showBaselineTab();
	}
}

void ResultViews::showTimetableTable() {
	if (!m_runController->hasRunResults()) {
		m_host.resultMessage("Run a simulation to open diagrams", 5000);
		return;
	}

	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	auto* window = new TimetableTableWindow(run->timetable, run->settings.startOffsetSeconds,
		csvProviderFrom(run, &timetableCsvFrom), m_window);
	window->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	window->setTrainColors(trainColorsForRun(run->results, m_host.sceneModel()));
	window->setRunProvenance(run->provenance);
	window->setAttribute(Qt::WA_DeleteOnClose);
	showWindow(window);
}

void ResultViews::showDelayDiagram() {
	if (!m_runController->hasRunResults()) {
		m_host.resultMessage("Run a simulation to open diagrams", 5000);
		return;
	}

	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	const int reference = chooseRouteDiagramReference(*run, "delays");
	if (reference < 0) return;
	const std::optional<DelayDiagramData> diagram = delayDiagramData(*run, reference);
	if (!diagram) return;
	const QString heading = ResultViewName::kDelays;
	RouteChart route = buildDelayChart(*diagram);

	DiagramWindow* win = new DiagramWindow(heading, m_window);
	// The header names the run and then the reference route, which the distance axis follows.
	win->setPresentation(heading, completedRunContext(run->provenance) + QStringLiteral(" \u00b7 ") + route.referenceRoute);
	win->setProperty("manualSection", QStringLiteral("timetable-results"));
	win->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	win->setChart(route.chart);
	if (route.stationAxis) win->setStationAxis(route.stationAxis, std::move(route.stationTicks));
	win->setTrainColors(trainColorsForRun(run->results, m_host.sceneModel()));
	win->setTrainServices(trainServicesForRun(run->results));
	win->setCsvProvider(csvProviderFrom(run, &timetableCsvFrom), "timetable.csv");
	win->setFigureFileName(QStringLiteral("delays.png"));
	// A run whose timetable has no planned arrivals, as Netherlands, has no arrival delay to draw, nor has a route whose
	// stations no train has one at.
	win->setEmptyText(QStringLiteral("No arrival delays at stations of %1").arg(QString::fromStdString(diagram->referencePath.id)));
	attachRunProvenance(win, run->provenance);
	connect(win, &DiagramWindow::trainSelected, this, &ResultViews::trainSelected);
	win->setTimeAxisX(false, run->settings.startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	showWindow(win);
}

void ResultViews::showTimetableGraph() {
	if (!m_runController->hasRunResults()) return;
	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	const int reference = chooseRouteDiagramReference(*run, "timetable graph");
	if (reference >= 0) buildRouteDiagram(run, true, reference);
}

void ResultViews::buildRouteDiagram(const std::shared_ptr<const CompletedRunData>& run, bool timetable, int referenceIndex) {
	const std::optional<RouteDiagramData> diagram = routeDiagramData(*run, timetable, referenceIndex);
	if (!diagram) return;
	const QString heading = timetable ? ResultViewName::kTimetableGraph : ResultViewName::kTrainPaths;
	// A train path chart counts the stops of the timetable graph of its reference route.
	const std::optional<RouteDiagramData> timetableGraph = timetable ? std::nullopt : routeDiagramData(*run, true, referenceIndex);
	RouteChart route = buildRouteDiagramChart(*diagram, timetableGraph ? *timetableGraph : *diagram, run->settings.horizonSeconds());
	DiagramWindow* win = new DiagramWindow(heading, m_window);
	// The header names the run and then the reference route, which the distance axis follows.
	win->setPresentation(heading, completedRunContext(run->provenance) + QStringLiteral(" \u00b7 ") + route.referenceRoute);
	win->setProperty("manualSection", QStringLiteral("train-paths"));
	win->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	win->setChart(route.chart);
	if (route.stationAxis) win->setStationAxis(route.stationAxis, std::move(route.stationTicks));
	win->setTrainColors(trainColorsForRun(run->results, m_host.sceneModel()));
	win->setTrainServices(trainServicesForRun(run->results));
	const auto graphCsv = [csvRows = diagram->csvRows](const QStringList& visible) {
		std::vector<std::string> ids;
		for (const QString& id : visible) ids.push_back(id.toStdString());
		return buildRouteDiagramCsv(csvRows, ids);
	};
	win->setCsvProvider(graphCsv, timetable ? "timetable_graph.csv" : "train_path_graph.csv");
	if (const auto routeDiagramBuilt = mainWindowTestHooks().routeDiagramBuilt)
		routeDiagramBuilt(graphCsv, trainIdsFrom(*run), timetable);
	attachRunProvenance(win, run->provenance);
	connect(win, &DiagramWindow::trainSelected, this, &ResultViews::trainSelected);
	win->setTimeAxisY(true, run->settings.startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	showWindow(win);
}

void ResultViews::showBlockingTimeDiagram() {
	if (!m_runController->hasRunResults()) {
		m_host.resultMessage("Run a simulation to open diagrams", 5000);
		return;
	}

	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	BlockingTimeScope scope = defaultBlockingTimeScopeFrom(*run);
	if (!m_host.dialogsSuppressed() && !chooseBlockingTimeScope(m_window, blockingTimeScopeChoicesFrom(*run), scope))
		return;
	const std::optional<BlockingTimeDiagramData> diagram = blockingTimeDiagramData(*run, scope);
	if (!diagram)
		return;
	// The route and blocks as the Capacity window names its section: "route0 (A), blocks 36 to 72".
	CapacityAnalysisScope blocks;
	blocks.routeIndex = scope.routeIndex;
	blocks.blockIds = scope.blockIds;
	blocks.sectionIndices = scope.sectionIndices;
	const QString route = run->hasRoute(scope.routeIndex) ? capacityScopeTextFrom(*run, blocks) : QStringLiteral("All routes");
	showBlockingTimeWindow(run, *diagram, false,
		QString("%1, %2 to %3")
			.arg(route, QString::fromStdString(formatSimTime(static_cast<long long>(scope.startTime), run->settings.startOffsetSeconds)),
				QString::fromStdString(formatSimTime(static_cast<long long>(scope.endTime), run->settings.startOffsetSeconds))),
		capacityScopeBlocksText(*run, blocks));
}

void ResultViews::showBlockingTimeWindow(const std::shared_ptr<const CompletedRunData>& run,
	const BlockingTimeDiagramData& diagram, bool compressed, const QString& context, const QString& contextToolTip) {
	const QString title = compressed ? QStringLiteral("Compressed blocking time") : QString(ResultViewName::kBlockingTime);
	const RouteDiagramPath& referencePath = diagram.referencePath;
	const std::vector<BlockingTimeDiagramSegment>& segments = diagram.segments;
	const std::vector<BlockingTimeDiagramSegment>& unmapped = diagram.unmapped;
	const std::vector<BlockSectionExtent>& extents = diagram.extents;
	const std::vector<BlockingTimePlannedReference>& plannedReferences = diagram.plannedReferences;
	const auto& plottedPlanned = diagram.plannedLines;
	const auto& trajectoryRows = diagram.trajectoryRows;

	const PositionScale scale(extents);
	QChart* chart = buildBlockingTimeChart(diagram, compressed, run->sectionLabels, scale);

	DiagramWindow* win = new DiagramWindow(title, m_window);
	win->setProperty("manualSection", compressed ? QStringLiteral("capacity") : QStringLiteral("blocking-time"));
	win->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	win->setChart(chart);
	// The strip marks the stations of the reference route and the critical and conflicting blocks of the chart.
	std::vector<double> stationKm;
	for (const RouteDiagramNode& node : referencePath.nodes)
		if (!node.stationId.empty()) stationKm.push_back(node.positionKm);
	win->setBlockSections(extents, QString::fromStdString(referencePath.id), &run->sectionLabels,
		blockSectionMarks(extents, segments, stationKm, compressed), diagram.routeSectionCount);
	win->setPositionAxis(scale);
	// The outlines and lines take the colours of the trains; critical blocks and conflicts keep their own. A repeated
	// train is a copy of its train, which gives it the colour of that train, and it has the service of its train; a train
	// without a service is a service of its own.
	QHash<QString, QString> services = trainServicesForRun(run->results);
	QHash<QString, QString> copies;
	for (const auto& [repeat, source] : diagram.repeatedTrains) {
		const QString repeatId = QString::fromStdString(repeat);
		const QString sourceId = QString::fromStdString(source);
		copies.insert(repeatId, sourceId);
		services.insert(sourceId, services.value(sourceId, sourceId));
		services.insert(repeatId, services.value(sourceId));
	}
	win->setTrainCopies(copies);
	win->setTrainColors(trainColorsForRun(run->results, m_host.sceneModel()));
	win->setTrainServices(services);
	const bool nothingPlotted = segments.empty() && plottedPlanned.empty() && trajectoryRows.empty();
	// The header names the run before the scope of the chart.
	win->setPresentation(title, completedRunContext(run->provenance) + QStringLiteral(" \u00b7 ") + context,
		QString::fromStdString(blockingTimeNote(diagram.omitted, nothingPlotted, !unmapped.empty(), referencePath.id)), contextToolTip);
	const std::function<std::string(const QStringList&)> scopedCsv =
		[segments, plannedReferences, trajectoryRows, extents, unmapped](const QStringList& visibleTrainIds) {
			return buildBlockingTimeCsv(visibleTrainIds, segments, plannedReferences, trajectoryRows, extents, unmapped);
		};
	win->setCsvProvider(scopedCsv, compressed ? "capacity_compressed_blocking_time.csv" : "blocking_time.csv",
		blockingTimeTrainIds(segments, plannedReferences, trajectoryRows, unmapped));
	attachRunProvenance(win, run->provenance);
	connect(win, &DiagramWindow::trainSelected, this, &ResultViews::trainSelected);
	win->setTimeAxisY(true, run->settings.startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	showWindow(win);
}

void ResultViews::showCapacityAnalysis() {
	if (!m_runController->hasRunResults()) {
		m_host.resultMessage("Run a simulation to open capacity analysis", 5000);
		return;
	}

	// A scripted run has nobody to close the message, so it shows none.
	const auto showCapacityMessage = [this](const QString& message) {
		UserPrompts([this] { return m_host.dialogsSuppressed(); }).showInfo(m_window, ResultViewName::kCapacity, message);
	};
	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	CapacityAnalysisScope scope = firstCapacityScopeWithPairFrom(*run);
	if (scope.routeIndex < 0) {
		showCapacityMessage("No route has complete blocking times for two or more trains. Run a case with signalling data.");
		return;
	}
	if ((!m_host.dialogsSuppressed() || mainWindowTestHooks().askCapacityScope)
		&& !chooseCapacityAnalysisScope(m_window, *run, scope))
		return;
	if (scope.routeIndex < 0 || scope.blockIds.empty()) {
		showCapacityMessage("The chosen route has no blocks. Choose another route.");
		return;
	}
	const std::string scopeIssue = capacityScopeIssueFrom(*run, scope);
	if (!scopeIssue.empty()) {
		showCapacityMessage(QString::fromStdString(scopeIssue));
		return;
	}
	if (capacityInputsFrom(*run, scope).size() < 2) {
		showCapacityMessage("Fewer than two trains use the chosen blocks. Choose other blocks or trains.");
		return;
	}
	// The export names the blocks by their ids, the window by their labels.
	const QString sectionLabel = capacityScopeLabelFrom(*run, scope);
	const QString sectionText = capacityScopeTextFrom(*run, scope);
	const CapacityAnalysisResult result = capacityAnalysisFrom(*run, scope);
	if (!result.analyzable) {
		showCapacityMessage("These trains still overlap after compression. Choose other trains or another order.");
		return;
	}
	if (result.cycleEndIdentity.empty()) {
		showCapacityMessage("The cycle-closing train must be a checked row below the first.");
		return;
	}
	if (result.cycleTime < 0.0) {
		showCapacityMessage(
			QString("%1 shares no block with an earlier train, so it closes no cycle. Choose another cycle-closing train.")
				.arg(QString::fromStdString(result.cycleEndIdentity)));
		return;
	}

	const RunProvenance provenance = run->provenance;
	const QString sectionTip = capacityScopeBlocksText(*run, scope);
	// The header names the run and the section; the window title names the view.
	auto* dialog = new CapacityWindow(result, completedRunContext(provenance) + QStringLiteral(" \u00b7 ") + sectionText, sectionTip,
		run->sectionLabels, m_window);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	// The file dialog and its messages belong to the Capacity window, so the focus returns to it.
	connect(dialog->exportButton(), &QPushButton::clicked, dialog, [this, dialog, result, sectionLabel, provenance]() {
		const auto operation = m_host.captureTelemetryOperation();
		saveCsvInteractive(
			dialog, "capacity_analysis.csv", operation,
			[result, sectionLabel] { return capacityAnalysisCsv(result, sectionLabel); },
			[provenance](const QString& path, const std::string& bytes) {
				return writeRunArtifactWithProvenance(path.toStdString(), "csv", bytes, provenance);
			});
	});
	connect(dialog->diagramButton(), &QPushButton::clicked, dialog, [this, run, scope, result, sectionText, sectionTip]() {
		const std::optional<BlockingTimeDiagramData> diagram = compressedBlockingTimeDiagramData(*run, scope, result);
		if (!diagram)
			return;
		// The header has the key figures of the Capacity window; the tooltip names the trains of the cycle.
		showBlockingTimeWindow(run, *diagram, true, sectionText + QStringLiteral(" \u00b7 ") + capacityCycleText(result),
			QStringList({QString("Cycle from %1 to %2").arg(QString::fromStdString(result.firstIdentity), QString::fromStdString(result.cycleEndIdentity)),
							sectionTip})
				.join('\n')
				.trimmed());
	});
	showWindow(dialog);
}

void ResultViews::showAreaCapacity() {
	if (!m_runController->hasRunResults()) {
		m_host.resultMessage("Run a simulation to open capacity by area", 5000);
		return;
	}
	// The options dialog reads the run until it closes, so this call holds it.
	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	if (QWidget* window = openAreaCapacityWindow(m_window, *run, [this] { return m_host.captureTelemetryOperation(); }))
		showWindow(window);
}
