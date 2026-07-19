#include "widgets/TimeProgressBar.h"
#include "util/TimeFormat.h"

TimeProgressBar::TimeProgressBar(QWidget* parent)
	: QProgressBar(parent) {
	setTextVisible(true);
	setFixedHeight(28);
	setFocusPolicy(Qt::NoFocus);
	setCursor(Qt::ArrowCursor);
}

TimeProgressBar::~TimeProgressBar() {
}

// updates bar and time
void TimeProgressBar::setProgress(int timestep, int totalSteps, long long startOffsetSeconds) {
	const int safeTotalSteps = qMax(0, totalSteps);
	setRange(0, qMax(0, safeTotalSteps - 1));
	const int clampedTimestep = safeTotalSteps > 0
		? qBound(0, timestep, safeTotalSteps - 1)
		: 0;
	setValue(clampedTimestep);

	const auto formatCount = [](int count) {
		QString text = QString::number(count);
		for (int index = text.size() - 3; index > 0; index -= 3)
			text.insert(index, QChar(','));
		return text;
	};
	const int displayedStep = safeTotalSteps > 0 ? clampedTimestep + 1 : 0;
	const QString stepCount = formatCount(safeTotalSteps);
	const QString displayedStepText = formatCount(displayedStep);
	setFormat(QStringLiteral("%1 | Step %2 of %3 | Ends %4")
		.arg(QString::fromStdString(formatSimTime(clampedTimestep, startOffsetSeconds)))
		.arg(displayedStepText)
		.arg(stepCount)
		.arg(QString::fromStdString(formatSimTime(safeTotalSteps, startOffsetSeconds))));
}
