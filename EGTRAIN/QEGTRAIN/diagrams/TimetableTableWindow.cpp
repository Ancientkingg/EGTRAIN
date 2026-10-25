#include "diagrams/TimetableTableWindow.h"
#include "diagrams/ResultFrame.h"
#include "diagrams/RunExports.h"
#include "diagrams/TrainFilterButton.h"
#include "util/TimeFormat.h"

#include <QBrush>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QScreen>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QStyle>
#include <QStyleOptionHeader>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <limits>
#include <utility>

namespace {

constexpr int kSortRole = Qt::UserRole + 1;

// Table item that sorts by a numeric key instead of its display text, so time
// and delay columns order correctly.
class SortableItem : public QTableWidgetItem {
public:
	using QTableWidgetItem::QTableWidgetItem;
	bool operator<(const QTableWidgetItem& other) const override {
		return data(kSortRole).toDouble() < other.data(kSortRole).toDouble();
	}
};

// Missing values sort behind every real value.
SortableItem* makeItem(const QString& text, bool available, double sortKey) {
	auto* item = new SortableItem(text);
	item->setData(kSortRole, available ? sortKey : std::numeric_limits<double>::max());
	item->setFlags(item->flags() & ~Qt::ItemIsEditable);
	return item;
}

} // namespace

TimetableTableWindow::TimetableTableWindow(std::vector<TimetableResultRow> rows,
	long long startOffsetSeconds,
	std::function<std::string(const QStringList&)> csvProvider,
	QWidget* parent)
	: QDialog(parent),
	  m_rows(std::move(rows)),
	  m_startOffset(startOffsetSeconds),
	  m_csvProvider(std::move(csvProvider)) {
	setModal(false);
	setProperty("dialogPresentation", true);
	setProperty("manualSection", QStringLiteral("timetable-results"));
	setWindowTitle("Timetable");
	const QRect available = screen() ? screen()->availableGeometry() : QRect(0, 0, 1280, 800);
	setMaximumSize(available.width() * 9 / 10, available.height() * 4 / 5);

	m_table = new QTableWidget;
	m_table->setColumnCount(9);
	m_table->setHorizontalHeaderLabels({"Train", "Station", "Journey order", "Planned arrival", "Planned departure",
		"Simulated arrival", "Simulated departure", "Arrival delay", "Departure delay"});
	m_table->setAlternatingRowColors(true);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	// Tab leaves the table for the next control; the arrow keys move between the cells.
	m_table->setTabKeyNavigation(false);
	m_table->verticalHeader()->setVisible(false);

	// The table at the left, with the run in the header, and the train filter and the exports in the panel.
	m_frame = new ResultFrame(resultTableArea({m_table}), "timetable", this);
	ResultPanel* panel = m_frame->panel();
	panel->readout()->hide();
	m_trainsButton = new TrainFilterButton(this);
	m_trainsButton->setSizePolicy(QSizePolicy::Expanding, m_trainsButton->sizePolicy().verticalPolicy());
	connect(m_trainsButton, &TrainFilterButton::selectionChanged,
		this, &TimetableTableWindow::applyTrainVisibility);
	panel->addSection(QString(), m_trainsButton);

	auto* exports = new QWidget(this);
	auto* exportRow = new QHBoxLayout(exports);
	exportRow->setContentsMargins(0, 0, 0, 0);
	exportRow->setSpacing(8);
	auto* pngButton = new QPushButton("PNG...", exports);
	pngButton->setObjectName("timetableExportPng");
	pngButton->setAccessibleName("Export PNG");
	pngButton->setToolTip("Save the table as an image");
	connect(pngButton, &QPushButton::clicked, this, &TimetableTableWindow::exportPng);
	auto* csvButton = new QPushButton("CSV...", exports);
	csvButton->setObjectName("timetableExportCsv");
	csvButton->setAccessibleName("Export CSV");
	csvButton->setToolTip("Save the rows of the visible trains as CSV");
	csvButton->setVisible(static_cast<bool>(m_csvProvider));
	connect(csvButton, &QPushButton::clicked, this, &TimetableTableWindow::exportCsv);
	exportRow->addWidget(pngButton, 1);
	exportRow->addWidget(csvButton, 1);
	panel->addSection("Export", exports);

	// No button of the window is a default button, so Enter presses none. The buttons of the panel lie deeper than the
	// direct children of the window, so the search takes every descendant.
	for (QPushButton* button : findChildren<QPushButton*>())
		button->setAutoDefault(false);
	// The window opens with the focus in the table, as the result windows with tabs do.
	m_table->setFocus();

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_frame);

	QSet<QString> seen;
	for (const TimetableResultRow& row : m_rows) {
		const QString id = QString::fromStdString(row.trainId);
		if (!seen.contains(id)) {
			seen.insert(id);
			m_trainOrder.append(id);
		}
	}
	fillTrainFilter();

	// The table is measured with the fonts and frames of the style sheet.
	ensurePolished();
	fillTable();
	// The window opens as the other table windows do; the table scrolls sideways when its columns do not fit.
	resize(qMin(960, maximumWidth()), qMin(640, maximumHeight()));
}

