#include "widgets/TimeProgressBar.h"
#include "util/TimeFormat.h"

#include <QFontDatabase>
#include <QStyleOptionProgressBar>
#include <QStylePainter>

TimeProgressBar::TimeProgressBar(QWidget* parent)
	: QProgressBar(parent) {
	setTextVisible(true);
	setFixedHeight(28);
	setFocusPolicy(Qt::NoFocus);
	setCursor(Qt::ArrowCursor);
	QFont timelineFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	timelineFont.setStyleHint(QFont::TypeWriter);
	timelineFont.setFixedPitch(true);
	setFont(timelineFont);
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
		for (auto index = text.size() - 3; index > 0; index -= 3)
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

// Draws the bar, then draws it again over the filled part with the text in the highlighted text colour,
// so the text reads on the fill and on the empty part of the bar.
void TimeProgressBar::paintEvent(QPaintEvent* event) {
	QProgressBar::paintEvent(event);
	QStyleOptionProgressBar option;
	initStyleOption(&option);
	const QRect contents = style()->subElementRect(QStyle::SE_ProgressBarContents, &option, this);
	const int fillWidth = QStyle::sliderPositionFromValue(minimum(), maximum(), value(), contents.width());
	if (fillWidth <= 0)
		return;
	option.palette.setColor(QPalette::Text, option.palette.color(QPalette::HighlightedText));
	QStylePainter painter(this);
	painter.setClipRect(contents.x(), contents.y(), fillWidth, contents.height());
	painter.drawControl(QStyle::CE_ProgressBar, option);
}
