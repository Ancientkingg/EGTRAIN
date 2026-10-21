#ifndef RESULTVIEWS_H
#define RESULTVIEWS_H

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class QWidget;
class RunController;
struct BlockingTimeDiagramData;
struct CompletedRunData;
struct SceneModel;
namespace telemetry {
class OperationObservation;
} // namespace telemetry

/// Asks for a path and writes CSV text atomically. A cancelled dialog or a failed write never leaves a partial file behind.
/// provider gives the text, and artifactWriter writes it together with its provenance and returns whether it succeeded.
/// Empty text writes nothing and says that there is no data. ".csv" is added to a path without it.
void saveCsvInteractive(QWidget* parent, const QString& suggestedName, const telemetry::OperationObservation& operation,
	const std::function<std::string()>& provider, const std::function<bool(const QString&, const std::string&)>& artifactWriter);

/// The name of each result view. The Diagrams menu, the Run Results menu and the window titles use these names.
namespace ResultViewName {
inline constexpr const char* kTimetable = "Timetable";								 ///< The table of planned and simulated stops.
inline constexpr const char* kTimetableGraph = "Timetable graph";					 ///< The route diagram of planned and simulated stops.
inline constexpr const char* kDelays = "Delays";									 ///< The delay chart.
inline constexpr const char* kDelayAnalysis = "Delay analysis";						 ///< The delay analysis window.
inline constexpr const char* kSpeedDistance = "Speed / distance";					 ///< Speed against distance per train.
inline constexpr const char* kSpeedTime = "Speed / time";							 ///< Speed against time per train.
inline constexpr const char* kTimeDistance = "Time / distance";						 ///< Time against distance per train.
inline constexpr const char* kTractiveEffortDistance = "Tractive effort / distance"; ///< Simulated effort against distance.
inline constexpr const char* kTrainPaths = "Train paths";							 ///< The route diagram of the train paths.
inline constexpr const char* kBlockingTime = "Blocking time";						 ///< The blocking-time diagram.
inline constexpr const char* kCapacity = "Capacity";								 ///< The capacity analysis of a section.
inline constexpr const char* kCapacityByArea = "Capacity by area";					 ///< The capacity analysis by area.
} // namespace ResultViewName

/// The windows that show the completed run: the per-train charts, the route and timetable diagrams, the delay charts, the
/// blocking-time diagram, and the capacity analyses. Each slot opens one window over the main window and reads the run of
/// the RunController. Without results a slot opens nothing; all but the train paths and the timetable graph say so in
/// the status bar.
class ResultViews : public QObject {
	Q_OBJECT

public:
	/// What the views ask of the window.
	class Host {
	public:
		virtual ~Host() = default;

		/// The telemetry record of the user action that is going on, which the exports of the windows report to.
		virtual telemetry::OperationObservation captureTelemetryOperation() const noexcept = 0;
		/// A scripted run has no user to answer a dialog.
		virtual bool dialogsSuppressed() const = 0;
		/// The scene that the run was made from, which gives the colours of the services.
		virtual const SceneModel& sceneModel() const = 0;
		/// Shows `text` in the status bar for `milliseconds`.
		virtual void resultMessage(const QString& text, int milliseconds) = 0;
	};

	/// The views are a child of `window`. They do not own `runController` or `host`, which have to outlive them.
	ResultViews(QWidget* window, RunController* runController, Host& host);

	/// Opens the window of a route diagram: the train paths, or with timetable the planned and simulated stops.
	/// referenceIndex is the runtime index of the route that gives the horizontal axis.
	void buildRouteDiagram(const std::shared_ptr<const CompletedRunData>& run, bool timetable, int referenceIndex);
	/// Opens the chart of the trajectories: 0 speed/distance, 1 speed/time, 2 time/distance, 3 simulated effort/distance.
	void buildPerTrainDiagram(int mode);

public slots:
	/// Opens ResultViewName::kSpeedDistance.
	void showSpeedDistanceDiagram();
	/// Opens ResultViewName::kSpeedTime.
	void showSpeedTimeDiagram();
	/// Opens ResultViewName::kTimeDistance.
	void showTimeDistanceDiagram();
	/// Opens ResultViewName::kTractiveEffortDistance.
	void showTractiveEffortDistanceDiagram();
	/// Asks for the reference route and opens ResultViewName::kTimetableGraph.
	void showTimetableGraph();
	/// Asks for the reference route and opens ResultViewName::kTrainPaths.
	void displayTrainPathDiagrams();
	/// Opens ResultViewName::kTimetable.
	void showTimetableTable();
	/// Opens ResultViewName::kDelays.
	void showDelayDiagram();
	/// Opens ResultViewName::kDelayAnalysis through RunController::openDelayAnalysis().
	void showDelayAnalysis();
	/// Opens ResultViewName::kDelayAnalysis on its baseline comparison.
	void showDelayComparison();
	/// Asks for the scope and opens ResultViewName::kBlockingTime.
	void showBlockingTimeDiagram();
	/// Asks for the occurrences, starting with the trains of the first block that two or more of them occupy and the
	/// blocks they share from there, and opens ResultViewName::kCapacity. Shows a message instead when the run has no such
	/// block or the chosen scope cannot be analysed.
	void showCapacityAnalysis();
	/// Opens ResultViewName::kCapacityByArea.
	void showAreaCapacity();

signals:
	/// A train was selected in one of the windows.
	void trainSelected(const QString& trainId);

private:
	// Opens a window on a blocking-time diagram of the run, with `context` as its context line. The compressed diagram of
	// a capacity result has its own title, CSV name and manual section, draws the capacity-critical envelopes amber and
	// says in its note that envelopes and samples are moved.
	void showBlockingTimeWindow(const std::shared_ptr<const CompletedRunData>& run, const BlockingTimeDiagramData& diagram,
		bool compressed, const QString& context);
	// The runtime index of the route that the user picks from the routes the run used, -1 when there is none or the user
	// cancels.
	int chooseRouteDiagramReference(const CompletedRunData& run, const QString& purpose) const;
	// Shows a result window. While another result window is open, the new one opens a step below and to the right of the
	// one that opened last, or at the top left of the screen when that step would leave the screen; otherwise Qt centres
	// it over the main window. The delay analysis and capacity by area windows, which their openers show, move there
	// after they appear.
	void showWindow(QWidget* window);

	QWidget* const m_window;
	RunController* const m_runController;
	Host& m_host;
	// The result windows that showWindow() opened, oldest first; a closed window is deleted and leaves a null entry.
	QList<QPointer<QWidget>> m_windows;
};

#endif // RESULTVIEWS_H
