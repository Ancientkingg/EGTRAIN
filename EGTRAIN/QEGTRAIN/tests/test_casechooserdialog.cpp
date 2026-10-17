#include "widgets/CaseChooserDialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

bool ok = true;

void check(bool condition, const QString& scenario, const char* message) {
	if (condition)
		return;
	ok = false;
	std::cerr << scenario.toStdString() << ": " << message << '\n';
}

// A scene folder that the chooser lists: it reads only the manifest name.
QString makeScene(const QDir& root, const QString& folder, const QString& name) {
	root.mkpath(folder);
	QFile manifest(root.filePath(folder + QStringLiteral("/scene.json")));
	if (!manifest.open(QIODevice::WriteOnly)) {
		std::cerr << "cannot write a scene manifest\n";
		std::exit(1);
	}
	manifest.write(QJsonDocument(QJsonObject{{QStringLiteral("name"), name}}).toJson());
	return root.filePath(folder);
}

void sendKey(QWidget* widget, Qt::Key key) {
	QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
	QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
	QApplication::sendEvent(widget, &press);
	QApplication::sendEvent(widget, &release);
	QApplication::processEvents();
}

void sendMouse(QWidget* widget, QEvent::Type type, const QPoint& point) {
	QMouseEvent event(type, QPointF(point), Qt::LeftButton,
		type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(widget, &event);
}

void doubleClick(QListWidget* list, QListWidgetItem* item) {
	const QPoint point = list->visualItemRect(item).center();
	sendMouse(list->viewport(), QEvent::MouseButtonPress, point);
	sendMouse(list->viewport(), QEvent::MouseButtonRelease, point);
	sendMouse(list->viewport(), QEvent::MouseButtonDblClick, point);
	sendMouse(list->viewport(), QEvent::MouseButtonRelease, point);
	QApplication::processEvents();
}

QStringList rowTexts(const QListWidget* list) {
	QStringList texts;
	for (int row = 0; row < list->count(); ++row)
		texts << list->item(row)->text();
	return texts;
}

QString currentText(const QListWidget* list) {
	return list->currentItem() ? list->currentItem()->text() : QString();
}

// The colour inside the left edge of a button, away from its border and text.
int faceGray(QPushButton* button) {
	const QPixmap grab = button->grab();
	return qGray(grab.toImage().pixel(qRound(6 * grab.devicePixelRatio()), grab.height() / 2));
}

// The dialog stays within 90 percent by 80 percent of the screen with its footer visible, however long the
// names are and however large the font.
void checkFits(const QString& scenario, const CaseChooserDialog& dialog, const QRect& screen) {
	check(dialog.width() <= screen.width() * 9 / 10 && dialog.height() <= screen.height() * 4 / 5, scenario,
		"the dialog is larger than the screen budget");
	const auto* buttons = dialog.findChild<QDialogButtonBox*>();
	const auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("dialogBodyScroll"));
	check(buttons && scroll && buttons->isVisible() && dialog.rect().contains(buttons->geometry())
			&& scroll->geometry().bottom() < buttons->geometry().top(),
		scenario, "the footer is not visible below the body");
	// With nothing to open, Open is disabled and Qt hands the default to the focused button.
	if (dialog.openButton()->isEnabled()) {
		check(dialog.openButton()->isDefault(), scenario, "Open is enabled but not the default button");
		check(faceGray(dialog.openButton()) + 60 < faceGray(dialog.continueButton()), scenario, "Open does not have the dark face of the default button");
	}
	check(dialog.openButton()->isVisible() && dialog.continueButton()->isVisible(), scenario, "a footer button is hidden");
	const QListWidget* list = dialog.list();
	check(list->isVisible() && list->height() > 0, scenario, "the list is not shown");
}

// The rows that cannot be chosen are bold headings and italic labels; a case row keeps the list font.
void checkRowStyles(const QString& scenario, const QListWidget* list) {
	for (int row = 0; row < list->count(); ++row) {
		const QListWidgetItem* item = list->item(row);
		const QString text = item->text();
		const QFont font = item->font();
		const bool selectable = !item->data(Qt::UserRole).toString().isEmpty();
		if (selectable) {
			check(bool(item->flags() & Qt::ItemIsEnabled), scenario, "a case row is not enabled");
			check(!item->data(Qt::FontRole).isValid(), scenario, "a case row has a font of its own");
			continue;
		}
		check(item->flags() == Qt::NoItemFlags, scenario, "a label row can be selected or reached with the keyboard");
		const bool heading = text == QLatin1String("Bundled cases") || text == QLatin1String("Recent cases");
		check(font.bold() == heading && font.italic() == !heading, scenario,
			heading ? "a group heading is not bold" : "an unavailable or message row is not italic and plain");
	}
}

