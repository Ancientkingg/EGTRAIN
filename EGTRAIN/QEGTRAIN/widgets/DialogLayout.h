#pragma once

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QVBoxLayout>
#include <QtGlobal>

namespace DialogLayout {

// Transfers body and actions to dialog. The caller owns button roles, connections,
// validation, default action and any changes to application state.
inline QScrollArea* install(QDialog& dialog, const QString& heading,
	const QString& context, QWidget* body,
	QDialogButtonBox* actions,
	QRect availableGeometry = QRect()) {
	if (!availableGeometry.isValid()) {
		QScreen* screen = QGuiApplication::screenAt(dialog.mapToGlobal(QPoint(0, 0)));
		if (!screen)
			screen = QGuiApplication::primaryScreen();
		if (screen)
			availableGeometry = screen->availableGeometry();
	}

	dialog.setProperty("dialogPresentation", true);
	const int lineHeight = QFontMetrics(dialog.font()).height();
	const int margin = qMax(16, lineHeight);
	const int spacing = qMax(8, lineHeight / 2);
	auto* layout = new QVBoxLayout(&dialog);
	layout->setContentsMargins(margin, margin, margin, margin);
	layout->setSpacing(spacing);

	auto* title = new QLabel(heading, &dialog);
	title->setObjectName(QStringLiteral("dialogHeading"));
	title->setWordWrap(true);
	title->setMinimumWidth(0);
	title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	title->setTextFormat(Qt::PlainText);
	QFont titleFont = dialog.font();
	titleFont.setBold(true);
	if (titleFont.pointSizeF() > 0)
		titleFont.setPointSizeF(titleFont.pointSizeF() * 1.25);
	else if (titleFont.pixelSize() > 0)
		titleFont.setPixelSize(qRound(titleFont.pixelSize() * 1.25));
	title->setFont(titleFont);
	layout->addWidget(title);

	auto* content = new QWidget;
	auto* contentLayout = new QVBoxLayout(content);
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->setSpacing(spacing);
	if (!context.isEmpty()) {
		auto* subtitle = new QLabel(context, content);
		subtitle->setObjectName(QStringLiteral("dialogContext"));
		subtitle->setTextFormat(Qt::PlainText);
		subtitle->setWordWrap(true);
		subtitle->setMinimumWidth(0);
		subtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		contentLayout->addWidget(subtitle);
	}
	contentLayout->addWidget(body);

	auto* scroll = new QScrollArea(&dialog);
	scroll->setObjectName(QStringLiteral("dialogBodyScroll"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setMinimumSize(0, 0);
	scroll->setWidget(content);
	QObject::connect(qApp, &QApplication::focusChanged, scroll,
		[scroll, content](QWidget*, QWidget* focused) {
			if (focused && content->isAncestorOf(focused))
				scroll->ensureWidgetVisible(focused);
		});
	layout->addWidget(scroll, 1);
	layout->addWidget(actions);

	// The scroll viewport, not its content, determines the dialog's minimum size.
	// Reserve room for window decorations and keep the footer outside the viewport.
	if (availableGeometry.isValid()) {
		const int maxWidth = qMax(1, availableGeometry.width() * 9 / 10);
		const int maxHeight = qMax(1, availableGeometry.height() * 4 / 5);
		dialog.setMaximumSize(maxWidth, maxHeight);
		dialog.resize(qMin(maxWidth, qMax(480, lineHeight * 32)),
			qMin(maxHeight, qMax(layout->sizeHint().height(), lineHeight * 16)));
	}
	return scroll;
}

// Widens an installed dialog to show its body without clipping, up to the width limit
// that install set. Call it after the body is filled; it never narrows the dialog.
inline void fitWidthToContent(QDialog& dialog) {
	auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("dialogBodyScroll"));
	if (!scroll || !dialog.layout())
		return;
	const QMargins margins = dialog.layout()->contentsMargins();
	const int needed = margins.left() + margins.right() + 2 * scroll->frameWidth()
		+ scroll->widget()->sizeHint().width() + scroll->verticalScrollBar()->sizeHint().width();
	dialog.resize(qMin(dialog.maximumWidth(), qMax(dialog.width(), needed)), dialog.height());
}

// How serious an inline message is. Each value selects one callout of the stylesheet.
enum class Severity { Neutral,
	Warning,
	Error };

namespace detail {
// Shows text in label as plain wrapped text with the role that the two property values give: a
// valid value sets that property and an invalid one removes it. The label is styled again, so the
// new role shows also when the label is already polished, and it is hidden when text is empty.
inline void showMessage(QLabel* label, const QString& text, const QVariant& status, const QVariant& help) {
	label->setTextFormat(Qt::PlainText);
	label->setWordWrap(true);
	QSizePolicy policy = label->sizePolicy();
	policy.setHorizontalPolicy(QSizePolicy::Ignored);
	label->setSizePolicy(policy);
	label->setText(text);
	label->setProperty("dialogStatus", status);
	label->setProperty("dialogHelp", help);
	label->style()->unpolish(label);
	label->style()->polish(label);
	label->setVisible(!text.isEmpty());
}
} // namespace detail

// Shows text in label as an inline status of the given severity. The text is plain and wraps, so
// a long word never widens the dialog. The label has this role only: a call replaces the help or
// status role of an earlier call, and may be made again to change role or text, also after the
// dialog is shown. An empty text hides the label. The label must already have a parent widget
// (the dialog body, the dialog itself or an editor panel), because showing a label without a
// parent opens a window of its own.
inline void setStatus(QLabel* label, Severity severity, const QString& text) {
	static const char* const names[] = {"neutral", "warning", "error"};
	detail::showMessage(label, text, QString::fromLatin1(names[static_cast<int>(severity)]), QVariant());
}

// Shows text in label as help text. The text is plain and wraps, so a long word never widens the
// dialog. The label has this role only: a call replaces the status role of an earlier call, and
// may be made again to change role or text, also after the dialog is shown. An empty text hides
// the label. The label must already have a parent widget (the dialog body, the dialog itself or
// an editor panel), because showing a label without a parent opens a window of its own.
inline void setHelp(QLabel* label, const QString& text) {
	detail::showMessage(label, text, QVariant(), true);
}

} // namespace DialogLayout
