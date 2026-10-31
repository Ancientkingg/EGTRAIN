// The scene model has a member named "signals", so its header comes before any Qt header.
#include "scene/SceneModel.h"

#include "diagrams/ResultFrame.h"
#include "diagrams/TimetableTableWindow.h"
#include "diagrams/TrainColors.h"
#include "diagrams/TrainFilterButton.h"
#include "widgets/DisplayPath.h"

#include <QApplication>
#include <QKeyEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFontMetrics>
#include <QHeaderView>
#include <QHelpEvent>
#include <QImage>
#include <QScreen>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionHeader>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTest>
#include <QTextDocument>
#include <QToolButton>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

static TrainRunResult runTrain(const char* trainId, const char* serviceId) {
	TrainRunResult train;
	train.trainId = trainId;
	train.serviceId = serviceId;
	return train;
}

// The filter swatch of one train, or an invalid colour when the row has no icon.
static QColor swatchColor(const QListWidget* list, int row) {
	const QIcon icon = list->item(row)->icon();
	return icon.isNull() ? QColor() : icon.pixmap(12, 12).toImage().pixelColor(6, 6);
}

// An export button of the panel.
static QPushButton* panelButton(QDialog& window, const char* name) {
	auto* panel = window.findChild<QWidget*>("resultPanel");
	return panel ? panel->findChild<QPushButton*>(name) : nullptr;
}

// The push buttons of `window` are neither auto default nor the default.
static void buttonFlags(QDialog& window, const QString& prefix) {
	for (auto* button : window.findChildren<QPushButton*>()) {
		QVERIFY2(!button->autoDefault(), qPrintable(prefix + " is not auto default: " + button->text()));
		QVERIFY2(!button->isDefault(), qPrintable(prefix + " is not the default: " + button->text()));
	}
}

static void pressKey(QWidget* widget, Qt::Key key) {
	QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
	QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
	QApplication::sendEvent(widget, &press);
	QApplication::sendEvent(widget, &release);
}

static void pressTab(QWidget* widget) {
	pressKey(widget, Qt::Key_Tab);
}

// Rows of two trains with the long ids and the times of a real timetable, so that the columns are wider than the table.
static std::vector<TimetableResultRow> twoTrainRows() {
	std::vector<TimetableResultRow> rows;
	for (const char* train : {"Guin-Paim-ALL-STOPS-1-1", "Guin-Paim-EXPRESS-1-1"})
		for (int stop = 0; stop < 4; ++stop) {
			TimetableResultRow row;
			row.trainId = train;
			row.stationId = "Tregonneau_Squiffiec";
			row.journeyIndex = stop + 1;
			row.plannedArrivalSeconds = {true, 600.0 * stop};
			row.plannedDepartureSeconds = {true, 600.0 * stop + 30.0};
			row.simulatedArrivalSeconds = {true, 600.0 * stop + 42.0};
			row.simulatedDepartureSeconds = {true, 600.0 * stop + 72.0};
			row.arrivalDelaySeconds = {true, 42.0};
			row.departureDelaySeconds = {true, 42.0};
			rows.push_back(row);
		}
	return rows;
}