void TimetableTableWindow::setTrainColors(const QHash<QString, QColor>& colors) {
	m_trainColors = colors;
	fillTrainFilter();
	applyTrainVisibility();
}

void TimetableTableWindow::fillTrainFilter() {
	QVector<QPair<QString, QColor>> trains;
	for (const QString& id : m_trainOrder)
		trains.append({id, m_trainColors.value(id)});
	m_trainsButton->setTrains(trains);
}

void TimetableTableWindow::setRunProvenance(RunProvenance provenance) {
	m_runProvenance = std::move(provenance);
	m_hasRunProvenance = true;
	setPresentation(QStringLiteral("Timetable"), runContextText(m_runProvenance));
}

void TimetableTableWindow::setPresentation(const QString& heading, const QString& context) {
	setWindowTitle(heading);
	m_frame->setContext(context);
}

void TimetableTableWindow::fillTable() {
	const auto timeText = [this](const RunResultValue& value) {
		return value.available
			? QString::fromStdString(formatSimTime(static_cast<long long>(value.value), m_startOffset))
			: QStringLiteral("-");
	};
	const auto delayText = [](const RunResultValue& value) {
		if (!value.available)
			return QStringLiteral("-");
		return QString("%1%2 s")
			.arg(value.value >= 0.0 ? "+" : "")
			.arg(value.value, 0, 'f', 0);
	};
	const auto setDelayColor = [](QTableWidgetItem* item, const RunResultValue& value) {
		if (!value.available)
			return;
		if (value.value > 60.0)
			item->setForeground(QBrush(QColor(0x81, 0x35, 0x2d)));
		else if (value.value > 0.0)
			item->setForeground(QBrush(QColor(0x74, 0x45, 0x1c)));
		else
			item->setForeground(QBrush(QColor(0, 100, 0)));
	};

	m_table->setSortingEnabled(false);
	m_table->setRowCount(static_cast<int>(m_rows.size()));
	for (int row = 0; row < static_cast<int>(m_rows.size()); ++row) {
		const TimetableResultRow& result = m_rows[static_cast<std::size_t>(row)];
		const QString trainId = QString::fromStdString(result.trainId);
		auto* trainItem = makeItem(trainId, true, 0.0);
		trainItem->setData(Qt::UserRole, trainId);
		m_table->setItem(row, 0, trainItem);
		m_table->setItem(row, 1, makeItem(QString::fromStdString(result.stationId), true, static_cast<double>(result.journeyIndex)));
		m_table->setItem(row, 2, makeItem(QString::number(result.journeyIndex), true, static_cast<double>(result.journeyIndex)));
		m_table->setItem(row, 3, makeItem(timeText(result.plannedArrivalSeconds), result.plannedArrivalSeconds.available, result.plannedArrivalSeconds.value));
		m_table->setItem(row, 4,
			makeItem(timeText(result.plannedDepartureSeconds), result.plannedDepartureSeconds.available,
				result.plannedDepartureSeconds.value));
		m_table->setItem(row, 5,
			makeItem(timeText(result.simulatedArrivalSeconds), result.simulatedArrivalSeconds.available,
				result.simulatedArrivalSeconds.value));
		m_table->setItem(row, 6,
			makeItem(timeText(result.simulatedDepartureSeconds), result.simulatedDepartureSeconds.available,
				result.simulatedDepartureSeconds.value));
		auto* arrivalDelayItem = makeItem(delayText(result.arrivalDelaySeconds),
			result.arrivalDelaySeconds.available, result.arrivalDelaySeconds.value);
		auto* departureDelayItem = makeItem(delayText(result.departureDelaySeconds),
			result.departureDelaySeconds.available, result.departureDelaySeconds.value);
		setDelayColor(arrivalDelayItem, result.arrivalDelaySeconds);
		setDelayColor(departureDelayItem, result.departureDelaySeconds);
		m_table->setItem(row, 7, arrivalDelayItem);
		m_table->setItem(row, 8, departureDelayItem);
		for (int column = 2; column < m_table->columnCount(); ++column)
			m_table->item(row, column)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
	}
	m_table->resizeColumnsToContents();
	// The header sizes a section for its title in the header font and without the sort arrow, but paints the title in the
	// font of its viewport, which is larger on macOS. Each column is at least as wide as the style makes a section for its
	// title in the viewport font, with room for the sort arrow.
	QHeaderView* header = m_table->horizontalHeader();
	QStyleOptionHeader option;
	option.initFrom(header);
	option.state |= QStyle::State_Horizontal;
	option.orientation = Qt::Horizontal;
	option.fontMetrics = QFontMetrics(header->viewport()->font());
	option.sortIndicator = QStyleOptionHeader::SortDown;
	option.rect = QRect(0, 0, header->defaultSectionSize(), header->sizeHint().height());
	const int arrowWidth = header->style()->subElementRect(QStyle::SE_HeaderArrow, &option, header).width();
	option.sortIndicator = QStyleOptionHeader::None;
	for (int col = 0; col < m_table->columnCount(); ++col) {
		option.text = m_table->horizontalHeaderItem(col)->text();
		const int headerWidth = header->style()->sizeFromContents(QStyle::CT_HeaderSection, &option, QSize(), header).width() + arrowWidth;
		if (m_table->columnWidth(col) < headerWidth)
			m_table->setColumnWidth(col, headerWidth);
	}
	m_table->setSortingEnabled(true);
	applyTrainVisibility();
}

