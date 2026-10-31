#include "util/CsvWriter.h"

#include <QTest>

#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <random>
#include <sstream>
#include <string>
#include <vector>

class TestCsvWriter : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void keepsPlainFields();
	void quotesStructuralCharacters();
	void keepsUtf8();
	void writesDecimalPoint();
	void writesSeventeenSignificantDigits();
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

// A number is the text that a stream with the classic locale and 17 significant digits writes, as the exports always
// wrote it.
void TestCsvWriter::writesSeventeenSignificantDigits() {
	const auto streamed = [](double value) {
		std::ostringstream stream;
		stream.imbue(std::locale::classic());
		stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
		return stream.str();
	};
	// the shortest form of a whole number and the 17 digits of a fraction
	QCOMPARE(csv::formatDouble(0.0), "0");
	QCOMPARE(csv::formatDouble(-0.0), "-0");
	QCOMPARE(csv::formatDouble(0.1), "0.10000000000000001");
	QCOMPARE(csv::formatDouble(-1234.5), "-1234.5");
	// an exponent from 17 integer digits on and below 1e-4
	QCOMPARE(csv::formatDouble(1e16), "10000000000000000");
	QCOMPARE(csv::formatDouble(1e17), "1e+17");
	QCOMPARE(csv::formatDouble(0.0001), "0.0001");
	QCOMPARE(csv::formatDouble(0.00001), "1.0000000000000001e-05");
	std::vector<double> values = {std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(),
		std::numeric_limits<double>::min(), std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::epsilon(),
		1.0 / 3.0, 2.0 / 3.0, 86399.999999999985, 123456789012345678.0, 0.30000000000000004, 1e-300, 9.999999999999999e22};
	// random bit patterns of every exponent, from a fixed seed
	std::mt19937_64 bits(20261031); // NOLINT(bugprone-random-generator-seed): the same values on every run
	for (int i = 0; i < 20000; ++i) {
		const std::uint64_t pattern = bits();
		double value = 0.0;
		std::memcpy(&value, &pattern, sizeof(value));
		if (std::isfinite(value))
			values.push_back(value);
	}
	// samples of a run: times and positions on a grid
	for (int step = 0; step < 5000; ++step) {
		values.push_back(step * 0.1);
		values.push_back(step * 0.25 + 1234.567);
	}
	for (double value : values) {
		const std::string expected = streamed(value);
		QVERIFY2(csv::formatDouble(value) == expected, expected.c_str());
	}
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
