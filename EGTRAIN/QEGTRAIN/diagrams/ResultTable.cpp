#include "diagrams/ResultTable.h"

#include "diagrams/RunResults.h"

#include <QHeaderView>
#include <QScrollBar>
#include <QTableWidget>

#include <algorithm>

namespace {

// The role of the header item of a column that says whether the column holds numbers.
constexpr int kNumberRole = Qt::UserRole;

} // namespace

QTableWidget* makeResultTable(QWidget* parent, const char* name, const QVector<ResultColumn>& columns) {
	auto* table = new QTableWidget(0, static_cast<int>(columns.size()), parent);
	table->setObjectName(name);
	for (int column = 0; column < table->columnCount(); ++column) {
		auto* header = new QTableWidgetItem(columns[column].header);
		header->setData(kNumberRole, columns[column].number);
		table->setHorizontalHeaderItem(column, header);
	}
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setAlternatingRowColors(true);
	// Tab leaves the table for the next control; the arrow keys move between the cells.
	table->setTabKeyNavigation(false);
	table->horizontalHeader()->setStretchLastSection(true);
	table->verticalHeader()->setVisible(false);
	return table;
}

void fillResultTable(QTableWidget* table, const std::vector<QStringList>& rows) {
	table->setRowCount(static_cast<int>(rows.size()));
	for (int row = 0; row < table->rowCount(); ++row) {
		for (int column = 0; column < table->columnCount(); ++column) {
			auto* item = new QTableWidgetItem(rows[static_cast<std::size_t>(row)].value(column));
			if (table->horizontalHeaderItem(column)->data(kNumberRole).toBool())
				item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
			table->setItem(row, column, item);
		}
	}
	// A stretched last column keeps at least the width that it had when the stretch was turned on, so the stretch is turned
	// on again once the columns fit their content: the last column then shrinks to its content, not to the width it had
	// before the rows came.
	QHeaderView* header = table->horizontalHeader();
	header->setStretchLastSection(false);
	table->resizeColumnsToContents();
	header->setStretchLastSection(true);
}

int resultTableWidth(const QTableWidget& table) {
	// The width that resizeColumnsToContents gives each column; QTableView has the hint of a column protected.
	const QAbstractItemView& view = table;
	int width = 2 * table.frameWidth() + table.verticalScrollBar()->sizeHint().width();
	for (int column = 0; column < table.columnCount(); ++column)
		width += std::max(view.sizeHintForColumn(column), table.horizontalHeader()->sectionSizeHint(column));
	return width;
}

QString decimalText(double value, int decimals) {
	QString text = QString::number(value, 'f', decimals);
	if (text.contains(QLatin1Char('.'))) {
		while (text.endsWith(QLatin1Char('0')))
			text.chop(1);
		if (text.endsWith(QLatin1Char('.')))
			text.chop(1);
	}
	return text == QStringLiteral("-0") ? QStringLiteral("0") : text;
}

QString signedDecimalText(double value, int decimals) {
	const QString text = decimalText(value, decimals);
	return value > 0.0 && text != QStringLiteral("0") ? QStringLiteral("+") + text : text;
}

QString decimalText(const RunResultValue& value, int decimals) {
	return value.available ? decimalText(value.value, decimals) : QStringLiteral("-");
}

QString signedDecimalText(const RunResultValue& value, int decimals) {
	return value.available ? signedDecimalText(value.value, decimals) : QStringLiteral("-");
}