// WCAG contrast ratio of two colours.
static double contrastRatio(const QColor& first, const QColor& second) {
	const auto luminance = [](const QColor& color) {
		const auto channel = [](double c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
		return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
	};
	const double a = luminance(first);
	const double b = luminance(second);
	return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

// The width of the title of `column` as the header paints it: the ink inside the label of the section and left of its
// sort arrow, which some Qt versions draw inside the label, with the section scrolled to the left edge of the header. The
// last pixel row of a section is its border.
static int paintedTitleWidth(QHeaderView& header, int column) {
	header.setOffset(header.sectionPosition(column));
	QApplication::processEvents();
	QStyleOptionHeader option;
	option.initFrom(&header);
	option.state |= QStyle::State_Horizontal;
	option.orientation = Qt::Horizontal;
	option.sortIndicator = header.isSortIndicatorShown() && header.sortIndicatorSection() == column ? QStyleOptionHeader::SortDown
																									: QStyleOptionHeader::None;
	option.rect = QRect(0, 0, header.sectionSize(column), header.height());
	const QRect label = header.style()->subElementRect(QStyle::SE_HeaderLabel, &option, &header);
	int right = label.right();
	if (option.sortIndicator != QStyleOptionHeader::None)
		right = std::min(right, header.style()->subElementRect(QStyle::SE_HeaderArrow, &option, &header).left() - 1);
	const QImage image = header.viewport()->grab(option.rect).toImage();
	const qreal ratio = image.devicePixelRatio();
	const auto pixel = [&image, ratio](int x, int y) { return image.pixelColor(qRound(x * ratio), qRound(y * ratio)); };
	const QColor face = pixel(1, 1);
	int first = right + 1;
	int last = label.left() - 1;
	for (int y = 0; y < header.height() - 1; ++y)
		for (int x = label.left(); x <= right; ++x)
			if (pixel(x, y) != face) {
				first = std::min(first, x);
				last = std::max(last, x);
			}
	return std::max(0, last - first + 1);
}

// Handles events until `done` holds, for at most about a second.
static bool waitFor(const std::function<bool()>& done) {
	for (int round = 0; round < 200 && !done(); ++round) {
		QApplication::processEvents(QEventLoop::AllEvents, 5);
		QThread::msleep(5);
	}
	return done();
}

// What the save dialog of an export showed, and the texts of the warnings after it.
struct ExportDialog {
	QString title;
	QStringList filters;
	QString suggested;
	QStringList warnings;
};

// Clicks `button`, answers the save dialog with `path`, or cancels it for an empty path, and closes the messages that
// follow. A dialog that stays open after the answer is cancelled.
static ExportDialog answerExport(QPushButton* button, const QString& path) {
	ExportDialog shown;
	bool answered = false;
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, [&shown, &answered, &path] {
		if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
			if (box->icon() == QMessageBox::Warning)
				shown.warnings << box->text();
			box->accept();
			return;
		}
		for (QWidget* widget : QApplication::topLevelWidgets())
			if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) {
				if (answered) {
					dialog->reject();
					continue;
				}
				answered = true;
				shown.title = dialog->windowTitle();
				shown.filters = dialog->nameFilters();
				shown.suggested = QFileInfo(dialog->selectedFiles().value(0)).fileName();
				auto* name = dialog->findChild<QLineEdit*>("fileNameEdit");
				if (path.isEmpty() || !name) {
					dialog->reject();
					continue;
				}
				// The whole path in the name field, which also takes a folder that does not exist.
				name->setText(path);
				// Through QDialog, as QFileDialog::accept is protected.
				static_cast<QDialog*>(dialog)->accept();
			}
	});
	timer.start(0);
	if (button) button->click();
	return shown;
}

// The text that the tooltip of `widget` shows once the pointer rests on it.
static QString shownTip(QWidget* widget) {
	const QPoint point(4, 4);
	QHelpEvent help(QEvent::ToolTip, point, widget->mapToGlobal(point));
	QApplication::sendEvent(widget, &help);
	const QString text = QToolTip::text();
	QToolTip::hideText();
	if (!Qt::mightBeRichText(text))
		return text;
	QTextDocument document;
	document.setHtml(text);
	return document.toPlainText();
}

// A window of one row of train-a at station-b with an 18 pt font, of the run of scenario B in case A.
struct RunWindow {
	TimetableTableWindow window;

	RunWindow() : window(oneRow(), 0, {}) {
		QFont scaled = window.font();
		scaled.setPointSizeF(18);
		window.setFont(scaled);
		RunProvenance provenance;
		provenance.caseName = "Case A";
		provenance.appliedScenario = "Scenario B";
		window.setRunProvenance(std::move(provenance));
		window.show();
		QApplication::processEvents();
	}

	static std::vector<TimetableResultRow> oneRow() {
		std::vector<TimetableResultRow> rows(1);
		rows.front().trainId = "train-a";
		rows.front().stationId = "station-b";
		return rows;
	}
};

class TestTimetableTableWindow : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void presentsRunAndData();
	void showsSwatchesOfColouredServices();
	void colorsTrainsOfRun();
	void startsNoExportOnEnter();
	void framesTableLikeChartWindows();
	void namesRun_data();
	void namesRun();
	void delaysReadOnBothRowColours();
	void exportKeepsWindowInFront();
	void exportAsksForFileOfItsKind();
	void showsContextAsTyped_data();
	void showsContextAsTyped();
	// Last, as they change the application fonts.
	void showsHeaderTitlesWhole();
	void tableImageShowsHeadersWhole_data();
	void tableImageShowsHeadersWhole();

