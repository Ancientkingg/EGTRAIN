#include "widgets/TimeProgressBar.h"

extern InitialParameters initial_variables;
TimeProgressBar::TimeProgressBar(QWidget* parent)
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

TimeProgressBar::~TimeProgressBar() {
}

// updates bar and time
void TimeProgressBar::setProgress(int t) {
	double progress = (t / initial_variables.times) * 100;
	setValue(progress);

	setFormat("Time: " + QString::number(t) + " / " + QString::number(initial_variables.times - 1)); // 0 to (times-1)
}
