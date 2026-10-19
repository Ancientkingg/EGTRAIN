#include "widgets/CaseChooserDialog.h"
#include "widgets/DisplayPath.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHelpEvent>
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
#include <QTextDocument>
#include <QToolTip>

#include <algorithm>
#include <cmath>
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
	QMouseEvent event(type, QPointF(point), QPointF(widget->mapToGlobal(point)), Qt::LeftButton,
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

// The text that a tooltip shows once the pointer rests on `point` of `widget`, without the break opportunities.
QString shownTip(QWidget* widget, const QPoint& point) {
	QHelpEvent help(QEvent::ToolTip, point, widget->mapToGlobal(point));
	QApplication::sendEvent(widget, &help);
	const QString text = QToolTip::text();
	QToolTip::hideText();
	if (!Qt::mightBeRichText(text))
		return text;
	QTextDocument document;
	document.setHtml(text);
	return document.toPlainText().remove(QChar(0x200B));
}

QStringList rowTexts(const QListWidget* list) {
	QStringList texts;
	for (int row = 0; row < list->count(); ++row)
		texts << list->item(row)->text();
	return texts;
}

void click(QListWidget* list, QListWidgetItem* item) {
	const QPoint point = list->visualItemRect(item).center();
	sendMouse(list->viewport(), QEvent::MouseButtonPress, point);
	sendMouse(list->viewport(), QEvent::MouseButtonRelease, point);
	QApplication::processEvents();
}

// The reason that the dialog shows under the list; empty when no reason is shown.
QString statusText(const CaseChooserDialog& dialog) {
	const auto* status = dialog.findChild<QLabel*>(QStringLiteral("caseChooserStatus"));
	return status && status->isVisible() ? status->text() : QString();
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
		const bool unavailable = text.startsWith(QLatin1String("Unavailable: "));
		// An unavailable row can be selected so that its reason shows, but it has no path to open.
		check(item->flags() == (unavailable ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags), scenario,
			unavailable ? "an unavailable row cannot be selected" : "a heading or message row can be selected or reached with the keyboard");
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
	const QString tip = shownTip(list->viewport(), list->visualItemRect(item).center());
	check(tip.contains(name) && tip.contains(display_path::format(path)), scenario,
		"the tooltip does not hold the whole name and path with the separators of this system");
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

// WCAG relative luminance of a colour.
double luminance(QRgb color) {
	auto channel = [](int value) {
		const double c = value / 255.0;
		return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
	};
	return 0.2126 * channel(qRed(color)) + 0.7152 * channel(qGreen(color)) + 0.0722 * channel(qBlue(color));
}

double contrastRatio(QRgb first, QRgb second) {
	const double a = luminance(first);
	const double b = luminance(second);
	return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

// Selects the row and checks that its drawn text has a contrast ratio of at least 4.5 against the highlight. The
// highlight is the colour at the right end of the row, the text colour the pixel of the text area farthest from it.
void checkSelectedRowContrast(const QString& scenario, QListWidget* list, QListWidgetItem* item) {
	list->setFocus();
	list->setCurrentItem(item);
	QApplication::processEvents();
	const QImage image = list->viewport()->grab().toImage();
	const double ratio = image.devicePixelRatio();
	const QRect row = list->visualItemRect(item);
	const QRgb highlight = image.pixel(qRound((row.right() - 6) * ratio), qRound(row.center().y() * ratio));
	QRgb ink = highlight;
	int farthest = 0;
	for (int y = qRound((row.top() + 2) * ratio); y < qRound((row.bottom() - 2) * ratio); ++y) {
		for (int x = qRound((row.left() + 4) * ratio); x < qRound((row.left() + 160) * ratio); ++x) {
			const int distance = std::abs(qGray(image.pixel(x, y)) - qGray(highlight));
			if (distance > farthest) {
				farthest = distance;
				ink = image.pixel(x, y);
			}
		}
	}
	check(contrastRatio(ink, highlight) >= 4.5, scenario, "the text of the selected row has a contrast ratio below 4.5 against its highlight");
}

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
		check(statusText(shown.dialog).isEmpty(), scenario, "a reason shows while a case row is selected");
		// A click selects the unavailable row, says why it cannot be opened and turns Open off; Enter and a double click
		// open nothing, not the case that was selected before.
		QListWidget* list = shown.dialog.list();
		list->setFocus();
		click(list, list->item(texts.size() - 2));
		check(currentText(list) == QLatin1String("Unavailable: gone case") && !shown.dialog.openButton()->isEnabled()
				&& shown.dialog.selectedPath().isEmpty() && !statusText(shown.dialog).isEmpty(),
			scenario, "an unavailable row is not selected with a reason and Open disabled");
		sendKey(list, Qt::Key_Return);
		doubleClick(list, list->item(texts.size() - 2));
		check(shown.dialog.isVisible() && shown.dialog.outcome() == CaseChooserDialog::Outcome::Continue
				&& shown.dialog.chosenPath().isEmpty(),
			scenario, "Enter or a double click on an unavailable row opened a case");
		sendKey(list, Qt::Key_Up);
		sendKey(list, Qt::Key_Up);
		check(currentText(list) == QLatin1String("Alpha case") && shown.dialog.openButton()->isEnabled() && statusText(shown.dialog).isEmpty(),
			scenario, "the reason stays or Open stays disabled after leaving the unavailable row");
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
		doubleClick(shown.dialog.list(), shown.dialog.list()->item(0));
		check(shown.dialog.isVisible() && shown.dialog.chosenPath().isEmpty() && !shown.dialog.list()->currentItem(), scenario,
			"a double click on a heading opened or selected it");
		doubleClick(shown.dialog.list(), shown.dialog.list()->item(1));
		check(shown.dialog.isVisible() && shown.dialog.chosenPath().isEmpty() && currentText(shown.dialog.list()) == QLatin1String("Unavailable: gone case")
				&& !shown.dialog.openButton()->isEnabled() && !statusText(shown.dialog).isEmpty(),
			scenario, "a double click on an unavailable row opened it or gave no reason");
		shown.dialog.list()->setFocus();
		sendKey(shown.dialog.list(), Qt::Key_Return);
		check(shown.dialog.isVisible() && shown.dialog.outcome() == CaseChooserDialog::Outcome::Continue, scenario,
			"Enter on an unavailable row opened something");
	}

	// 4b. Several spellings of one path list one row, also when the case is missing; a different path stays.
	{
		const QString scenario = tag + QStringLiteral(" spellings of one path");
		CaseChooserDialog::Options spelled;
		spelled.availableGeometry = screen;
		const QString gone = s.missing.first();
		const QString elsewhere = QFileInfo(s.outside).absolutePath();
		spelled.recentPaths = QStringList{gone, gone + QLatin1Char('/'), QFileInfo(gone).absolutePath() + QStringLiteral("/./gone_case"),
			s.outside, s.outside + QLatin1Char('/'), elsewhere + QStringLiteral("/../elsewhere/recent_one"), s.missing.last()};
#ifdef Q_OS_WIN
		spelled.recentPaths << gone.toUpper() << s.outside.toUpper();
#endif
		Shown shown(scenario, spelled, screen);
		check(rowTexts(shown.dialog.list())
				== QStringList({QStringLiteral("Recent cases"), QStringLiteral("Unavailable: gone case"), QStringLiteral("Recent One"),
					QStringLiteral("Unavailable: other gone case")}),
			scenario, "the spellings of one path do not list one row each");
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

	// Arrow keys skip the headings, reach the unavailable row and stop at the first and last case.
	{
		const QString scenario = QStringLiteral("keyboard");
		Shown shown(scenario, options, screen);
		QListWidget* list = shown.dialog.list();
		check(rowTexts(list)
				== QStringList({QStringLiteral("Bundled cases"), QStringLiteral("Alpha case"), QStringLiteral("Beta case"),
					QStringLiteral("Recent cases"), QStringLiteral("Unavailable: gone case"), QStringLiteral("Recent One")}),
			scenario, "unexpected rows");
		list->setFocus();
		const QStringList down = {QStringLiteral("Beta case"), QStringLiteral("Unavailable: gone case"), QStringLiteral("Recent One"),
			QStringLiteral("Recent One")};
		const QStringList up = {QStringLiteral("Unavailable: gone case"), QStringLiteral("Beta case"), QStringLiteral("Alpha case"),
			QStringLiteral("Alpha case")};
		check(currentText(list) == QLatin1String("Alpha case"), scenario, "the first case is not selected");
		for (const QString& expected : down) {
			sendKey(list, Qt::Key_Down);
			check(currentText(list) == expected, scenario, "Down did not skip the headings");
		}
		for (const QString& expected : up) {
			sendKey(list, Qt::Key_Up);
			check(currentText(list) == expected, scenario, "Up did not skip the headings");
		}
		check(shown.dialog.openButton()->isEnabled(), scenario, "Open is disabled on a case row");
		// Down then Enter opens the row under the cursor through the default Open button.
		sendKey(list, Qt::Key_Down);
		sendKey(list, Qt::Key_Return);
		check(!shown.dialog.isVisible() && shown.dialog.result() == QDialog::Accepted
				&& shown.dialog.outcome() == CaseChooserDialog::Outcome::OpenSelected && shown.dialog.chosenPath() == betaPath,
			scenario, "Down then Enter did not open the case");
	}

	// A selected case row and a selected unavailable row stay readable.
	{
		const QString scenario = QStringLiteral("selected row contrast");
		Shown shown(scenario, options, screen);
		QListWidget* list = shown.dialog.list();
		shown.dialog.activateWindow();
		for (const QString& text : {QStringLiteral("Beta case"), QStringLiteral("Unavailable: gone case")}) {
			const QList<QListWidgetItem*> found = list->findItems(text, Qt::MatchExactly);
			check(found.size() == 1, scenario, "a row to select is missing");
			if (found.size() == 1)
				checkSelectedRowContrast(scenario + QLatin1Char(' ') + text, list, found.first());
		}
		const QList<QListWidgetItem*> unavailable = list->findItems(QStringLiteral("Unavailable: gone case"), Qt::MatchExactly);
		check(unavailable.size() == 1 && unavailable.first()->isSelected() && unavailable.first()->font().italic(), scenario,
			"a selected unavailable row is not italic");
	}

	// A name or path with "&", "<", ">" or an entity shows as typed in the tooltip of a case row and of an unavailable row.
	{
		const QString scenario = QStringLiteral("tooltip");
		const QString odd = QStringLiteral("A & B &lt;x&gt;");
		Scenarios own;
		const QString oddTitle = QStringLiteral("Odd <b>name</b> & &amp; <x>");
		const QString path = own.bundled(odd, oddTitle);
		const QString gone = own.temporary.filePath(QStringLiteral("moved/gone & &lt;case&gt;"));
		CaseChooserDialog::Options marked = options;
		marked.sceneRoots = QStringList{own.scenes.path()};
		marked.recentPaths = QStringList{gone};
		Shown shown(scenario, marked, screen);
		const QListWidget* list = shown.dialog.list();
		QString caseTip, goneTip;
		for (int row = 0; row < list->count(); ++row) {
			const QRect rect = list->visualItemRect(list->item(row));
			if (list->item(row)->text() == oddTitle)
				caseTip = shownTip(list->viewport(), rect.center());
			else if (list->item(row)->text().startsWith(QLatin1String("Unavailable: ")))
				goneTip = shownTip(list->viewport(), rect.center());
		}
		// The rich text breaks the line and may add a space after the break, so the lines are compared on their own.
		// The rich text breaks the line, so the name and the path are compared as the two lines of the tooltip.
		const auto lines = [](QString tip) {
			return tip.replace(QChar::LineSeparator, QLatin1Char('\n')).split(QLatin1Char('\n'));
		};
		check(lines(caseTip) == QStringList({oddTitle, display_path::format(path)}), scenario, "the tooltip of a case row is not its name and path as typed");
		check(lines(goneTip) == QStringList({QFileInfo(gone).fileName(), display_path::format(gone)}), scenario,
			"the tooltip of an unavailable row is not its name and path as typed");
	}

	// The tooltip shows the path with the separators of Windows, and the stored path stays as it is.
	{
		const QString scenario = QStringLiteral("native separators");
		const QChar savedSeparator = display_path::separator();
		display_path::separator() = QLatin1Char('\\');
		Scenarios own;
		const QString path = own.bundled(QStringLiteral("native_case"), QStringLiteral("Native case"));
		const QString gone = own.temporary.filePath(QStringLiteral("moved/gone_case"));
		CaseChooserDialog::Options windows = options;
		windows.sceneRoots = QStringList{own.scenes.path()};
		windows.recentPaths = QStringList{gone};
		Shown shown(scenario, windows, screen);
		const QListWidget* list = shown.dialog.list();
		QString caseTip, goneTip;
		const QListWidgetItem* caseItem = nullptr;
		for (int row = 0; row < list->count(); ++row) {
			const QRect rect = list->visualItemRect(list->item(row));
			if (list->item(row)->text() == QLatin1String("Native case")) {
				caseItem = list->item(row);
				caseTip = shownTip(list->viewport(), rect.center());
			} else if (list->item(row)->text().startsWith(QLatin1String("Unavailable: "))) {
				goneTip = shownTip(list->viewport(), rect.center());
			}
		}
		display_path::separator() = savedSeparator;
		const auto backslashes = [](QString text) { return text.replace(QLatin1Char('/'), QLatin1Char('\\')); };
		const auto lines = [](QString tip) {
			return tip.replace(QChar::LineSeparator, QLatin1Char('\n')).split(QLatin1Char('\n'));
		};
		check(lines(caseTip) == QStringList({QStringLiteral("Native case"), backslashes(path)}), scenario,
			"the tooltip of a case row does not show its path with backslashes");
		check(lines(goneTip) == QStringList({QStringLiteral("gone case"), backslashes(gone)}), scenario,
			"the tooltip of an unavailable row does not show its path with backslashes");
		check(caseItem && caseItem->data(Qt::UserRole).toString() == path, scenario, "the stored path of a case row changed");
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

// A bundle is named without its extension, in the chooser and in the label that sceneDisplayName gives the Recent Scenes
// menu; a folder is named by its manifest.
void exerciseBundleLabels(Scenarios& s, const QRect& screen) {
	const QString scenario = QStringLiteral("bundle labels");
	const QString bundle = s.temporary.filePath(QStringLiteral("elsewhere/Saved_case.EGSCENE"));
	QFile file(bundle);
	if (!file.open(QIODevice::WriteOnly)) {
		std::cerr << "cannot write a bundle file\n";
		std::exit(1);
	}
	file.close();
	const QString goneBundle = s.temporary.filePath(QStringLiteral("moved/Gone_bundle.egscene"));
	check(sceneDisplayName(bundle) == QLatin1String("Saved case"), scenario, "a bundle is not named without its extension");
	check(sceneDisplayName(goneBundle) == QLatin1String("Gone bundle"), scenario, "a missing bundle is not named without its extension");
	check(sceneDisplayName(s.outside) == QLatin1String("Recent One"), scenario, "a folder is not named by its manifest");
	CaseChooserDialog::Options options;
	options.recentPaths = QStringList{bundle, goneBundle, s.outside};
	options.availableGeometry = screen;
	Shown shown(scenario, options, screen);
	check(rowTexts(shown.dialog.list())
			== QStringList({QStringLiteral("Recent cases"), QStringLiteral("Saved case"), QStringLiteral("Unavailable: Gone bundle"),
				QStringLiteral("Recent One")}),
		scenario, "the recent rows are not named without the extension");
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
	exerciseBundleLabels(scenarios, screen);
	return ok ? 0 : 1;
}
