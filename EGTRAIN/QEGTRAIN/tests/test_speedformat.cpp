#include "../SpeedFormat.h"

#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

int main() {
	bool ok = true;
	ok &= expect(formatSpeedLabel(0.0) == "0 km/h", "zero speed label");
	ok &= expect(formatSpeedLabel(82.6) == "83 km/h", "round speed up");
	ok &= expect(formatSpeedLabel(82.4) == "82 km/h", "round speed down");
	ok &= expect(formatSpeedLabel(-3.0) == "0 km/h", "negative speed clamps");

	if (!ok)
		return 1;

	std::cout << "all SpeedFormat tests passed\n";
	return 0;
}
