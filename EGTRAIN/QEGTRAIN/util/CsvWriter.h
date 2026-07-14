#ifndef CSVWRITER_H
#define CSVWRITER_H

#include <string>
#include <vector>

// Small locale-independent CSV builder for exporting raw result data.
// Values keep full simulation precision; the on-screen formatting used for
// labels never reduces the exported numbers.
namespace csv {

// A missing value is written as an empty field. This is the single documented
// representation used across every export.
extern const char kMissingValue[];

// Line terminator written between records (RFC 4180 style).
extern const char kLineEnding[];

// Escape one field. Fields that contain a comma, double quote, carriage return,
// or line feed are wrapped in double quotes with inner quotes doubled.
std::string escapeField(const std::string& field);

// Format a double with a decimal point and full round-trip precision,
// independent of the active locale. Non-finite values become the missing value.
std::string formatDouble(double value);

// Join fields into one record, escaping each field.
std::string makeRow(const std::vector<std::string>& fields);

// Build a full document from a header row followed by data rows.
std::string makeDocument(const std::vector<std::string>& header,
						 const std::vector<std::vector<std::string>>& rows);

} // namespace csv

#endif // CSVWRITER_H
