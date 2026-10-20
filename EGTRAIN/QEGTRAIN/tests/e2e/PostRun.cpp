#include "tests/e2e/MainWindowTestAccess.h"

#include "app/DispatchController.h"
#include "app/GuiSimulationSnapshot.h"
#include "app/MainWindowSupport.h"
#include "app/ReplayCoverage.h"
#include "diagrams/AreaCapacityWindow.h"
#include "diagrams/CapacityAnalysis.h"
#include "diagrams/CapacityScope.h"
#include "diagrams/DelayAnalysisWindow.h"
#include "diagrams/DiagramWindow.h"
#include "diagrams/RouteReferenceChoice.h"
#include "diagrams/RunExports.h"
#include "diagrams/RunResults.h"
#include "diagrams/TrainFilterButton.h"
#include "editors/ScenarioPanel.h"
#include "editors/TrainUnitSourceLinker.h"
#include "graphics/FollowCamera.h"
#include "graphics/NetworkView.h"
#include "graphics/VisualPolish.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/TrainBadgeItem.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"
#include "scene/SceneModel.h"
#include "ui_MainWindow.h"
#include "simulation/SimulationWorker.h"
#include "util/PlaybackProfiler.h"
#include "util/TimeFormat.h"
#include "widgets/ConsoleWidget.h"
#include "widgets/ElidedLabel.h"
#include "widgets/InfoDockWidget.h"
#include "widgets/NetworkLegendWidget.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QColor>
#include <QCoreApplication>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QGraphicsItem>
#include <QHeaderView>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineF>
#include <QList>
#include <QListWidget>
#include <QMenu>
#include <QMetaObject>
#include <QMetaType>
#include <QMouseEvent>
#include <QObject>
#include <QPen>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QPushButton>
#include <QRectF>
#include <QScrollArea>
#include <QScrollBar>
#include <QSize>
#include <QSlider>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTabBar>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVariant>
#include <QVector>
#include <QWheelEvent>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QCategoryAxis>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QXYSeries>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

// Whether the marker of the first live snapshot of an autostarted run has been printed.
bool autostartProgressReported = false;
// Whether the case was edited while the run went on, which the check of a discarded run needs after the window has cleared it.
bool sceneEditedDuringRun = false;
// How many runs the operational completion check has seen, and the frame on which the first one ended.
int operationalCompletions = 0;
std::shared_ptr<const GuiSimulationSnapshot> operationalFirstFrame;

// True when every head shows what the snapshot holds for it: the display code
// and failed flag of its state, or unavailable when the snapshot has none.
bool signalHeadsShow(const QList<SignalItem*>& heads, const GuiSimulationSnapshot& snapshot) {
	std::map<std::pair<std::string, bool>, const GuiSignalState*> states;
	for (const GuiSignalState& state : snapshot.signalStates)
		states[{state.sectionId, state.reversedDirection}] = &state;
	for (const SignalItem* head : heads) {
		const auto found = states.find({head->sectionAheadId, head->reversedDirection});
		const bool known = found != states.end();
		const int code = known ? guiSignalDisplayCode(*found->second) : kGuiSignalUnavailable;
		if (head->aspectCode() != code || head->failed() != (known && found->second->failed))
			return false;
	}
	return true;
}

// True when no head shows a failure on a section that the snapshot does not
// report as blocked. anyFailed and anyBlocked say whether either appears.
bool signalFailuresAreBlocked(const QList<SignalItem*>& heads, const GuiSimulationSnapshot& snapshot,
	bool& anyFailed, bool& anyBlocked) {
	std::set<std::string> blocked;
	for (const GuiSectionState& section : snapshot.sectionStates)
		if (section.blocked)
			blocked.insert(section.sectionId);
	anyBlocked = !blocked.empty();
	anyFailed = false;
	bool covered = true;
	for (const SignalItem* head : heads)
		if (head->failed()) {
			anyFailed = true;
			covered = covered && blocked.count(head->sectionAheadId) > 0;
		}
	return covered;
}

// State of the live signal head checks (QEGTRAIN_E2E_SIGNAL_HEADS).
struct SignalHeadsState {
	std::vector<int> pauseSteps; // QEGTRAIN_E2E_PAUSE_STEPS, ascending
	std::size_t nextPause = 0;
	bool pausing = false;
	bool failed = false;
	int renders = 0;
	int failedRenders = 0;
};

SignalHeadsState& signalHeadsState() {
	static SignalHeadsState state = [] {
		SignalHeadsState initial;
		for (const QString& step : qEnvironmentVariable("QEGTRAIN_E2E_PAUSE_STEPS").split(',', Qt::SkipEmptyParts))
			initial.pauseSteps.push_back(step.toInt());
		return initial;
	}();
	return state;
}

std::string exactNumber(double value) {
	char buffer[40];
	std::snprintf(buffer, sizeof buffer, "%.17g", value);
	return buffer;
}

std::string propertyText(const QVariant& value) {
	if (value.userType() == QMetaType::QStringList)
		return value.toStringList().join('|').toStdString();
	return value.toString().toStdString();
}

std::string penText(const QPen& pen) {
	return pen.color().name(QColor::HexArgb).toStdString() + " " + exactNumber(pen.widthF()) + " "
		+ std::to_string(static_cast<int>(pen.style()));
}

std::string pointsText(const QList<QPointF>& points) {
	std::string text;
	for (const QPointF& point : points)
		text += exactNumber(point.x()) + "," + exactNumber(point.y()) + "\n";
	return text;
}

// The chart of a result window as exact text: its axes, then every series with its name, properties, pen and points.
// Two builds of one diagram can be compared byte by byte.
std::string chartDump(const DiagramWindow& window) {
	std::string text = "window " + window.windowTitle().toStdString() + "\n";
	const auto* view = window.findChild<QChartView*>();
	if (!view || !view->chart())
		return text + "no chart\n";
	const QChart* chart = view->chart();
	text += "title " + chart->title().toStdString() + "\n";
	for (const QAbstractAxis* axis : chart->axes()) {
		text += "axis " + std::to_string(static_cast<int>(axis->orientation())) + " " + std::to_string(static_cast<int>(axis->type()))
			+ " " + axis->titleText().toStdString() + (axis->isReverse() ? " reversed" : "");
		if (const auto* value = qobject_cast<const QValueAxis*>(axis))
			text += " " + exactNumber(value->min()) + " " + exactNumber(value->max());
		text += "\n";
	}
	for (const QAbstractSeries* series : chart->series()) {
		text += "series " + std::to_string(static_cast<int>(series->type())) + " " + series->name().toStdString()
			+ (series->isVisible() ? "" : " hidden") + "\n";
		for (const QByteArray& name : series->dynamicPropertyNames())
			text += "  " + name.toStdString() + "=" + propertyText(series->property(name.constData())) + "\n";
		if (const auto* line = qobject_cast<const QXYSeries*>(series)) {
			text += "  pen " + penText(line->pen()) + "\n" + pointsText(line->points());
		} else if (const auto* area = qobject_cast<const QAreaSeries*>(series)) {
			text += "  pen " + penText(area->pen()) + " fill " + area->brush().color().name(QColor::HexArgb).toStdString() + "\n";
			if (area->upperSeries())
				text += "  upper\n" + pointsText(area->upperSeries()->points());
			if (area->lowerSeries())
				text += "  lower\n" + pointsText(area->lowerSeries()->points());
		}
	}
	return text;
}

bool writeFile(const QString& path, const std::string& content) {
	QFile out(path);
	return out.open(QIODevice::WriteOnly) && out.write(content.data(), static_cast<qint64>(content.size())) == static_cast<qint64>(content.size());
}

// The result window that the main window opened last.
DiagramWindow* newestWindow(const MainWindow& w) {
	const auto windows = w.findChildren<DiagramWindow*>();
	return windows.isEmpty() ? nullptr : windows.last();
}

// Writes the dump of the chart of a result window.
bool exportChart(DiagramWindow* window, const QString& dir, const QString& name) {
	if (!window)
		return false;
	QApplication::processEvents();
	return writeFile(dir + "/" + name + "_series.txt", chartDump(*window));
}

// Writes what a result window shows and exports: the dump of its chart and the CSV for all trains, for the first train
// and for none, as the train filter sets them.
bool exportWindow(DiagramWindow* window, const QString& dir, const QString& name,
	const std::function<bool(const QString&, const std::string&)>& dump) {
	auto* filter = window ? window->findChild<TrainFilterButton*>() : nullptr;
	auto* list = filter ? filter->menu()->findChild<QListWidget*>() : nullptr;
	if (!list)
		return false;
	bool ok = exportChart(window, dir, name);
	ok &= dump(name + "_window_all.csv", window->csvContent());
	for (int row = 1; row < list->count(); ++row)
		list->item(row)->setCheckState(Qt::Unchecked);
	ok &= dump(name + "_window_one.csv", window->csvContent());
	if (list->count())
		list->item(0)->setCheckState(Qt::Unchecked);
	ok &= dump(name + "_window_none.csv", window->csvContent());
	return ok;
}

} // namespace

// Starts the run of the case 1.5 s after the first show when QEGTRAIN_AUTOSTART is set, unless the playback profiler starts
// it. A run with QEGTRAIN_E2E_PAUSE_STEPS goes at almost full speed.
void MainWindowTestAccess::autostart(MainWindow& w) {
	if (qEnvironmentVariableIsSet("QEGTRAIN_AUTOSTART") && !PlaybackProfiler::enabled()) {
#ifdef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
		if (!qEnvironmentVariableIsSet("QEGTRAIN_E2E_TELEMETRY_SMOKE"))
#endif
			QTimer::singleShot(1500, w.m_runController, &RunController::runCurrent);
	}
	// At full speed a short run can end before a pause of the signal head check takes effect.
	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_PAUSE_STEPS"))
		w.m_speedSlider->setValue(kMaxStepDelayMs - 2);
}

// QEGTRAIN_E2E_OPERATIONAL_DISCARD edits the case while the run goes on, so that its results are discarded. The first live
// snapshot of an autostarted run prints a marker, and QEGTRAIN_E2E_CLOSE_DURING_RUN closes the window then.
void MainWindowTestAccess::snapshotDelivered(MainWindow& w) {
	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_OPERATIONAL_DISCARD")
		&& !w.m_runController->m_sceneChangedDuringRun && w.m_caseDescriptionEdit) {
		w.m_caseDescriptionEdit->setText(QStringLiteral("discarded operational run"));
		w.commitCaseSettings();
		sceneEditedDuringRun = w.m_runController->m_sceneChangedDuringRun;
	}
	if (!autostartProgressReported && qEnvironmentVariableIsSet("QEGTRAIN_AUTOSTART")) {
		// fprintf so the marker reaches the real stdout; std::cout is captured
		// by the console pane once the window exists
		std::fprintf(stdout, "E2E_GUI_AUTOSTART_RUNNING timestep=%d\n", w.m_snapshot->timestep);
		std::fflush(stdout);
		autostartProgressReported = true;
		if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_CLOSE_DURING_RUN")) {
			// The close request returns with the run still stopping and the window open.
			// The window closes by itself when the worker has finished.
			QTimer::singleShot(0, &w, [&w]() {
				w.close();
				const bool deferred = w.m_runController->m_worker && w.isVisible()
					&& w.statusBar()->currentMessage() == QStringLiteral("Stopping simulation...");
				std::fprintf(stdout, "E2E_CLOSE_DURING_RUN deferred=%d\n", deferred ? 1 : 0);
				std::fflush(stdout);
			});
		}
	}
}

