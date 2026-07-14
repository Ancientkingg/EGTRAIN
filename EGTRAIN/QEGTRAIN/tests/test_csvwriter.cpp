#include "util/CsvWriter.h"

#include <clocale>
#include <iostream>
#include <limits>
#include <string>

static bool ok = true;

static bool expect(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "failed: " << message << "\n";
		ok = false;
	}
	return condition;
}

static bool expectEq(const std::string& actual, const std::string& expected, const char* message) {
	if (actual != expected) {
		std::cerr << "failed: " << message << "\n  expected [" << expected << "]\n  actual   [" << actual << "]\n";
		ok = false;
		return false;
	}
	return true;
}

int main() {
	// A locale that uses a comma decimal separator must not leak into numbers.
	std::setlocale(LC_ALL, "de_DE.UTF-8");

	// Plain fields pass through unchanged.
	expectEq(csv::escapeField("Central"), "Central", "plain field unchanged");
	expectEq(csv::escapeField(""), "", "empty field stays empty");

	// Fields with structural characters are quoted; inner quotes are doubled.
	expectEq(csv::escapeField("a,b"), "\"a,b\"", "comma triggers quoting");
	expectEq(csv::escapeField("say \"hi\""), "\"say \"\"hi\"\"\"", "inner quotes doubled");
	expectEq(csv::escapeField("line1\nline2"), "\"line1\nline2\"", "newline triggers quoting");
	expectEq(csv::escapeField("a\rb"), "\"a\rb\"", "carriage return triggers quoting");

	// UTF-8 station names are preserved byte for byte.
	expectEq(csv::escapeField("Bécon"), "Bécon", "utf-8 preserved");
	expectEq(csv::escapeField("Ras Beirut, ن"), "\"Ras Beirut, ن\"", "utf-8 with comma quoted");

	// Numbers use a decimal point regardless of the active locale.
	expectEq(csv::formatDouble(1.5), "1.5", "decimal point not comma");
	expect(csv::formatDouble(0.1).find(',') == std::string::npos, "no locale comma in 0.1");
	expect(csv::formatDouble(0.1).rfind("0.1", 0) == 0, "raw precision keeps fractional part");
	expectEq(csv::formatDouble(42.0), "42", "whole number has no trailing noise");

	// Non-finite values collapse to the documented missing value (empty).
	expectEq(csv::formatDouble(std::numeric_limits<double>::quiet_NaN()), csv::kMissingValue, "NaN is missing value");
	expectEq(csv::formatDouble(std::numeric_limits<double>::infinity()), csv::kMissingValue, "infinity is missing value");

	// Rows join with commas and escape each field.
	expectEq(csv::makeRow({"IC1", "a,b", "12.5"}), "IC1,\"a,b\",12.5", "row joins and escapes");
	expectEq(csv::makeRow({}), "", "empty row is empty");

	// A document is a header row plus data rows, each ended with CRLF.
	const std::string doc = csv::makeDocument(
		{"train", "value"},
		{{"IC1", "1.5"}, {"IC2", ""}});
	expectEq(doc, "train,value\r\nIC1,1.5\r\nIC2,\r\n", "document has header and CRLF rows");

	if (!ok)
		return 1;
	std::cout << "all CsvWriter tests passed\n";
	return 0;
}
