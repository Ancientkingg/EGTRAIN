#include "widgets/TimeProgressBar.h"
#include "util/TimeFormat.h"

#include <QCursor>
#include <QFontDatabase>
#include <QStyleOptionProgressBar>
#include <QStylePainter>
#include <QToolTip>

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
	// QProgressBar::setValue() paints at once, as the bar draws its own text, and that paint also draws every other part
	// of the window that waits for one, such as the network view, in the middle of a snapshot delivery. With updates off
	// it only marks the bar, which then paints with the rest of the window.
	setUpdatesEnabled(false);
	setValue(clampedTimestep);
	setUpdatesEnabled(true);

	const auto formatCount = [](int count) {
		QString text = QString::number(count);
		for (auto index = text.size() - 3; index > 0; index -= 3)
			text.insert(index, QChar(','));
		return text;
	};
	const int displayedStep = safeTotalSteps > 0 ? clampedTimestep + 1 : 0;
	setFormat(QStringLiteral("%1 of %2")
			.arg(QString::fromStdString(formatSimTime(clampedTimestep, startOffsetSeconds)),
				QString::fromStdString(formatSimTime(safeTotalSteps, startOffsetSeconds))));
	const QString tip = QStringLiteral("Step %1 of %2").arg(formatCount(displayedStep), formatCount(safeTotalSteps));
	// Qt keeps a shown tooltip as it is, so the bar gives its own shown tooltip the new text.
	if (QToolTip::isVisible() && QToolTip::text() == toolTip())
		QToolTip::showText(QCursor::pos(), tip, this);
	setToolTip(tip);
}

// The style puts the text against the border, so the bar draws its text itself, 6 pixels inside the edges: in the text
// colour on the empty part and in the highlighted text colour on the fill.
void TimeProgressBar::paintEvent(QPaintEvent*) {
	QStyleOptionProgressBar option;
	initStyleOption(&option);
	QStyleOptionProgressBar label = option;
	label.rect = option.rect.adjusted(6, 0, -6, 0);
	label.rect = style()->subElementRect(QStyle::SE_ProgressBarLabel, &label, this);
	option.textVisible = false;
	const QRect contents = style()->subElementRect(QStyle::SE_ProgressBarContents, &option, this);
	const int fillWidth = QStyle::sliderPositionFromValue(minimum(), maximum(), value(), contents.width());
	const QRect fill(contents.x(), contents.y(), fillWidth, contents.height());
	QStylePainter painter(this);
	painter.drawControl(QStyle::CE_ProgressBar, option);
	painter.setClipRegion(QRegion(rect()).subtracted(fill));
	painter.drawControl(QStyle::CE_ProgressBarLabel, label);
	if (fillWidth <= 0)
		return;
	label.palette.setColor(QPalette::Text, label.palette.color(QPalette::HighlightedText));
	painter.setClipRect(fill);
	painter.drawControl(QStyle::CE_ProgressBarLabel, label);
}