private:
	QFont m_applicationFont;
};

void TestTimetableTableWindow::initTestCase() {
	QFile stylesheet(QStringLiteral(EGTRAIN_DIALOG_QSS));
	QVERIFY2(stylesheet.open(QIODevice::ReadOnly), qPrintable("cannot read " + stylesheet.fileName()));
	qApp->setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
	m_applicationFont = QApplication::font();
}

void TestTimetableTableWindow::presentsRunAndData() {
	RunWindow run;
	TimetableTableWindow& window = run.window;
	const auto* table = window.findChild<QTableWidget*>();
	auto* context = window.findChild<QLabel*>("timetableContext");
	const QRect screen = window.screen()->availableGeometry();
	// timetable presentation or data changed
	QVERIFY(window.width() <= screen.width() * 9 / 10);
	QVERIFY(window.height() <= screen.height() * 4 / 5);
	if (context == nullptr)
		QFAIL("the window has no context label");
	QVERIFY(context->isVisible());
	QCOMPARE(context->text(), "Case A / Scenario B");
	QCOMPARE(context->textFormat(), Qt::PlainText);
	if (table == nullptr)
		QFAIL("the window has no table");
	QCOMPARE(table->rowCount(), 1);
	QCOMPARE(table->item(0, 0)->text(), "train-a");
	QVERIFY(window.findChild<TrainFilterButton*>()->isVisible());
	// The sheet paints headers in weight 600; the header widget carries it so header text is measured in that weight.
	const QHeaderView* header = table->horizontalHeader();
	// the header font has the weight that the sheet paints
	if (header == nullptr)
		QFAIL("the table has no header");
	QVERIFY(header->font().bold());
	// the journey order is aligned to the right and the station to the left
	QVERIFY(table->item(0, 2)->textAlignment() & Qt::AlignRight);
	QVERIFY(!(table->item(0, 1)->textAlignment() & Qt::AlignRight));
}

void TestTimetableTableWindow::showsSwatchesOfColouredServices() {
	std::vector<TimetableResultRow> twoTrains(2);
	twoTrains[0].trainId = "train-a";
	twoTrains[1].trainId = "train-b";
	TimetableTableWindow colored(std::move(twoTrains), 0, {});
	const auto* list = colored.findChild<TrainFilterButton*>()->menu()->findChild<QListWidget*>();
	// the train filter has no swatches without colours
	if (list == nullptr)
		QFAIL("the train filter has no list");
	QCOMPARE(list->count(), 2);
	QVERIFY(!swatchColor(list, 0).isValid());
	QVERIFY(!swatchColor(list, 1).isValid());
	colored.setTrainColors({{"train-a", QColor(60, 141, 210)}, {"train-c", QColor(1, 2, 3)}, {"train-b", QColor()}});
	// a train of a coloured service has its swatch and another train has none
	QCOMPARE(list->count(), 2);
	QCOMPARE(list->item(0)->text(), "train-a");
	QCOMPARE(swatchColor(list, 0), QColor(60, 141, 210));
	QVERIFY(!swatchColor(list, 1).isValid());
}

// The map that the result windows receive: one entry for each train of a service with a valid colour.
void TestTimetableTableWindow::colorsTrainsOfRun() {
	SceneModel scene;
	for (const char* color : {"#3C8DD2", "", "not a colour"}) {
		SceneService service;
		service.id = std::string("service-") + std::to_string(scene.services.size());
		service.visualizationColor = color;
		scene.services.push_back(service);
	}
	RunResults run;
	run.trains = {runTrain("service-0-1", "service-0"), runTrain("service-0-2", "service-0"),
		runTrain("service-1-1", "service-1"), runTrain("service-2-1", "service-2"),
		runTrain("legacy-train", ""), runTrain("orphan-1", "service-9")};
	const QHash<QString, QColor> colors = trainColorsForRun(run, scene);
	// only the trains of the coloured service have a colour
	QCOMPARE(colors.size(), 2);
	// every occurrence of a service has its colour
	QCOMPARE(colors.value("service-0-1"), QColor(60, 141, 210));
	QCOMPARE(colors.value("service-0-2"), QColor(60, 141, 210));
	// no colour, an invalid colour, no service and an unknown service give no entry
	QVERIFY(!colors.contains("service-1-1"));
	QVERIFY(!colors.contains("service-2-1"));
	QVERIFY(!colors.contains("legacy-train"));
	QVERIFY(!colors.contains("orphan-1"));
	QVERIFY2(trainColorsForRun(RunResults(), scene).isEmpty(), "an empty run gives an empty map");
}

