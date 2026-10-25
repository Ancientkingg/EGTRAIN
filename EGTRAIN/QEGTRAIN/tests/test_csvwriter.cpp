#include "util/CsvWriter.h"

#include <QTest>

#include <clocale>
#include <limits>
#include <string>

class TestCsvWriter : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void keepsPlainFields();
	void quotesStructuralCharacters();
	void keepsUtf8();
	void writesDecimalPoint();
	void writesNonFiniteAsMissing();
	void joinsRows();
	void endsRowsOfDocumentWithCrLf();
};

void TestCsvWriter::initTestCase() {
	// A locale that uses a comma decimal separator must not leak into numbers.
	std::setlocale(LC_ALL, "de_DE.UTF-8");
}

// Plain fields pass through unchanged.
void TestCsvWriter::keepsPlainFields() {
	// plain field unchanged
	QCOMPARE(csv::escapeField("Central"), "Central");
	// empty field stays empty
	QCOMPARE(csv::escapeField(""), "");
}

// Fields with structural characters are quoted; inner quotes are doubled.
void TestCsvWriter::quotesStructuralCharacters() {
	// comma triggers quoting
	QCOMPARE(csv::escapeField("a,b"), "\"a,b\"");
	// inner quotes doubled
	QCOMPARE(csv::escapeField("say \"hi\""), "\"say \"\"hi\"\"\"");
	// newline triggers quoting
	QCOMPARE(csv::escapeField("line1\nline2"), "\"line1\nline2\"");
	// carriage return triggers quoting
	QCOMPARE(csv::escapeField("a\rb"), "\"a\rb\"");
}

// UTF-8 station names are preserved byte for byte.
void TestCsvWriter::keepsUtf8() {
	// utf-8 preserved
	QCOMPARE(csv::escapeField("Bécon"), "Bécon");
	// utf-8 with comma quoted
	QCOMPARE(csv::escapeField("Ras Beirut, ن"), "\"Ras Beirut, ن\"");
}

// Numbers use a decimal point regardless of the active locale.
void TestCsvWriter::writesDecimalPoint() {
	// decimal point not comma
	QCOMPARE(csv::formatDouble(1.5), "1.5");
	QVERIFY2(csv::formatDouble(0.1).find(',') == std::string::npos, "no locale comma in 0.1");
	QVERIFY2(csv::formatDouble(0.1).rfind("0.1", 0) == 0, "raw precision keeps fractional part");
	// whole number has no trailing noise
	QCOMPARE(csv::formatDouble(42.0), "42");
}

// Non-finite values collapse to the documented missing value (empty).
void TestCsvWriter::writesNonFiniteAsMissing() {
	// NaN is missing value
	QCOMPARE(csv::formatDouble(std::numeric_limits<double>::quiet_NaN()), std::string(csv::kMissingValue));
	// infinity is missing value
	QCOMPARE(csv::formatDouble(std::numeric_limits<double>::infinity()), std::string(csv::kMissingValue));
}

// Rows join with commas and escape each field.
void TestCsvWriter::joinsRows() {
	// row joins and escapes
	QCOMPARE(csv::makeRow({"IC1", "a,b", "12.5"}), "IC1,\"a,b\",12.5");
	// empty row is empty
	QCOMPARE(csv::makeRow({}), "");
}

// A document is a header row plus data rows, each ended with CRLF.
void TestCsvWriter::endsRowsOfDocumentWithCrLf() {
	const std::string doc = csv::makeDocument(
		{"train", "value"},
		{{"IC1", "1.5"}, {"IC2", ""}});
	// document has header and CRLF rows
	QCOMPARE(doc, "train,value\r\nIC1,1.5\r\nIC2,\r\n");
}

QTEST_APPLESS_MAIN(TestCsvWriter)
#include "test_csvwriter.moc"
