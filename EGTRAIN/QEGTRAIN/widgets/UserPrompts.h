#ifndef USERPROMPTS_H
#define USERPROMPTS_H

#include <QInputDialog>
#include <QMessageBox>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <cstdio>
#include <functional>
#include <utility>

// The messages and questions that the editors put to the user. A panel gets one from the window, and a test replaces
// the virtual functions to answer without a dialog.
class UserPrompts {
public:
	// `suppressed` tells whether a scripted run has no user to answer. Modal boxes would deadlock such a run, so
	// showError then writes the message to stderr instead.
	explicit UserPrompts(std::function<bool()> suppressed = {}) : m_suppressed(std::move(suppressed)) {}
	virtual ~UserPrompts() = default;

	virtual void showError(QWidget* parent, const QString& title, const QString& message, bool warningIcon = false) {
		if (m_suppressed && m_suppressed()) {
			std::fprintf(stderr, "E2E_DIALOG_SUPPRESSED: %s: %s\n", title.toStdString().c_str(), message.toStdString().c_str());
			std::fflush(stderr);
			return;
		}
		if (warningIcon)
			QMessageBox::warning(parent, title, message);
		else
			QMessageBox::critical(parent, title, message);
	}

	// A Yes or No question whose default answer is No.
	virtual bool confirm(QWidget* parent, const QString& title, const QString& question) {
		return QMessageBox::question(parent, title, question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
			== QMessageBox::Yes;
	}

	// One of `items`, for example the id of a rolling stock unit; empty when the user cancels.
	virtual QString chooseItem(QWidget* parent, const QString& title, const QString& label, const QStringList& items) {
		bool accepted = false;
		const QString chosen = QInputDialog::getItem(parent, title, label, items, 0, false, &accepted);
		return accepted ? chosen : QString();
	}

private:
	std::function<bool()> m_suppressed;
};

#endif // USERPROMPTS_H