void TestTimetableTableWindow::startsNoExportOnEnter() {
	int calls = 0;
	const auto provider = [&calls](const QStringList&) { ++calls; return std::string(); };
	std::vector<TimetableResultRow> rows(1);
	rows.front().trainId = "train-a";
	TimetableTableWindow window(rows, 0, provider);
	window.show();
	QApplication::processEvents();
	auto* csv = panelButton(window, "timetableExportCsv");
	auto* png = panelButton(window, "timetableExportPng");
	auto* table = window.findChild<QTableWidget*>();
	// timetable window has its export buttons in the panel
	if (csv == nullptr)
		QFAIL("timetable window has its CSV export button in the panel");
	if (png == nullptr)
		QFAIL("timetable window has its PNG export button in the panel");
	if (table == nullptr)
		QFAIL("timetable window has its table");
	buttonFlags(window, "timetable window button");
	if (QTest::currentTestFailed())
		return;
	// timetable window shows Export CSV with a provider
	QVERIFY(csv->isVisible());
	QVERIFY(csv->isEnabled());
	QVERIFY2(!table->tabKeyNavigation(), "Tab leaves the timetable table");
	table->setCurrentCell(0, 0);
	table->setFocus();
	pressKey(table, Qt::Key_Return);
	pressKey(table, Qt::Key_Enter);
	// Enter in the table starts no CSV export
	QCOMPARE(calls, 0);
	calls = 0;
	pressKey(&window, Qt::Key_Return);
	// Enter in the window starts no CSV export
	QCOMPARE(calls, 0);
	TimetableTableWindow empty(rows, 0, {});
	empty.show();
	QApplication::processEvents();
	auto* emptyCsv = panelButton(empty, "timetableExportCsv");
	auto* emptyPng = panelButton(empty, "timetableExportPng");
	// timetable window without provider has its export buttons
	if (emptyCsv == nullptr)
		QFAIL("timetable window without provider has its CSV export button");
	if (emptyPng == nullptr)
		QFAIL("timetable window without provider has its PNG export button");
	QVERIFY2(emptyCsv->isHidden(), "a timetable window without a provider has no Export CSV button");
	QVERIFY2(emptyPng->isVisible(), "a timetable window without a provider keeps Export PNG");
	buttonFlags(empty, "timetable window without provider button");
}

