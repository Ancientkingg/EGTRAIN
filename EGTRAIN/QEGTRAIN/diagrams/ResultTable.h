#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <utility>
#include <vector>

class QTableWidget;
class QWidget;
struct RunResultValue;

/// A column of a result table: its header, and whether it holds numbers, which lie at the right.
struct ResultColumn {
	/// A column headed `text` that holds numbers when `numbers` is true.
	ResultColumn(const char* text, bool numbers = false) : header(QString::fromUtf8(text)), number(numbers) {}
	/// A column headed `text` that holds numbers when `numbers` is true.
	ResultColumn(QString text, bool numbers) : header(std::move(text)), number(numbers) {}
	QString header; ///< Text of the header.
	bool number;	///< True when the cells hold numbers.
};

/// The `number` of a ResultColumn that holds numbers.
inline constexpr bool kNumberColumn = true;

/// A read-only table of a result window named `name`, with these columns: whole rows are selected, the rows alternate in
/// colour and have no header, the last column stretches, and Tab leaves the table while the arrow keys move between the cells.
QTableWidget* makeResultTable(QWidget* parent, const char* name, const QVector<ResultColumn>& columns);

/// Puts `rows` in the table of makeResultTable in place of its rows, with the numbers at the right, and fits the columns to
/// their content.
void fillResultTable(QTableWidget* table, const std::vector<QStringList>& rows);

/// `value` with at most `decimals` decimals and no trailing zeros, such as 168.2, -72.25 or 301; never "-0".
QString decimalText(double value, int decimals = 2);

/// decimalText with a plus sign in front of a value above zero that does not round to 0, such as +168.2 or -72.25.
QString signedDecimalText(double value, int decimals = 2);

/// decimalText of an available value, and "-" for one that is not.
QString decimalText(const RunResultValue& value, int decimals = 2);

/// signedDecimalText of an available value, and "-" for one that is not.
QString signedDecimalText(const RunResultValue& value, int decimals = 2);
