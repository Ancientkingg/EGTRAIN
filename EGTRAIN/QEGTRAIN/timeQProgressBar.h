#ifndef TIMEQPROGRESSBAR_H
#define TIMEQPROGRESSBAR_H

#include <QProgressBar>

// EGTRAIN files
#include "Infrastructure.h"

class timeQProgressBar : public QProgressBar {
	Q_OBJECT

public:
	timeQProgressBar(QWidget* parent = nullptr);
	~timeQProgressBar();

	// updates bar and time
	void setProgress(int t);
};

#endif // TIMEQPROGRESSBAR_H
