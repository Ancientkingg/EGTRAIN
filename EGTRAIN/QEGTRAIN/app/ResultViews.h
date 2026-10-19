#ifndef RESULTVIEWS_H
#define RESULTVIEWS_H

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class QWidget;
class RunController;
struct BlockingTimeDiagramSegment;
struct BlockSectionExtent;
struct CapacityAnalysisResult;
struct CompletedRunData;
struct SceneModel;
namespace telemetry {
class OperationObservation;
} // namespace telemetry

// Asks for a path and writes CSV text atomically. A cancelled dialog or a failed write never leaves a partial file behind.
// provider gives the text, and artifactWriter writes it together with its provenance.
void saveCsvInteractive(QWidget* parent, const QString& suggestedName, const telemetry::OperationObservation& operation,
	const std::function<std::string()>& provider, const std::function<bool(const QString&, const std::string&)>& artifactWriter);

// The name of each result view. The Diagrams menu, the Run Results menu and the window titles use these names.
namespace ResultViewName {
inline constexpr const char* kTimetable = "Timetable";
inline constexpr const char* kTimetableGraph = "Timetable graph";
inline constexpr const char* kDelays = "Delays";
inline constexpr const char* kDelayAnalysis = "Delay analysis";
inline constexpr const char* kSpeedDistance = "Speed / distance";
inline constexpr const char* kSpeedTime = "Speed / time";
inline constexpr const char* kTimeDistance = "Time / distance";
inline constexpr const char* kTractiveEffortDistance = "Tractive effort / distance";
inline constexpr const char* kTrainPaths = "Train paths";
inline constexpr const char* kBlockingTime = "Blocking time";
inline constexpr const char* kCapacity = "Capacity";
inline constexpr const char* kCapacityByArea = "Capacity by area";
} // namespace ResultViewName

// The windows that show the completed run: the per-train charts, the route and timetable diagrams, the delay charts, the
// blocking-time diagram, and the capacity analyses. Each slot opens one window over the main window and reads the run of
// the RunController. Without results a slot opens nothing and says so in the status bar.
class ResultViews : public QObject {
	Q_OBJECT

public:
	// What the views ask of the window.
	class Host {
	public:
		virtual ~Host() = default;

		virtual telemetry::OperationObservation captureTelemetryOperation() const noexcept = 0;
		// A scripted run has no user to answer a dialog.
		virtual bool dialogsSuppressed() const = 0;
		// The scene that the run was made from, which gives the colours of the services.
		virtual const SceneModel& sceneModel() const = 0;
		// A message for the status bar.
		virtual void resultMessage(const QString& text, int milliseconds) = 0;
	};

	ResultViews(QWidget* window, RunController* runController, Host& host);

	// Opens the window of a route diagram: the train paths, or with timetable the planned and simulated stops.
	// referenceIndex is the runtime index of the route that gives the horizontal axis.
	void buildRouteDiagram(const std::shared_ptr<const CompletedRunData>& run, bool timetable, int referenceIndex);
	// Opens the chart of the trajectories: 0 speed/distance, 1 speed/time, 2 time/distance, 3 simulated effort/distance.
	void buildPerTrainDiagram(int mode);

public slots:
	void showSpeedDistanceDiagram();
	void showSpeedTimeDiagram();
	void showTimeDistanceDiagram();
	void showTractiveEffortDistanceDiagram();
	void showTimetableGraph();
	void displayTrainPathDiagrams();
	void showTimetableTable();
	void showDelayDiagram();
	void showDelayAnalysis();
	void showBlockingTimeDiagram();
	void showCapacityAnalysis();
	void showAreaCapacity();

signals:
	// A train was selected in one of the windows.
	void trainSelected(const QString& trainId);

private:
	void showCompressedBlockingTimeDiagram(const CapacityAnalysisResult& result, const QString& sectionLabel,
		const std::shared_ptr<const CompletedRunData>& run, const std::vector<BlockingTimeDiagramSegment>& segments,
		double axisMinKm, double axisMaxKm, const QString& referenceId, const std::vector<BlockSectionExtent>& extents,
		const std::vector<BlockingTimeDiagramSegment>& unmapped);
	// The runtime index of the route that the user picks from the routes the run used, -1 when there is none or the user
	// cancels.
	int chooseRouteDiagramReference(const CompletedRunData& run, const QString& purpose) const;

	QWidget* const m_window;
	RunController* const m_runController;
	Host& m_host;
};

#endif // RESULTVIEWS_H