// The text of a list row as the list draws it: the row text elided to the width of the text area of the row.
class RowTextProbe : public QStyledItemDelegate {
public:
	QString drawnText(const QListWidget* list, int row) const {
		const QModelIndex index = list->model()->index(row, 0);
		QStyleOptionViewItem option;
		option.initFrom(list->viewport());
		option.widget = list;
		option.rect = list->visualRect(index);
		option.textElideMode = list->textElideMode();
		initStyleOption(&option, index);
		const QRect textArea = list->style()->subElementRect(QStyle::SE_ItemViewItemText, &option, list);
		return option.fontMetrics.elidedText(option.text, option.textElideMode, textArea.width());
	}
};

// A row with a 300 character name stays inside the list: no horizontal scroll bar, a row that does not extend past the
// viewport, the middle of the name elided and its two ends shown. The tooltip and the item data keep the whole name and path.
void checkLongRow(const QString& scenario, const CaseChooserDialog& dialog, const QString& name, const QString& path, int row) {
	const QListWidget* list = dialog.list();
	const QListWidgetItem* item = list->item(row);
	check(item && item->text() == name, scenario, "the row text is not the manifest name");
	if (!item)
		return;
	check(list->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff && !list->horizontalScrollBar()->isVisible()
			&& list->horizontalScrollBar()->maximum() == 0,
		scenario, "the list has a horizontal scroll bar");
	check(list->visualItemRect(item).width() <= list->viewport()->width(), scenario, "the row is wider than the list");
	const RowTextProbe probe;
	const QString drawn = probe.drawnText(list, row);
	check(drawn != name && drawn.contains(QChar(0x2026)) && drawn.startsWith(name.left(5)) && drawn.endsWith(name.right(4)), scenario,
		"the visible text is not the name elided in the middle");
	check(list->fontMetrics().horizontalAdvance(drawn) <= list->viewport()->width(), scenario, "the visible text is wider than the list");
	check(item->toolTip().contains(name.toHtmlEscaped()) && item->toolTip().contains(path.toHtmlEscaped()), scenario,
		"the tooltip does not hold the whole name and path");
	check(item->data(Qt::UserRole).toString() == path, scenario, "the item data is not the full path");
}

struct Scenarios {
	QTemporaryDir temporary;
	QDir scenes;
	QString outside;
	QStringList missing;
	Scenarios() {
		scenes = QDir(temporary.filePath(QStringLiteral("Scenes")));
		scenes.mkpath(QStringLiteral("."));
		const QDir elsewhere(temporary.filePath(QStringLiteral("elsewhere")));
		outside = makeScene(elsewhere, QStringLiteral("recent_one"), QStringLiteral("Recent One"));
		missing = QStringList{temporary.filePath(QStringLiteral("moved/gone_case")), temporary.filePath(QStringLiteral("moved/other_gone_case"))};
	}
	QString bundled(const QString& folder, const QString& name) { return makeScene(scenes, folder, name); }
};

// Opens one dialog, checks the layout, and returns it still open for further checks.
struct Shown {
	CaseChooserDialog dialog;
	Shown(const QString& scenario, const CaseChooserDialog::Options& options, const QRect& screen)
		: dialog(options) {
		dialog.show();
		QApplication::processEvents();
		checkFits(scenario, dialog, screen);
		checkRowStyles(scenario, dialog.list());
	}
};

