#include "widgets/UserPrompts.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListWidget>
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
#include <optional>
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
	void returnsChosenButton();
	void returnsChosenItem();
	void choiceFitsTitleWithinScreen();
	void returnsTypedText();
	void chooseItemsDialog();
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
	QString informativeText;
	QMessageBox::Icon icon = QMessageBox::NoIcon;
	int shown = 0;
	auto answering = answerDialog<QMessageBox>(shown, [&](QMessageBox* box) {
		text = box->text();
		informativeText = box->informativeText();
		icon = box->icon();
		box->accept();
	});
	prompts.showError(nullptr, "Title", "Message");
	// An error shows a critical message box with the title as its main text, as macOS draws no title on a message box,
	// and the message below it.
	QCOMPARE(shown, 1);
	QCOMPARE(text, "Title");
	QCOMPARE(informativeText, "Message");
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

void TestUserPrompts::returnsChosenButton() {
	UserPrompts prompts([] { return true; });
	QString defaultText;
	QStringList texts;
	int shown = 0;
	auto answering = answerDialog<QMessageBox>(shown, [&](QMessageBox* box) {
		defaultText = box->defaultButton() ? box->defaultButton()->text() : QString();
		for (const QAbstractButton* button : box->buttons())
			texts << button->text();
		for (QAbstractButton* button : box->buttons()) {
			if (button->text() == "Folder")
				button->click();
		}
	});
	// The index of the chosen button is returned, also in a suppressed run.
	QCOMPARE(prompts.chooseButton(nullptr, "Save As", "Which format?", {"File", "Folder"}, 0), 1);
	QCOMPARE(shown, 1);
	// The default button is the one asked for.
	QCOMPARE(defaultText, "File");
	// The box has a button for each choice and Cancel.
	QCOMPARE(texts.size(), 3);
	QVERIFY(texts.contains("File") && texts.contains("Folder"));
	answering = answerDialog<QMessageBox>(shown, [&](QMessageBox* box) {
		defaultText = box->defaultButton() ? box->defaultButton()->text() : QString();
		box->button(QMessageBox::Cancel)->click();
	});
	// Cancel returns -1, and the default can be another button.
	QCOMPARE(prompts.chooseButton(nullptr, "Save As", "Which format?", {"File", "Folder"}, 1), -1);
	QCOMPARE(defaultText, "Folder");
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

// The typed text is returned, the starting text when the user changes nothing, and no text when the user cancels.
void TestUserPrompts::returnsTypedText() {
	UserPrompts prompts;
	int shown = 0;
	auto answering = answerDialog<QInputDialog>(shown, [](QInputDialog* dialog) {
		dialog->setTextValue("Guingamp");
		dialog->accept();
	});
	QCOMPARE(prompts.askText(nullptr, "Add Station", "Station name:"), std::optional<QString>("Guingamp"));
	answering = answerDialog<QInputDialog>(shown, [](QInputDialog* dialog) { dialog->accept(); });
	QCOMPARE(prompts.askText(nullptr, "Add Station", "Station name:", "Paimpol"), std::optional<QString>("Paimpol"));
	answering = answerDialog<QInputDialog>(shown, [](QInputDialog* dialog) { dialog->reject(); });
	QVERIFY2(!prompts.askText(nullptr, "Add Station", "Station name:", "Paimpol"), "cancelling returns no text");
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

// The list dialog shows the entries, keeps a disabled entry unchecked, filters by words, returns the checked ids in list
// order and returns nothing on Cancel.
void TestUserPrompts::chooseItemsDialog() {
	UserPrompts prompts([] { return true; });
	const std::vector<UserPrompts::ListChoice> choices = {
		{"node-a0 (track-a, 0.000 km)", "node-a0", true},
		{"node-a1 (track-a, 1.000 km): Beta", "node-a1", false},
		{"node-b0 (track-b, 2.000 km)", "node-b0", true},
	};
	QStringList shownLabels;
	QList<bool> hidden;
	bool offCannotBeChecked = false;
	bool filterHides = false;
	bool filterAnyCase = false;
	bool okIsDefault = false;
	QStringList buttonTexts;
	int shown = 0;
	auto answering = answerDialog<QDialog>(shown, [&](QDialog* dialog) {
		auto* list = dialog->findChild<QListWidget*>("chooseItemsList");
		auto* filter = dialog->findChild<QLineEdit*>("chooseItemsFilter");
		auto* box = dialog->findChild<QDialogButtonBox*>();
		for (int row = 0; row < list->count(); ++row)
			shownLabels << list->item(row)->text();
		offCannotBeChecked = !(list->item(1)->flags() & Qt::ItemIsEnabled);
		okIsDefault = box->button(QDialogButtonBox::Ok)->isDefault();
		for (const QAbstractButton* button : box->buttons())
			buttonTexts << button->text();
		filter->setText("TRACK-B 2.0");
		for (int row = 0; row < list->count(); ++row)
			hidden << list->item(row)->isHidden();
		filter->clear();
		filterHides = hidden == QList<bool>({true, true, false});
		filter->setText("track-a");
		filterAnyCase = !list->item(0)->isHidden() && !list->item(1)->isHidden() && list->item(2)->isHidden();
		filter->clear();
		list->item(2)->setCheckState(Qt::Checked);
		list->item(0)->setCheckState(Qt::Checked);
		dialog->accept();
	});
	const std::optional<QStringList> chosen = prompts.chooseItems(nullptr, "Platform Nodes", "Context.", choices, {"node-a1"});
	// It asks also in a suppressed run, and returns the checked ids in the order of the list, a disabled one included.
	QCOMPARE(shown, 1);
	QVERIFY(chosen.has_value());
	QCOMPARE(*chosen, QStringList({"node-a0", "node-a1", "node-b0"}));
	QCOMPARE(shownLabels, QStringList({"node-a0 (track-a, 0.000 km)", "node-a1 (track-a, 1.000 km): Beta", "node-b0 (track-b, 2.000 km)"}));
	QVERIFY2(offCannotBeChecked, "a disabled entry is not enabled in the list");
	QVERIFY2(filterHides, "the filter hides the entries that lack a word");
	QVERIFY2(filterAnyCase, "the filter ignores case");
	QVERIFY2(okIsDefault, "OK is the default button");
	QCOMPARE(buttonTexts.size(), 2);
	QVERIFY(buttonTexts.contains("OK") && buttonTexts.contains("Cancel"));

	answering = answerDialog<QDialog>(shown, [](QDialog* dialog) {
		dialog->findChild<QListWidget*>("chooseItemsList")->item(0)->setCheckState(Qt::Checked);
		dialog->reject();
	});
	// Cancel returns nothing.
	QVERIFY(!prompts.chooseItems(nullptr, "Platform Nodes", "Context.", choices, {}).has_value());
	QCOMPARE(shown, 1);
}

QTEST_MAIN(TestUserPrompts)
#include "test_userprompts.moc"
