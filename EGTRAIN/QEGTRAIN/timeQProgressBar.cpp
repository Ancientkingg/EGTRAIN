#include "timeQProgressBar.h"

extern initial_parameters initial_variables;
timeQProgressBar::timeQProgressBar(QWidget* parent)
	: QProgressBar(parent) {
	setTextVisible(true);

	QString style = "QProgressBar{\
						border-radius: 5px;\
						text-align: right;\
						margin-right: 30ex;\
					}";

	setStyleSheet(style);

	setFixedHeight(10);
}

timeQProgressBar::~timeQProgressBar() {
}

// updates bar and time
void timeQProgressBar::setProgress(int t) {
	double progress = (t / initial_variables.times) * 100;
	setValue(progress);

	setFormat("Time: " + QString::number(t) + " / " + QString::number(initial_variables.times - 1)); // 0 to (times-1)
}
