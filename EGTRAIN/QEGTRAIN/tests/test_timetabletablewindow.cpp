// The scene model has a member named "signals", so its header comes before any Qt header.
#include "scene/SceneModel.h"

#include "diagrams/RunExports.h"
#include "diagrams/TimetableTableWindow.h"
#include "diagrams/TrainColors.h"
#include "diagrams/TrainFilterButton.h"

#include <QApplication>
#include <QKeyEvent>
#include <QFile>
#include <QFontMetrics>
#include <QHeaderView>
#include <QHelpEvent>
#include <QScreen>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QStyle>
#include <QStyleOptionHeader>
#include <QTableWidget>
#include <QTextDocument>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <iostream>

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

static QPushButton* topButton(QDialog& window, const QString& text) {
	for (auto* button : window.findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly))
		if (button->text() == text) return button;
	return nullptr;
}

static bool buttonFlags(QDialog& window, const QString& prefix) {
	bool ok = true;
	for (auto* button : window.findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly)) {
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
	auto* csv = topButton(window, "Export CSV...");
	auto* png = topButton(window, "Export PNG...");
	auto* table = window.findChild<QTableWidget*>();
	bool ok = expect(csv && png && table, "timetable window has its top bar buttons");
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
	auto* emptyCsv = topButton(empty, "Export CSV...");
	auto* emptyPng = topButton(empty, "Export PNG...");
	if (!expect(emptyCsv && emptyPng, "timetable window without provider has its top bar buttons")) return false;
	ok &= expect(emptyCsv->isHidden(), "a timetable window without a provider has no Export CSV button");
	ok &= expect(emptyPng->isVisible(), "a timetable window without a provider keeps Export PNG");
	ok &= buttonFlags(empty, "timetable window without provider button");
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
		&& context && context->isVisible() && context->text().contains("Case A")
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
	ok &= exerciseDelayColors();
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
