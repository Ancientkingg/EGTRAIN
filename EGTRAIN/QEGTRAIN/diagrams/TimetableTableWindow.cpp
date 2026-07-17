#include "diagrams/TimetableTableWindow.h"
#include "diagrams/TrainFilterButton.h"
#include "util/TimeFormat.h"

#include <QBrush>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <limits>

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
	setWindowTitle("Timetable: planned vs simulated");
	resize(1080, 640);

	m_trainsButton = new TrainFilterButton(this);
	connect(m_trainsButton, &TrainFilterButton::selectionChanged,
			this, &TimetableTableWindow::applyTrainVisibility);

	QPushButton* csvButton = new QPushButton("Export CSV...", this);
	csvButton->setToolTip("Write the rows of the visible trains to a CSV file");
	csvButton->setEnabled(static_cast<bool>(m_csvProvider));
	connect(csvButton, &QPushButton::clicked, this, &TimetableTableWindow::exportCsv);

	QPushButton* pngButton = new QPushButton("Export PNG...", this);
	pngButton->setToolTip("Save the table as an image");
	connect(pngButton, &QPushButton::clicked, this, &TimetableTableWindow::exportPng);

	QHBoxLayout* topBar = new QHBoxLayout();
	topBar->addWidget(m_trainsButton);
	topBar->addStretch();
	topBar->addWidget(csvButton);
	topBar->addWidget(pngButton);

	m_table = new QTableWidget(this);
	m_table->setColumnCount(8);
	m_table->setHorizontalHeaderLabels({
		"Train", "Station call", "Planned arrival", "Planned departure",
		"Simulated arrival", "Simulated departure", "Arrival delay", "Departure delay"});
	m_table->setAlternatingRowColors(true);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->verticalHeader()->setVisible(false);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->addLayout(topBar);
	layout->addWidget(m_table, 1);

	QStringList trainOrder;
	QSet<QString> seen;
	for (const TimetableResultRow& row : m_rows) {
		const QString id = QString::fromStdString(row.trainId);
		if (!seen.contains(id)) {
			seen.insert(id);
			trainOrder.append(id);
		}
	}
	QVector<QPair<QString, QColor>> trains;
	for (const QString& id : trainOrder)
		trains.append({id, QColor()});
	m_trainsButton->setTrains(trains);

	fillTable();
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
			item->setForeground(QBrush(Qt::red));
		else if (value.value > 0.0)
			item->setForeground(QBrush(Qt::darkYellow));
		else
			item->setForeground(QBrush(Qt::darkGreen));
	};

	m_table->setSortingEnabled(false);
	m_table->setRowCount(static_cast<int>(m_rows.size()));
	for (int row = 0; row < static_cast<int>(m_rows.size()); ++row) {
		const TimetableResultRow& result = m_rows[static_cast<std::size_t>(row)];
		const QString trainId = QString::fromStdString(result.trainId);
		auto* trainItem = makeItem(trainId, true, 0.0);
		trainItem->setData(Qt::UserRole, trainId);
		m_table->setItem(row, 0, trainItem);
		m_table->setItem(row, 1, makeItem(
			QString("%1 (visit %2)").arg(QString::fromStdString(result.stationId)).arg(result.callIndex),
			true, static_cast<double>(result.journeyIndex)));
		m_table->setItem(row, 2, makeItem(timeText(result.plannedArrivalSeconds),
			result.plannedArrivalSeconds.available, result.plannedArrivalSeconds.value));
		m_table->setItem(row, 3, makeItem(timeText(result.plannedDepartureSeconds),
			result.plannedDepartureSeconds.available, result.plannedDepartureSeconds.value));
		m_table->setItem(row, 4, makeItem(timeText(result.simulatedArrivalSeconds),
			result.simulatedArrivalSeconds.available, result.simulatedArrivalSeconds.value));
		m_table->setItem(row, 5, makeItem(timeText(result.simulatedDepartureSeconds),
			result.simulatedDepartureSeconds.available, result.simulatedDepartureSeconds.value));
		auto* arrivalDelayItem = makeItem(delayText(result.arrivalDelaySeconds),
			result.arrivalDelaySeconds.available, result.arrivalDelaySeconds.value);
		auto* departureDelayItem = makeItem(delayText(result.departureDelaySeconds),
			result.departureDelaySeconds.available, result.departureDelaySeconds.value);
		setDelayColor(arrivalDelayItem, result.arrivalDelaySeconds);
		setDelayColor(departureDelayItem, result.departureDelaySeconds);
		m_table->setItem(row, 6, arrivalDelayItem);
		m_table->setItem(row, 7, departureDelayItem);
	}
	m_table->resizeColumnsToContents();
	// Content sizing can undercut the header text; keep every title readable.
	const QFontMetrics metrics(m_table->horizontalHeader()->font());
	for (int col = 0; col < m_table->columnCount(); ++col) {
		const int headerWidth = metrics.horizontalAdvance(m_table->horizontalHeaderItem(col)->text()) + 28;
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
	if (!m_csvProvider)
		return;
	const std::string content = m_csvProvider(m_trainsButton->visibleTrainIds());
	if (content.empty()) {
		QMessageBox::information(this, "Nothing to export",
								 "There is no data to export for the visible trains.");
		return;
	}
	QString path = QFileDialog::getSaveFileName(this, "Export Data", "timetable.csv", "CSV File (*.csv)");
	if (path.isEmpty())
		return;
	if (QFileInfo(path).suffix().compare("csv", Qt::CaseInsensitive) != 0)
		path += ".csv";
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		QMessageBox::warning(this, "Export failed",
							 QString("Could not open the file for writing:\n%1").arg(path));
		return;
	}
	const QByteArray bytes = QByteArray::fromStdString(content);
	if (file.write(bytes) != bytes.size() || !file.commit()) {
		QMessageBox::warning(this, "Export failed",
							 QString("Could not write the data to:\n%1").arg(path));
	}
}

void TimetableTableWindow::exportPng() {
	QString path = QFileDialog::getSaveFileName(this, "Export Table", "timetable.png", "PNG Image (*.png)");
	if (path.isEmpty())
		return;
	if (QFileInfo(path).suffix().compare("png", Qt::CaseInsensitive) != 0)
		path += ".png";
	QPixmap pix = m_table->grab();
	if (!pix.save(path, "PNG")) {
		QMessageBox::warning(this, "Export failed",
							 QString("Could not write the image to:\n%1").arg(path));
	}
}
