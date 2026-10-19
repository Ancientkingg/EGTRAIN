#ifndef TIMEPROGRESSBAR_H
#define TIMEPROGRESSBAR_H

#include <QProgressBar>

/// The progress bar of a run under the network: how far the run is, as the clock time, the step and the end time. It
/// ignores mouse and wheel input, so it shows the run and does not scrub it.
class TimeProgressBar : public QProgressBar {
	Q_OBJECT

public:
	/// A bar 28 pixels high in the fixed-width system font, without keyboard focus.
	TimeProgressBar(QWidget* parent = nullptr);
	~TimeProgressBar() override;

	/// Shows step `timestep` (from 0) of a run of `totalSteps` steps, clamped to the run, as "<clock> | Step <n> of
	/// <total> | Ends <clock>". The clock times take one step as one second after `startOffsetSeconds`, the base time of
	/// the run in seconds since midnight.
	void setProgress(int timestep, int totalSteps, long long startOffsetSeconds);

protected:
	/// Draws the bar, then the filled part again with its text in the highlighted text colour.
	void paintEvent(QPaintEvent* event) override;
};

#endif // TIMEPROGRESSBAR_H