// Live check of the signal heads (QEGTRAIN_E2E_SIGNAL_HEADS). After every
// render the heads show the snapshot they were drawn from, and a failed head
// stands on a blocked section. At the steps of QEGTRAIN_E2E_PAUSE_STEPS the run
// is paused: the canvas must then stand on the last snapshot that was delivered.
void MainWindowTestAccess::checkSignalHeads(MainWindow& w) {
	static const bool enabled = qEnvironmentVariableIsSet("QEGTRAIN_E2E_SIGNAL_HEADS");
	SignalHeadsState& state = signalHeadsState();
	if (!enabled || state.failed || !w.m_snapshot)
		return;
	const auto fail = [&state](const QString& why) {
		state.failed = true;
		std::fprintf(stderr, "E2E_SIGNAL_HEADS_FAIL: %s\n", qPrintable(why));
		QCoreApplication::exit(2);
	};
	if (!signalHeadsShow(w.m_presenter->allSignals, *w.m_snapshot))
		return fail(QStringLiteral("heads do not show the snapshot of step %1").arg(w.m_snapshot->timestep));
	bool anyFailed = false;
	bool anyBlocked = false;
	if (!signalFailuresAreBlocked(w.m_presenter->allSignals, *w.m_snapshot, anyFailed, anyBlocked))
		return fail(QStringLiteral("a failed head stands on a section that is not blocked at step %1").arg(w.m_snapshot->timestep));
	++state.renders;
	state.failedRenders += anyFailed ? 1 : 0;
	if (state.pausing || state.nextPause >= state.pauseSteps.size()
		|| w.m_snapshot->timestep < state.pauseSteps[state.nextPause])
		return;
	state.pausing = true;
	w.ui->actionSimulationPause->trigger();
	QTimer::singleShot(300, &w, [&w, fail]() {
		SignalHeadsState& pause = signalHeadsState();
		const int requested = pause.pauseSteps[pause.nextPause];
		if (!w.m_snapshot || !w.m_lastRenderedSnapshot || w.m_lastRenderedSnapshot != w.m_snapshot
			|| !signalHeadsShow(w.m_presenter->allSignals, *w.m_snapshot))
			return fail(QStringLiteral("paused after step %1: the canvas shows step %2, the last delivered snapshot is step %3")
					.arg(requested)
					.arg(w.m_lastRenderedSnapshot ? w.m_lastRenderedSnapshot->timestep : -1)
					.arg(w.m_snapshot ? w.m_snapshot->timestep : -1));
		++pause.nextPause;
		pause.pausing = false;
		w.ui->actionSimulationPause->trigger();
	});
}

// An autostarted run prints the size of its Run Results table.
void MainWindowTestAccess::runResultsRefreshed(MainWindow& w) {
	if (qEnvironmentVariableIsSet("QEGTRAIN_AUTOSTART")) {
		std::fprintf(stdout, "\nE2E_GUI_RUN_RESULTS rows=%d trains=%d dock_visible=%d output_dir=%s\n",
			w.m_runResultsTable->rowCount(), static_cast<int>(w.m_runController->m_completedRun->results.trains.size()),
			w.m_runResultsDock->isVisible() ? 1 : 0, initial_variables.OutputMainFolder.c_str());
		std::fflush(stdout);
	}
}

// QEGTRAIN_E2E_ROUTE_DIAGRAM writes the CSV of each route diagram for all trains and for the first one.
void MainWindowTestAccess::routeDiagramBuilt(const std::function<std::string(const QStringList&)>& csv,
	const QStringList& trainIds, bool timetable) {
	if (!qEnvironmentVariableIsSet("QEGTRAIN_E2E_ROUTE_DIAGRAM"))
		return;
	const QString dir = qEnvironmentVariable("QEGTRAIN_E2E_ROUTE_DIAGRAM");
	const QString basename = timetable ? "timetable_graph" : "train_path_graph";
	for (const auto& selection : {std::pair<QString, QStringList>{"all", trainIds},
			 std::pair<QString, QStringList>{"first", QStringList{trainIds.first()}}}) {
		QFile out(dir + "/" + basename + "_" + selection.first + ".csv");
		if (out.open(QIODevice::WriteOnly)) out.write(QByteArray::fromStdString(csv(selection.second)));
	}
}

// QEGTRAIN_E2E_ROUTE_DIAGRAM saves the route diagrams as pictures and QEGTRAIN_E2E_EXPORT_DIR writes every CSV export of the
// run. Both end the run and the process.
bool MainWindowTestAccess::runFinished(MainWindow& w) {
	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_ROUTE_DIAGRAM")) {
		bool ok = w.m_runController->m_resultsAvailable && !w.m_runController->m_completedRun->trains.empty();
		if (ok) {
			const int reference = w.m_runController->m_completedRun->trains.front().routeIndex;
			w.buildRouteDiagram(w.m_runController->m_completedRun, false, reference);
			w.buildRouteDiagram(w.m_runController->m_completedRun, true, reference);
			QApplication::processEvents();
			const auto windows = w.findChildren<DiagramWindow*>();
			ok = windows.size() == 2;
			for (DiagramWindow* window : windows) {
				auto* view = window->findChild<QChartView*>();
				const auto axes = view ? view->chart()->axes(Qt::Vertical) : QList<QAbstractAxis*>();
				auto* clock = axes.isEmpty() ? nullptr : qobject_cast<QCategoryAxis*>(axes.first());
				ok &= clock && clock->isReverse() && clock->min() == 0
					&& clock->categoriesLabels().contains(QString::fromStdString(formatSimTime(0, w.m_runController->m_startOffsetSeconds)));
				ok &= view && !view->chart()->axes(Qt::Horizontal).isEmpty();
				if (view) {
					for (auto* abstractSeries : view->chart()->series()) {
						auto* line = qobject_cast<QLineSeries*>(abstractSeries);
						if (line && ((window->windowTitle().startsWith("Timetable") && !line->name().contains("dwell")) || line->count() == 1))
							ok &= line->pointsVisible();
					}
					const QString file = qEnvironmentVariable("QEGTRAIN_E2E_ROUTE_DIAGRAM") + "/"
						+ (window->windowTitle().startsWith("Timetable") ? "timetable_graph" : "train_path_graph") + ".png";
					ok &= view->grab().save(file);
				}
			}
			// Each route that trains run on is the reference once: the dump of the chart and the CSV for all trains.
			const QString dir = qEnvironmentVariable("QEGTRAIN_E2E_ROUTE_DIAGRAM");
			for (const auto& used : usedRouteReferencesFrom(*w.m_runController->m_completedRun)) {
				for (const bool timetable : {false, true}) {
					w.buildRouteDiagram(w.m_runController->m_completedRun, timetable, used.first);
					DiagramWindow* window = newestWindow(w);
					const QString name = QString("%1_route%2").arg(timetable ? "timetable_graph" : "train_path_graph").arg(used.first);
					ok &= exportChart(window, dir, name);
					ok &= window && writeFile(dir + "/" + name + ".csv", window->csvContent());
					if (window)
						window->close();
				}
			}
			const QVector<RouteReferenceChoice> choices =
				buildRouteReferenceChoices(w.m_document->mutableModel(), usedRouteReferencesFrom(*w.m_runController->m_completedRun));
			for (const RouteReferenceChoice& choice : choices)
				std::fprintf(stdout, "E2E_ROUTE_CHOICE %s\n", choice.label.toStdString().c_str());
			RouteReferenceDialog chooser(choices, "train paths", &w);
			ok &= !choices.isEmpty() && chooser.grab().save(qEnvironmentVariable("QEGTRAIN_E2E_ROUTE_DIAGRAM") + "/route_reference_chooser.png");
		}
		std::fprintf(ok ? stdout : stderr, ok ? "E2E_ROUTE_DIAGRAM_OK\n" : "E2E_ROUTE_DIAGRAM_FAIL\n");
		std::fflush(ok ? stdout : stderr);
		w.m_runController->clearSimulationWorker(false);
		QCoreApplication::exit(ok ? 0 : 2);
		return true;
	}
	// verification hook: write every CSV export from the completed run, then exit
	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_EXPORT_DIR")) {
		const QString dir = qEnvironmentVariable("QEGTRAIN_E2E_EXPORT_DIR");
		// Skip an export with no rows, matching the interactive "nothing to export".
		const auto dump = [&dir, &w](const QString& file, const std::string& content) {
			if (content.empty())
				return true;
			const QString path = dir + "/" + file;
			if (writeCsvFileWithProvenance(path, content, w.m_runController->m_completedRun->provenance))
				return true;
			std::fprintf(stderr, "E2E_PROVENANCE_EXPORT_WARNING: %s\n", path.toStdString().c_str());
			return false;
		};
		bool ok = w.m_runController->m_resultsAvailable;
		if (ok) {
			const QStringList ids = trainIdsFrom(*w.m_runController->m_completedRun);
			ok &= dump("trajectory.csv", trajectoryCsvFrom(*w.m_runController->m_completedRun, ids));
			ok &= dump("timetable.csv", timetableCsvFrom(*w.m_runController->m_completedRun, ids));
			const std::string blockingCsv = blockingTimeCsvFrom(*w.m_runController->m_completedRun, ids);
			ok &= dump("blocking_time.csv", blockingCsv);
			w.showBlockingTimeDiagram();
			const auto windows = w.findChildren<DiagramWindow*>();
			auto* blocking = windows.isEmpty() ? nullptr : windows.last();
			auto* filter = blocking ? blocking->findChild<TrainFilterButton*>() : nullptr;
			auto* list = filter ? filter->menu()->findChild<QListWidget*>() : nullptr;
			// A scene with no routed train has no blocking-time window and nothing to export.
			if (!blockingCsv.empty())
				ok &= blocking && list && !blocking->csvContent().empty();
			if (blocking && list) {
				ok &= exportChart(blocking, dir, "blocking_time");
				ok &= blocking->grab().save(dir + "/blocking_time_window_all.png");
				ok &= dump("blocking_time_window_all.csv", blocking->csvContent());
				for (int row = 1; row < list->count(); ++row) list->item(row)->setCheckState(Qt::Unchecked);
				ok &= dump("blocking_time_window_one.csv", blocking->csvContent());
				if (list->count()) list->item(0)->setCheckState(Qt::Unchecked);
				ok &= dump("blocking_time_window_none.csv", blocking->csvContent());
				QApplication::processEvents();
				ok &= blocking->grab().save(dir + "/blocking_time_window_none.png");
				const auto* view = blocking->findChild<QChartView*>();
				ok &= view && view->chart()->findChild<QObject*>("blockSectionOverlay");
				ok &= filter->visibleTrainIds().isEmpty();
				for (int row = 0; row < list->count(); ++row) ok &= !list->item(row)->text().startsWith("Key:");
				blocking->close();
			}
			w.showDelayDiagram();
			DiagramWindow* delay = newestWindow(w);
			ok &= exportWindow(delay, dir, "delay_diagram", dump);
			if (delay)
				delay->close();
			// The Calls tab of the delay analysis. A run with no timetable row has nothing to export.
			DelayAnalysisWindow* analysis = w.m_runController->openDelayAnalysis();
			ok &= analysis != nullptr;
			if (analysis) {
				if (!w.m_runController->m_completedRun->timetable.empty())
					ok &= analysis->exportCsvTo(dir + "/delay_analysis.csv");
				analysis->close();
			}
			// The four per-train diagrams share one export, so the CSV of the first stands for all.
			const char* const perTrainNames[] = {"speed_distance", "speed_time", "time_distance", "effort_distance"};
			for (int mode = 0; mode < 4; ++mode) {
				w.buildPerTrainDiagram(mode);
				DiagramWindow* perTrain = newestWindow(w);
				ok &= mode == 0 ? exportWindow(perTrain, dir, perTrainNames[mode], dump)
								: exportChart(perTrain, dir, perTrainNames[mode]);
				if (perTrain)
					perTrain->close();
			}
			ok &= dump("run_summary.csv", buildRunSummaryCsv(w.m_runController->m_completedRun->results));
			const std::string capacityCsv = capacityCsvFrom(*w.m_runController->m_completedRun, firstCapacityScopeWithPairFrom(*w.m_runController->m_completedRun));
			if (capacityCsv.empty()) {
				std::fprintf(stdout, "E2E_CAPACITY_EXPORT_UNAVAILABLE\n");
			} else {
				ok &= dump("capacity_analysis.csv", capacityCsv);
			}
			// The options of the area window with all its defaults: the first route, its whole block range and every train.
			const AreaCapacityScope areaScope = AreaCapacityScopeDialog(*w.m_runController->m_completedRun).scope();
			const std::string areaCsv = areaCapacityCsv(analyzeAreaCapacity(areaCapacityInputFrom(*w.m_runController->m_completedRun, areaScope)),
				areaCapacityLabel(*w.m_runController->m_completedRun, areaScope));
			if (areaCsv.empty()) {
				std::fprintf(stdout, "E2E_AREA_CAPACITY_EXPORT_UNAVAILABLE\n");
			} else {
				ok &= dump("area_capacity.csv", areaCsv);
			}
		}
		std::fprintf(ok ? stdout : stderr, ok ? "E2E_CSV_EXPORT_OK\n" : "E2E_CSV_EXPORT_FAIL\n");
		std::fflush(stdout);
		std::fflush(stderr);
		w.m_runController->clearSimulationWorker(false);
		w.refreshInfrastructurePanel();
		w.refreshIncidentPanel();
		w.m_trainUnitSourceLinker->processChanges();
		QCoreApplication::exit(ok ? 0 : 2);
		return true;
	}
	return false;
}

