#ifndef RUNRESULTSDOCK_H
#define RUNRESULTSDOCK_H

#include <QString>

class QDockWidget;
class QLabel;
class QMainWindow;
class QPushButton;
class QTableWidget;
class ResultViews;
class RunController;
namespace telemetry {
class OperationObservation;
} // namespace telemetry

/// The Run Results dock: the table of the completed run, the lines that say which run it is, the menu that opens the result
/// views, the exports of the table, and the delay baseline. It sits at the bottom of the window and stays hidden until a
/// run has results.
class RunResultsDock {
public:
	/// What the dock asks of the window.
	class Host {
	public:
		virtual ~Host() = default;

		/// The telemetry record of the user action that is going on, which the exports report to.
		virtual telemetry::OperationObservation captureTelemetryOperation() const noexcept = 0;
		/// The summary adds the incident evidence and the number of destination terminations.
		virtual bool advancedDetailsEnabled() const = 0;
		/// The table of a completed run has been written and the dock is shown.
		virtual void runResultsRefreshed() = 0;
	};

	/// Builds the dock and adds it to the bottom of the window, hidden. The entries of its result view menu open the
	/// windows of views. The window owns the widgets; the dock does not own `runController`, `views` or `host`.
	RunResultsDock(QMainWindow* window, RunController* runController, ResultViews* views, Host& host);

	/// The dock widget, which the window owns.
	QDockWidget* dock() const { return m_dock; }
	/// The table of the results: one row per train, then the network total. Times are clock times and H:MM:SS, speeds
	/// km/h, performance percent and energy kWh.
	QTableWidget* table() const { return m_table; }
	/// The lines that say which run the table shows.
	QLabel* summaryLabel() const { return m_summaryLabel; }
	/// The text about the delay baseline (RunController::DelayBaselineState::feedback).
	QLabel* delayFeedbackLabel() const { return m_delayFeedbackLabel; }
	/// The button that makes the completed run the delay baseline.
	QPushButton* setDelayBaselineButton() const { return m_setDelayBaselineButton; }
	/// The button that compares the completed run with the delay baseline.
	QPushButton* compareDelayButton() const { return m_compareDelayButton; }

	/// Writes the table and the summary of the completed run and shows the dock, a third of the window high when it opens at
	/// the bottom. Does nothing without trains in the results.
	void refresh();
	/// Writes the lines that say which run the table shows and its status, each entry on a line of its own.
	void refreshSummary();
	/// Empties the table and hides the dock, which takes the title and the summary of a run without results.
	void clear(const QString& summary, const QString& title);
	/// Enables the result views, and the delay baseline buttons as far as the baseline allows.
	void updateAvailability(bool resultsAvailable);
	/// A number of seconds as H:MM:SS. The hours do not wrap at a day.
	static QString durationText(long long seconds);

private:
	QMainWindow* const m_window;
	RunController* const m_runController;
	Host& m_host;
	QDockWidget* m_dock = nullptr;
	QTableWidget* m_table = nullptr;
	QLabel* m_summaryLabel = nullptr;
	QLabel* m_delayFeedbackLabel = nullptr;
	QPushButton* m_setDelayBaselineButton = nullptr;
	QPushButton* m_compareDelayButton = nullptr;
};

#endif // RUNRESULTSDOCK_H
