#include "util/TrajectoryUtil.h"

#include <cmath>
#include <iostream>
#include <limits>
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

	const auto completedRun = validTrajectorySegments({10, 20, -9999, 0, 0}, 0, 1);
	ok &= expect(completedRun.size() == 1 && completedRun[0].first == 0 && completedRun[0].last == 1,
				 "completed run excludes trailing allocation after End_Time");

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
	ok &= expect(validTrajectorySegments({0, 0}, -1, 1).empty(), "inactive trains have no trajectory segments");

	ok &= expect(trajectoryExportCells({0, 0, 10, 20, 0, 0}, 2, 3) ==
					 std::vector<double>({-9999, -9999, 10, 20, -9999, -9999}),
				 "export cells preserve active bounds and alignment");
	ok &= expect(trajectoryExportCells({0, 10, -9999, 30, 40, 0}, 0, 5) ==
					 std::vector<double>({0, 10, -9999, 30, 40, 0}),
				 "export cells preserve internal gaps and later samples");
	ok &= expect(trajectoryExportCells({0, 10, 20}, -1, 2) ==
					 std::vector<double>({-9999, -9999, -9999}),
				 "inactive export cells are all missing");
	ok &= expect(trajectoryExportCells({0}, 0, 0) == std::vector<double>({0}),
					 "zero is valid inside active bounds");
	ok &= expect(trajectoryExportCells({0, 0, 10, -9999, 30, 40}, 2, 4) ==
					 std::vector<double>({-9999, -9999, 10, -9999, 30, -9999}),
					 "service-leg export keeps leading blanks, bounds, and later spans");
	ok &= expect(trajectoryExportCells({0, 10, 20}, -1, 2) ==
					 std::vector<double>({-9999, -9999, -9999}),
					 "service-leg export keeps inactive trains blank");

	ok &= expect(shiftedTrajectoryExportCells({0, 10, 20, 30, 40}, 1, 4, 3, 1, 6) ==
					 std::vector<double>({-9999, -9999, 10, 20, 30, 40}),
					 "compressed export maps active source start to departure time");
	ok &= expect(shiftedTrajectoryExportCells({0, 10, -9999, 30, 40}, 0, 4, 2, 0, 6) ==
					 std::vector<double>({-9999, -9999, 0, 10, -9999, 30, 40}),
					 "compressed export preserves zero, gaps, and later valid spans");
	ok &= expect(shiftedTrajectoryExportCells({10, 20, 30}, 0, 1, 0, 0, 3) ==
					 std::vector<double>({10, 20, -9999, -9999}),
					 "compressed export blanks samples after End_Time");
	ok &= expect(shiftedTrajectoryExportCells({0, 10, 20}, -1, 2, 0, 0, 2) ==
					 std::vector<double>({-9999, -9999, -9999}),
					 "compressed export keeps inactive source blank");
	ok &= expect(shiftedTrajectoryExportCells({0, std::numeric_limits<double>::quiet_NaN(), 20}, 0, 2, 0, 0, 2) ==
					 std::vector<double>({0, -9999, 20}),
					 "compressed export blanks nonfinite source samples");
	ok &= expect(shiftedTrajectoryExportCells({0}, 0, 0, 0, 0, 1) ==
					 std::vector<double>({0, -9999}),
					 "compressed export blanks short source vectors");

	int earliest = -1;
	earliest = recordEarliestTrajectoryIndex(earliest, 4, false);
	ok &= expect(earliest == -1, "inactive train does not record an active sample");
	earliest = recordEarliestTrajectoryIndex(earliest, 7, true);
	ok &= expect(earliest == 7, "first active sample is recorded");
	earliest = recordEarliestTrajectoryIndex(earliest, 2, true);
	ok &= expect(earliest == 7, "active sample recording is first-write-wins");
	ok &= expect(replicatedEarliestTrajectoryIndex(7, 12) == 19,
				 "replicated active sample applies the time offset");
	ok &= expect(replicatedEarliestTrajectoryIndex(-1, 12) == -1,
				 "inactive source remains inactive when replicated");

	int authoritativeEndTime = 1;
	{
		TrajectoryEndTimeOverride temporaryEnd(authoritativeEndTime, 4);
		ok &= expect(authoritativeEndTime == 4, "legacy service output sees the temporary end bound");
	}
	ok &= expect(authoritativeEndTime == 1, "temporary service output bound restores End_Time");

	if (!ok)
		return 1;

	std::cout << "all TrajectoryUtil tests passed\n";
	return 0;
}