// The checks that follow the end of a run: the signal heads against the replay (QEGTRAIN_E2E_SIGNAL_HEADS), the legend of a
// discarded run (QEGTRAIN_E2E_OPERATIONAL_DISCARD), the replay row of a whole run (QEGTRAIN_E2E_REPLAY_WHOLE_RUN) and the
// completion with its rerun (QEGTRAIN_E2E_OPERATIONAL_COMPLETION).
void MainWindowTestAccess::runSettled(MainWindow& w) {
	if (w.m_runController->m_resultsAvailable && !w.m_completedReplay.empty() && qEnvironmentVariableIsSet("QEGTRAIN_E2E_SIGNAL_HEADS"))
		QTimer::singleShot(0, &w, [&w] { runReplaySignals(w); });

	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_OPERATIONAL_DISCARD")) {
		const QStringList labels = w.m_networkLegendWidget ? w.m_networkLegendWidget->entryLabels() : QStringList();
		const bool preview = sceneEditedDuringRun && w.m_runController->m_runtimeStatus == QStringLiteral("Failed")
			&& w.m_completedReplay.empty() && !w.m_replayBar->isVisible()
			&& w.m_showingTrackPreview && !w.m_runController->m_worker && !w.m_runController->m_resultsAvailable
			&& labels.contains(QStringLiteral("Local track"))
			&& labels.contains(QStringLiteral("High speed track (200+ km/h)"))
			&& !labels.contains(QStringLiteral("Permissive signalling"))
			&& !labels.contains(QStringLiteral("Occupied section"));
		std::fprintf(preview ? stdout : stderr, preview ? "E2E_OPERATIONAL_DISCARD_OK\n" : "E2E_OPERATIONAL_DISCARD_FAIL: preview legend\n");
		std::fflush(preview ? stdout : stderr);
		QCoreApplication::exit(preview ? 0 : 2);
		return;
	}

	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_REPLAY_WHOLE_RUN")) {
		// A scene run to its end is replayed from 0 s however large it is.
		QString problem = QStringLiteral("the run left no replay");
		const bool whole = !w.m_completedReplay.empty() && w.m_replayBar->isVisible() && checkReplayWholeRun(w, w.m_completedReplay.lastTime() / 2, problem);
		if (whole)
			std::fprintf(stdout, "E2E_REPLAY_WHOLE_RUN_OK frames=%zu last=%d bytes=%zu budget=%zu\n", w.m_completedReplay.size(), w.m_completedReplay.lastTime(),
				w.m_completedReplay.payloadBytes(), w.m_completedReplay.budgetBytes());
		else
			std::fprintf(stderr, "E2E_REPLAY_WHOLE_RUN_FAIL: %s\n", qPrintable(problem));
		std::fflush(whole ? stdout : stderr);
		QCoreApplication::exit(whole ? 0 : 2);
		return;
	}

	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_OPERATIONAL_COMPLETION"))
		checkOperationalCompletion(w);
}

