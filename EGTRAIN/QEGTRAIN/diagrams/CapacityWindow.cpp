#include "diagrams/CapacityWindow.h"

#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/CapacityScope.h"
#include "diagrams/ResultFrame.h"
#include "diagrams/RunExports.h"
#include "util/CsvWriter.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <string>
#include <vector>

namespace {

// Numbers are aligned to the right, as in the other analysis tables. A cell that names blocks by their short ids has
// their labels as its tooltip.
QTableWidgetItem* cell(const QString& text, bool number, const QString& tooltip = QString()) {
	auto* item = new QTableWidgetItem(text);
	if (number)
		item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
	if (!tooltip.isEmpty())
		item->setToolTip(tooltip);
	return item;
}

// A tab title with its count, which it leaves out when it is 0.
QString countedTitle(const QString& title, int count) {
	return count == 0 ? title : QString("%1 (%2)").arg(title).arg(count);
}

QString number(double value) {
	return QString::fromStdString(csv::formatDouble(value));
}

// A block by its short id, as the section strip names it.
QString blockText(const std::string& id) {
	return QString::fromStdString(shortBlockId(id));
}

} // namespace

QString capacityCycleText(const CapacityAnalysisResult& result) {
	QString cycle = "cycle " + QString::number(result.cycleTime, 'f', 1) + " s";
	if (result.cyclePercentage < 0.0 || !(result.periodSeconds > 0.0)) return cycle;
	return cycle + ", " + QString::number(result.cyclePercentage, 'f', 1) + "% of " + capacityPeriodText(result.periodSeconds);
}

