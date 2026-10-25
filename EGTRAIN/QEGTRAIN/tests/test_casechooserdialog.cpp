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
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

// A scene folder that the chooser lists: it reads only the manifest name.
QString makeScene(const QDir& root, const QString& folder, const QString& name) {
	root.mkpath(folder);
	QFile manifest(root.filePath(folder + QStringLiteral("/scene.json")));
	if (!manifest.open(QIODevice::WriteOnly))
		qFatal("cannot write a scene manifest");
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
// names are and however large the font. The result is the first way in which it does not, or empty.
QString fitProblem(const CaseChooserDialog& dialog, const QRect& screen) {
	if (dialog.width() > screen.width() * 9 / 10 || dialog.height() > screen.height() * 4 / 5)
		return QStringLiteral("the dialog is larger than the screen budget");
	const auto* buttons = dialog.findChild<QDialogButtonBox*>();
	const auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("dialogBodyScroll"));
	if (!(buttons && scroll && buttons->isVisible() && dialog.rect().contains(buttons->geometry())
			&& scroll->geometry().bottom() < buttons->geometry().top()))
		return QStringLiteral("the footer is not visible below the body");
	// With nothing to open, Open is disabled and, outside a macOS footer, Qt hands the default to the focused button.
	if (dialog.openButton()->isEnabled()) {
		if (!dialog.openButton()->isDefault())
			return QStringLiteral("Open is enabled but not the default button");
		if (faceGray(dialog.openButton()) + 60 >= faceGray(dialog.continueButton()))
			return QStringLiteral("Open does not have the dark face of the default button");
	}
	if (!dialog.openButton()->isVisible() || !dialog.continueButton()->isVisible())
		return QStringLiteral("a footer button is hidden");
	const QListWidget* list = dialog.list();
	if (!list->isVisible() || list->height() <= 0)
		return QStringLiteral("the list is not shown");
	return QString();
}

