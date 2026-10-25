#include "widgets/UserPrompts.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEventLoop>
#include <QFontMetrics>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QTest>
#include <QTimer>

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Calls `answer` with the first visible dialog of type T, as soon as one is shown, and counts it in `shown`. The timer
// repeats until it finds a dialog; destroying it stops the search.
template <typename T>
static std::unique_ptr<QTimer> answerDialog(int& shown, const std::function<void(T*)>& answer) {
	shown = 0;
	auto timer = std::make_unique<QTimer>();
	QTimer* poll = timer.get();
	QObject::connect(poll, &QTimer::timeout, poll, [poll, &shown, answer] {
		for (QWidget* widget : QApplication::topLevelWidgets()) {
			auto* dialog = qobject_cast<T*>(widget);
			if (!dialog || !dialog->isVisible())
				continue;
			poll->stop();
			++shown;
			answer(dialog);
			break;
		}
	});
	poll->start(10);
	return timer;
}

// Runs the event loop for a while, so that a dialog that was wrongly opened would be answered.
static void waitFor(int milliseconds) {
	QEventLoop loop;
	QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
	loop.exec();
}

class TestUserPrompts : public QObject {
	Q_OBJECT

private slots:
	void suppressedErrorIsLoggedWithoutDialog();
	void showsErrorBox();
	void showsErrorWhenNotScripted();
	void showsInfoBox();
	void suppressedInfoOpensNoDialog();
	void confirmsWithYesOnly();
	void returnsChosenItem();
	void choiceFitsTitleWithinScreen();
};

void TestUserPrompts::suppressedErrorIsLoggedWithoutDialog() {
	UserPrompts prompts([] { return true; });
	int shown = 0;
	std::vector<std::string> errors;
	eglog::setSink([&errors](eglog::Category, eglog::Level level, const std::string& text) {
		if (level == eglog::Level::Error)
			errors.push_back(text);
	});
	const auto answering = answerDialog<QMessageBox>(shown, [](QMessageBox* box) { box->accept(); });
	prompts.showError(nullptr, "Title", "Message");
	// The fixed wait checks that no dialog opens.
	waitFor(150);
	eglog::setSink({});
	// A suppressed error opens no dialog.
	QCOMPARE(shown, 0);
	// An error is logged once.
	QCOMPARE(errors.size(), std::size_t(1));
	QVERIFY(errors.front().find("Title: Message") != std::string::npos);
}

void TestUserPrompts::showsErrorBox() {
	UserPrompts prompts;
	QString text;
	QMessageBox::Icon icon = QMessageBox::NoIcon;
	int shown = 0;
	auto answering = answerDialog<QMessageBox>(shown, [&](QMessageBox* box) {
		text = box->text();
		icon = box->icon();
		box->accept();
	});
	prompts.showError(nullptr, "Title", "Message");
	// An error shows a critical message box with the message.
	QCOMPARE(shown, 1);
	QCOMPARE(text, "Message");
	QCOMPARE(icon, QMessageBox::Critical);
	answering = answerDialog<QMessageBox>(shown, [&](QMessageBox* box) {
		icon = box->icon();
		box->accept();
	});
	prompts.showError(nullptr, "Title", "Message", true);
	// An error can show a warning icon instead.
	QCOMPARE(shown, 1);
	QCOMPARE(icon, QMessageBox::Warning);
}

void TestUserPrompts::showsErrorWhenNotScripted() {
	UserPrompts prompts([] { return false; });
	int shown = 0;
	const auto answering = answerDialog<QMessageBox>(shown, [](QMessageBox* box) { box->accept(); });
	prompts.showError(nullptr, "Title", "Message");
	// An error is shown when the run is not scripted.
	QCOMPARE(shown, 1);
}

