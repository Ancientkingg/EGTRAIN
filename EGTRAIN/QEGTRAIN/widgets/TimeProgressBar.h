#ifndef TIMEPROGRESSBAR_H
#define TIMEPROGRESSBAR_H

#include <QProgressBar>

class TimeProgressBar : public QProgressBar {
	Q_OBJECT

public:
	TimeProgressBar(QWidget* parent = nullptr);
	~TimeProgressBar() override;

	// updates bar and time
	void setProgress(int timestep, int totalSteps, long long startOffsetSeconds);

protected:
	/// Draws the bar, then the filled part again with its text in the highlighted text colour.
	void paintEvent(QPaintEvent* event) override;
};

#endif // TIMEPROGRESSBAR_H