// The rows that cannot be chosen are bold headings and italic labels; a case row keeps the list font. The result is the
// first row that does not, or empty.
QString rowStyleProblem(const QListWidget* list) {
	for (int row = 0; row < list->count(); ++row) {
		const QListWidgetItem* item = list->item(row);
		const QString text = item->text();
		const QFont font = item->font();
		const bool selectable = !item->data(Qt::UserRole).toString().isEmpty();
		if (selectable) {
			if (!(item->flags() & Qt::ItemIsEnabled))
				return QStringLiteral("a case row is not enabled");
			if (item->data(Qt::FontRole).isValid())
				return QStringLiteral("a case row has a font of its own");
			continue;
		}
		const bool unavailable = text.startsWith(QLatin1String("Unavailable: "));
		// An unavailable row can be selected so that its reason shows, but it has no path to open.
		if (item->flags() != (unavailable ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags))
			return unavailable ? QStringLiteral("an unavailable row cannot be selected")
							   : QStringLiteral("a heading or message row can be selected or reached with the keyboard");
		const bool heading = text == QLatin1String("Bundled cases") || text == QLatin1String("Recent cases");
		if (font.bold() != heading || font.italic() != !heading)
			return heading ? QStringLiteral("a group heading is not bold") : QStringLiteral("an unavailable or message row is not italic and plain");
	}
	return QString();
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
// The result is the first way in which the row does not, or empty.
QString longRowProblem(const CaseChooserDialog& dialog, const QString& name, const QString& path, int row) {
	const QListWidget* list = dialog.list();
	const QListWidgetItem* item = list->item(row);
	if (!item || item->text() != name)
		return QStringLiteral("the row text is not the manifest name");
	if (list->horizontalScrollBarPolicy() != Qt::ScrollBarAlwaysOff || list->horizontalScrollBar()->isVisible()
		|| list->horizontalScrollBar()->maximum() != 0)
		return QStringLiteral("the list has a horizontal scroll bar");
	if (list->visualItemRect(item).width() > list->viewport()->width())
		return QStringLiteral("the row is wider than the list");
	const RowTextProbe probe;
	const QString drawn = probe.drawnText(list, row);
	if (drawn == name || !drawn.contains(QChar(0x2026)) || !drawn.startsWith(name.left(5)) || !drawn.endsWith(name.right(4)))
		return QStringLiteral("the visible text is not the name elided in the middle");
	if (list->fontMetrics().horizontalAdvance(drawn) > list->viewport()->width())
		return QStringLiteral("the visible text is wider than the list");
	const QString tip = shownTip(list->viewport(), list->visualItemRect(item).center());
	if (!tip.contains(name) || !tip.contains(display_path::format(path)))
		return QStringLiteral("the tooltip does not hold the whole name and path with the separators of this system");
	if (item->data(Qt::UserRole).toString() != path)
		return QStringLiteral("the item data is not the full path");
	return QString();
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

// Opens one dialog and checks the layout, and keeps it open for further checks. `problem` is the first way in which the
// layout is wrong, or empty.
struct Shown {
	CaseChooserDialog dialog;
	QString problem;
	Shown(const CaseChooserDialog::Options& options, const QRect& screen)
		: dialog(options) {
		dialog.show();
		QApplication::processEvents();
		problem = fitProblem(dialog, screen);
		if (problem.isEmpty())
			problem = rowStyleProblem(dialog.list());
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

// Selects the row and gives the contrast ratio of its drawn text against the highlight. The highlight is the colour at
// the right end of the row, the text colour the pixel of the text area farthest from it.
double selectedRowContrast(QListWidget* list, QListWidgetItem* item) {
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
	return contrastRatio(ink, highlight);
}

// The lines of a tooltip. The rich text breaks the line, so the name and the path are compared as the two lines of the
// tooltip.
QStringList tipLines(QString tip) {
	return tip.replace(QChar::LineSeparator, QLatin1Char('\n')).split(QLatin1Char('\n'));
}

// The application font at 12 points times scale until the result goes out of scope.
[[nodiscard]] auto useScaledFont(qreal scale) {
	const QFont original = QApplication::font();
	QFont font = original;
	font.setPointSizeF(12 * scale);
	QApplication::setFont(font);
	return qScopeGuard([original] { QApplication::setFont(original); });
}

// The layout checks run with the application font at 12 points and at 1.5 times that.
void addScaleRows() {
	QTest::addColumn<qreal>("scale");
	QTest::newRow("1.0x") << qreal(1.0);
	QTest::newRow("1.5x") << qreal(1.5);
}

const QRect kScreen(0, 0, 1280, 800);

} // namespace

// The case chooser lists the bundled and the recent cases within the screen, at two font sizes, and opens the chosen case
// with the keyboard, the mouse and its buttons. The application style sheet is loaded for every check.
class TestCaseChooserDialog : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void nothingLoaded_data() { addScaleRows(); }
	void nothingLoaded();
	void longLoadedName_data() { addScaleRows(); }
	void longLoadedName();
	void missingRecentCases_data() { addScaleRows(); }
	void missingRecentCases();
	void emptyChooser_data() { addScaleRows(); }
	void emptyChooser();
	void onlyUnavailable_data() { addScaleRows(); }
	void onlyUnavailable();
	void spellingsOfOnePath_data() { addScaleRows(); }
	void spellingsOfOnePath();
	void fortyBundledCases_data() { addScaleRows(); }
	void fortyBundledCases();
	void longCaseTitle_data() { addScaleRows(); }
	void longCaseTitle();
	void arrowKeysSkipHeadings();
	void selectedRowsStayReadable();
	void tooltipsShowNamesAsTyped();
	void tooltipsShowNativeSeparators();
	void doubleClickOpensRow();
	void openButtonOpensSelectedRow();
	void escapeChoosesNothing();
	void leftButtonChoosesNothing();
	void tabReachesMenuAndFooter();
	void openIsDefaultWhileTabbing();
	void bundlesAreNamedWithoutExtension();

private:
	CaseChooserDialog::Options layoutOptions() const;
	CaseChooserDialog::Options interactionOptions() const;

	Scenarios m_scenarios;
};

void TestCaseChooserDialog::initTestCase() {
	QFile qss(QStringLiteral(EGTRAIN_DIALOG_QSS));
	QVERIFY2(qss.open(QIODevice::ReadOnly), "cannot load application QSS");
	// The test binary has no Qt resources, so point the stylesheet at the icon files.
	QString styleSheet = QString::fromUtf8(qss.readAll());
	styleSheet.replace(QStringLiteral(":/icons/"), QFileInfo(qss).absolutePath() + QStringLiteral("/../icons/"));
	qApp->setStyleSheet(styleSheet);
	m_scenarios.bundled(QStringLiteral("alpha"), QStringLiteral("Alpha case"));
	m_scenarios.bundled(QStringLiteral("beta"), QStringLiteral("Beta case"));
}

// The bundled scenes, without a loaded case or recent cases.
CaseChooserDialog::Options TestCaseChooserDialog::layoutOptions() const {
	CaseChooserDialog::Options options;
	options.sceneRoots = QStringList{m_scenarios.scenes.path()};
	options.availableGeometry = kScreen;
	return options;
}

// The bundled scenes, and a missing and an existing recent case.
CaseChooserDialog::Options TestCaseChooserDialog::interactionOptions() const {
	CaseChooserDialog::Options options;
	options.sceneRoots = QStringList{m_scenarios.scenes.path()};
	options.recentPaths = QStringList{m_scenarios.missing.first(), m_scenarios.outside};
	options.availableGeometry = kScreen;
	return options;
}

// No case loaded, nothing recent.
void TestCaseChooserDialog::nothingLoaded() {
	QFETCH(qreal, scale);
	const auto font = useScaledFont(scale);
	Shown shown(layoutOptions(), kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
	const QListWidget* list = shown.dialog.list();
	// The list has the bundled group first, and no recent group without a recent case.
	QCOMPARE(rowTexts(list).first(), "Bundled cases");
	QVERIFY(!rowTexts(list).contains(QLatin1String("Recent cases")));
	// The left button is Cancel.
	QCOMPARE(shown.dialog.continueButton()->text(), "Cancel");
	bool offersContinue = false;
	for (const QPushButton* button : shown.dialog.findChildren<QPushButton*>())
		offersContinue = offersContinue || button->text() == QLatin1String("Continue");
	QVERIFY2(!offersContinue, "a dialog with no case loaded offers Continue");
	QVERIFY2(!shown.dialog.findChild<QLabel*>(QStringLiteral("dialogContext")), "a dialog with no case loaded has a context line");
	// The first case is selected with Open enabled.
	QCOMPARE(currentText(list), "Alpha case");
	QVERIFY(shown.dialog.openButton()->isEnabled());
}

// A loaded case with a 400 character name: the context line wraps and no button repeats the name.
void TestCaseChooserDialog::longLoadedName() {
	QFETCH(qreal, scale);
	const auto font = useScaledFont(scale);
	CaseChooserDialog::Options loaded = layoutOptions();
	loaded.currentCaseName = QString(400, QLatin1Char('N'));
	Shown shown(loaded, kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
	// The left button is Continue.
	QCOMPARE(shown.dialog.continueButton()->text(), "Continue");
	// The Continue button is not as wide as the name.
	QCOMPARE_LT(shown.dialog.continueButton()->width(), kScreen.width() / 4);
	const auto* context = shown.dialog.findChild<QLabel*>(QStringLiteral("dialogContext"));
	if (context == nullptr)
		QFAIL("the context line does not name the loaded case");
	// The context line names the loaded case.
	QCOMPARE(context->text(), QStringLiteral("Current case: ") + loaded.currentCaseName);
}

// Recent cases that are all missing: they stay in the list as unavailable and cannot be chosen.
void TestCaseChooserDialog::missingRecentCases() {
	QFETCH(qreal, scale);
	const auto font = useScaledFont(scale);
	CaseChooserDialog::Options recents = layoutOptions();
	recents.recentPaths = m_scenarios.missing;
	Shown shown(recents, kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
	const QStringList texts = rowTexts(shown.dialog.list());
	// The missing recent cases are listed as unavailable.
	QCOMPARE(texts.mid(texts.size() - 3),
		QStringList({QStringLiteral("Recent cases"), QStringLiteral("Unavailable: gone case"), QStringLiteral("Unavailable: other gone case")}));
	// An unavailable row does not change the selection.
	QCOMPARE(currentText(shown.dialog.list()), "Alpha case");
	QVERIFY(shown.dialog.openButton()->isEnabled());
	QVERIFY2(statusText(shown.dialog).isEmpty(), "a reason shows while a case row is selected");
	// A click selects the unavailable row, says why it cannot be opened and turns Open off; Enter and a double click
	// open nothing, not the case that was selected before.
	QListWidget* list = shown.dialog.list();
	list->setFocus();
	click(list, list->item(static_cast<int>(texts.size()) - 2));
	// An unavailable row is selected with a reason and Open disabled.
	QCOMPARE(currentText(list), "Unavailable: gone case");
	QVERIFY(!shown.dialog.openButton()->isEnabled());
	QVERIFY(shown.dialog.selectedPath().isEmpty());
	QVERIFY(!statusText(shown.dialog).isEmpty());
	sendKey(list, Qt::Key_Return);
	doubleClick(list, list->item(static_cast<int>(texts.size()) - 2));
	// Enter or a double click on an unavailable row opens no case.
	QVERIFY(shown.dialog.isVisible());
	QCOMPARE(shown.dialog.outcome(), CaseChooserDialog::Outcome::Continue);
	QVERIFY(shown.dialog.chosenPath().isEmpty());
	sendKey(list, Qt::Key_Up);
	sendKey(list, Qt::Key_Up);
	// The reason goes and Open is enabled again after leaving the unavailable row.
	QCOMPARE(currentText(list), "Alpha case");
	QVERIFY(shown.dialog.openButton()->isEnabled());
	QVERIFY(statusText(shown.dialog).isEmpty());
}

// Nothing to list at all: Open stays disabled and Enter opens nothing.
void TestCaseChooserDialog::emptyChooser() {
	QFETCH(qreal, scale);
	const auto font = useScaledFont(scale);
	CaseChooserDialog::Options none;
	none.availableGeometry = kScreen;
	Shown shown(none, kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
	const QListWidget* list = shown.dialog.list();
	// An empty chooser shows the single message row.
	QCOMPARE(rowTexts(list), QStringList({QStringLiteral("No cases found. Use Other ways to open... below.")}));
	// An empty chooser has no selection and a disabled Open button.
	QVERIFY(!list->currentItem());
	QVERIFY(!shown.dialog.openButton()->isEnabled());
	QVERIFY(shown.dialog.selectedPath().isEmpty());
	// The other ways to open are there.
	QCOMPARE(shown.dialog.otherMenu()->actions().size(), 3);
	shown.dialog.list()->setFocus();
	sendKey(shown.dialog.list(), Qt::Key_Down);
	sendKey(shown.dialog.list(), Qt::Key_Return);
	// Down and Enter in an empty chooser select or open nothing.
	QVERIFY(shown.dialog.isVisible());
	QCOMPARE(shown.dialog.outcome(), CaseChooserDialog::Outcome::Continue);
	QVERIFY(!shown.dialog.list()->currentItem());
}

// Nothing but unavailable recent cases: Open stays disabled and Enter opens nothing.
void TestCaseChooserDialog::onlyUnavailable() {
	QFETCH(qreal, scale);
	const auto font = useScaledFont(scale);
	CaseChooserDialog::Options gone;
	gone.recentPaths = m_scenarios.missing;
	gone.availableGeometry = kScreen;
	Shown shown(gone, kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
	// Unavailable recent cases alone are listed under their heading.
	QCOMPARE(rowTexts(shown.dialog.list()),
		QStringList({QStringLiteral("Recent cases"), QStringLiteral("Unavailable: gone case"), QStringLiteral("Unavailable: other gone case")}));
	// No heading or unavailable row is selected, and Open is disabled.
	QVERIFY(!shown.dialog.list()->currentItem());
	QVERIFY(!shown.dialog.openButton()->isEnabled());
	doubleClick(shown.dialog.list(), shown.dialog.list()->item(0));
	// A double click on a heading opens or selects nothing.
	QVERIFY(shown.dialog.isVisible());
	QVERIFY(shown.dialog.chosenPath().isEmpty());
	QVERIFY(!shown.dialog.list()->currentItem());
	doubleClick(shown.dialog.list(), shown.dialog.list()->item(1));
	// A double click on an unavailable row opens nothing and gives a reason.
	QVERIFY(shown.dialog.isVisible());
	QVERIFY(shown.dialog.chosenPath().isEmpty());
	QCOMPARE(currentText(shown.dialog.list()), "Unavailable: gone case");
	QVERIFY(!shown.dialog.openButton()->isEnabled());
	QVERIFY(!statusText(shown.dialog).isEmpty());
	shown.dialog.list()->setFocus();
	sendKey(shown.dialog.list(), Qt::Key_Return);
	// Enter on an unavailable row opens nothing.
	QVERIFY(shown.dialog.isVisible());
	QCOMPARE(shown.dialog.outcome(), CaseChooserDialog::Outcome::Continue);
}

// Several spellings of one path list one row, also when the case is missing; a different path stays.
void TestCaseChooserDialog::spellingsOfOnePath() {
	QFETCH(qreal, scale);
	const auto font = useScaledFont(scale);
	CaseChooserDialog::Options spelled;
	spelled.availableGeometry = kScreen;
	const QString gone = m_scenarios.missing.first();
	const QString outside = m_scenarios.outside;
	const QString elsewhere = QFileInfo(outside).absolutePath();
	spelled.recentPaths = QStringList{gone, gone + QLatin1Char('/'), QFileInfo(gone).absolutePath() + QStringLiteral("/./gone_case"),
		outside, outside + QLatin1Char('/'), elsewhere + QStringLiteral("/../elsewhere/recent_one"), m_scenarios.missing.last()};
#ifdef Q_OS_WIN
	spelled.recentPaths << gone.toUpper() << outside.toUpper();
#endif
	Shown shown(spelled, kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
	// The spellings of one path list one row each.
	QCOMPARE(rowTexts(shown.dialog.list()),
		QStringList({QStringLiteral("Recent cases"), QStringLiteral("Unavailable: gone case"), QStringLiteral("Recent One"),
			QStringLiteral("Unavailable: other gone case")}));
}

// Forty bundled cases: the list scrolls and the footer stays.
void TestCaseChooserDialog::fortyBundledCases() {
	QFETCH(qreal, scale);
	const auto font = useScaledFont(scale);
	Scenarios many;
	for (int i = 0; i < 40; ++i)
		many.bundled(QStringLiteral("many_%1").arg(i, 2, 10, QLatin1Char('0')),
			QStringLiteral("Bundled case number %1").arg(i + 1));
	CaseChooserDialog::Options bundled = layoutOptions();
	bundled.sceneRoots = QStringList{many.scenes.path()};
	Shown shown(bundled, kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
	// Every bundled case is listed.
	QCOMPARE(shown.dialog.list()->count(), 41);
	const auto* scroll = shown.dialog.findChild<QScrollArea*>(QStringLiteral("dialogBodyScroll"));
	QVERIFY2(shown.dialog.list()->verticalScrollBar()->maximum() > 0 || scroll->verticalScrollBar()->maximum() > 0,
		"forty cases do not scroll");
}

// A case title of 300 characters does not widen the dialog or add a horizontal scroll bar, in the bundled group and in the
// recent group. The row elides the middle of the name; the opened path is complete.
void TestCaseChooserDialog::longCaseTitle() {
	QFETCH(qreal, scale);
	const auto font = useScaledFont(scale);
	const QString longName = QStringLiteral("Start") + QString(291, QLatin1Char('W')) + QStringLiteral("Tail");
	Scenarios one;
	one.bundled(QStringLiteral("long_title"), longName);
	const QString recent = makeScene(QDir(one.temporary.filePath(QStringLiteral("elsewhere"))), QStringLiteral("long_recent"), longName);
	CaseChooserDialog::Options titled = layoutOptions();
	titled.sceneRoots = QStringList{one.scenes.path()};
	titled.recentPaths = QStringList{recent};
	Shown shown(titled, kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
	// The list holds two headings and two cases.
	QCOMPARE(shown.dialog.list()->count(), 4);
	const QString bundledProblem = longRowProblem(shown.dialog, longName, one.scenes.absoluteFilePath(QStringLiteral("long_title")), 1);
	QVERIFY2(bundledProblem.isEmpty(), qPrintable(QStringLiteral("bundled: ") + bundledProblem));
	const QString recentProblem = longRowProblem(shown.dialog, longName, recent, 3);
	QVERIFY2(recentProblem.isEmpty(), qPrintable(QStringLiteral("recent: ") + recentProblem));
	shown.dialog.list()->setCurrentRow(3);
	shown.dialog.openButton()->click();
	// Open returns the full path of the recent case.
	QCOMPARE(shown.dialog.outcome(), CaseChooserDialog::Outcome::OpenSelected);
	QCOMPARE(shown.dialog.chosenPath(), recent);
}

// Arrow keys skip the headings, reach the unavailable row and stop at the first and last case.
void TestCaseChooserDialog::arrowKeysSkipHeadings() {
	Shown shown(interactionOptions(), kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
	QListWidget* list = shown.dialog.list();
	// The rows of the bundled and the recent group.
	QCOMPARE(rowTexts(list),
		QStringList({QStringLiteral("Bundled cases"), QStringLiteral("Alpha case"), QStringLiteral("Beta case"),
			QStringLiteral("Recent cases"), QStringLiteral("Unavailable: gone case"), QStringLiteral("Recent One")}));
	list->setFocus();
	const QStringList down = {QStringLiteral("Beta case"), QStringLiteral("Unavailable: gone case"), QStringLiteral("Recent One"),
		QStringLiteral("Recent One")};
	const QStringList up = {QStringLiteral("Unavailable: gone case"), QStringLiteral("Beta case"), QStringLiteral("Alpha case"),
		QStringLiteral("Alpha case")};
	// The first case is selected.
	QCOMPARE(currentText(list), "Alpha case");
	for (const QString& expected : down) {
		sendKey(list, Qt::Key_Down);
		// Down skips the headings.
		QCOMPARE(currentText(list), expected);
	}
	for (const QString& expected : up) {
		sendKey(list, Qt::Key_Up);
		// Up skips the headings.
		QCOMPARE(currentText(list), expected);
	}
	QVERIFY2(shown.dialog.openButton()->isEnabled(), "Open is disabled on a case row");
	// Down then Enter opens the row under the cursor through the default Open button.
	sendKey(list, Qt::Key_Down);
	sendKey(list, Qt::Key_Return);
	// Down then Enter opens the case.
	QVERIFY(!shown.dialog.isVisible());
	QCOMPARE(shown.dialog.result(), int(QDialog::Accepted));
	QCOMPARE(shown.dialog.outcome(), CaseChooserDialog::Outcome::OpenSelected);
	QCOMPARE(shown.dialog.chosenPath(), m_scenarios.scenes.filePath(QStringLiteral("beta")));
}

// A selected case row and a selected unavailable row stay readable.
void TestCaseChooserDialog::selectedRowsStayReadable() {
	Shown shown(interactionOptions(), kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
	QListWidget* list = shown.dialog.list();
	shown.dialog.activateWindow();
	for (const QString& text : {QStringLiteral("Beta case"), QStringLiteral("Unavailable: gone case")}) {
		const QList<QListWidgetItem*> found = list->findItems(text, Qt::MatchExactly);
		QVERIFY2(found.size() == 1, qPrintable(text + QStringLiteral(": a row to select is missing")));
		const double contrast = selectedRowContrast(list, found.first());
		QVERIFY2(contrast >= 4.5,
			qPrintable(QStringLiteral("%1: the text of the selected row has a contrast ratio of %2, below 4.5 against its highlight").arg(text).arg(contrast)));
	}
	const QList<QListWidgetItem*> unavailable = list->findItems(QStringLiteral("Unavailable: gone case"), Qt::MatchExactly);
	// A selected unavailable row is italic.
	QCOMPARE(unavailable.size(), 1);
	QVERIFY(unavailable.first()->isSelected());
	QVERIFY(unavailable.first()->font().italic());
}

// A name or path with "&", "<", ">" or an entity shows as typed in the tooltip of a case row and of an unavailable row.
void TestCaseChooserDialog::tooltipsShowNamesAsTyped() {
	const QString odd = QStringLiteral("A & B &lt;x&gt;");
	Scenarios own;
	const QString oddTitle = QStringLiteral("Odd <b>name</b> & &amp; <x>");
	const QString path = own.bundled(odd, oddTitle);
	const QString gone = own.temporary.filePath(QStringLiteral("moved/gone & &lt;case&gt;"));
	CaseChooserDialog::Options marked = interactionOptions();
	marked.sceneRoots = QStringList{own.scenes.path()};
	marked.recentPaths = QStringList{gone};
	Shown shown(marked, kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
	const QListWidget* list = shown.dialog.list();
	QString caseTip, goneTip;
	for (int row = 0; row < list->count(); ++row) {
		const QRect rect = list->visualItemRect(list->item(row));
		if (list->item(row)->text() == oddTitle)
			caseTip = shownTip(list->viewport(), rect.center());
		else if (list->item(row)->text().startsWith(QLatin1String("Unavailable: ")))
			goneTip = shownTip(list->viewport(), rect.center());
	}
	// The tooltip of a case row is its name and path as typed.
	QCOMPARE(tipLines(caseTip), QStringList({oddTitle, display_path::format(path)}));
	// The tooltip of an unavailable row is its name and path as typed.
	QCOMPARE(tipLines(goneTip), QStringList({QFileInfo(gone).fileName(), display_path::format(gone)}));
}

// The tooltip shows the path with the separators of Windows, and the stored path stays as it is.
void TestCaseChooserDialog::tooltipsShowNativeSeparators() {
	const QChar savedSeparator = display_path::separator();
	const auto restoreSeparator = qScopeGuard([savedSeparator] { display_path::separator() = savedSeparator; });
	display_path::separator() = QLatin1Char('\\');
	Scenarios own;
	const QString path = own.bundled(QStringLiteral("native_case"), QStringLiteral("Native case"));
	const QString gone = own.temporary.filePath(QStringLiteral("moved/gone_case"));
	CaseChooserDialog::Options windows = interactionOptions();
	windows.sceneRoots = QStringList{own.scenes.path()};
	windows.recentPaths = QStringList{gone};
	Shown shown(windows, kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
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
	const auto backslashes = [](QString text) { return text.replace(QLatin1Char('/'), QLatin1Char('\\')); };
	// The tooltip of a case row shows its path with backslashes.
	QCOMPARE(tipLines(caseTip), QStringList({QStringLiteral("Native case"), backslashes(path)}));
	// The tooltip of an unavailable row shows its path with backslashes.
	QCOMPARE(tipLines(goneTip), QStringList({QStringLiteral("gone case"), backslashes(gone)}));
	// The stored path of a case row is unchanged.
	if (caseItem == nullptr)
		QFAIL("the stored path of a case row changed");
	QCOMPARE(caseItem->data(Qt::UserRole).toString(), path);
}

// A double click opens the row.
void TestCaseChooserDialog::doubleClickOpensRow() {
	Shown shown(interactionOptions(), kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
	doubleClick(shown.dialog.list(), shown.dialog.list()->item(2));
	// A double click opens the case.
	QCOMPARE(shown.dialog.result(), int(QDialog::Accepted));
	QCOMPARE(shown.dialog.outcome(), CaseChooserDialog::Outcome::OpenSelected);
	QCOMPARE(shown.dialog.chosenPath(), m_scenarios.scenes.filePath(QStringLiteral("beta")));
}

// Open opens the selected row.
void TestCaseChooserDialog::openButtonOpensSelectedRow() {
	Shown shown(interactionOptions(), kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
	shown.dialog.list()->setCurrentRow(2);
	shown.dialog.openButton()->click();
	// Open opens the selected case.
	QCOMPARE(shown.dialog.result(), int(QDialog::Accepted));
	QCOMPARE(shown.dialog.outcome(), CaseChooserDialog::Outcome::OpenSelected);
	QCOMPARE(shown.dialog.chosenPath(), m_scenarios.scenes.filePath(QStringLiteral("beta")));
}

// Escape leaves without a path.
void TestCaseChooserDialog::escapeChoosesNothing() {
	Shown shown(interactionOptions(), kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
	sendKey(&shown.dialog, Qt::Key_Escape);
	// Escape chooses nothing.
	QCOMPARE(shown.dialog.result(), int(QDialog::Rejected));
	QCOMPARE(shown.dialog.outcome(), CaseChooserDialog::Outcome::Continue);
	QVERIFY(shown.dialog.chosenPath().isEmpty());
}

// The left button leaves without a path.
void TestCaseChooserDialog::leftButtonChoosesNothing() {
	Shown shown(interactionOptions(), kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
	shown.dialog.continueButton()->click();
	// Cancel chooses nothing.
	QCOMPARE(shown.dialog.result(), int(QDialog::Rejected));
	QCOMPARE(shown.dialog.outcome(), CaseChooserDialog::Outcome::Continue);
	QVERIFY(shown.dialog.chosenPath().isEmpty());
}

// After the list, Tab reaches the menu button and then the two footer buttons, whatever order the platform gives them.
void TestCaseChooserDialog::tabReachesMenuAndFooter() {
	Shown shown(interactionOptions(), kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
	QList<const QPushButton*> buttons;
	for (const QWidget* widget = shown.dialog.list()->nextInFocusChain(); widget != shown.dialog.list(); widget = widget->nextInFocusChain()) {
		const auto* button = qobject_cast<const QPushButton*>(widget);
		if (button && button->isVisible() && (button->focusPolicy() & Qt::TabFocus))
			buttons << button;
	}
	const auto* other = shown.dialog.findChild<QPushButton*>(QStringLiteral("caseChooserOtherButton"));
	// Tab goes from the list to the menu button and then to Open and the left button.
	QCOMPARE(buttons.size(), 3);
	QCOMPARE(buttons.first(), other);
	QVERIFY((buttons[1] == shown.dialog.openButton() && buttons[2] == shown.dialog.continueButton())
		|| (buttons[1] == shown.dialog.continueButton() && buttons[2] == shown.dialog.openButton()));
}

// With the footer of Windows and Linux, which the offscreen platform gives, a footer button is the default while it has
// the focus. At every other stop of Tab, the list and the menu button included, Open is the default, so Return on the
// menu button opens the selected case.
void TestCaseChooserDialog::openIsDefaultWhileTabbing() {
	CaseChooserDialog::Options loaded = interactionOptions();
	loaded.currentCaseName = QStringLiteral("Loaded case");
	Shown shown(loaded, kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
	shown.dialog.activateWindow();
	auto* other = shown.dialog.findChild<QPushButton*>(QStringLiteral("caseChooserOtherButton"));
	shown.dialog.openButton()->setFocus();
	bool defaults = true;
	for (int stop = 0; stop < 4 && shown.dialog.focusWidget(); ++stop) {
		sendKey(shown.dialog.focusWidget(), Qt::Key_Tab);
		const bool onContinue = shown.dialog.focusWidget() == shown.dialog.continueButton();
		defaults = defaults && shown.dialog.openButton()->isDefault() != onContinue && shown.dialog.continueButton()->isDefault() == onContinue
			&& !other->isDefault();
	}
	QVERIFY2(defaults, "Open is not the default while the list or Other ways to open... has the focus");
	other->setFocus();
	// Return on a menu button that took the default would open its menu and wait.
	if (shown.dialog.openButton()->isDefault() && !other->isDefault()) {
		sendKey(other, Qt::Key_Return);
		// Return on Other ways to open... opens the selected case.
		QCOMPARE(shown.dialog.outcome(), CaseChooserDialog::Outcome::OpenSelected);
		QCOMPARE(shown.dialog.chosenPath(), m_scenarios.scenes.filePath(QStringLiteral("alpha")));
	}
}

// A bundle is named without its extension, in the chooser and in the label that sceneDisplayName gives the Recent Cases
// menu; a folder is named by its manifest.
void TestCaseChooserDialog::bundlesAreNamedWithoutExtension() {
	const QString bundle = m_scenarios.temporary.filePath(QStringLiteral("elsewhere/Saved_case.EGSCENE"));
	QFile file(bundle);
	if (!file.open(QIODevice::WriteOnly))
		qFatal("cannot write a bundle file");
	file.close();
	const QString goneBundle = m_scenarios.temporary.filePath(QStringLiteral("moved/Gone_bundle.egscene"));
	// A bundle is named without its extension.
	QCOMPARE(sceneDisplayName(bundle), "Saved case");
	// A missing bundle is named without its extension.
	QCOMPARE(sceneDisplayName(goneBundle), "Gone bundle");
	// A folder is named by its manifest.
	QCOMPARE(sceneDisplayName(m_scenarios.outside), "Recent One");
	CaseChooserDialog::Options options;
	options.recentPaths = QStringList{bundle, goneBundle, m_scenarios.outside};
	options.availableGeometry = kScreen;
	Shown shown(options, kScreen);
	QVERIFY2(shown.problem.isEmpty(), qPrintable(shown.problem));
	// The recent rows are named without the extension.
	QCOMPARE(rowTexts(shown.dialog.list()),
		QStringList({QStringLiteral("Recent cases"), QStringLiteral("Saved case"), QStringLiteral("Unavailable: Gone bundle"),
			QStringLiteral("Recent One")}));
}

QTEST_MAIN(TestCaseChooserDialog)
#include "test_casechooserdialog.moc"
