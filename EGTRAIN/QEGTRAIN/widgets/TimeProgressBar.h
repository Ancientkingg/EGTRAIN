#ifndef TIMEPROGRESSBAR_H
#define TIMEPROGRESSBAR_H

#include <QProgressBar>

/// The progress bar of a run under the network: how far the run is, as the clock time and the end time, with the step in
/// its tooltip. It ignores mouse and wheel input, so it shows the run and does not scrub it.
class TimeProgressBar : public QProgressBar {
	Q_OBJECT

public:
	/// A bar 28 pixels high in the fixed-width system font, without keyboard focus.
	TimeProgressBar(QWidget* parent = nullptr);
	~TimeProgressBar() override;

	/// Shows step `timestep` (from 0) of a run of `totalSteps` steps, clamped to the run, as "<clock> of <end clock>" with
	/// the tooltip "Step <n> of <total>", which a shown tooltip of the bar follows. The clock times take one step as one
	/// second after `startOffsetSeconds`, the base time of the run in seconds since midnight.
	void setProgress(int timestep, int totalSteps, long long startOffsetSeconds);

protected:
	/// Draws the bar with its text 6 pixels inside the edges, in the highlighted text colour on the filled part.
	void paintEvent(QPaintEvent* event) override;
};

#endif // TIMEPROGRESSBAR_H
