// The scene model has a member named "signals", so its header comes before any Qt header.
#include "scene/SceneModel.h"

#include "diagrams/RunExports.h"
#include "diagrams/TimetableTableWindow.h"
#include "diagrams/TrainColors.h"
#include "diagrams/TrainFilterButton.h"

#include <QApplication>
#include <QKeyEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFontMetrics>
#include <QHeaderView>
#include <QHelpEvent>
#include <QScreen>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionHeader>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTextDocument>
#include <QToolButton>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <tuple>
#include <utility>

static bool expect(bool condition, const char* message) {
	if (!condition) std::cerr << "failed: " << message << "\n";
	return condition;
}

static TrainRunResult runTrain(const char* trainId, const char* serviceId) {
	TrainRunResult train;
	train.trainId = trainId;
	train.serviceId = serviceId;
	return train;
}

// The map that the result windows receive: one entry for each train of a service with a valid colour.
static bool exerciseTrainColorsForRun() {
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
	bool ok = expect(colors.size() == 2, "only the trains of the coloured service have a colour");
	ok &= expect(colors.value("service-0-1") == QColor(60, 141, 210) && colors.value("service-0-2") == QColor(60, 141, 210),
		"every occurrence of a service has its colour");
	ok &= expect(!colors.contains("service-1-1") && !colors.contains("service-2-1") && !colors.contains("legacy-train")
			&& !colors.contains("orphan-1"),
		"no colour, an invalid colour, no service and an unknown service give no entry");
	ok &= expect(trainColorsForRun(RunResults(), scene).isEmpty(), "an empty run gives an empty map");
	return ok;
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

static bool buttonFlags(QDialog& window, const QString& prefix) {
	bool ok = true;
	for (auto* button : window.findChildren<QPushButton*>()) {
		ok &= expect(!button->autoDefault(), qPrintable(prefix + " is not auto default: " + button->text()));
		ok &= expect(!button->isDefault(), qPrintable(prefix + " is not the default: " + button->text()));
	}
	return ok;
}

static void pressKey(QWidget* widget, Qt::Key key) {
	QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
	QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
	QApplication::sendEvent(widget, &press);
	QApplication::sendEvent(widget, &release);
}

static bool exerciseButtonDefaults() {
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
	bool ok = expect(csv && png && table, "timetable window has its export buttons in the panel");
	if (!ok) return false;
	ok &= buttonFlags(window, "timetable window button");
	ok &= expect(csv->isVisible() && csv->isEnabled(), "timetable window shows Export CSV with a provider");
	ok &= expect(!table->tabKeyNavigation(), "Tab leaves the timetable table");
	table->setCurrentCell(0, 0);
	table->setFocus();
	pressKey(table, Qt::Key_Return);
	pressKey(table, Qt::Key_Enter);
	ok &= expect(calls == 0, "Enter in the table starts no CSV export");
	calls = 0;
	pressKey(&window, Qt::Key_Return);
	ok &= expect(calls == 0, "Enter in the window starts no CSV export");
	TimetableTableWindow empty(rows, 0, {});
	empty.show();
	QApplication::processEvents();
	auto* emptyCsv = panelButton(empty, "timetableExportCsv");
	auto* emptyPng = panelButton(empty, "timetableExportPng");
	if (!expect(emptyCsv && emptyPng, "timetable window without provider has its export buttons")) return false;
	ok &= expect(emptyCsv->isHidden(), "a timetable window without a provider has no Export CSV button");
	ok &= expect(emptyPng->isVisible(), "a timetable window without a provider keeps Export PNG");
	ok &= buttonFlags(empty, "timetable window without provider button");
	return ok;
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

// The window of the chart windows: the run in the header with only the panel button at its right, the table at the left
// and a panel of 200 px with the train filter without a heading and the exports under the heading Export. The table
// scrolls sideways when its columns do not fit, and Tab goes through the header, the table and the panel.
static bool exerciseFrame() {
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
	if (!expect(header && panel && table && trains && png && csv, "the window has a header, a table and a panel")) return false;
	QStringList headerButtons;
	for (const QToolButton* button : header->findChildren<QToolButton*>())
		headerButtons << button->objectName();
	bool ok = expect(headerButtons == QStringList({"timetablePanelToggle"}), qPrintable("the header has only the panel button: " + headerButtons.join(", ")));
	ok &= expect(window.findChild<QLabel*>("timetableContext")->text() == "Paimpol / baseline", "the header names the run");
	ok &= expect(panel->isAncestorOf(trains) && panel->isAncestorOf(png) && panel->isAncestorOf(csv) && !panel->isAncestorOf(table),
		"the train filter and the exports lie in the panel and the table beside it");
	ok &= expect(png->text() == "PNG..." && png->accessibleName() == "Export PNG" && csv->text() == "CSV..." && csv->accessibleName() == "Export CSV",
		"the exports read PNG... and CSV...");
	QStringList headings;
	for (const QLabel* label : panel->findChildren<QLabel*>())
		if (label->property("resultSectionHeading").toBool() && label->isVisible())
			headings << label->text();
	ok &= expect(headings == QStringList({"Export"}), qPrintable("only the exports have a heading: " + headings.join(", ")));
	ok &= expect(window.width() <= 960 && panel->width() == 200,
		qPrintable(QString("the window opens at most 960 px wide with a panel of 200 px: %1, %2").arg(window.width()).arg(panel->width())));
	int columns = 0;
	for (int column = 0; column < table->columnCount(); ++column)
		columns += table->columnWidth(column);
	ok &= expect(columns > table->viewport()->width() && table->horizontalScrollBar()->maximum() > 0,
		"the table scrolls sideways when its columns do not fit");
	// The table lies inside the margins of the white area beside the panel.
	const QRect inArea = table->geometry();
	ok &= expect(table->parentWidget()->objectName() == "resultTableArea" && inArea.left() >= 12 && inArea.top() >= 12
			&& table->parentWidget()->width() - inArea.right() > 12,
		"the table keeps the margins of its area");
	ok &= expect(window.focusWidget() == table, "the window opens with the focus in the table");

	window.findChild<QToolButton*>("timetablePanelToggle")->setFocus(Qt::TabFocusReason);
	QStringList order;
	for (int step = 0; step < 5 && QApplication::focusWidget(); ++step) {
		QWidget* focused = QApplication::focusWidget();
		order << (focused == table ? QStringLiteral("table") : focused == trains ? QStringLiteral("trains")
																				 : focused->objectName());
		pressTab(focused);
	}
	ok &= expect(order == QStringList({"timetablePanelToggle", "table", "trains", "timetableExportPng", "timetableExportCsv"}),
		qPrintable("Tab goes through the header, the table and the panel: " + order.join(", ")));

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
	ok &= expect(width >= columns && width < columns + frame && height >= rows && height < rows + frame && rows == 4 * table->rowHeight(0),
		qPrintable(QString("the picture is %1 by %2 px, the columns %3 px and the visible rows %4 px").arg(width).arg(height).arg(columns).arg(rows)));
	return ok;
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

// A late, a slightly late and an early delay each read at 4.5:1 or more on both row colours of the table.
static bool exerciseDelayColors() {
	std::vector<TimetableResultRow> rows(3);
	const double delays[] = {90.0, 30.0, -10.0};
	for (std::size_t index = 0; index < rows.size(); ++index) {
		rows[index].trainId = "train-a";
		rows[index].journeyIndex = static_cast<int>(index);
		rows[index].arrivalDelaySeconds = {true, delays[index]};
	}
	TimetableTableWindow window(std::move(rows), 0, {});
	const auto* table = window.findChild<QTableWidget*>();
	bool ok = expect(table && table->rowCount() == 3, "the delay table has three rows");
	for (int row = 0; ok && row < table->rowCount(); ++row) {
		const QColor ink = table->item(row, 7)->foreground().color();
		ok &= expect(contrastRatio(ink, table->palette().color(QPalette::Base)) >= 4.5
				&& contrastRatio(ink, table->palette().color(QPalette::AlternateBase)) >= 4.5,
			qPrintable(QString("the delay %1 reads on both row colours").arg(table->item(row, 7)->text())));
	}
	return ok;
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

// Under the style sheet, with a header font smaller than the application font as on macOS, every title of the timetable
// shows whole, also in the column that the table is sorted by.
static bool exerciseHeaderTitles() {
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
	return expect(cutTitles.isEmpty(), qPrintable("every header title shows whole, but not " + cutTitles.join(", ")));
}

// The picture of a table that the Run Results dock exports shows each header whole under the style sheet, at the default
// font and at 1.5 times its size, when the headers have a smaller font of their own, as on macOS.
static bool exerciseTableImageHeaders() {
	const QFont original = QApplication::font();
	bool ok = true;
	for (const double scale : {1.0, 1.5}) {
		QFont font = original;
		font.setPointSizeF(original.pointSizeF() * scale);
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
		ok &= expect(whole, qPrintable(QString("the table picture shows every header whole at %1 times the font").arg(scale)));
	}
	QApplication::setFont(original);
	return ok;
}

// The header names the run as the other result windows do: the scenario alone for a case without a name, and "(none)"
// for a run without either.
static bool exerciseRunContext() {
	bool ok = true;
	for (const auto& [caseName, scenario, shown] : {std::tuple("", "S", "S"), std::tuple("Paimpol", "", "Paimpol"), std::tuple("", "", "(none)")}) {
		TimetableTableWindow window(twoTrainRows(), 0, {});
		RunProvenance provenance;
		provenance.caseName = caseName;
		provenance.appliedScenario = scenario;
		window.setRunProvenance(provenance);
		const auto* context = window.findChild<QLabel*>("timetableContext");
		ok &= expect(context && context->text() == shown && !context->isHidden(),
			qPrintable(QString("the header of a run of case \"%1\" and scenario \"%2\" reads %3: %4")
					.arg(caseName, scenario, shown, context ? context->text() : QStringLiteral("missing"))));
	}
	return ok;
}

// Handles events until `done` holds, for at most about a second.
static bool waitFor(const std::function<bool()>& done) {
	for (int round = 0; round < 200 && !done(); ++round) {
		QApplication::processEvents(QEventLoop::AllEvents, 5);
		QThread::msleep(5);
	}
	return done();
}

// After the save dialog of an export closes, cancelled or saved, the window is raised and active again, also when the
// main window took the activation while the dialog was open, as AppKit gives it on macOS.
static bool exerciseExportKeepsWindowInFront() {
	QTemporaryDir directory;
	QMainWindow mainWindow;
	mainWindow.resize(500, 400);
	mainWindow.show();
	auto* window = new TimetableTableWindow(twoTrainRows(), 0, [](const QStringList&) { return std::string("csv"); }, &mainWindow);
	window->show();
	window->activateWindow();
	bool ok = expect(directory.isValid() && waitFor([window] { return QApplication::activeWindow() == window; }),
		"the window is active before the export");
	if (!ok) return false;
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
			ok &= expect(button && mainActive && active,
				qPrintable(QString("%1 %2 leaves the window active, though the main window took the activation")
						.arg(path.isEmpty() ? "a cancelled" : "a saved", name)));
		}
		ok &= expect(QFile::exists(saved), qPrintable(QString("the saved export of %1 is written").arg(name)));
	}
	return ok;
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

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	QFile stylesheet(QStringLiteral(EGTRAIN_DIALOG_QSS));
	if (!stylesheet.open(QIODevice::ReadOnly)) return 1;
	app.setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
	std::vector<TimetableResultRow> rows(1);
	rows.front().trainId = "train-a";
	rows.front().stationId = "station-b";
	TimetableTableWindow window(std::move(rows), 0, {});
	QFont scaled = window.font();
	scaled.setPointSizeF(18);
	window.setFont(scaled);
	RunProvenance provenance;
	provenance.caseName = "Case A";
	provenance.appliedScenario = "Scenario B";
	window.setRunProvenance(std::move(provenance));
	window.show();
	app.processEvents();
	const auto* table = window.findChild<QTableWidget*>();
	auto* context = window.findChild<QLabel*>("timetableContext");
	const QRect screen = window.screen()->availableGeometry();
	bool ok = window.width() <= screen.width() * 9 / 10
		&& window.height() <= screen.height() * 4 / 5
		&& context && context->isVisible() && context->text() == "Case A / Scenario B"
		&& context->textFormat() == Qt::PlainText
		&& table && table->rowCount() == 1 && table->item(0, 0)->text() == "train-a"
		&& window.findChild<TrainFilterButton*>()->isVisible();
	if (!ok) std::cerr << "timetable presentation or data changed\n";
	// The sheet paints headers in weight 600; the header widget carries it so header text is measured in that weight.
	const QHeaderView* header = table ? table->horizontalHeader() : nullptr;
	ok &= expect(header && header->font().bold(), "the header font has the weight that the sheet paints");
	ok &= expect(table && (table->item(0, 2)->textAlignment() & Qt::AlignRight) && !(table->item(0, 1)->textAlignment() & Qt::AlignRight),
		"the journey order is aligned to the right and the station to the left");

	std::vector<TimetableResultRow> twoTrains(2);
	twoTrains[0].trainId = "train-a";
	twoTrains[1].trainId = "train-b";
	TimetableTableWindow colored(std::move(twoTrains), 0, {});
	const auto* list = colored.findChild<TrainFilterButton*>()->menu()->findChild<QListWidget*>();
	ok &= expect(list && list->count() == 2 && !swatchColor(list, 0).isValid() && !swatchColor(list, 1).isValid(),
		"the train filter has no swatches without colours");
	colored.setTrainColors({{"train-a", QColor(60, 141, 210)}, {"train-c", QColor(1, 2, 3)}, {"train-b", QColor()}});
	ok &= expect(list && list->count() == 2 && list->item(0)->text() == "train-a"
			&& swatchColor(list, 0) == QColor(60, 141, 210) && !swatchColor(list, 1).isValid(),
		"a train of a coloured service has its swatch and another train has none");
	ok &= exerciseTrainColorsForRun();
	ok &= exerciseButtonDefaults();
	ok &= exerciseFrame();
	ok &= exerciseRunContext();
	ok &= exerciseDelayColors();
	ok &= exerciseExportKeepsWindowInFront();
	for (const QString& text : {QStringLiteral("Case A & B | Scenario C"), QStringLiteral("Train <b>1</b> & <i>2</i>"),
			 QStringLiteral("Id &lt;x&gt; &amp; <not a tag>")}) {
		window.setPresentation("Timetable", text);
		app.processEvents();
		ok &= expect(context->isVisible() && shownTip(context) == text,
			"the context tooltip shows the context as typed");
	}
	// Last, as they change the application fonts.
	ok &= exerciseHeaderTitles();
	ok &= exerciseTableImageHeaders();
	return ok ? 0 : 1;
}
