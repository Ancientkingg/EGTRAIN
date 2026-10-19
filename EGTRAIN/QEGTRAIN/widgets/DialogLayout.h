#pragma once

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLabel>
#include <QLayout>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>

#include "widgets/FocusWheel.h"

/// The common layout of the dialogs: a heading, an optional context line, a body that scrolls and a footer of
/// buttons, and the inline status and help labels inside them.
namespace DialogLayout {

/// Helpers of the functions below.
namespace detail {
/// The dialog height at which the body of an installed dialog needs no vertical scroll bar when the dialog is
/// width wide: the whole content and every other visible item of the layout (the heading, the footer and any
/// widget a caller added), with the margins and spacing between them.
inline int heightForContent(QDialog& dialog, int width) {
	// The style sheet adds padding and borders to labels when they are polished, which changes their size hints.
	dialog.ensurePolished();
	QLayout* layout = dialog.layout();
	auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("dialogBodyScroll"));
	if (!layout || !scroll)
		return 0;
	const QMargins margins = layout->contentsMargins();
	const int innerWidth = width - margins.left() - margins.right();
	const int frame = 2 * scroll->frameWidth();
	int height = margins.top() + margins.bottom();
	int shown = 0;
	for (int i = 0; i < layout->count(); ++i) {
		QLayoutItem* item = layout->itemAt(i);
		if (item->isEmpty())
			continue;
		++shown;
		if (item->widget() == scroll) {
			QWidget* content = scroll->widget();
			height += frame + (content->hasHeightForWidth() ? content->heightForWidth(innerWidth - frame) : content->sizeHint().height());
		} else {
			height += item->hasHeightForWidth() ? item->heightForWidth(innerWidth) : item->sizeHint().height();
		}
	}
	return height + qMax(0, shown - 1) * layout->spacing();
}
} // namespace detail

/// Lays out dialog as a heading, the context line when it is not empty, body in a scroll area and actions as the
/// footer, and returns the scroll area. Transfers body and actions to dialog. The caller owns button roles, connections,
/// validation, default action and any changes to application state. The combo boxes and spin
/// boxes in body change with the mouse wheel only while they have focus, so the wheel scrolls
/// the body. The footer buttons are styled again, because the caller named them and made one
/// the default after they were built, and a style cached by then misses the rules that name it.
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
	// The body takes the height that the content has left over, so that the context line keeps its own height.
	contentLayout->addWidget(body, 1);
	FocusWheel::guardAll(body);

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
	for (QAbstractButton* button : actions->buttons()) {
		button->style()->unpolish(button);
		button->style()->polish(button);
	}

	// The scroll viewport, not its content, determines the dialog's minimum size.
	// Reserve room for window decorations and keep the footer outside the viewport.
	if (availableGeometry.isValid()) {
		const int maxWidth = qMax(1, availableGeometry.width() * 9 / 10);
		const int maxHeight = qMax(1, availableGeometry.height() * 4 / 5);
		dialog.setMaximumSize(maxWidth, maxHeight);
		const int width = qMin(maxWidth, qMax(480, lineHeight * 32));
		dialog.resize(width, qMin(maxHeight, detail::heightForContent(dialog, width)));
	}
	return scroll;
}

/// Widens an installed dialog to show its body without clipping, up to the width limit
/// that install set. Call it after the body is filled, and again after a change to the body
/// of a dialog that is shown; it never narrows the dialog. It also makes the dialog tall enough for the
/// body at the new width, up to the height limit, and never shortens it.
inline void fitWidthToContent(QDialog& dialog) {
	auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("dialogBodyScroll"));
	if (!scroll || !dialog.layout())
		return;
	// The layouts of the body keep size hints from before the body was filled or changed, also in a dialog that is not
	// shown yet, so they are brought up to date before the body is measured.
	for (QLayout* layout : scroll->widget()->findChildren<QLayout*>())
		layout->activate();
	const QMargins margins = dialog.layout()->contentsMargins();
	const int needed = margins.left() + margins.right() + 2 * scroll->frameWidth()
		+ scroll->widget()->sizeHint().width() + scroll->verticalScrollBar()->sizeHint().width();
	const int width = qMin(dialog.maximumWidth(), qMax(dialog.width(), needed));
	dialog.resize(width, qMin(dialog.maximumHeight(), qMax(dialog.height(), detail::heightForContent(dialog, width))));
}

/// How serious an inline message is. Each value selects one callout of the stylesheet.
enum class Severity { Neutral, ///< A note.
	Warning,				   ///< Something to check before going on.
	Error };				   ///< Something that blocks the action.

namespace detail {
/// Shows text in label as plain wrapped text with the role that the two property values give: a
/// valid value sets that property and an invalid one removes it. The label is styled again, so the
/// new role shows also when the label is already polished, and it is hidden when text is empty.
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
	// A message in a shown dialog adds height to the body. Once the layout has made room for it, the dialog grows
	// to fit, up to its height limit, so that the body does not scroll the message into view.
	auto* dialog = qobject_cast<QDialog*>(label->window());
	if (!text.isEmpty() && dialog && dialog->isVisible() && dialog->findChild<QScrollArea*>(QStringLiteral("dialogBodyScroll")))
		QTimer::singleShot(0, dialog, [dialog] { fitWidthToContent(*dialog); });
}
} // namespace detail

/// Shows text in label as an inline status of the given severity. The text is plain and wraps, so
/// a long word never widens the dialog. The label has this role only: a call replaces the help or
/// status role of an earlier call, and may be made again to change role or text, also after the
/// dialog is shown. An empty text hides the label. The label must already have a parent widget
/// (the dialog body, the dialog itself or an editor panel), because showing a label without a
/// parent opens a window of its own.
inline void setStatus(QLabel* label, Severity severity, const QString& text) {
	static const char* const names[] = {"neutral", "warning", "error"};
	detail::showMessage(label, text, QString::fromLatin1(names[static_cast<int>(severity)]), QVariant());
}

/// Shows text in label as help text. The text is plain and wraps, so a long word never widens the
/// dialog. The label has this role only: a call replaces the status role of an earlier call, and
/// may be made again to change role or text, also after the dialog is shown. An empty text hides
/// the label. The label must already have a parent widget (the dialog body, the dialog itself or
/// an editor panel), because showing a label without a parent opens a window of its own.
inline void setHelp(QLabel* label, const QString& text) {
	detail::showMessage(label, text, QVariant(), true);
}

} // namespace DialogLayout