// The six states of the chooser at one font size.
void exerciseLayouts(Scenarios& s, const QRect& screen, const QString& tag) {
	const QStringList roots = {s.scenes.path()};
	const QString longName = QStringLiteral("Start") + QString(291, QLatin1Char('W')) + QStringLiteral("Tail");
	CaseChooserDialog::Options options;
	options.sceneRoots = roots;
	options.availableGeometry = screen;

	// 1. No case loaded, nothing recent.
	{
		const QString scenario = tag + QStringLiteral(" nothing loaded");
		Shown shown(scenario, options, screen);
		const QListWidget* list = shown.dialog.list();
		check(rowTexts(list).first() == QLatin1String("Bundled cases") && !rowTexts(list).contains(QLatin1String("Recent cases")),
			scenario, "the list has no bundled group first, or lists a recent group without a recent case");
		check(shown.dialog.continueButton()->text() == QLatin1String("Cancel"), scenario, "the left button is not Cancel");
		bool offersContinue = false;
		for (const QPushButton* button : shown.dialog.findChildren<QPushButton*>())
			offersContinue = offersContinue || button->text() == QLatin1String("Continue");
		check(!offersContinue, scenario, "a dialog with no case loaded offers Continue");
		check(shown.dialog.findChild<QLabel*>(QStringLiteral("dialogContext"))->text() == QLatin1String("Choose a case to open."),
			scenario, "wrong context line");
		check(currentText(list) == QLatin1String("Alpha case") && shown.dialog.openButton()->isEnabled(), scenario,
			"the first case is not selected with Open enabled");
	}

	// 2. A loaded case with a 400 character name: the context line wraps and no button repeats the name.
	{
		const QString scenario = tag + QStringLiteral(" long loaded name");
		CaseChooserDialog::Options loaded = options;
		loaded.currentCaseName = QString(400, QLatin1Char('N'));
		Shown shown(scenario, loaded, screen);
		check(shown.dialog.continueButton()->text() == QLatin1String("Continue"), scenario, "the left button is not Continue");
		check(shown.dialog.continueButton()->width() < screen.width() / 4, scenario, "the Continue button is as wide as the name");
		check(shown.dialog.findChild<QLabel*>(QStringLiteral("dialogContext"))->text().contains(loaded.currentCaseName),
			scenario, "the context line does not name the loaded case");
	}

	// 3. Recent cases that are all missing: they stay in the list as unavailable and cannot be chosen.
	{
		const QString scenario = tag + QStringLiteral(" missing recent cases");
		CaseChooserDialog::Options recents = options;
		recents.recentPaths = s.missing;
		Shown shown(scenario, recents, screen);
		const QStringList texts = rowTexts(shown.dialog.list());
		check(texts.mid(texts.size() - 3)
				== QStringList({QStringLiteral("Recent cases"), QStringLiteral("Unavailable: gone case"),
					QStringLiteral("Unavailable: other gone case")}),
			scenario, "the missing recent cases are not listed as unavailable");
		check(currentText(shown.dialog.list()) == QLatin1String("Alpha case") && shown.dialog.openButton()->isEnabled(), scenario,
			"an unavailable row changed the selection");
	}

	// 4. Nothing to list at all, and nothing but unavailable recent cases: Open stays disabled and Enter opens nothing.
	{
		const QString scenario = tag + QStringLiteral(" empty");
		CaseChooserDialog::Options none;
		none.availableGeometry = screen;
		Shown shown(scenario, none, screen);
		const QListWidget* list = shown.dialog.list();
		check(rowTexts(list) == QStringList({QStringLiteral("No cases found. Use Other ways to open... below.")}), scenario,
			"an empty chooser does not show the single message row");
		check(!list->currentItem() && !shown.dialog.openButton()->isEnabled() && shown.dialog.selectedPath().isEmpty(), scenario,
			"an empty chooser has a selection or an enabled Open button");
		check(shown.dialog.otherMenu()->actions().size() == 3, scenario, "the other ways to open are missing");
		shown.dialog.list()->setFocus();
		sendKey(shown.dialog.list(), Qt::Key_Down);
		sendKey(shown.dialog.list(), Qt::Key_Return);
		check(shown.dialog.isVisible() && shown.dialog.outcome() == CaseChooserDialog::Outcome::Continue && !shown.dialog.list()->currentItem(),
			scenario, "Down and Enter in an empty chooser selected or opened something");
	}
	{
		const QString scenario = tag + QStringLiteral(" only unavailable");
		CaseChooserDialog::Options gone;
		gone.recentPaths = s.missing;
		gone.availableGeometry = screen;
		Shown shown(scenario, gone, screen);
		check(rowTexts(shown.dialog.list())
				== QStringList({QStringLiteral("Recent cases"), QStringLiteral("Unavailable: gone case"),
					QStringLiteral("Unavailable: other gone case")}),
			scenario, "unavailable recent cases alone are not listed under their heading");
		check(!shown.dialog.list()->currentItem() && !shown.dialog.openButton()->isEnabled(), scenario,
			"a heading or an unavailable row is selected or Open is enabled");
		doubleClick(shown.dialog.list(), shown.dialog.list()->item(1));
		doubleClick(shown.dialog.list(), shown.dialog.list()->item(0));
		check(shown.dialog.isVisible() && shown.dialog.chosenPath().isEmpty() && !shown.dialog.list()->currentItem(), scenario,
			"a double click on a heading or an unavailable row opened or selected it");
	}

	// 5. Forty bundled cases: the list scrolls and the footer stays.
	{
		const QString scenario = tag + QStringLiteral(" forty bundled cases");
		Scenarios many;
		for (int i = 0; i < 40; ++i)
			many.bundled(QStringLiteral("many_%1").arg(i, 2, 10, QLatin1Char('0')),
				QStringLiteral("Bundled case number %1").arg(i + 1));
		CaseChooserDialog::Options bundled = options;
		bundled.sceneRoots = QStringList{many.scenes.path()};
		Shown shown(scenario, bundled, screen);
		check(shown.dialog.list()->count() == 41, scenario, "not every bundled case is listed");
		const auto* scroll = shown.dialog.findChild<QScrollArea*>(QStringLiteral("dialogBodyScroll"));
		check(shown.dialog.list()->verticalScrollBar()->maximum() > 0 || scroll->verticalScrollBar()->maximum() > 0, scenario,
			"forty cases do not scroll");
	}

	// 6. A case title of 300 characters does not widen the dialog or add a horizontal scroll bar, in the bundled group and in the
	// recent group. The row elides the middle of the name; the opened path is complete.
	{
		const QString scenario = tag + QStringLiteral(" long case title");
		Scenarios one;
		one.bundled(QStringLiteral("long_title"), longName);
		const QString recent = makeScene(QDir(one.temporary.filePath(QStringLiteral("elsewhere"))), QStringLiteral("long_recent"), longName);
		CaseChooserDialog::Options titled = options;
		titled.sceneRoots = QStringList{one.scenes.path()};
		titled.recentPaths = QStringList{recent};
		Shown shown(scenario, titled, screen);
		const QListWidget* list = shown.dialog.list();
		check(list->count() == 4, scenario, "the list does not hold two headings and two cases");
		checkLongRow(scenario + QStringLiteral(" bundled"), shown.dialog, longName, one.scenes.absoluteFilePath(QStringLiteral("long_title")), 1);
		checkLongRow(scenario + QStringLiteral(" recent"), shown.dialog, longName, recent, 3);
		shown.dialog.list()->setCurrentRow(3);
		shown.dialog.openButton()->click();
		check(shown.dialog.outcome() == CaseChooserDialog::Outcome::OpenSelected && shown.dialog.chosenPath() == recent, scenario,
			"Open did not return the full path of the recent case");
	}
}

