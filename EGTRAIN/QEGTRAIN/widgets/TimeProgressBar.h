#ifndef TIMEPROGRESSBAR_H
#define TIMEPROGRESSBAR_H

#include <QProgressBar>

class TimeProgressBar : public QProgressBar {
	Q_OBJECT

public:
	TimeProgressBar(QWidget* parent = nullptr);
	~TimeProgressBar();

	// updates bar and time
	void setProgress(int timestep, int totalSteps, long long startOffsetSeconds);
};

#endif // TIMEPROGRESSBAR_H
