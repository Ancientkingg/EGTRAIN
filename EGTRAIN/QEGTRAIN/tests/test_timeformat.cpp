// Minimal tests for time formatting utility.
#include "util/TimeFormat.h"
#include "util/timeutil.hpp"

#include <iostream>
#include <string>
#include <limits>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

int main() {
	bool ok = true;

	// Basic seconds since start, no offset.
	ok &= expect(formatSimTime(0) == "00:00:00", "formatSimTime zero");
	ok &= expect(formatSimTime(61) == "00:01:01", "formatSimTime minute second");
	ok &= expect(formatSimTime(3661) == "01:01:01", "formatSimTime hour minute second");

	// Offset (08:00 = 28800s) plus 90s sim time.
	ok &= expect(formatSimTime(90, 28800) == "08:01:30", "formatSimTime offset");
	ok &= expect(formatSimTime(3600, 23 * 3600) == "00:00:00", "formatSimTime wrap");
	ok &= expect(formatSimTime(-5) == "00:00:00", "formatSimTime negative clamp");

	// Parse HH:MM to seconds since midnight.
	ok &= expect(parseClockToSeconds("08:00") == 28800, "parseClockToSeconds 08:00");
	ok &= expect(parseClockToSeconds("00:00") == 0, "parseClockToSeconds 00:00");
	ok &= expect(parseClockToSeconds("23:59") == 86340, "parseClockToSeconds 23:59");
	ok &= expect(parseClockToSeconds("8") == -1, "parseClockToSeconds short malformed");
	ok &= expect(parseClockToSeconds("ab:cd") == -1, "parseClockToSeconds nonnumeric malformed");

	for (const auto& sample : {std::make_pair(0.0, "08:00:00"), {90.0, "08:01:30"},
			{90.125, "08:01:30.125"}, {57600.0, "+1d 00:00:00"}, {144001.0, "+2d 00:00:01"}}) {
		std::optional<double> value;
		ok &= expect(formatPlannedTime(sample.first, true, 28800) == sample.second
				&& parsePlannedTime(sample.second, true, 28800, value) && value == sample.first,
				"planned clock representation roundtrips");
	}
	std::optional<double> value = 90.1234567890123;
	for (const bool clock : {false, true}) {
		const double original = *value;
		ok &= expect(parsePlannedTime(formatPlannedTime(value, clock, 28800), clock, 28800, value)
				&& value == original, "unchanged planned text preserves exact precision");
	}
	ok &= expect(parsePlannedTime("", true, 28800, value) && !value
			&& formatPlannedTime(value, false, 0).empty(), "blank planned time is absent");
	ok &= expect(parsePlannedTime("00:00:00", true, 0, value) && value == 0.0,
			"zero-base midnight is present zero");
	for (const char* invalid : {"07:59:59", "24:00:00", "08:60:00", "08:00:60", "8:00:00",
			"08:00", "08:00:00junk", "+1d", "nan", "-1d 08:00:00"})
		ok &= expect(!parsePlannedTime(invalid, true, 28800, value) && value == 0.0,
				"invalid clock input leaves value unchanged; no implicit tomorrow");
	for (const char* invalid : {"-1", "nan", "inf", "1e309", "12junk", " 12"})
		ok &= expect(!parsePlannedTime(invalid, false, 0, value) && value == 0.0,
				"invalid elapsed input leaves value unchanged");
	value = -28801.5;
	ok &= expect(formatPlannedTime(value, true, 28800) == "-1d 23:59:58.5"
			&& parsePlannedTime("-1d 23:59:58.5", true, 28800, value) && value == -28801.5
			&& !parsePlannedTime("-1d 23:59:58", true, 28800, value),
			"unchanged negative context is preserved but negative edits are rejected");
	ok &= expect(formatPlannedTime(-115200.0, true, 28800) == "-1d 00:00:00",
			"negative-midnight context does not display signed zero seconds");
	value = std::numeric_limits<double>::infinity();
	ok &= expect(!parsePlannedTime(formatPlannedTime(value, false, 0), false, 0, value),
			"non-finite stored values cannot pass unchanged-text validation");

	std::tm timestamp{};
	timestamp.tm_year = 124;
	timestamp.tm_mon = 3;
	timestamp.tm_mday = 2;
	timestamp.tm_hour = 4;
	timestamp.tm_min = 5;
	timestamp.tm_sec = 6;
	timestamp.tm_wday = 2;
	ok &= expect(formatDateTime(timestamp) == "Tue 02.04.2024 4:05:06", "formatDateTime single digits");
	ok &= expect(formatDateTimeFilename(timestamp) == "2024_04_02_4_05_06", "formatDateTimeFilename single digits");

	if (!ok)
		return 1;

	std::cout << "all TimeFormat tests passed\n";
	return 0;
}