// The window of the chart windows: the run in the header with only the panel button at its right, the table at the left
// and a panel of 200 px with the train filter without a heading and the exports under the heading Export. The table
// scrolls sideways when its columns do not fit, and Tab goes through the header, the table and the panel.
void TestTimetableTableWindow::framesTableLikeChartWindows() {
	TimetableTableWindow window(twoTrainRows(), 0, [](const QStringList&) { return std::string("csv"); });
	RunProvenance provenance;
	provenance.caseName = "Paimpol";
	provenance.appliedScenario = "baseline";
	window.setRunProvenance(provenance);
	window.show();
	QApplication::processEvents();
	auto* header = window.findChild<QFrame*>("resultHeader");
	auto* panel = window.findChild<QWidget*>("resultPanel");
	auto* table = window.findChild<QTableWidget*>();
	auto* trains = window.findChild<TrainFilterButton*>();
	auto* png = panelButton(window, "timetableExportPng");
	auto* csv = panelButton(window, "timetableExportCsv");
	// the window has a header, a table and a panel
	if (header == nullptr)
		QFAIL("the window has a header");
	if (panel == nullptr)
		QFAIL("the window has a panel");
	if (table == nullptr)
		QFAIL("the window has a table");
	if (trains == nullptr)
		QFAIL("the window has a train filter");
	if (png == nullptr)
		QFAIL("the window has a PNG export");
	if (csv == nullptr)
		QFAIL("the window has a CSV export");
	QStringList headerButtons;
	for (const QToolButton* button : header->findChildren<QToolButton*>())
		headerButtons << button->objectName();
	// the header has only the panel button
	QCOMPARE(headerButtons, QStringList({"timetablePanelToggle"}));
	// the header names the run
	QCOMPARE(window.findChild<QLabel*>("timetableContext")->text(), "Paimpol / baseline");
	// the train filter and the exports lie in the panel and the table beside it
	QVERIFY(panel->isAncestorOf(trains));
	QVERIFY(panel->isAncestorOf(png));
	QVERIFY(panel->isAncestorOf(csv));
	QVERIFY(!panel->isAncestorOf(table));
	// the exports read PNG... and CSV...
	QCOMPARE(png->text(), "PNG...");
	QCOMPARE(png->accessibleName(), "Export PNG");
	QCOMPARE(csv->text(), "CSV...");
	QCOMPARE(csv->accessibleName(), "Export CSV");
	QStringList headings;
	for (const QLabel* label : panel->findChildren<QLabel*>())
		if (label->property("resultSectionHeading").toBool() && label->isVisible())
			headings << label->text();
	// only the exports have a heading
	QCOMPARE(headings, QStringList({"Export"}));
	// the window opens at most 960 px wide with a panel of 200 px
	QVERIFY2(window.width() <= 960, qPrintable(QString("window width %1").arg(window.width())));
	QCOMPARE(panel->width(), 200);
	int columns = 0;
	for (int column = 0; column < table->columnCount(); ++column)
		columns += table->columnWidth(column);
	// the table scrolls sideways when its columns do not fit
	QVERIFY(columns > table->viewport()->width());
	QVERIFY(table->horizontalScrollBar()->maximum() > 0);
	// The table lies inside the margins of the white area beside the panel.
	const QRect inArea = table->geometry();
	// the table keeps the margins of its area
	QCOMPARE(table->parentWidget()->objectName(), "resultTableArea");
	QVERIFY(inArea.left() >= 12);
	QVERIFY(inArea.top() >= 12);
	QVERIFY(table->parentWidget()->width() - inArea.right() > 12);
	QVERIFY2(window.focusWidget() == table, "the window opens with the focus in the table");

	window.findChild<QToolButton*>("timetablePanelToggle")->setFocus(Qt::TabFocusReason);
	QStringList order;
	for (int step = 0; step < 5 && QApplication::focusWidget(); ++step) {
		QWidget* focused = QApplication::focusWidget();
		order << (focused == table ? QStringLiteral("table") : focused == trains ? QStringLiteral("trains")
																				 : focused->objectName());
		pressTab(focused);
	}
	// Tab goes through the header, the table and the panel
	QCOMPARE(order, QStringList({"timetablePanelToggle", "table", "trains", "timetableExportPng", "timetableExportCsv"}));

	// The picture holds every column and only the rows of the visible trains, without row numbers.
	auto* list = trains->menu()->findChild<QListWidget*>();
	list->item(1)->setCheckState(Qt::Unchecked);
	QApplication::processEvents();
	const QImage image = window.tableImage();
	int rows = 0;
	for (int row = 0; row < table->rowCount(); ++row)
		rows += table->isRowHidden(row) ? 0 : table->rowHeight(row);
	// The frame of the picture is a few pixels; row numbers or a hidden row would add more than a row of text.
	const int frame = table->fontMetrics().height() / 2;
	const int width = qRound(image.width() / image.devicePixelRatio());
	const int height = qRound(image.height() / image.devicePixelRatio()) - table->horizontalHeader()->height();
	const QString picture =
		QString("the picture is %1 by %2 px, the columns %3 px and the visible rows %4 px").arg(width).arg(height).arg(columns).arg(rows);
	QVERIFY2(width >= columns, qPrintable(picture));
	QVERIFY2(width < columns + frame, qPrintable(picture));
	QVERIFY2(height >= rows, qPrintable(picture));
	QVERIFY2(height < rows + frame, qPrintable(picture));
	QVERIFY2(rows == 4 * table->rowHeight(0), qPrintable(picture));
}

// The header names the run as the other result windows do: the scenario alone for a case without a name, and "(none)"
// for a run without either.
void TestTimetableTableWindow::namesRun_data() {
	QTest::addColumn<QString>("caseName");
	QTest::addColumn<QString>("scenario");
	QTest::addColumn<QString>("shown");
	QTest::newRow("scenario") << "" << "S" << "S";
	QTest::newRow("case") << "Paimpol" << "" << "Paimpol";
	QTest::newRow("neither") << "" << "" << "(none)";
}

