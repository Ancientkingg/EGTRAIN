// Minimal tests for time formatting utility.
#include "../TimeFormat.h"

#include <iostream>
#include <string>

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

	if (!ok)
		return 1;

	std::cout << "all TimeFormat tests passed\n";
	return 0;
}