// Checks the window after a completed run: the table of results, the cleanup of the canvas, the replay row, the frames of the
// replay against the graphics, Follow in the replay, the inspected train, station and signal, and the passenger layer. The
// first completion reruns the case. The second changes the scenario and then opens the case again, each of which must leave
// no graphics of the run.
void MainWindowTestAccess::checkOperationalCompletion(MainWindow& w) {
	// The time headers keep their names. The energy headers are short with the full name as their tooltip.
	const QStringList headers{"Start time (hh:mm:ss)", "End time (hh:mm:ss)", "Travel time (h:mm:ss)", "Energy (kWh)", "Energy with regen (kWh)",
		"Substation (kWh)", "Substation with regen (kWh)"};
	const QStringList energyNames{"Energy consumed (kWh)", "Energy consumed with regenerative braking (kWh)", "Substation request (kWh)",
		"Substation request with regenerative braking (kWh)"};
	for (int i = 0; i < headers.size(); ++i) {
		const auto* header = w.m_runResultsTable ? w.m_runResultsTable->horizontalHeaderItem(4 + i) : nullptr;
		if (!header || header->text() != headers[i] || (i >= 3 && header->toolTip() != energyNames[i - 3])) {
			std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: run results header %d\n", 4 + i);
			QCoreApplication::exit(2);
			return;
		}
	}
	// Names are left-aligned, numbers and times right-aligned.
	for (int row = 0; row < w.m_runResultsTable->rowCount(); ++row) {
		for (int column = 0; column < w.m_runResultsTable->columnCount(); ++column) {
			const QTableWidgetItem* item = w.m_runResultsTable->item(row, column);
			if (!item || bool(item->textAlignment() & Qt::AlignRight) != (column >= 2)) {
				std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: run results alignment in row %d, column %d\n", row, column);
				QCoreApplication::exit(2);
				return;
			}
		}
	}
	const bool clean = w.m_runController->m_runtimeStatus == QStringLiteral("Completed") && w.m_snapshot
		&& (operationalCompletions == 0
			|| w.m_snapshot != operationalFirstFrame)
		&& std::all_of(w.m_presenter->allArcs.cbegin(), w.m_presenter->allArcs.cend(), [](const TrackLineItem* track) {
			   return !track || track->operationalState() == TrackOperationalState::Free;
		   });
	if (!clean || ++operationalCompletions > 2) {
		std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: final frame or overlay cleanup\n");
		QCoreApplication::exit(2);
		return;
	}
	// Completion must not activate historical overlays before an explicit seek.
	const bool replayAvailable = !w.m_completedReplay.empty() && w.m_replayBar->isVisible()
		&& !w.m_replayActive && w.m_completedReplay.lastTime() >= w.m_completedReplay.firstTime();
	if (!replayAvailable) {
		std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: replay unavailable or automatically activated\n");
		QCoreApplication::exit(2);
		return;
	}
	const int first = w.m_completedReplay.firstTime();
	const int last = w.m_completedReplay.lastTime();
	const int middle = first + (last - first) / 2;
	const RunResults completedResults = w.m_runController->m_completedRun->results;
	const auto completedTimetable = w.m_runController->m_completedRun->timetable;
	QString rowProblem;
	if (!checkReplayRow(w, middle, rowProblem)) {
		std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: replay row, %s\n", qPrintable(rowProblem));
		QCoreApplication::exit(2);
		return;
	}
	w.m_replaySlider->setValue(middle);
	const auto expected = w.m_completedReplay.atOrBefore(middle);
	const auto analyticsUnchanged = [&w, &completedResults, &completedTimetable]() {
		const auto sameValues = [](std::initializer_list<std::pair<RunResultValue, RunResultValue>> values) {
			return std::all_of(values.begin(), values.end(), [](const auto& pair) {
				return pair.first.available == pair.second.available
					&& pair.first.value == pair.second.value;
			});
		};
		if (!w.m_runController->m_completedRun)
			return false;
		const auto& now = w.m_runController->m_completedRun->results;
		if (now.trains.size() != completedResults.trains.size()
			|| w.m_runController->m_completedRun->timetable.size() != completedTimetable.size()
			|| !sameValues({{now.networkStartSeconds, completedResults.networkStartSeconds},
				{now.networkEndSeconds, completedResults.networkEndSeconds},
				{now.networkTravelSeconds, completedResults.networkTravelSeconds},
				{now.energyConsumedKWh, completedResults.energyConsumedKWh},
				{now.energyWithRegenKWh, completedResults.energyWithRegenKWh},
				{now.substationKWh, completedResults.substationKWh},
				{now.substationWithRegenKWh, completedResults.substationWithRegenKWh}})) return false;
		for (std::size_t i = 0; i < now.trains.size(); ++i) {
			const auto& a = now.trains[i];
			const auto& b = completedResults.trains[i];
			if (a.trainId != b.trainId || a.operatingCode != b.operatingCode
				|| a.serviceId != b.serviceId || a.occurrence != b.occurrence
				|| a.performancePercent != b.performancePercent
				|| a.hasConfiguredMaximumSpeed != b.hasConfiguredMaximumSpeed
				|| a.configuredMaximumSpeedKmh != b.configuredMaximumSpeedKmh
				|| a.compositionMaximumSpeedMs != b.compositionMaximumSpeedMs
				|| a.appliedMaximumSpeedMs != b.appliedMaximumSpeedMs
				|| a.appliedMaximumSpeedKmh != b.appliedMaximumSpeedKmh
				|| a.directIncidentIds != b.directIncidentIds
				|| a.destinationTerminationRequested != b.destinationTerminationRequested
				|| a.destinationTerminated != b.destinationTerminated
				|| !sameValues({{a.startSeconds, b.startSeconds}, {a.endSeconds, b.endSeconds},
					{a.travelSeconds, b.travelSeconds}, {a.energyConsumedKWh, b.energyConsumedKWh},
					{a.energyWithRegenKWh, b.energyWithRegenKWh},
					{a.substationKWh, b.substationKWh},
					{a.substationWithRegenKWh, b.substationWithRegenKWh},
					{a.firstDirectIncidentTime, b.firstDirectIncidentTime},
					{a.firstDirectIncidentLocation, b.firstDirectIncidentLocation}})) return false;
		}
		for (std::size_t i = 0; i < completedTimetable.size(); ++i) {
			const auto& a = w.m_runController->m_completedRun->timetable[i];
			const auto& b = completedTimetable[i];
			if (a.trainId != b.trainId || a.operatingCode != b.operatingCode
				|| a.serviceId != b.serviceId || a.occurrence != b.occurrence
				|| a.stationId != b.stationId || a.journeyIndex != b.journeyIndex
				|| a.callIndex != b.callIndex
				|| !sameValues({{a.plannedArrivalSeconds, b.plannedArrivalSeconds},
					{a.plannedDepartureSeconds, b.plannedDepartureSeconds},
					{a.simulatedArrivalSeconds, b.simulatedArrivalSeconds},
					{a.simulatedDepartureSeconds, b.simulatedDepartureSeconds},
					{a.arrivalDelaySeconds, b.arrivalDelaySeconds},
					{a.departureDelaySeconds, b.departureDelaySeconds}})) return false;
		}
		return true;
	};
	const auto coherent = [&w, &analyticsUnchanged](const std::shared_ptr<const GuiSimulationSnapshot>& frame) {
		if (!analyticsUnchanged()) return false;
		if (w.m_snapshot != frame) return false;
		for (const auto& state : frame->trains) {
			TrainItemGroup* item = w.resolveTrainItem(state.index);
			TrainBadgeItem* badge = w.m_presenter->m_trainBadges.value(state.index, nullptr);
			if (!guiReplayTrainHasPosition(state, frame->timestep)) {
				if ((item && item->isVisible()) || (badge && badge->isVisible())) return false;
				continue;
			}
			if (!item || !item->trainPolygonItemList) return false;
			bool drawable = false;
			for (int wagon = 0; wagon <= state.wagonCount; ++wagon) {
				QPolygonF expectedPolygon;
				w.m_presenter->getTrainPolygon(&expectedPolygon, wagon, state);
				if (wagon >= item->trainPolygonItemList->size()
					|| item->trainPolygonItemList->at(wagon)->polygon() != expectedPolygon)
					return false;
				drawable |= !expectedPolygon.isEmpty();
			}
			if (drawable && (!item->isVisible() || !badge || !badge->isVisible())) return false;
		}
		for (const auto& signal : frame->signalStates) {
			auto it = w.m_presenter->m_signalsByAheadId.find(signal.sectionId);
			if (it == w.m_presenter->m_signalsByAheadId.end()) continue;
			for (const auto* item : it->second)
				if (item && item->reversedDirection == signal.reversedDirection
					&& (item->aspectCode() != guiSignalDisplayCode(signal)
						|| item->failed() != signal.failed)) return false;
		}
		for (const auto& section : frame->sectionStates) {
			auto it = w.m_presenter->m_tracksBySectionId.find(section.sectionId);
			if (it == w.m_presenter->m_tracksBySectionId.end()) continue;
			for (auto* track : it->second)
				if (track && section.blocked && track->operationalState() != TrackOperationalState::Blocked)
					return false;
		}
		return w.m_presenter->m_trainAnimations.isEmpty();
	};
	if (!coherent(expected)) {
		std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: middle replay incoherent\n");
		QCoreApplication::exit(2);
		return;
	}
	// Complete the newly shown replay/results layout before pointer input.
	QApplication::processEvents();
	w.m_replaySlider->setFocus();
	QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
	QApplication::sendEvent(w.m_replaySlider, &right);
	const bool keyboardSeek = w.m_replayRequestedTime == middle + 1;
	QWheelEvent wheel(QPointF(w.m_replaySlider->rect().center()),
		QPointF(w.m_replaySlider->mapToGlobal(w.m_replaySlider->rect().center())),
		QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
	QApplication::sendEvent(w.m_replaySlider, &wheel);
	const bool wheelSeek = w.m_replayRequestedTime != middle + 1;
	w.seekReplay(first);
	const QPoint replayClick(w.m_replaySlider->width() - 8, w.m_replaySlider->height() / 2);
	QMouseEvent click(QEvent::MouseButtonPress, replayClick, w.m_replaySlider->mapToGlobal(replayClick),
		Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(w.m_replaySlider, &click);
	const bool mouseSeek = w.m_replayRequestedTime > first;
	QMouseEvent release(QEvent::MouseButtonRelease, replayClick, w.m_replaySlider->mapToGlobal(replayClick),
		Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(w.m_replaySlider, &release);
	w.seekReplay(first);
	if (!keyboardSeek || !wheelSeek || !mouseSeek
		|| !coherent(w.m_completedReplay.atOrBefore(first))) {
		std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: beginning replay incoherent (key=%d wheel=%d mouse=%d frame=%d slider=%dx%d visible=%d enabled=%d)\n",
			keyboardSeek, wheelSeek, mouseSeek, coherent(w.m_completedReplay.atOrBefore(first)),
			w.m_replaySlider->width(), w.m_replaySlider->height(),
			w.m_replaySlider->isVisible(), w.m_replaySlider->isEnabled());
		QCoreApplication::exit(2);
		return;
	}
	const auto firstFrame = w.m_completedReplay.atOrBefore(first);
	const auto future = std::find_if(firstFrame->trains.cbegin(), firstFrame->trains.cend(),
		[first, last](const GuiTrainState& state) {
			return state.departureTime > first && state.departureTime < last;
		});
	if (future == firstFrame->trains.cend()) {
		std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: no future departure fixture\n");
		QCoreApplication::exit(2);
		return;
	}
	{
		// Follow a train through its whole run in the replay: before it enters, while it runs and after
		// it has left. The train list keeps the identity of the chosen train, the sentence follows the
		// displayed time, and the view moves to the train only while it runs.
		const auto stateIn = [](const std::shared_ptr<const GuiSimulationSnapshot>& frame, int index) -> const GuiTrainState* {
			const auto found = std::find_if(frame->trains.cbegin(), frame->trains.cend(),
				[index](const GuiTrainState& state) { return state.index == index; });
			return found == frame->trains.cend() ? nullptr : &*found;
		};
		const GuiTrainState* followedState = nullptr;
		QPointF followedCentre;
		for (const GuiTrainState& candidate : firstFrame->trains) {
			if (candidate.departureTime <= first || candidate.departureTime >= last)
				continue;
			const auto running = w.m_completedReplay.atOrBefore(candidate.departureTime + GuiReplayHistory::cadenceSeconds);
			const GuiTrainState* during = stateIn(running, candidate.index);
			QPolygonF body;
			if (during)
				w.m_presenter->getTrainPolygon(&body, 0, *during);
			if (during && guiReplayTrainHasPosition(*during, running->timestep) && !body.isEmpty()) {
				followedState = &candidate;
				followedCentre = body.boundingRect().center();
				break;
			}
		}
		if (!followedState) {
			std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: no train that enters within the replay\n");
			QCoreApplication::exit(2);
			return;
		}
		const int followedIndex = followedState->index;
		const int enterTime = followedState->departureTime;
		const int followedRow = w.m_followTrainCombo->findData(followedIndex);
		const QString name = w.m_followTrainCombo->itemData(followedRow, kFollowNameRole).toString();
		const QString scheduled = QString::fromStdString(formatSimTime(enterTime, w.m_runController->m_startOffsetSeconds));
		const QString notEntered = name + " is scheduled to enter at " + scheduled + ". ";
		const auto viewCentre = [&w]() { return w.networkView->mapToScene(w.networkView->viewport()->rect().center()); };
		const auto pixelsBetween = [&w](const QPointF& from, const QPointF& to) {
			return QLineF(w.networkView->mapFromScene(from), w.networkView->mapFromScene(to)).length();
		};
		// The view is centred on the train as far as the scene allows.
		const auto viewOnTrain = [&w, &viewCentre, &pixelsBetween](int index) {
			TrainItemGroup* item = w.resolveTrainItem(index);
			if (!item)
				return false;
			const QPointF actual = viewCentre();
			w.networkView->centerOn(item->sceneBoundingRect().center());
			return pixelsBetween(actual, viewCentre()) <= 2.0;
		};
		// The corner of the scene that is farthest from the train, where the user can leave the view.
		const auto cornerAwayFromTrain = [&w, followedCentre]() {
			const QRectF bounds = w.networkView->sceneRect();
			const QPointF corners[] = {bounds.topLeft(), bounds.topRight(), bounds.bottomLeft(), bounds.bottomRight()};
			return *std::max_element(std::begin(corners), std::end(corners), [followedCentre](const QPointF& a, const QPointF& b) {
				return QLineF(a, followedCentre).length() < QLineF(b, followedCentre).length();
			});
		};
		const auto stillFollowing = [&w, followedIndex]() {
			return w.m_followAction->isChecked() && w.m_followTrainIndex == followedIndex
				&& w.m_followTrainCombo->currentData().toInt() == followedIndex;
		};
		// The status bar shows one clause; its tooltip holds the whole explanation.
		const auto sentence = [&w]() { return w.m_followStatusLabel->toolTip(); };
		const auto fail = [](const char* what) {
			std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: historical Follow, %s\n", what);
			QCoreApplication::exit(2);
		};

		// Before the entry: the view stays where it is, also when the time moves on. The item of the train
		// is hidden but still holds the geometry of its run, so a view that moved to it would show where the
		// train was and not where it is.
		w.seekReplay(enterTime + GuiReplayHistory::cadenceSeconds);
		w.seekReplay(first);
		w.networkView->fitToTopology();
		w.networkView->zoomBy(8.0);
		w.networkView->centerOn(cornerAwayFromTrain());
		const QPointF cameraBefore = viewCentre();
		const TrainItemGroup* staleItem = w.resolveTrainItem(followedIndex);
		if (!staleItem || staleItem->isVisible() || !w.trainHasGeometry(followedIndex)
			|| pixelsBetween(cameraBefore, staleItem->sceneBoundingRect().center()) <= 20.0)
			return fail("before the entry, the train has no hidden item with the geometry of its run away from the view");
		w.setFollowTrain(followedIndex);
		const bool waiting = stillFollowing() && w.statusBar()->currentMessage() == name + " enters at " + scheduled
			&& w.m_followStatusLabel->text() == w.statusBar()->currentMessage()
			&& w.m_followTrainCombo->itemText(followedRow) == name + " (scheduled " + scheduled + ")"
			&& std::none_of(w.m_presenter->m_stationOverlays.cbegin(), w.m_presenter->m_stationOverlays.cend(),
				[](const StationOverlayItem* overlay) { return overlay && overlay->isFollowed(); });
		w.seekReplay(enterTime - 1);
		if (!waiting || !stillFollowing() || sentence() != notEntered + "Follow starts when it enters."
			|| pixelsBetween(cameraBefore, viewCentre()) > 0.5)
			return fail("before the entry");
		std::fprintf(stdout, "E2E_FOLLOW_REPLAY_BEFORE_OK\n");
		std::fflush(stdout);

		// While the train runs: the view moves to it.
		w.seekReplay(enterTime + GuiReplayHistory::cadenceSeconds);
		TrainItemGroup* followed = w.resolveTrainItem(followedIndex);
		if (!followed || !followed->isVisible() || !stillFollowing() || sentence() != name + " is running. The view follows it."
			|| w.m_followTrainCombo->itemText(followedRow) != name + " (running)" || pixelsBetween(cameraBefore, viewCentre()) <= 20.0
			|| !viewOnTrain(followedIndex))
			return fail("while the train runs");
		// Seeking within the run changes no state, so it leaves the list alone.
		const FollowListUnderFrames listUnderFrames = showFramesOverMarkedList(w.m_followTrainCombo, [&w, enterTime]() {
			w.seekReplay(enterTime + 2 * GuiReplayHistory::cadenceSeconds);
			w.seekReplay(enterTime + GuiReplayHistory::cadenceSeconds);
		});
		w.setStartOffset(w.m_runController->m_startOffsetSeconds); // writes every row again
		if (listUnderFrames.changes != 0 || !listUnderFrames.rowsKept)
			return fail("while the train runs, the list was written without a change of state");
		std::fprintf(stdout, "E2E_FOLLOW_REPLAY_DURING_OK\n");
		std::fflush(stdout);

		// A seek and a step of the playback cut to the train: the view is on the train at once and no glide
		// runs, also when the user has panned the view away from the train.
		panViewAway(w.networkView);
		const QPointF cameraPanned = viewCentre();
		w.seekReplay(enterTime + 2 * GuiReplayHistory::cadenceSeconds);
		const bool seekCut = !w.m_followCamera->running() && pixelsBetween(cameraPanned, viewCentre()) > 20.0 && viewOnTrain(followedIndex);
		panViewAway(w.networkView);
		const QPointF cameraPannedAgain = viewCentre();
		w.m_replayTimer->start();
		const bool stepDelivered = QMetaObject::invokeMethod(w.m_replayTimer, "timeout");
		w.m_replayTimer->stop();
		const bool stepCut = stepDelivered && !w.m_followCamera->running() && pixelsBetween(cameraPannedAgain, viewCentre()) > 20.0
			&& viewOnTrain(followedIndex);
		if (!seekCut || !stepCut)
			return fail("a seek or a step of the playback did not cut to the train");
		std::fprintf(stdout, "E2E_FOLLOW_CAMERA_REPLAY_OK\n");
		std::fflush(stdout);

		// The Trains layer off: the sentence and the list say so, the station emphasis goes, and the view stays
		// where the user put it, also when the time moves on. On again: the view returns to the train.
		const auto stationEmphasised = [&w]() {
			return std::any_of(w.m_presenter->m_stationOverlays.cbegin(), w.m_presenter->m_stationOverlays.cend(),
				[](const StationOverlayItem* overlay) { return overlay && overlay->isFollowed(); });
		};
		const bool emphasisBefore = stationEmphasised();
		w.m_trainLayerCheck->setChecked(false);
		const bool emphasisGone = emphasisBefore && !stationEmphasised();
		w.networkView->centerOn(cornerAwayFromTrain());
		const QPointF cameraHidden = viewCentre();
		w.seekReplay(enterTime + 2 * GuiReplayHistory::cadenceSeconds);
		const bool hiddenShown = stillFollowing()
			&& sentence() == name + " is running, but the Trains layer is switched off, so the view does not follow it."
			&& w.m_followTrainCombo->itemText(followedRow) == name + " (hidden)" && pixelsBetween(cameraHidden, viewCentre()) <= 0.5
			&& emphasisGone && !stationEmphasised();
		w.m_trainLayerCheck->setChecked(true);
		if (!hiddenShown || !stillFollowing() || sentence() != name + " is running. The view follows it." || !stationEmphasised()
			|| !viewOnTrain(followedIndex))
			return fail("with the Trains layer off");
		w.seekReplay(enterTime + GuiReplayHistory::cadenceSeconds);
		std::fprintf(stdout, "E2E_FOLLOW_REPLAY_LAYER_OK\n");
		std::fflush(stdout);

		// With Follow on, the arrow keys of the list move Follow to the train they choose.
		w.m_followTrainCombo->setFocus();
		const int otherRow = followedRow + 1 < w.m_followTrainCombo->count() ? followedRow + 1 : followedRow - 1;
		QKeyEvent step(QEvent::KeyPress, otherRow > followedRow ? Qt::Key_Down : Qt::Key_Up, Qt::NoModifier);
		QApplication::sendEvent(w.m_followTrainCombo, &step);
		const bool retargeted = w.m_followTrainCombo->currentIndex() == otherRow && w.m_followAction->isChecked()
			&& w.m_followTrainIndex == w.m_followTrainCombo->itemData(otherRow).toInt()
			&& sentence().startsWith(w.m_followTrainCombo->itemData(otherRow, kFollowNameRole).toString() + " ");
		w.m_followTrainCombo->setCurrentIndex(followedRow);
		if (!retargeted || !stillFollowing() || sentence() != name + " is running. The view follows it." || !viewOnTrain(followedIndex))
			return fail("with the keyboard while following");
		std::fprintf(stdout, "E2E_FOLLOW_SELECT_ON_OK\n");
		std::fflush(stdout);

		// After the train has left: Follow stays on, the view stays where the user put it,
		// and going back to the run continues the following. No train of this run leaves within
		// its time, so the frame in which the train has left is a copy of the frame it runs in.
		auto leftFrame = std::make_shared<GuiSimulationSnapshot>(*w.m_snapshot);
		for (GuiTrainState& state : leftFrame->trains)
			if (state.index == followedIndex)
				state.outOfSimulation = true;
		w.networkView->centerOn(cornerAwayFromTrain());
		const QPointF cameraAway = viewCentre();
		w.m_snapshot = leftFrame;
		w.renderSnapshot(true);
		if (!stillFollowing()
			|| sentence() != name + " has left the network at this time. Follow stays on and continues when you go back in the replay."
			|| w.m_followTrainCombo->itemText(followedRow) != name + " (finished)" || !w.m_followAction->isEnabled()
			|| pixelsBetween(cameraAway, viewCentre()) > 0.5)
			return fail("after the train has left");
		// The camera has forgotten the train, so no later zoom or Fit can move the view to it.
		if (w.m_followCamera->hasTarget() || w.m_followCamera->running())
			return fail("after the train has left, the camera still holds the train");
		w.seekReplay(enterTime + GuiReplayHistory::cadenceSeconds);
		if (!stillFollowing() || sentence() != name + " is running. The view follows it." || !viewOnTrain(followedIndex))
			return fail("after going back");
		std::fprintf(stdout, "E2E_FOLLOW_REPLAY_AFTER_OK\n");
		std::fflush(stdout);
		w.setFollowTrain(-1);
		if (w.m_followAction->isChecked() || sentence() != name + " is running. Follow can be switched on now.")
			return fail("after switching off");

		// With Follow off, the arrow keys of the train list change the sentence and keep the identity.
		w.seekReplay(first);
		w.m_followTrainCombo->setCurrentIndex(0);
		w.m_followTrainCombo->setFocus();
		QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
		QApplication::sendEvent(w.m_followTrainCombo, &down);
		const QString nextName = w.m_followTrainCombo->itemData(1, kFollowNameRole).toString();
		if (w.m_followTrainCombo->currentIndex() != 1 || w.m_followTrainCombo->currentData().toInt() != 1 || w.m_followTrainIndex != -1
			|| !sentence().startsWith(nextName + " ") || w.m_followAction->isChecked())
			return fail("with the keyboard");
		std::fprintf(stdout, "E2E_FOLLOW_SELECT_OK\n");
		std::fflush(stdout);
		// The layer check below needs the item and the badge of the future train, which exist once it has run.
		w.seekReplay(future->departureTime + GuiReplayHistory::cadenceSeconds);
	}
	w.seekReplay(future->departureTime - 1);
	w.m_trainLayerCheck->setChecked(false);
	w.m_trainLayerCheck->setChecked(true);
	if (auto* item = w.resolveTrainItem(future->index); !item || item->isVisible()
		|| !w.m_presenter->m_trainBadges.value(future->index)
		|| w.m_presenter->m_trainBadges.value(future->index)->isVisible()) {
		std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: layer restored future train or lost fixture\n");
		QCoreApplication::exit(2);
		return;
	}
	std::shared_ptr<const GuiSimulationSnapshot> activeFrame;
	int activeIndex = -1;
	for (int time = first; time <= last && activeIndex < 0; time += GuiReplayHistory::cadenceSeconds) {
		const auto frame = w.m_completedReplay.atOrBefore(time);
		for (const auto& train : frame->trains) {
			if (!guiReplayTrainHasPosition(train, frame->timestep)) continue;
			QPolygonF polygon;
			w.m_presenter->getTrainPolygon(&polygon, 0, train);
			if (polygon.isEmpty()) continue;
			activeFrame = frame;
			activeIndex = train.index;
			break;
		}
	}
	if (!activeFrame) {
		std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: no drawable active train fixture\n");
		QCoreApplication::exit(2);
		return;
	}
	w.seekReplay(activeFrame->timestep);
	if (!w.resolveTrainItem(activeIndex) || !w.resolveTrainItem(activeIndex)->isVisible()
		|| !coherent(activeFrame)) {
		std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: active train missing\n");
		QCoreApplication::exit(2);
		return;
	}
	// Simulate a later exit without changing immutable history, then seek back.
	auto exitedFrame = std::make_shared<GuiSimulationSnapshot>(*activeFrame);
	for (auto& state : exitedFrame->trains)
		if (state.index == activeIndex) {
			state.outOfSimulation = true;
			state.occupiedArcs.clear();
		}
	w.m_snapshot = exitedFrame;
	w.renderSnapshot(true);
	const bool hiddenAtExit = !w.resolveTrainItem(activeIndex)->isVisible();
	w.seekReplay(activeFrame->timestep);
	if (!hiddenAtExit || !w.resolveTrainItem(activeIndex)
		|| !w.resolveTrainItem(activeIndex)->isVisible() || !coherent(activeFrame)) {
		std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: exited train did not return at sampled geometry\n");
		QCoreApplication::exit(2);
		return;
	}
	TrainBodyItem* bodySelection = w.resolveTrainBodyItem(activeIndex);
	if (!bodySelection) {
		std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: no train inspector fixture\n");
		QCoreApplication::exit(2);
		return;
	}
	w.m_inspector->displayTrainDetails(bodySelection, false);
	w.seekReplay(last);
	w.seekReplay(activeFrame->timestep);
	if (w.m_inspector->selectedTrainIndex() != activeIndex || !w.m_inspector->trainInfoWidget->isVisible()
		|| !w.resolveTrainItem(activeIndex)->graphicsEffect()) {
		std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: train inspection lost\n");
		QCoreApplication::exit(2);
		return;
	}
	if (initial_variables.PAX_GUI) {
		if (w.m_presenter->trainPaxItem != w.resolveTrainItem(activeIndex) || !w.m_presenter->trainPaxInfoItem
			|| !w.m_presenter->trainPaxInfoItem->isVisible()) {
			std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: no passenger inspection fixture\n");
			QCoreApplication::exit(2);
			return;
		}
		w.m_inspector->handleCloseInfoDockWidget();
		w.seekReplay(activeFrame->timestep + GuiReplayHistory::cadenceSeconds);
		if (w.m_presenter->trainPaxItem || w.m_presenter->trainPaxInfoItem) {
			std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: closed passenger inspection returned\n");
			QCoreApplication::exit(2);
			return;
		}
		w.seekReplay(future->departureTime + GuiReplayHistory::cadenceSeconds);
		TrainBodyItem* futureBody = w.resolveTrainBodyItem(future->index);
		if (!futureBody) {
			std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: future passenger train missing\n");
			QCoreApplication::exit(2);
			return;
		}
		w.m_inspector->displayTrainDetails(futureBody, false);
		TrainItemGroup* futureTarget = w.resolveTrainItem(future->index);
		w.seekReplay(future->departureTime - 1);
		if (w.m_presenter->trainPaxItem != futureTarget || w.m_presenter->trainPaxInfoItem) {
			std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: temporary absence lost target\n");
			QCoreApplication::exit(2);
			return;
		}
		w.seekReplay(future->departureTime + GuiReplayHistory::cadenceSeconds);
		if (w.m_presenter->trainPaxItem != futureTarget || !w.m_presenter->trainPaxInfoItem || !w.m_presenter->trainPaxInfoItem->isVisible()) {
			std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: passenger inspection did not return\n");
			QCoreApplication::exit(2);
			return;
		}
		w.m_passengerLayerCheck->setChecked(false);
		w.seekReplay(future->departureTime - 1);
		w.seekReplay(future->departureTime + GuiReplayHistory::cadenceSeconds);
		if (w.m_presenter->trainPaxItem != futureTarget || w.m_presenter->trainPaxInfoItem) {
			std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: disabled passenger layer repainted overlay\n");
			QCoreApplication::exit(2);
			return;
		}
		w.m_passengerLayerCheck->setChecked(true);
	}
	w.seekReplay(middle);
	if (!coherent(expected)) {
		std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: end-to-middle replay or analytics incoherent\n");
		QCoreApplication::exit(2);
		return;
	}
	StationOverlayItem* stationSelection = nullptr;
	for (auto* overlay : w.m_presenter->m_stationOverlays)
		if (overlay && overlay->hasSourceIdentity()
			&& w.resolveStationNodeItem(overlay->sourceNodeId(), overlay->sourceTrack())) {
			stationSelection = overlay;
			break;
		}
	if (!stationSelection) {
		std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: no station selection fixture\n");
		QCoreApplication::exit(2);
		return;
	}
	w.m_inspector->displayStationNodeInfo(w.resolveStationNodeItem(
		stationSelection->sourceNodeId(), stationSelection->sourceTrack()));
	w.seekReplay(first);
	if (!stationSelection->isSelected() || !w.m_inspector->m_hasSelectedStationIdentity
		|| !w.m_inspector->dock()->isVisible()
		|| (initial_variables.PAX_GUI && (w.m_presenter->trainPaxItem || w.m_presenter->trainPaxInfoItem))) {
		std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: station selection lost\n");
		QCoreApplication::exit(2);
		return;
	}
	SignalItem* signalSelection = nullptr;
	for (auto* decoration : w.m_presenter->m_signalDecorations)
		if (auto* signal = qgraphicsitem_cast<SignalItem*>(decoration);
			signal && signal->isVisible() && signal->groupedSignalCount() == 0) {
			signalSelection = signal;
			break;
		}
	if (!signalSelection) {
		std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: no individual signal inspection fixture\n");
		QCoreApplication::exit(2);
		return;
	}
	w.m_inspector->displaySignallingInfo(signalSelection);
	w.seekReplay(middle);
	w.m_replayTimer->start();
	const bool timerDelivered = QMetaObject::invokeMethod(w.m_replayTimer, "timeout");
	w.m_replayTimer->stop();
	const QString inspectedAspect = signalAspectLabel(signalSelection);
	if (!timerDelivered || w.m_presenter->m_inspectedSignal != signalSelection
		|| !w.m_inspector->signallingInfoWidget->isVisible() || !signalSelection->graphicsEffect()
		|| (initial_variables.PAX_GUI && (w.m_presenter->trainPaxItem || w.m_presenter->trainPaxInfoItem))
		|| w.m_inspector->signallingGroupDetails->toPlainText().isEmpty()
		|| w.m_inspector->signallingAspectText->text() != inspectedAspect
		|| !coherent(w.m_completedReplay.atOrBefore(w.m_replayRequestedTime))) {
		std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: signal inspection lost on seek/play (timer=%d selected=%d visible=%d effect=%d details=%d coherent=%d)\n",
			timerDelivered, w.m_presenter->m_inspectedSignal == signalSelection, w.m_inspector->signallingInfoWidget->isVisible(),
			signalSelection->graphicsEffect() != nullptr, !w.m_inspector->signallingGroupDetails->toPlainText().isEmpty(),
			coherent(w.m_completedReplay.atOrBefore(w.m_replayRequestedTime)));
		QCoreApplication::exit(2);
		return;
	}
	w.m_inspector->signallingGroupDetails->selectAll();
	const QString selectedDetails = w.m_inspector->signallingGroupDetails->textCursor().selectedText();
	const int detailScroll = w.m_inspector->signallingGroupDetails->verticalScrollBar()->value();
	w.updateSignalCues();
	if (w.m_inspector->signallingGroupDetails->textCursor().selectedText() != selectedDetails
		|| w.m_inspector->signallingGroupDetails->verticalScrollBar()->value() != detailScroll) {
		std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: unchanged signal refresh resets inspection selection\n");
		QCoreApplication::exit(2);
		return;
	}
	const auto selectedFrame = w.m_snapshot;
	QMetaObject::invokeMethod(&w, "waitForUpdates", Qt::QueuedConnection);
	QTimer::singleShot(0, &w, [&w, selectedFrame]() {
		if (w.m_snapshot != selectedFrame || simulation.takeSimulationSnapshot()) {
			std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: late notification\n");
			QCoreApplication::exit(2);
			return;
		}
		if (operationalCompletions == 1) {
			operationalFirstFrame = selectedFrame;
			w.m_replayTimer->start();
			w.m_runController->runCurrent();
			if (!w.m_runController->m_worker || w.m_replayTimer->isActive() || !w.m_completedReplay.empty()) {
				std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: rerun retained replay\n");
				QCoreApplication::exit(2);
			}
			return;
		}
		const QString scenePath = qEnvironmentVariable("QEGTRAIN_E2E_OPERATIONAL_COMPLETION");
		if (w.m_document->mutableModel().scenarios.empty()) {
			std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: no scenario fixture\n");
			QCoreApplication::exit(2);
			return;
		}
		auto alternate = w.m_document->mutableModel().scenarios.front();
		alternate.id = "__replay_e2e_alternate__";
		w.m_document->mutableModel().scenarios.push_back(alternate);
		w.setFollowTrain(w.m_followTrainCombo->itemData(0).toInt());
		if (w.m_followTrainIndex != w.m_followTrainCombo->itemData(0).toInt()) {
			std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: Follow in the replay before the scenario changes\n");
			QCoreApplication::exit(2);
			return;
		}
		w.m_replayTimer->start();
		w.m_scenarioPanel->selectScenario(static_cast<int>(w.m_document->mutableModel().scenarios.size()) - 1);
		if (w.m_replayTimer->isActive() || !w.m_completedReplay.empty() || w.m_snapshot
			|| !w.m_showingTrackPreview || !w.m_presenter->allTrains.isEmpty() || !w.m_presenter->m_activeTrackItems.empty()
			|| w.m_followTrainIndex >= 0 || w.m_followCamera->hasTarget() || w.m_followCamera->running() || w.m_replayBar->isVisible()) {
			std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: scenario retained historical graphics\n");
			QCoreApplication::exit(2);
			return;
		}
		// The trains of the run are gone, so Follow is off and says that the case has to run.
		const QString runFirst = QStringLiteral("Run the case first, then you can follow a train.");
		if (w.m_followTrainCombo->count() != 1 || w.m_followTrainCombo->currentData().toInt() != -1
			|| w.m_followTrainCombo->currentText() != "Run the case to follow trains" || w.m_followAction->isEnabled()
			|| w.m_followAction->isChecked() || w.m_followStatusLabel->toolTip() != runFirst || w.m_followAction->toolTip() != runFirst) {
			std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: Follow after the scenario changed\n");
			QCoreApplication::exit(2);
			return;
		}
		std::fprintf(stdout, "E2E_FOLLOW_RESET_OK\n");
		std::fflush(stdout);
		QMetaObject::invokeMethod(&w, "waitForUpdates", Qt::QueuedConnection);
		w.m_replayTimer->start();
		if (!w.openSceneDirectory(scenePath) || w.m_snapshot || w.m_runController->m_worker || !w.m_presenter->allArcs.isEmpty()
			|| w.m_replayTimer->isActive() || !w.m_completedReplay.empty()) {
			std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: case teardown\n");
			QCoreApplication::exit(2);
			return;
		}
		QTimer::singleShot(0, &w, [&w]() {
			if (w.m_snapshot || simulation.takeSimulationSnapshot()) {
				std::fprintf(stderr, "E2E_OPERATIONAL_COMPLETION_FAIL: stale case notification\n");
				QCoreApplication::exit(2);
				return;
			}
			std::fprintf(stdout, "E2E_OPERATIONAL_COMPLETION_OK\n");
			std::fflush(stdout);
			QCoreApplication::exit(0);
		});
	});
}

// Checks the replay row of a run that was kept whole, as the completion wrote it: the slider covers
// 0 s to the last time, which is the end of the run, the row says so, the texts follow a selected
// frame, Start goes to 0 s and the row keeps its size while the selection moves.
bool MainWindowTestAccess::checkReplayWholeRun(MainWindow& w, int middle, QString& failure) {
	const int last = w.m_completedReplay.lastTime();
	const QString cadence = QString::number(GuiReplayHistory::cadenceSeconds);
	const auto fail = [&failure](const QString& why) {
		failure = why;
		return false;
	};
	const QString whole = QStringLiteral("Whole run, 0 to %1 s, a frame every %2 s.").arg(last).arg(cadence);
	// The label names the selected time as the clock time of the progress bar.
	const auto clock = [&w](int time) { return QString::fromStdString(formatSimTime(time, w.m_runController->m_startOffsetSeconds)); };
	if (w.m_completedReplay.firstTime() != 0 || w.m_completedReplay.truncated() || !replaySliderCovers(w, 0, last))
		return fail(QStringLiteral("the slider of a whole run does not cover 0 to %1 s").arg(last));
	const auto finalFrame = w.m_completedReplay.atOrBefore(last);
	if (!finalFrame || finalFrame->timestep != last || finalFrame->totalTimesteps - 1 != last)
		return fail(QStringLiteral("the last kept frame, at %1 s, is not the last timestep of a run of %2 timesteps")
				.arg(last)
				.arg(finalFrame ? finalFrame->totalTimesteps : 0));
	if (w.m_replayLabel->text() != QStringLiteral("Replay") || w.m_replayCoverageLabel->text() != whole
		|| w.m_replayStartButton->toolTip() != QStringLiteral("Go to the start of the run"))
		return fail(QStringLiteral("the row of a whole run says '%1' and '%2' with the tooltip '%3'")
				.arg(w.m_replayLabel->text(), w.m_replayCoverageLabel->text(), w.m_replayStartButton->toolTip()));
	w.m_replaySlider->setValue(middle);
	const auto shown = w.m_completedReplay.atOrBefore(middle);
	if (w.m_replayLabel->text() != clock(shown->timestep) || w.m_replayCoverageLabel->text() != whole)
		return fail(QStringLiteral("a whole run says '%1' and '%2' for the frame at %3 s")
				.arg(w.m_replayLabel->text(), w.m_replayCoverageLabel->text())
				.arg(shown->timestep));
	// At Fit the first visible train is at least the minimum size on screen, in the first frame that shows a train.
	const auto firstVisible = [&w] {
		return std::find_if(w.m_presenter->allTrains.cbegin(), w.m_presenter->allTrains.cend(),
			[](const TrainItemGroup* item) { return item && item->isVisible() && item->trainPolygonItemList; });
	};
	for (int time = 0; time <= last && firstVisible() == w.m_presenter->allTrains.cend(); time += GuiReplayHistory::cadenceSeconds)
		w.seekReplay(time);
	w.fitView();
	const QString sceneName = QString::fromStdString(w.m_document->model().name);
	const auto train = firstVisible();
	if (train == w.m_presenter->allTrains.cend())
		return fail(QStringLiteral("%1: no train is visible at Fit in any frame").arg(sceneName));
	for (const TrainBodyItem* body : *(*train)->trainPolygonItemList) {
		if (!body || body->polygon().isEmpty())
			continue;
		const QRectF pixels = w.networkView->viewportTransform().mapRect(body->mapRectToScene(body->shape().boundingRect()));
		if (qMin(pixels.width(), pixels.height()) < TrainBodyItem::MinPixels - 0.5)
			return fail(QStringLiteral("%1: a train body is %2 x %3 logical pixels at Fit, less than %4")
					.arg(sceneName)
					.arg(pixels.width())
					.arg(pixels.height())
					.arg(TrainBodyItem::MinPixels));
	}
	w.m_replayStartButton->click();
	if (w.m_replayRequestedTime != 0 || !w.m_snapshot || w.m_snapshot->timestep != 0 || w.m_replaySlider->value() != 0
		|| w.m_replayLabel->text() != clock(0) || w.m_replayCoverageLabel->text() != whole)
		return fail(QStringLiteral("Start of a whole run did not go to 0 s, the row says '%1' and '%2'")
				.arg(w.m_replayLabel->text(), w.m_replayCoverageLabel->text()));
	const QSize size = replayRowSizeAt(w, 0);
	const QSize sizeInMiddle = replayRowSizeAt(w, middle);
	const QSize sizeAtEnd = replayRowSizeAt(w, last);
	if (sizeInMiddle != size || sizeAtEnd != size)
		return fail(QStringLiteral("the row of a whole run is %1 x %2 at 0 s, %3 x %4 at %5 s and %6 x %7 at %8 s (slider width x row height)")
				.arg(size.width())
				.arg(size.height())
				.arg(sizeInMiddle.width())
				.arg(sizeInMiddle.height())
				.arg(middle)
				.arg(sizeAtEnd.width())
				.arg(sizeAtEnd.height())
				.arg(last));
	std::fprintf(stdout, "E2E_REPLAY_ROW_WHOLE_OK label=\"%s\" slider_width=%d row_height=%d\n", qPrintable(whole), size.width(), size.height());
	std::fflush(stdout);
	return true;
}

bool MainWindowTestAccess::replaySliderCovers(const MainWindow& w, int from, int to) {
	return w.m_replaySlider->minimum() == from && w.m_replaySlider->maximum() == to && w.m_replaySlider->value() == to && w.m_replaySlider->isEnabled()
		&& w.m_replayPlayButton->isEnabled();
}

// Selects the frame at the time and returns the width of the slider and the height of the row once
// the layout has settled. Neither changes with the selected time.
QSize MainWindowTestAccess::replayRowSizeAt(MainWindow& w, int time) {
	w.seekReplay(time);
	QApplication::processEvents();
	QApplication::processEvents();
	return QSize(w.m_replaySlider->width(), w.m_replayBar->height());
}

// Checks the replay row of the finished run: the row of the whole run, and the row for a history
// that dropped its first part and for one that kept no frame. Those histories are built here, the
// first from the frames of the run with a budget that holds about half of them, and go through the
// code that fills the row after a run. The history of the run is put back, and the window shows
// the frame at the middle again. It also checks the width of Play, the Tab order and the focus frames after a run.
bool MainWindowTestAccess::checkReplayRow(MainWindow& w, int middle, QString& failure) {
	if (!checkReplayWholeRun(w, middle, failure))
		return false;
	const int first = w.m_completedReplay.firstTime();
	const int last = w.m_completedReplay.lastTime();
	const QString cadence = QString::number(GuiReplayHistory::cadenceSeconds);
	const auto fail = [&failure](const QString& why) {
		failure = why;
		return false;
	};

	// Start, Play and End show an icon and have a tooltip and a name. While the replay plays, Play shows another icon,
	// tooltip and the name Pause, and keeps its width, so End does not move.
	auto* endButton = w.m_replayBar->findChild<QPushButton*>("replayEndButton");
	const auto iconOf = [](const QPushButton* button) { return button->icon().pixmap(16, 16).toImage(); };
	const auto shows = [&iconOf](const QPushButton* button, const char* name) {
		return button && !iconOf(button).isNull() && !button->toolTip().isEmpty() && button->accessibleName() == QLatin1String(name);
	};
	const bool named = shows(w.m_replayStartButton, "Start") && shows(w.m_replayPlayButton, "Play") && shows(endButton, "End");
	const QImage playIcon = iconOf(w.m_replayPlayButton);
	const QString playTip = w.m_replayPlayButton->toolTip();
	const int endX = endButton ? endButton->x() : -1;
	w.m_replayPlayButton->click();
	QApplication::processEvents();
	QApplication::processEvents();
	const bool paused = shows(w.m_replayPlayButton, "Pause") && iconOf(w.m_replayPlayButton) != playIcon && w.m_replayPlayButton->toolTip() != playTip;
	const bool endKept = endButton && endButton->x() == endX;
	w.m_replayPlayButton->click();
	QApplication::processEvents();
	if (!named || !paused || !shows(w.m_replayPlayButton, "Play") || iconOf(w.m_replayPlayButton) != playIcon)
		return fail(QStringLiteral("Start, Play and End do not show an icon, a tooltip and their name, or Play does not turn into Pause and back"));
	if (!endKept)
		return fail(QStringLiteral("End moved when Play turned into Pause"));
	// Opened in a maximised window on a 1024 by 768 screen, Run Results shows the header and a row of its table without
	// scrolling. In a window of 819 by 614 pixels (1024 by 768 at 125 percent) it takes at most half of the height that it
	// shares with the network view, and the window can be lower than a screen of 683 by 464 pixels (1024 by 768 at 150 percent)
	// less its frame.
	const QSize sizeBefore = w.size();
	const auto openResults = [&w](int width, int height) {
		w.m_runResultsDock->hide();
		w.resize(width, height);
		QApplication::processEvents();
		w.m_runResultsPanel->refresh();
		QApplication::processEvents();
	};
	openResults(1024, 690);
	const auto* resultsScroll = qobject_cast<QScrollArea*>(w.m_runResultsDock->widget());
	const int rowBottom = resultsScroll
		? w.m_runResultsTable->mapTo(resultsScroll->viewport(), QPoint(0, w.m_runResultsTable->viewport()->y() + w.m_runResultsTable->rowHeight(0))).y()
		: -1;
	const int shownHeight = resultsScroll ? resultsScroll->viewport()->height() : -1;
	// A dock on the right, such as the inspector after the signal checks, keeps the window wider than 819 pixels.
	QList<QDockWidget*> rightDocks;
	for (QDockWidget* dock : w.findChildren<QDockWidget*>())
		if (dock->isVisible() && !dock->isFloating() && w.dockWidgetArea(dock) == Qt::RightDockWidgetArea) {
			rightDocks << dock;
			dock->hide();
		}
	QApplication::processEvents();
	openResults(819, 614);
	const QSize smallSize = w.size();
	const int resultsHeight = w.m_runResultsDock->height();
	const int canvasHeight = w.networkView->height();
	const int minimumHeight = w.minimumSizeHint().height();
	const auto* delayCaption = w.findChild<QLabel*>("delayComparisonCaption");
	const bool tableApart = delayCaption && delayCaption->y() > w.m_runResultsTable->geometry().bottom();
	for (QDockWidget* dock : rightDocks)
		dock->show();
	std::fprintf(stdout, "E2E_RUN_RESULTS_SMALL_WINDOW size=%dx%d results=%d canvas=%d minimum_height=%d row_bottom_1024=%d shown_1024=%d\n",
		smallSize.width(), smallSize.height(), resultsHeight, canvasHeight, minimumHeight, rowBottom, shownHeight);
	w.resize(sizeBefore);
	QApplication::processEvents();
	if (rowBottom < 0 || rowBottom > shownHeight)
		return fail(QStringLiteral("the first row of Run Results ends at %1 px in a dock that shows %2 px at 1024 x 690").arg(rowBottom).arg(shownHeight));
	if (!tableApart)
		return fail(QStringLiteral("the delay comparison covers the table of Run Results at 819 x 614"));
	if (smallSize != QSize(819, 614) || resultsHeight > canvasHeight + 2)
		return fail(QStringLiteral("Run Results is %1 px high below a network view of %2 px at %3 x %4")
				.arg(resultsHeight)
				.arg(canvasHeight)
				.arg(smallSize.width())
				.arg(smallSize.height()));
	if (minimumHeight > 400)
		return fail(QStringLiteral("the window after a run needs %1 px of height").arg(minimumHeight));
	// Tab goes Start, Play, End and the slider, reaches the buttons above the table of Run Results before the table, leaves the
	// table for the buttons after it and comes back to Start through the layer check boxes. No stop is a dock tab bar or the
	// scroll area of a dock, which show no focus. The table shows the focus with its frame, the network view with a 2 px frame.
	w.m_replayStartButton->setFocus(Qt::TabFocusReason);
	QApplication::processEvents();
	QList<QWidget*> order;
	QString tableEdge;
	QString canvasEdge;
	QWidget* focused = QApplication::focusWidget();
	while (focused && order.size() < 60 && !order.contains(focused)) {
		order << focused;
		if (focused == w.m_runResultsTable || focused == w.networkView) {
			const QImage shown = focused->grab().toImage();
			QString& edge = focused == w.networkView ? canvasEdge : tableEdge;
			edge = shown.pixelColor(0, shown.height() / 2).name() + shown.pixelColor(1, shown.height() / 2).name();
		}
		QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
		QApplication::sendEvent(focused, &tab);
		QApplication::processEvents();
		focused = QApplication::focusWidget();
	}
	const QList<QWidget*> replayRow{w.m_replayStartButton, w.m_replayPlayButton, endButton, w.m_replaySlider};
	const QList<QWidget*> results{w.findChild<QWidget*>("runResultsViewsButton"), w.findChild<QWidget*>("resultView_ExportCSV"),
		w.findChild<QWidget*>("resultView_ExportPNG"), w.m_runResultsTable};
	const int views = order.indexOf(results.first());
	QStringList names;
	for (QWidget* widget : order)
		names << QStringLiteral("%1(%2)").arg(widget->metaObject()->className(), widget->objectName());
	if (focused != w.m_replayStartButton || order.mid(0, replayRow.size()) != replayRow || views < 0
		|| order.mid(views, results.size()) != results || !order.contains(w.m_signalLayerCheck)
		|| std::any_of(order.cbegin(), order.cend(), [](QWidget* widget) { return qobject_cast<QTabBar*>(widget) || qobject_cast<QScrollArea*>(widget); }))
		return fail(QStringLiteral("the Tab order after a run is %1").arg(names.join(QStringLiteral(", "))));
	if (!tableEdge.startsWith(QLatin1String("#315a70")) || canvasEdge != QLatin1String("#5f8fa8#5f8fa8"))
		return fail(QStringLiteral("the focus frame of the Run Results table is %1 and that of the network view %2").arg(tableEdge, canvasEdge));

	// The same run with its first part dropped.
	std::vector<std::shared_ptr<const GuiSimulationSnapshot>> frames;
	for (int time = first; time < last; time += GuiReplayHistory::cadenceSeconds)
		frames.push_back(w.m_completedReplay.atOrBefore(time));
	frames.push_back(w.m_completedReplay.atOrBefore(last));
	GuiReplayHistory everything;
	for (const auto& frame : frames)
		everything.record(frame);
	GuiReplayHistory kept(everything.payloadBytes() / 2);
	for (const auto& frame : frames)
		kept.record(frame);
	if (everything.truncated() || !kept.truncated() || kept.size() < 2 || kept.firstTime() <= first || kept.lastTime() != last)
		return fail(QStringLiteral("a history with half the budget did not drop only its first part"));

	GuiReplayHistory run = std::move(w.m_completedReplay);
	w.m_completedReplay = std::move(kept);
	w.showReplayBar();
	const auto droppedRow = [&w, last, &cadence]() -> QString {
		const int from = w.m_completedReplay.firstTime();
		const QString memory = QString::fromStdString(replayMemoryText(w.m_completedReplay.budgetBytes()));
		// The second text and the tooltip of Start for a start clock.
		const auto words = [from, last, &cadence, &memory](long long offset) {
			const QString start = QString::fromStdString(formatSimTime(from, offset)) + QStringLiteral(" (%1 s)").arg(from);
			const QString sentence = QStringLiteral("Starts at %1, ends at %2 s, a frame every %3 s. The earlier part was not kept because the run is "
													"larger than the replay memory (%4).")
										 .arg(start)
										 .arg(last)
										 .arg(cadence, memory);
			return std::make_pair(sentence, QStringLiteral("Go to the first kept time, %1. The earlier part of the run was not kept.").arg(start));
		};
		const auto [dropped, tip] = words(w.m_runController->m_startOffsetSeconds);
		const auto clock = [&w](int time) { return QString::fromStdString(formatSimTime(time, w.m_runController->m_startOffsetSeconds)); };
		if (!replaySliderCovers(w, from, last))
			return QStringLiteral("the slider does not cover %1 to %2 s").arg(from).arg(last);
		if (w.m_replayLabel->text() != QStringLiteral("Replay") || w.m_replayCoverageLabel->text() != dropped)
			return QStringLiteral("the row says '%1' and '%2'").arg(w.m_replayLabel->text(), w.m_replayCoverageLabel->text());
		if (w.m_replayStartButton->toolTip() != tip)
			return QStringLiteral("Start says '%1'").arg(w.m_replayStartButton->toolTip());
		const QString row = w.m_replayCoverageLabel->text();
		w.m_replayStartButton->click();
		if (w.m_replayRequestedTime != from || !w.m_snapshot || w.m_snapshot->timestep != from || w.m_replaySlider->value() != from
			|| w.m_replayLabel->text() != clock(from) || w.m_replayCoverageLabel->text() != dropped)
			return QStringLiteral("Start did not go to %1 s, the row says '%2' and '%3'").arg(from).arg(w.m_replayLabel->text(), w.m_replayCoverageLabel->text());
		w.seekReplay(0);
		if (w.m_replayRequestedTime != from || w.m_snapshot->timestep != from)
			return QStringLiteral("a time before the kept interval did not select %1 s").arg(from);
		w.m_replaySlider->setValue(last);
		if (w.m_replayRequestedTime != last || w.m_snapshot->timestep != last || w.m_replayLabel->text() != clock(last)
			|| w.m_replayCoverageLabel->text() != dropped)
			return QStringLiteral("the end of the slider did not select %1 s, the row says '%2' and '%3'")
				.arg(last)
				.arg(w.m_replayLabel->text(), w.m_replayCoverageLabel->text());
		// The long sentence has the width of the row to itself, and the row keeps its size while the selection moves.
		const QSize size = replayRowSizeAt(w, from);
		const QSize sizeAtEnd = replayRowSizeAt(w, last);
		if (sizeAtEnd != size)
			return QStringLiteral("the row with a dropped first part is %1 x %2 at %3 s and %4 x %5 at %6 s (slider width x row height)")
				.arg(size.width())
				.arg(size.height())
				.arg(from)
				.arg(sizeAtEnd.width())
				.arg(sizeAtEnd.height())
				.arg(last);
		w.seekReplay(last);
		// A change of the start time moves the clock in the label, the sentence and the tooltip.
		const long long offsetBefore = w.m_runController->m_startOffsetSeconds;
		w.setStartOffset(offsetBefore + 3600);
		const auto [movedSentence, movedTip] = words(offsetBefore + 3600);
		const bool moved = w.m_replayLabel->text() == clock(last) && w.m_replayCoverageLabel->text() == movedSentence
			&& w.m_replayStartButton->toolTip() == movedTip && movedTip != tip;
		w.setStartOffset(offsetBefore);
		if (!moved || w.m_replayLabel->text() != clock(last) || w.m_replayCoverageLabel->text() != dropped || w.m_replayStartButton->toolTip() != tip)
			return QStringLiteral("a change of the start time did not move the clock in the row");
		std::fprintf(stdout, "E2E_REPLAY_ROW_DROPPED_OK from=%d label=\"%s\" start_tooltip=\"%s\" slider_width=%d row_height=%d\n", from, qPrintable(row),
			qPrintable(tip), size.width(), size.height());
		std::fflush(stdout);
		return QString();
	};
	QString problem = droppedRow();

	// A run of which not one frame fits.
	const auto unavailableRow = [&w]() -> QString {
		GuiReplayHistory refused(1u << 20);
		auto large = std::make_shared<GuiSimulationSnapshot>();
		large->passengers.resize(1);
		large->passengers.front().id = std::string(2u << 20, 'x');
		refused.record(large);
		w.m_completedReplay = std::move(refused);
		w.m_replayBar->hide();
		w.showReplayBar();
		const QString expected = QStringLiteral("One moment of this run needs more than the replay memory (1 MiB).");
		if (!w.m_completedReplay.oversize() || !w.m_completedReplay.empty() || !w.m_replayBar->isVisible() || w.m_replayLabel->text() != QStringLiteral("No replay")
			|| w.m_replayCoverageLabel->text() != expected || w.m_replaySlider->isEnabled() || w.m_replayPlayButton->isEnabled()
			|| w.m_replayStartButton->toolTip() != QStringLiteral("Go to the start of the run"))
			return QStringLiteral("the row says '%1' and '%2', the slider is %3, Play is %4, Start says '%5'")
				.arg(w.m_replayLabel->text(), w.m_replayCoverageLabel->text(), w.m_replaySlider->isEnabled() ? "enabled" : "disabled",
					w.m_replayPlayButton->isEnabled() ? "enabled" : "disabled", w.m_replayStartButton->toolTip());
		// The slider shows no interval of the run before it.
		if (w.m_replaySlider->minimum() != 0 || w.m_replaySlider->maximum() != 0 || w.m_replaySlider->value() != 0)
			return QStringLiteral("the slider still covers %1 to %2 s with the handle at %3 s")
				.arg(w.m_replaySlider->minimum())
				.arg(w.m_replaySlider->maximum())
				.arg(w.m_replaySlider->value());
		std::fprintf(stdout, "E2E_REPLAY_ROW_UNAVAILABLE_OK label=\"No replay\" coverage=\"%s\"\n", qPrintable(expected));
		std::fflush(stdout);
		return QString();
	};
	if (!problem.isEmpty()) {
		problem = QStringLiteral("history with a dropped first part: ") + problem;
	} else {
		problem = unavailableRow();
		if (!problem.isEmpty())
			problem = QStringLiteral("history that cannot be replayed: ") + problem;
	}

	w.m_completedReplay = std::move(run);
	w.showReplayBar();
	w.seekReplay(middle);
	return problem.isEmpty() ? true : fail(problem);
}

// Checks the signal heads against the frames of the replay once the run is
// over: the final live frame, then every replay second backwards and forwards.
// QEGTRAIN_E2E_SIGNAL_HEADS names what the run has to show: "levels" (heads take
// stop, caution and proceed and return to proceed), "failure" (the same, and
// failed heads exactly while a section is blocked), "none" (every head
// unavailable) or "any".
void MainWindowTestAccess::runReplaySignals(MainWindow& w) {
	SignalHeadsState& live = signalHeadsState();
	const QString expected = qEnvironmentVariable("QEGTRAIN_E2E_SIGNAL_HEADS");
	const auto fail = [&live](const QString& why) {
		live.failed = true;
		std::fprintf(stderr, "E2E_SIGNAL_HEADS_FAIL: %s\n", qPrintable(why));
		QCoreApplication::exit(2);
	};
	if (live.failed)
		return;
	if (w.m_completedReplay.empty() || w.m_presenter->allSignals.isEmpty() || !w.m_snapshot)
		return fail(QStringLiteral("no replay or no signal heads"));
	if (live.nextPause < live.pauseSteps.size())
		return fail(QStringLiteral("the run ended before the pause at step %1").arg(live.pauseSteps[live.nextPause]));
	const auto finalFrame = w.m_snapshot;
	if (!signalHeadsShow(w.m_presenter->allSignals, *finalFrame))
		return fail(QStringLiteral("heads do not show the final live frame"));
	std::set<int> frames;
	std::map<const SignalItem*, bool> restricted;
	long stops = 0, cautions = 0, proceeds = 0, unavailable = 0, failedHeads = 0;
	int returned = 0, differFromFinal = 0, failedFrames = 0, blockedFrames = 0, seeks = 0;
	for (const bool backwards : {true, false}) {
		for (int step = 0; step <= w.m_completedReplay.lastTime() - w.m_completedReplay.firstTime(); ++step) {
			const int time = backwards ? w.m_completedReplay.lastTime() - step : w.m_completedReplay.firstTime() + step;
			w.seekReplay(time);
			const auto frame = w.m_completedReplay.atOrBefore(time);
			bool anyFailed = false;
			bool anyBlocked = false;
			if (!frame || w.m_snapshot != frame || !signalHeadsShow(w.m_presenter->allSignals, *frame))
				return fail(QStringLiteral("heads do not show the replay frame of second %1").arg(time));
			if (!signalFailuresAreBlocked(w.m_presenter->allSignals, *frame, anyFailed, anyBlocked))
				return fail(QStringLiteral("a failed head stands on a section that is not blocked in second %1").arg(time));
			++seeks;
			differFromFinal += signalHeadsShow(w.m_presenter->allSignals, *finalFrame) ? 0 : 1;
			if (!backwards) {
				for (const SignalItem* head : w.m_presenter->allSignals) {
					const SignalCueKind cue = classifySignalCue(head->aspectCode());
					if (head->failed() || cue == SignalCueKind::Stop || cue == SignalCueKind::Caution)
						restricted[head] = true;
					else if (cue == SignalCueKind::Proceed && restricted[head]) {
						restricted[head] = false;
						++returned;
					}
				}
			} else if (frames.insert(frame->timestep).second) {
				failedFrames += anyFailed ? 1 : 0;
				blockedFrames += anyBlocked ? 1 : 0;
				for (const SignalItem* head : w.m_presenter->allSignals) {
					const SignalCueKind cue = classifySignalCue(head->aspectCode());
					failedHeads += head->failed() ? 1 : 0;
					stops += !head->failed() && cue == SignalCueKind::Stop;
					cautions += !head->failed() && cue == SignalCueKind::Caution;
					proceeds += !head->failed() && cue == SignalCueKind::Proceed;
					unavailable += !head->failed() && cue == SignalCueKind::Neutral;
				}
			}
		}
	}
	const bool moving = stops > 0 && cautions > 0 && proceeds > 0 && returned > 0 && differFromFinal > 0;
	const bool nothing = stops + cautions + proceeds + failedHeads == 0 && unavailable > 0 && differFromFinal == 0;
	const bool failing = failedFrames > 0 && failedFrames == blockedFrames && failedFrames < static_cast<int>(frames.size());
	QString problem;
	if (expected == QLatin1String("levels") && (!moving || failedHeads != 0))
		problem = QStringLiteral("the heads did not go through stop, caution and proceed and back");
	else if (expected == QLatin1String("failure") && (!moving || !failing || live.failedRenders == 0))
		problem = QStringLiteral("the failed heads do not match the window of the failure");
	else if (expected == QLatin1String("none") && !nothing)
		problem = QStringLiteral("a head shows more than unavailable in a scene without a signalling level");
	const QString summary = QStringLiteral("mode=%1 seeks=%2 frames=%3 stop=%4 caution=%5 proceed=%6 unavailable=%7 failed=%8 returned=%9 "
										   "differFromFinal=%10 failedFrames=%11 blockedFrames=%12 liveRenders=%13 liveFailedRenders=%14 pauses=%15")
								.arg(expected)
								.arg(seeks)
								.arg(frames.size())
								.arg(stops)
								.arg(cautions)
								.arg(proceeds)
								.arg(unavailable)
								.arg(failedHeads)
								.arg(returned)
								.arg(differFromFinal)
								.arg(failedFrames)
								.arg(blockedFrames)
								.arg(live.renders)
								.arg(live.failedRenders)
								.arg(live.pauseSteps.size());
	if (!problem.isEmpty())
		return fail(problem + QLatin1Char(' ') + summary);
	std::fprintf(stdout, "E2E_SIGNAL_HEADS_OK %s\n", qPrintable(summary));
	std::fflush(stdout);
	QCoreApplication::exit(0);
}