void TestTimetableTableWindow::namesRun() {
	QFETCH(QString, caseName);
	QFETCH(QString, scenario);
	QFETCH(QString, shown);
	TimetableTableWindow window(twoTrainRows(), 0, {});
	RunProvenance provenance;
	provenance.caseName = caseName.toStdString();
	provenance.appliedScenario = scenario.toStdString();
	window.setRunProvenance(provenance);
	const auto* context = window.findChild<QLabel*>("timetableContext");
	// the header of a run reads the case and the scenario
	if (context == nullptr)
		QFAIL("the header has no context label");
	QCOMPARE(context->text(), shown);
	QVERIFY(!context->isHidden());
}

// A late, a slightly late and an early delay each read at 4.5:1 or more on both row colours of the table.
void TestTimetableTableWindow::delaysReadOnBothRowColours() {
	std::vector<TimetableResultRow> rows(3);
	const double delays[] = {90.0, 30.0, -10.0};
	for (std::size_t index = 0; index < rows.size(); ++index) {
		rows[index].trainId = "train-a";
		rows[index].journeyIndex = static_cast<int>(index);
		rows[index].arrivalDelaySeconds = {true, delays[index]};
	}
	TimetableTableWindow window(std::move(rows), 0, {});
	const auto* table = window.findChild<QTableWidget*>();
	// the delay table has three rows
	if (table == nullptr)
		QFAIL("the window has no table");
	QCOMPARE(table->rowCount(), 3);
	for (int row = 0; row < table->rowCount(); ++row) {
		const QColor ink = table->item(row, 7)->foreground().color();
		const QString reads = QString("the delay %1 reads on both row colours").arg(table->item(row, 7)->text());
		QVERIFY2(contrastRatio(ink, table->palette().color(QPalette::Base)) >= 4.5, qPrintable(reads));
		QVERIFY2(contrastRatio(ink, table->palette().color(QPalette::AlternateBase)) >= 4.5, qPrintable(reads));
	}
}

// After the save dialog of an export closes, cancelled or saved, the window is raised and active again, also when the
// main window took the activation while the dialog was open, as AppKit gives it on macOS.
void TestTimetableTableWindow::exportKeepsWindowInFront() {
	QTemporaryDir directory;
	QMainWindow mainWindow;
	mainWindow.resize(500, 400);
	mainWindow.show();
	auto* window = new TimetableTableWindow(twoTrainRows(), 0, [](const QStringList&) { return std::string("csv"); }, &mainWindow);
	window->show();
	window->activateWindow();
	// the window is active before the export
	QVERIFY(directory.isValid());
	QVERIFY(waitFor([window] { return QApplication::activeWindow() == window; }));
	for (const auto& [name, file] : {std::pair("timetableExportCsv", "front.csv"), std::pair("timetableExportPng", "front.png")}) {
		QPushButton* button = panelButton(*window, name);
		const QString saved = QDir(directory.path()).filePath(file);
		for (const QString& path : {QString(), saved}) {
			bool mainActive = false;
			QTimer::singleShot(0, window, [&mainWindow, &mainActive, &path] {
				for (QWidget* widget : QApplication::topLevelWidgets())
					if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) {
						mainWindow.activateWindow();
						mainActive = waitFor([&mainWindow] { return QApplication::activeWindow() == &mainWindow; });
						if (path.isEmpty()) {
							dialog->reject();
						} else {
							dialog->setDirectory(QFileInfo(path).absolutePath());
							dialog->selectFile(QFileInfo(path).fileName());
							// Through QDialog, as QFileDialog::accept is protected.
							static_cast<QDialog*>(dialog)->accept();
						}
					}
			});
			if (button) button->click();
			const bool active = waitFor([window] { return QApplication::activeWindow() == window; });
			const QString front = QString("%1 %2 leaves the window active, though the main window took the activation")
									  .arg(path.isEmpty() ? "a cancelled" : "a saved", name);
			QVERIFY2(button, qPrintable(front));
			QVERIFY2(mainActive, qPrintable(front));
			QVERIFY2(active, qPrintable(front));
		}
		QVERIFY2(QFile::exists(saved), qPrintable(QString("the saved export of %1 is written").arg(name)));
	}
}

