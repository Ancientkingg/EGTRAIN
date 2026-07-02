#include "../TrajectoryUtil.h"

#include <cmath>
#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

int main() {
	bool ok = true;
	ok &= expect(std::fabs(trajectoryTimeSeconds(0, 1.0) - 0.0) < 1e-9, "zero trajectory time");
	ok &= expect(std::fabs(trajectoryTimeSeconds(10, 1.0) - 10.0) < 1e-9, "identity trajectory time");
	ok &= expect(std::fabs(trajectoryTimeSeconds(5, 0.5) - 2.5) < 1e-9, "scaled trajectory time");

	if (!ok)
		return 1;

	std::cout << "all TrajectoryUtil tests passed\n";
	return 0;
}
