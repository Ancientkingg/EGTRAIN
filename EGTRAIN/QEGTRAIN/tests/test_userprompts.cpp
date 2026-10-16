#include "widgets/UserPrompts.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEventLoop>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

#include <functional>
#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

// Calls `answer` with the first visible dialog of type T that appears, and counts it in `shown`.
template <typename T>
static void answerDialog(int& shown, const std::function<void(T*)>& answer) {
	shown = 0;
	QTimer::singleShot(50, qApp, [&shown, answer] {
		for (QWidget* widget : QApplication::topLevelWidgets()) {
			auto* dialog = qobject_cast<T*>(widget);
			if (!dialog || !dialog->isVisible())
				continue;
			++shown;
			answer(dialog);
			break;
		}
	});
}

// Runs the event loop for a while, so that a dialog that was wrongly opened would be answered.
static void waitFor(int milliseconds) {
	QEventLoop loop;
	QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
	loop.exec();
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = true;

	{
		UserPrompts prompts([] { return true; });
		int shown = 0;
		answerDialog<QMessageBox>(shown, [](QMessageBox* box) { box->accept(); });
		prompts.showError(nullptr, "Title", "Message");
		waitFor(150);
		ok &= expect(shown == 0, "a suppressed error opens no dialog");
	}
	{
		UserPrompts prompts;
		QString text;
		QMessageBox::Icon icon = QMessageBox::NoIcon;
		int shown = 0;
		answerDialog<QMessageBox>(shown, [&](QMessageBox* box) {
			text = box->text();
			icon = box->icon();
			box->accept();
		});
		prompts.showError(nullptr, "Title", "Message");
		ok &= expect(shown == 1 && text == "Message" && icon == QMessageBox::Critical,
			"an error shows a critical message box with the message");
		answerDialog<QMessageBox>(shown, [&](QMessageBox* box) {
			icon = box->icon();
			box->accept();
		});
		prompts.showError(nullptr, "Title", "Message", true);
		ok &= expect(shown == 1 && icon == QMessageBox::Warning, "an error can show a warning icon instead");
	}
	{
		UserPrompts prompts([] { return false; });
		int shown = 0;
		answerDialog<QMessageBox>(shown, [](QMessageBox* box) { box->accept(); });
		prompts.showError(nullptr, "Title", "Message");
		ok &= expect(shown == 1, "an error is shown when the run is not scripted");
	}
	{
		UserPrompts prompts;
		bool defaultIsNo = false;
		int shown = 0;
		answerDialog<QMessageBox>(shown, [&](QMessageBox* box) {
			defaultIsNo = box->defaultButton() == box->button(QMessageBox::No);
			box->button(QMessageBox::Yes)->click();
		});
		ok &= expect(prompts.confirm(nullptr, "Delete", "Delete it?"), "Yes confirms");
		ok &= expect(defaultIsNo, "the default answer of a confirmation is No");
		answerDialog<QMessageBox>(shown, [](QMessageBox* box) { box->button(QMessageBox::No)->click(); });
		ok &= expect(!prompts.confirm(nullptr, "Delete", "Delete it?"), "No does not confirm");
	}
	{
		UserPrompts prompts;
		int shown = 0;
		answerDialog<QInputDialog>(shown, [](QInputDialog* dialog) {
			dialog->setTextValue("unit-b");
			dialog->accept();
		});
		ok &= expect(prompts.chooseItem(nullptr, "Add", "Unit:", {"unit-a", "unit-b"}) == "unit-b",
			"the chosen item is returned");
		answerDialog<QInputDialog>(shown, [](QInputDialog* dialog) { dialog->accept(); });
		ok &= expect(prompts.chooseItem(nullptr, "Add", "Unit:", {"unit-a", "unit-b"}) == "unit-a",
			"the first item is chosen when the user changes nothing");
		answerDialog<QInputDialog>(shown, [](QInputDialog* dialog) { dialog->reject(); });
		ok &= expect(prompts.chooseItem(nullptr, "Add", "Unit:", {"unit-a", "unit-b"}).isEmpty(),
			"cancelling the choice returns an empty text");
	}

	if (!ok)
		return 1;
	std::cout << "all user prompt tests passed\n";
	return 0;
}