// CSV... and PNG... ask for a file of their kind under the name of the table, and add the suffix to a name without it.
// A write that fails says so with the path as the person reads it, here in the Windows form.
void TestTimetableTableWindow::exportAsksForFileOfItsKind() {
	QTemporaryDir directory;
	TimetableTableWindow window(twoTrainRows(), 0, [](const QStringList&) { return std::string("csv"); });
	window.show();
	QPushButton* csvButton = panelButton(window, "timetableExportCsv");
	QPushButton* pngButton = panelButton(window, "timetableExportPng");
	// the window has both exports
	QVERIFY(directory.isValid());
	if (csvButton == nullptr)
		QFAIL("the window has a CSV export");
	if (pngButton == nullptr)
		QFAIL("the window has a PNG export");
	const QChar separator = display_path::separator();
	display_path::separator() = QLatin1Char('\\');
	const auto restoreSeparator = qScopeGuard([separator] { display_path::separator() = separator; });
	const QString csvPath = QDir(directory.path()).filePath("missing/table.csv");
	const ExportDialog csv = answerExport(csvButton, csvPath);
	// CSV... asks for a CSV file
	QCOMPARE(csv.title, "Export Data");
	QCOMPARE(csv.filters, QStringList("CSV File (*.csv)"));
	QCOMPARE(csv.suggested, "timetable.csv");
	// a CSV file that cannot be written is named as the person reads it
	QCOMPARE(csv.warnings, QStringList("Could not save the data to:\n" + display_path::format(csvPath)));
	const QString pngPath = QDir(directory.path()).filePath("missing/table.png");
	const ExportDialog png = answerExport(pngButton, pngPath);
	// PNG... asks for a PNG image
	QCOMPARE(png.title, "Export Table");
	QCOMPARE(png.filters, QStringList("PNG Image (*.png)"));
	QCOMPARE(png.suggested, "timetable.png");
	// an image that cannot be written is named as the person reads it
	QCOMPARE(png.warnings, QStringList("Could not save the image to:\n" + display_path::format(pngPath)));
	display_path::separator() = separator;
	const ExportDialog cancelled = answerExport(csvButton, QString());
	// a cancelled export writes nothing and says nothing
	QCOMPARE(cancelled.title, "Export Data");
	QVERIFY(cancelled.warnings.isEmpty());
	QVERIFY(QDir(directory.path()).isEmpty());
	const QString plain = QDir(directory.path()).filePath("plain");
	// CSV... adds .csv to a name without it
	QVERIFY(answerExport(csvButton, plain).warnings.isEmpty());
	QVERIFY(QFile::exists(plain + ".csv"));
	QVERIFY(!QFile::exists(plain));
	// PNG... adds .png to a name without it
	QVERIFY(answerExport(pngButton, plain).warnings.isEmpty());
	QVERIFY(!QImage(plain + ".png").isNull());
}

void TestTimetableTableWindow::showsContextAsTyped_data() {
	QTest::addColumn<QString>("text");
	QTest::newRow("ampersand and bar") << QStringLiteral("Case A & B | Scenario C");
	QTest::newRow("tags") << QStringLiteral("Train <b>1</b> & <i>2</i>");
	QTest::newRow("entities") << QStringLiteral("Id &lt;x&gt; &amp; <not a tag>");
}

void TestTimetableTableWindow::showsContextAsTyped() {
	QFETCH(QString, text);
	RunWindow run;
	auto* context = run.window.findChild<QLabel*>("timetableContext");
	if (context == nullptr)
		QFAIL("the window has no context label");
	run.window.setPresentation("Timetable", text);
	QApplication::processEvents();
	// the context tooltip shows the context as typed
	QVERIFY(context->isVisible());
	QCOMPARE(shownTip(context), text);
}

