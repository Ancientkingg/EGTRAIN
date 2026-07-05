#ifndef TIMEPROGRESSBAR_H
#define TIMEPROGRESSBAR_H

#include <QProgressBar>

// EGTRAIN files
#include "simulation/Infrastructure.h"

class TimeProgressBar : public QProgressBar {
	Q_OBJECT

public:
	TimeProgressBar(QWidget* parent = nullptr);
	~TimeProgressBar();

	// updates bar and time
	void setProgress(int t);
};

#endif // TIMEPROGRESSBAR_H