void TestUserPrompts::showsInfoBox() {
	UserPrompts prompts;
	QString text;
	QMessageBox::Icon icon = QMessageBox::NoIcon;
	int shown = 0;
	const auto answering = answerDialog<QMessageBox>(shown, [&](QMessageBox* box) {
		text = box->text();
		icon = box->icon();
		box->accept();
	});
	prompts.showInfo(nullptr, "Title", "Message");
	// A message shows an information box with the message.
	QCOMPARE(shown, 1);
	QCOMPARE(text, "Message");
	QCOMPARE(icon, QMessageBox::Information);
}

void TestUserPrompts::suppressedInfoOpensNoDialog() {
	UserPrompts prompts([] { return true; });
	int shown = 0;
	const auto answering = answerDialog<QMessageBox>(shown, [](QMessageBox* box) { box->accept(); });
	prompts.showInfo(nullptr, "Title", "Message");
	// The fixed wait checks that no dialog opens.
	waitFor(150);
	// A suppressed message opens no dialog.
	QCOMPARE(shown, 0);
}

void TestUserPrompts::confirmsWithYesOnly() {
	UserPrompts prompts;
	bool defaultIsNo = false;
	int shown = 0;
	auto answering = answerDialog<QMessageBox>(shown, [&](QMessageBox* box) {
		defaultIsNo = box->defaultButton() == box->button(QMessageBox::No);
		box->button(QMessageBox::Yes)->click();
	});
	QVERIFY2(prompts.confirm(nullptr, "Delete", "Delete it?"), "Yes confirms");
	QVERIFY2(defaultIsNo, "the default answer of a confirmation is No");
	answering = answerDialog<QMessageBox>(shown, [](QMessageBox* box) { box->button(QMessageBox::No)->click(); });
	QVERIFY2(!prompts.confirm(nullptr, "Delete", "Delete it?"), "No does not confirm");
}

void TestUserPrompts::returnsChosenItem() {
	UserPrompts prompts;
	int shown = 0;
	auto answering = answerDialog<QInputDialog>(shown, [](QInputDialog* dialog) {
		dialog->setTextValue("unit-b");
		dialog->accept();
	});
	// The chosen item is returned.
	QCOMPARE(prompts.chooseItem(nullptr, "Add", "Unit:", {"unit-a", "unit-b"}), "unit-b");
	answering = answerDialog<QInputDialog>(shown, [](QInputDialog* dialog) { dialog->accept(); });
	// The first item is chosen when the user changes nothing.
	QCOMPARE(prompts.chooseItem(nullptr, "Add", "Unit:", {"unit-a", "unit-b"}), "unit-a");
	answering = answerDialog<QInputDialog>(shown, [](QInputDialog* dialog) { dialog->reject(); });
	QVERIFY2(prompts.chooseItem(nullptr, "Add", "Unit:", {"unit-a", "unit-b"}).isEmpty(), "cancelling the choice returns an empty text");
}

// The dialog is wide enough for its title and the window buttons beside it, and never wider than the screen.
void TestUserPrompts::choiceFitsTitleWithinScreen() {
	UserPrompts prompts;
	int shown = 0;
	int width = 0;
	int screenWidth = 0;
	const auto measure = [&](QInputDialog* dialog) {
		width = dialog->width();
		screenWidth = dialog->screen()->availableGeometry().width();
		dialog->reject();
	};
	const QFontMetrics title(QApplication::font("QMdiSubWindowTitleBar"));
	auto answering = answerDialog<QInputDialog>(shown, measure);
	prompts.chooseItem(nullptr, "Add Rolling Stock Unit", "Rolling stock unit:", {"Draisy"});
	// The choice is as wide as its title and the window buttons.
	QCOMPARE(shown, 1);
	QCOMPARE_GE(width, title.horizontalAdvance("Add Rolling Stock Unit") + 10 * title.height());
	answering = answerDialog<QInputDialog>(shown, measure);
	prompts.chooseItem(nullptr, QString(300, QLatin1Char('W')), "Rolling stock unit:", {"Draisy"});
	// The choice with a title wider than the screen is as wide as the screen.
	QCOMPARE(shown, 1);
	QCOMPARE(width, screenWidth);
}

QTEST_MAIN(TestUserPrompts)
#include "test_userprompts.moc"