void exerciseInteraction(Scenarios& s, const QRect& screen) {
	CaseChooserDialog::Options options;
	options.sceneRoots = QStringList{s.scenes.path()};
	options.recentPaths = QStringList{s.missing.first(), s.outside};
	options.availableGeometry = screen;
	const QString betaPath = s.scenes.filePath(QStringLiteral("beta"));

	// Arrow keys skip the headings and the unavailable row and stop at the first and last case.
	{
		const QString scenario = QStringLiteral("keyboard");
		Shown shown(scenario, options, screen);
		QListWidget* list = shown.dialog.list();
		check(rowTexts(list)
				== QStringList({QStringLiteral("Bundled cases"), QStringLiteral("Alpha case"), QStringLiteral("Beta case"),
					QStringLiteral("Recent cases"), QStringLiteral("Unavailable: gone case"), QStringLiteral("Recent One")}),
			scenario, "unexpected rows");
		list->setFocus();
		const QStringList down = {QStringLiteral("Beta case"), QStringLiteral("Recent One"), QStringLiteral("Recent One")};
		const QStringList up = {QStringLiteral("Beta case"), QStringLiteral("Alpha case"), QStringLiteral("Alpha case")};
		check(currentText(list) == QLatin1String("Alpha case"), scenario, "the first case is not selected");
		for (const QString& expected : down) {
			sendKey(list, Qt::Key_Down);
			check(currentText(list) == expected, scenario, "Down did not skip the headings and the unavailable row");
		}
		for (const QString& expected : up) {
			sendKey(list, Qt::Key_Up);
			check(currentText(list) == expected, scenario, "Up did not skip the headings and the unavailable row");
		}
		check(shown.dialog.openButton()->isEnabled(), scenario, "Open is disabled on a case row");
		// Down then Enter opens the row under the cursor through the default Open button.
		sendKey(list, Qt::Key_Down);
		sendKey(list, Qt::Key_Return);
		check(!shown.dialog.isVisible() && shown.dialog.result() == QDialog::Accepted
				&& shown.dialog.outcome() == CaseChooserDialog::Outcome::OpenSelected && shown.dialog.chosenPath() == betaPath,
			scenario, "Down then Enter did not open the case");
	}

	// A double click opens the row; Open opens the selected row.
	{
		const QString scenario = QStringLiteral("double click");
		Shown shown(scenario, options, screen);
		doubleClick(shown.dialog.list(), shown.dialog.list()->item(2));
		check(shown.dialog.result() == QDialog::Accepted && shown.dialog.outcome() == CaseChooserDialog::Outcome::OpenSelected
				&& shown.dialog.chosenPath() == betaPath,
			scenario, "a double click did not open the case");
	}
	{
		const QString scenario = QStringLiteral("open button");
		Shown shown(scenario, options, screen);
		shown.dialog.list()->setCurrentRow(2);
		shown.dialog.openButton()->click();
		check(shown.dialog.result() == QDialog::Accepted && shown.dialog.outcome() == CaseChooserDialog::Outcome::OpenSelected
				&& shown.dialog.chosenPath() == betaPath,
			scenario, "Open did not open the selected case");
	}

	// Escape and the left button leave without a path.
	{
		const QString scenario = QStringLiteral("escape");
		Shown shown(scenario, options, screen);
		sendKey(&shown.dialog, Qt::Key_Escape);
		check(shown.dialog.result() == QDialog::Rejected && shown.dialog.outcome() == CaseChooserDialog::Outcome::Continue
				&& shown.dialog.chosenPath().isEmpty(),
			scenario, "Escape chose something");
	}
	{
		const QString scenario = QStringLiteral("left button");
		Shown shown(scenario, options, screen);
		shown.dialog.continueButton()->click();
		check(shown.dialog.result() == QDialog::Rejected && shown.dialog.outcome() == CaseChooserDialog::Outcome::Continue
				&& shown.dialog.chosenPath().isEmpty(),
			scenario, "Cancel chose something");
	}

	// After the list, Tab reaches the menu button and then the two footer buttons, whatever order the platform gives them.
	{
		const QString scenario = QStringLiteral("tab order");
		Shown shown(scenario, options, screen);
		QList<const QPushButton*> buttons;
		for (const QWidget* widget = shown.dialog.list()->nextInFocusChain(); widget != shown.dialog.list(); widget = widget->nextInFocusChain()) {
			const auto* button = qobject_cast<const QPushButton*>(widget);
			if (button && button->isVisible() && (button->focusPolicy() & Qt::TabFocus))
				buttons << button;
		}
		const auto* other = shown.dialog.findChild<QPushButton*>(QStringLiteral("caseChooserOtherButton"));
		check(buttons.size() == 3 && buttons.first() == other
				&& ((buttons[1] == shown.dialog.openButton() && buttons[2] == shown.dialog.continueButton())
					|| (buttons[1] == shown.dialog.continueButton() && buttons[2] == shown.dialog.openButton())),
			scenario, "Tab does not go from the list to the menu button and then to Open and the left button");
	}
}

} // namespace

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	QFile qss(QStringLiteral(EGTRAIN_DIALOG_QSS));
	if (!qss.open(QIODevice::ReadOnly)) {
		std::cerr << "cannot load application QSS\n";
		return 1;
	}
	// The test binary has no Qt resources, so point the stylesheet at the icon files.
	QString styleSheet = QString::fromUtf8(qss.readAll());
	styleSheet.replace(QStringLiteral(":/icons/"), QFileInfo(qss).absolutePath() + QStringLiteral("/../icons/"));
	app.setStyleSheet(styleSheet);

	Scenarios scenarios;
	scenarios.bundled(QStringLiteral("alpha"), QStringLiteral("Alpha case"));
	scenarios.bundled(QStringLiteral("beta"), QStringLiteral("Beta case"));
	const QRect screen(0, 0, 1280, 800);
	const QFont original = QApplication::font();
	for (const qreal scale : {1.0, 1.5}) {
		QFont font = original;
		font.setPointSizeF(12 * scale);
		QApplication::setFont(font);
		exerciseLayouts(scenarios, screen, scale == 1.0 ? QStringLiteral("1.0x") : QStringLiteral("1.5x"));
	}
	QApplication::setFont(original);
	exerciseInteraction(scenarios, screen);
	return ok ? 0 : 1;
}