void TimetableTableWindow::applyTrainVisibility() {
	if (!m_table || !m_trainsButton)
		return;
	for (int row = 0; row < m_table->rowCount(); ++row) {
		QTableWidgetItem* trainItem = m_table->item(row, 0);
		if (!trainItem)
			continue;
		const QString trainId = trainItem->data(Qt::UserRole).toString();
		m_table->setRowHidden(row, !m_trainsButton->isTrainVisible(trainId));
	}
}

void TimetableTableWindow::exportCsv() {
	const auto operation = m_telemetryCapture ? m_telemetryCapture() : telemetry::OperationObservation();
	if (!m_csvProvider)
		return;
	const std::string content = m_csvProvider(m_trainsButton->visibleTrainIds());
	if (content.empty()) {
		// The window comes back in front once the message closes.
		const KeepInFront front(this);
		QMessageBox::information(this, "Nothing to export",
			"There is no data to export for the visible trains.");
		return;
	}
	const auto csv = [&content]() -> const std::string& { return content; };
	saveExportInteractive(this, telemetry::ExportKind::Csv, "Export Data", "timetable.csv", operation, csv,
		m_hasRunProvenance ? runProvenanceWriter(m_runProvenance) : ExportWriter());
}

QImage TimetableTableWindow::tableImage() const {
	return fullTableImage(*m_table);
}

void TimetableTableWindow::exportPng() {
	const auto operation = m_telemetryCapture ? m_telemetryCapture() : telemetry::OperationObservation();
	const auto png = [this] { return pngBytes(tableImage()); };
	saveExportInteractive(this, telemetry::ExportKind::Png, "Export Table", "timetable.png", operation, png,
		m_hasRunProvenance ? runProvenanceWriter(m_runProvenance) : ExportWriter());
}