CapacityWindow::CapacityWindow(const CapacityAnalysisResult& result, const QString& context, const QString& contextTip,
	const SceneSectionLabels& labels, QWidget* parent)
	: QDialog(parent) {
	setModal(false);
	setProperty("manualSection", QStringLiteral("capacity"));
	setWindowTitle("Capacity");
	// "Reference time: planned departure at Guingamp" for the tooltip of a cell that names a train; empty without one.
	const auto referenceTip = [&result](const std::string& identity) {
		const QString reference = capacityReferenceOf(result, identity);
		return reference.isEmpty() ? QString() : "Reference time: " + reference;
	};

	auto* tabs = new QTabWidget;
	const auto addTable = [tabs](const QStringList& headers, int rows) {
		auto* table = new QTableWidget(rows, static_cast<int>(headers.size()), tabs);
		table->setHorizontalHeaderLabels(headers);
		table->setEditTriggers(QAbstractItemView::NoEditTriggers);
		table->setSelectionBehavior(QAbstractItemView::SelectRows);
		table->setAlternatingRowColors(true);
		// Without row numbers, as the other analysis tables.
		table->verticalHeader()->setVisible(false);
		// Tab leaves the table for the buttons of the panel; the arrow keys move between the cells.
		table->setTabKeyNavigation(false);
		table->horizontalHeader()->setStretchLastSection(true);
		tabs->addTab(table, QString());
		return table;
	};
	auto* pairTable = addTable({"Leader", "Follower", "Leader code", "Follower code", "Scheduled headway (s)",
								   "Minimum headway (s)", "Buffer (s)", "Governing block"},
		static_cast<int>(result.pairs.size()));
	for (int row = 0; row < pairTable->rowCount(); ++row) {
		const CapacityPairRow& pair = result.pairs[static_cast<std::size_t>(row)];
		const QStringList values = {QString::fromStdString(pair.leaderIdentity), QString::fromStdString(pair.followerIdentity),
			QString::fromStdString(pair.leaderOperatingCode), QString::fromStdString(pair.followerOperatingCode),
			number(pair.scheduledHeadway), number(pair.minimumHeadway), number(pair.buffer),
			QString::fromStdString(capacityEvidenceBlocks(pair.governingEvidence))};
		const QStringList tips = {referenceTip(pair.leaderIdentity), referenceTip(pair.followerIdentity), QString(), QString(), QString(),
			QString(), QString(), QString::fromStdString(capacityEvidenceText(pair.governingEvidence, &labels))};
		for (int column = 0; column < values.size(); ++column)
			pairTable->setItem(row, column, cell(values[column], column >= 4 && column <= 6, tips[column]));
	}
	tabs->setTabText(0, countedTitle("Pairs", pairTable->rowCount()));

	auto* compressionTable = addTable({"Train", "Operating code", "Original reference (s)", "Scheduled reference (s)",
										  "Compressed reference (s)", "Shift (s)", "Governing predecessor"},
		static_cast<int>(result.compression.size()));
	for (int row = 0; row < compressionTable->rowCount(); ++row) {
		const CapacityCompressionRow& compressed = result.compression[static_cast<std::size_t>(row)];
		QStringList governing;
		QStringList governingTips;
		for (const CapacityCompressionEvidence& predecessor : compressed.governingPredecessors) {
			const QString start = QString("%1 (%2 s)").arg(QString::fromStdString(predecessor.predecessorIdentity), number(predecessor.minimumHeadway));
			governing << start + ": " + QString::fromStdString(capacityEvidenceBlocks(predecessor.governingEvidence));
			governingTips << start + "\n" + QString::fromStdString(capacityEvidenceText(predecessor.governingEvidence, &labels));
		}
		const QStringList values = {QString::fromStdString(compressed.identity), QString::fromStdString(compressed.operatingCode),
			number(compressed.originalReference), number(compressed.scheduledReference), number(compressed.compressedReference),
			number(compressed.shift), governing.join(", ")};
		const QStringList tips = {referenceTip(compressed.identity), QString(), QString(), QString(), QString(), QString(), governingTips.join('\n')};
		for (int column = 0; column < values.size(); ++column)
			compressionTable->setItem(row, column, cell(values[column], column >= 2 && column <= 5, tips[column]));
	}
	tabs->setTabText(1, countedTitle("Compression", compressionTable->rowCount()));

	auto* criticalTable = addTable({"Leader", "Follower", "Leader block", "Follower block", "Gap (s)"},
		static_cast<int>(result.criticalBlocks.size()));
	for (int row = 0; row < criticalTable->rowCount(); ++row) {
		const CapacityCriticalBlock& critical = result.criticalBlocks[static_cast<std::size_t>(row)];
		const QStringList values = {QString::fromStdString(critical.leaderIdentity), QString::fromStdString(critical.followerIdentity),
			blockText(critical.leaderBlockId), blockText(critical.followerBlockId), number(critical.gap)};
		const QStringList tips = {QString(), QString(), QString::fromStdString(readableSectionLabel(critical.leaderBlockId, labels)),
			QString::fromStdString(readableSectionLabel(critical.followerBlockId, labels)), QString()};
		for (int column = 0; column < values.size(); ++column)
			criticalTable->setItem(row, column, cell(values[column], column == 4, tips[column]));
	}
	tabs->setTabText(2, countedTitle("Critical blocks", criticalTable->rowCount()));
	for (int index = 0; index < tabs->count(); ++index)
		qobject_cast<QTableWidget*>(tabs->widget(index))->resizeColumnsToContents();

	// The tabs at the left, with the run and the section in the header. The panel holds the figures of the chain, the
	// compressed blocking time and the export.
	auto* frame = new ResultFrame(resultTableArea({tabs}), "capacity");
	frame->setContext(context, contextTip);
	ResultPanel* panel = frame->panel();
	panel->readout()->hide();
	auto* figures = new QWidget(frame);
	auto* figureColumn = new QVBoxLayout(figures);
	figureColumn->setContentsMargins(0, 0, 0, 0);
	figureColumn->setSpacing(8);
	auto* cycle = new ResultFigure("capacityCycle", figures);
	// The two trains that close the cycle and their reference times, which the Compression tab has as well.
	QStringList cycleTips = {QString("From %1 to %2").arg(QString::fromStdString(result.firstIdentity), QString::fromStdString(result.cycleEndIdentity))};
	for (const std::string& identity : {result.firstIdentity, result.cycleEndIdentity})
		if (const QString reference = capacityReferenceOf(result, identity); !reference.isEmpty())
			cycleTips << QString::fromStdString(identity) + ": " + reference;
	cycle->setFigure(QString::number(result.cycleTime, 'f', 1) + " s", QStringLiteral("Cycle"), cycleTips.join('\n'));
	auto* share = new ResultFigure("capacityShare", figures);
	share->setFigure(QString::number(result.cyclePercentage, 'f', 1) + "%", "of " + capacityPeriodText(result.periodSeconds));
	// The blocks of the leaders where two trains touch after the compression, each once, in the order of the tab, by their
	// short ids as the section strip names them; the tooltip gives the label of each.
	QStringList criticalIds;
	QStringList criticalShortIds;
	QStringList criticalTips;
	for (const CapacityCriticalBlock& critical : result.criticalBlocks) {
		const QString id = QString::fromStdString(critical.leaderBlockId);
		if (criticalIds.contains(id))
			continue;
		criticalIds << id;
		criticalShortIds << blockText(critical.leaderBlockId);
		criticalTips << criticalShortIds.last() + ": " + QString::fromStdString(readableSectionLabel(critical.leaderBlockId, labels));
	}
	auto* critical = new ResultFigure("capacityCriticalBlock", figures);
	critical->setFigure(criticalShortIds.isEmpty() ? QStringLiteral("-") : criticalShortIds.join(", "),
		criticalIds.size() > 1 ? QStringLiteral("Critical blocks") : QStringLiteral("Critical block"), criticalTips.join('\n'));
	for (ResultFigure* figure : {cycle, share, critical})
		figureColumn->addWidget(figure);
	panel->addSection(QString(), figures);

	m_diagramButton = new QPushButton("Open Diagram", frame);
	m_diagramButton->setObjectName("capacityOpenDiagram");
	m_diagramButton->setAccessibleDescription("Opens the compressed blocking time diagram");
	m_diagramButton->setToolTip("Compressed blocking time");
	panel->addSection(QString(), m_diagramButton);
	m_exportButton = new QPushButton("CSV...", frame);
	m_exportButton->setObjectName("capacityExport");
	m_exportButton->setAccessibleName("Export CSV");
	m_exportButton->setToolTip("Save the tables as CSV");
	panel->addSection("Export", m_exportButton);

	auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::close);
	frame->installInDialog(*this, QStringLiteral("Capacity"), closeButtons);
	// The window opens as Capacity by area does; a table scrolls sideways when its columns do not fit.
	resize(qMin(maximumWidth(), 960), qMin(maximumHeight(), 640));
	// No button of the window is a default button, so Return presses none.
	for (QPushButton* button : findChildren<QPushButton*>())
		button->setAutoDefault(false);
}