// Under the style sheet, with a header font smaller than the application font as on macOS, every title of the timetable
// shows whole, also in the column that the table is sorted by.
void TestTimetableTableWindow::showsHeaderTitlesWhole() {
	QFont headerFont = QApplication::font();
	headerFont.setPointSizeF(headerFont.pointSizeF() * 11 / 13);
	QApplication::setFont(headerFont, "QHeaderView");
	std::vector<TimetableResultRow> rows(3);
	for (std::size_t index = 0; index < rows.size(); ++index) {
		TimetableResultRow& row = rows[index];
		const double arrival = 600.0 * static_cast<double>(index);
		row.trainId = "train-a";
		row.stationId = "station-b";
		row.journeyIndex = static_cast<int>(index) + 1;
		row.plannedArrivalSeconds = {true, arrival};
		row.plannedDepartureSeconds = {true, arrival + 30.0};
		row.simulatedArrivalSeconds = {true, arrival + 42.0};
		row.simulatedDepartureSeconds = {true, arrival + 72.0};
		row.arrivalDelaySeconds = {true, 42.0};
		row.departureDelaySeconds = {true, 42.0};
	}
	TimetableTableWindow window(std::move(rows), 0, {});
	window.show();
	QApplication::processEvents();
	auto* table = window.findChild<QTableWidget*>();
	QHeaderView* header = table->horizontalHeader();
	// A title is cut when a section that is wider by 100 pixels shows more of it.
	const auto cut = [header](int column) {
		const int size = header->sectionSize(column);
		const int painted = paintedTitleWidth(*header, column);
		header->resizeSection(column, size + 100);
		const int whole = paintedTitleWidth(*header, column);
		header->resizeSection(column, size);
		return painted + 1 < whole;
	};
	QStringList cutTitles;
	for (int column = 0; column < table->columnCount(); ++column) {
		if (cut(column))
			cutTitles << table->horizontalHeaderItem(column)->text();
	}
	for (int column = 0; column < table->columnCount(); ++column) {
		table->sortByColumn(column, Qt::AscendingOrder);
		if (cut(column))
			cutTitles << table->horizontalHeaderItem(column)->text() + " (sorted)";
	}
	QVERIFY2(cutTitles.isEmpty(), qPrintable("every header title shows whole, but not " + cutTitles.join(", ")));
}

void TestTimetableTableWindow::tableImageShowsHeadersWhole_data() {
	QTest::addColumn<double>("scale");
	QTest::newRow("default font") << 1.0;
	QTest::newRow("font 1.5 times") << 1.5;
}

// The picture of a table that the Run Results dock exports shows each header whole under the style sheet, at the default
// font and at 1.5 times its size, when the headers have a smaller font of their own, as on macOS.
void TestTimetableTableWindow::tableImageShowsHeadersWhole() {
	QFETCH(double, scale);
	QFont font = m_applicationFont;
	font.setPointSizeF(m_applicationFont.pointSizeF() * scale);
	QApplication::setFont(font);
	QFont headerFont = font;
	headerFont.setPointSizeF(font.pointSizeF() * 11 / 13);
	QApplication::setFont(headerFont, "QHeaderView");
	QTableWidget table(1, 4);
	table.setHorizontalHeaderLabels({"Train", "Maximum speed (km/h)", "Start time (hh:mm:ss)", "Substation with regen (kWh)"});
	table.resizeColumnsToContents();
	table.show();
	QApplication::processEvents();

	const QImage image = fullTableImage(table);
	const qreal ratio = image.devicePixelRatio();
	const auto pixel = [&image, ratio](int x, int y) { return image.pixelColor(qRound(x * ratio), qRound(y * ratio)); };
	int columnsWidth = 0;
	for (int column = 0; column < table.columnCount(); ++column)
		columnsWidth += table.columnWidth(column);
	const int frame = (qRound(image.width() / ratio) - table.verticalHeader()->width() - columnsWidth) / 2;
	const QFontMetrics metrics(table.horizontalHeader()->font());
	// Text drawn whole in the header font is no wider than its advance. Text in a larger font is cut by its section and
	// fills it inside the padding, which is wider by the header margins. The last pixel of a section is its border.
	bool whole = true;
	int left = frame + table.verticalHeader()->width();
	for (int column = 0; column < table.columnCount(); ++column) {
		const int right = left + table.columnWidth(column) - 1;
		const QColor face = pixel(left, frame);
		int first = right;
		int last = left;
		for (int y = frame; y < frame + table.horizontalHeader()->height() - 1; ++y)
			for (int x = left; x < right; ++x)
				if (pixel(x, y) != face) {
					first = std::min(first, x);
					last = std::max(last, x);
				}
		const int advance = metrics.horizontalAdvance(table.horizontalHeaderItem(column)->text());
		whole = whole && last - first < advance + 2;
		left = right + 1;
	}
	QApplication::setFont(m_applicationFont);
	QVERIFY2(whole, qPrintable(QString("the table picture shows every header whole at %1 times the font").arg(scale)));
}

QTEST_MAIN(TestTimetableTableWindow)
#include "test_timetabletablewindow.moc"
