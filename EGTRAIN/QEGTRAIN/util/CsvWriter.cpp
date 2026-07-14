#include "util/CsvWriter.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace csv {

const char kMissingValue[] = "";
const char kLineEnding[] = "\r\n";

std::string escapeField(const std::string& field) {
	bool needsQuoting = false;
	for (char c : field) {
		if (c == ',' || c == '"' || c == '\n' || c == '\r') {
			needsQuoting = true;
			break;
		}
	}
	if (!needsQuoting)
		return field;

	std::string quoted;
	quoted.reserve(field.size() + 2);
	quoted.push_back('"');
	for (char c : field) {
		if (c == '"')
			quoted.push_back('"');
		quoted.push_back(c);
	}
	quoted.push_back('"');
	return quoted;
}

std::string formatDouble(double value) {
	if (!std::isfinite(value))
		return kMissingValue;
	std::ostringstream stream;
	stream.imbue(std::locale::classic());
	stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
	return stream.str();
}

std::string makeRow(const std::vector<std::string>& fields) {
	std::string row;
	for (std::size_t i = 0; i < fields.size(); ++i) {
		if (i > 0)
			row.push_back(',');
		row += escapeField(fields[i]);
	}
	return row;
}

std::string makeDocument(const std::vector<std::string>& header,
						 const std::vector<std::vector<std::string>>& rows) {
	std::string document;
	document += makeRow(header);
	document += kLineEnding;
	for (const std::vector<std::string>& row : rows) {
		document += makeRow(row);
		document += kLineEnding;
	}
	return document;
}

} // namespace csv
