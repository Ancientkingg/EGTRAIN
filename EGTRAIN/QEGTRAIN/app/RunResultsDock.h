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

// The Run Results dock: the table of the completed run, the lines that say which run it is, the menu that opens the result
// views, the exports of the table, and the delay baseline. It sits at the bottom of the window and stays hidden until a
// run has results.
class RunResultsDock {
public:
	// What the dock asks of the window.
	class Host {
	public:
		virtual ~Host() = default;

		virtual telemetry::OperationObservation captureTelemetryOperation() const noexcept = 0;
		virtual bool advancedDetailsEnabled() const = 0;
		// The table of a completed run has been written and the dock is shown.
		virtual void runResultsRefreshed() = 0;
	};

	// Builds the dock and adds it to the bottom of the window, hidden. The entries of its result view menu open the
	// windows of views.
	RunResultsDock(QMainWindow* window, RunController* runController, ResultViews* views, Host& host);

	QDockWidget* dock() const { return m_dock; }
	QTableWidget* table() const { return m_table; }
	QLabel* summaryLabel() const { return m_summaryLabel; }
	QLabel* delayFeedbackLabel() const { return m_delayFeedbackLabel; }
	QPushButton* setDelayBaselineButton() const { return m_setDelayBaselineButton; }
	QPushButton* compareDelayButton() const { return m_compareDelayButton; }

	// Writes the table and the summary of the completed run and shows the dock, a third of the window high when it opens at
	// the bottom. Does nothing without trains in the results.
	void refresh();
	// Writes the lines that say which run the table shows and its status, each entry on a line of its own.
	void refreshSummary();
	// Empties the table and hides the dock, which takes the title and the summary of a run without results.
	void clear(const QString& summary, const QString& title);
	// Enables the result views, and the delay baseline buttons as far as the baseline allows.
	void updateAvailability(bool resultsAvailable);
	// A number of seconds as H:MM:SS. The hours do not wrap at a day.
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
