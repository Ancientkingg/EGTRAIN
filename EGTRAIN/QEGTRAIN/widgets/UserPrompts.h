#ifndef USERPROMPTS_H
#define USERPROMPTS_H

#include <QApplication>
#include <QFontMetrics>
#include <QInputDialog>
#include <QMessageBox>
#include <QScreen>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "util/Log.h"

#include <cstdio>
#include <functional>
#include <utility>

/// The messages and questions that the editors put to the user. A panel gets one from the window, and a test replaces
/// the virtual functions to answer without a dialog.
class UserPrompts {
public:
	/// `suppressed` tells whether a scripted run has no user to answer. Modal boxes would deadlock such a run, so
	/// showError then writes the message to stderr instead. An empty function never suppresses.
	explicit UserPrompts(std::function<bool()> suppressed = {}) : m_suppressed(std::move(suppressed)) {}
	virtual ~UserPrompts() = default;

	/// An error message in a modal box, with a warning icon instead of the critical one when warningIcon is set. In a
	/// suppressed run it writes "QEGTRAIN_DIALOG_SUPPRESSED: title: message" to stderr instead. The title and the message
	/// also go to the log, at error level in the category app.ui.
	virtual void showError(QWidget* parent, const QString& title, const QString& message, bool warningIcon = false) {
		EG_LOG(AppUi, Error) << title.toStdString() << ": " << message.toStdString() << '\n';
		if (m_suppressed && m_suppressed()) {
			std::fprintf(stderr, "QEGTRAIN_DIALOG_SUPPRESSED: %s: %s\n", title.toStdString().c_str(), message.toStdString().c_str());
			std::fflush(stderr);
			return;
		}
		if (warningIcon)
			QMessageBox::warning(parent, title, message);
		else
			QMessageBox::critical(parent, title, message);
	}

	/// A message that the user acknowledges. A scripted run has no user to do that, so nothing is shown then.
	virtual void showInfo(QWidget* parent, const QString& title, const QString& message) {
		if (m_suppressed && m_suppressed())
			return;
		QMessageBox::information(parent, title, message);
	}

	/// A Yes or No question whose default answer is No; true for Yes. It asks also in a suppressed run.
	virtual bool confirm(QWidget* parent, const QString& title, const QString& question) {
		return QMessageBox::question(parent, title, question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
			== QMessageBox::Yes;
	}

	/// One of `items`, for example the id of a rolling stock unit; empty when the user cancels. It asks also in a
	/// suppressed run. The dialog is wide enough for its window title, within the screen.
	virtual QString chooseItem(QWidget* parent, const QString& title, const QString& label, const QStringList& items) {
		QInputDialog dialog(parent);
		dialog.setWindowTitle(title);
		dialog.setLabelText(label);
		dialog.setComboBoxItems(items);
		dialog.setTextValue(items.value(0));
		// The title bar shortens a title that does not fit beside the window buttons, and macOS centres the title, so the
		// title gets room for them on both sides.
		const QFontMetrics metrics(QApplication::font("QMdiSubWindowTitleBar"));
		int width = qMax(dialog.sizeHint().width(), metrics.horizontalAdvance(title) + 10 * metrics.height());
		if (const QScreen* screen = dialog.screen())
			width = qMin(width, screen->availableGeometry().width());
		dialog.resize(width, dialog.sizeHint().height());
		return dialog.exec() == QDialog::Accepted ? dialog.textValue() : QString();
	}

private:
	std::function<bool()> m_suppressed;
};

#endif // USERPROMPTS_H
