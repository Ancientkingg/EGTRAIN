#include "util/TrajectoryUtil.h"

#include <cmath>
#include <iostream>
#include <vector>

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

	const auto activeRun = validTrajectorySegments({0, 0, 10, 20, 0, 0}, 2, 3);
	ok &= expect(activeRun.size() == 1 && activeRun[0].first == 2 && activeRun[0].last == 3,
				 "active bounds preserve both endpoints");

	const auto internalGap = validTrajectorySegments({0, 10, -9999, 30, 40, 0}, 0, 4);
	ok &= expect(internalGap.size() == 2 && internalGap[0].first == 0 && internalGap[0].last == 1 &&
					 internalGap[1].first == 3 && internalGap[1].last == 4,
				 "internal sentinel splits segments and keeps zero");

	const auto leadingTrailingGaps = validTrajectorySegments({-9999, 5, 6, -9999}, 0, 3);
	ok &= expect(leadingTrailingGaps.size() == 1 && leadingTrailingGaps[0].first == 1 &&
					 leadingTrailingGaps[0].last == 2,
				 "leading and trailing sentinels are excluded");

	const auto onePoint = validTrajectorySegments({-9999, 5, -9999}, 0, 2);
	ok &= expect(onePoint.size() == 1 && onePoint[0].first == 1 && onePoint[0].last == 1,
				 "one-point segment is retained");

	const auto clampedBounds = validTrajectorySegments({1, 2, 3}, -5, 9);
	ok &= expect(clampedBounds.size() == 1 && clampedBounds[0].first == 0 && clampedBounds[0].last == 2,
				 "out-of-range bounds clamp safely");
	ok &= expect(validTrajectorySegments({1, 2, 3}, 2, 1).empty(), "reversed bounds are empty");

	if (!ok)
		return 1;

	std::cout << "all TrajectoryUtil tests passed\n";
	return 0;
}
