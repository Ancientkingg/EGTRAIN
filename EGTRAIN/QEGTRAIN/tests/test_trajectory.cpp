#include "util/TrajectoryUtil.h"

#include <QTest>

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

class TestTrajectory : public QObject {
	Q_OBJECT

private slots:
	void scalesTrajectoryTime();
	void validSegmentsFollowActiveBounds();
	void exportCellsFollowActiveBounds();
	void shiftedExportCellsMapToDeparture();
	void recordsEarliestActiveSample();
	void endTimeOverrideIsRestored();
};

void TestTrajectory::scalesTrajectoryTime() {
	QVERIFY2(std::fabs(trajectoryTimeSeconds(0, 1.0) - 0.0) < 1e-9, "zero trajectory time");
	QVERIFY2(std::fabs(trajectoryTimeSeconds(10, 1.0) - 10.0) < 1e-9, "identity trajectory time");
	QVERIFY2(std::fabs(trajectoryTimeSeconds(5, 0.5) - 2.5) < 1e-9, "scaled trajectory time");
}

void TestTrajectory::validSegmentsFollowActiveBounds() {
	const auto activeRun = validTrajectorySegments({0, 0, 10, 20, 0, 0}, 2, 3);
	// Active bounds preserve both endpoints.
	QCOMPARE(activeRun.size(), std::size_t(1));
	QCOMPARE(activeRun[0].first, 2);
	QCOMPARE(activeRun[0].last, 3);

	const auto completedRun = validTrajectorySegments({10, 20, -9999, 0, 0}, 0, 1);
	// Completed run excludes trailing allocation after End_Time.
	QCOMPARE(completedRun.size(), std::size_t(1));
	QCOMPARE(completedRun[0].first, 0);
	QCOMPARE(completedRun[0].last, 1);

	const auto internalGap = validTrajectorySegments({0, 10, -9999, 30, 40, 0}, 0, 4);
	// Internal sentinel splits segments and keeps zero.
	QCOMPARE(internalGap.size(), std::size_t(2));
	QCOMPARE(internalGap[0].first, 0);
	QCOMPARE(internalGap[0].last, 1);
	QCOMPARE(internalGap[1].first, 3);
	QCOMPARE(internalGap[1].last, 4);

	const auto leadingTrailingGaps = validTrajectorySegments({-9999, 5, 6, -9999}, 0, 3);
	// Leading and trailing sentinels are excluded.
	QCOMPARE(leadingTrailingGaps.size(), std::size_t(1));
	QCOMPARE(leadingTrailingGaps[0].first, 1);
	QCOMPARE(leadingTrailingGaps[0].last, 2);

	const auto onePoint = validTrajectorySegments({-9999, 5, -9999}, 0, 2);
	// One-point segment is retained.
	QCOMPARE(onePoint.size(), std::size_t(1));
	QCOMPARE(onePoint[0].first, 1);
	QCOMPARE(onePoint[0].last, 1);

	const auto clampedBounds = validTrajectorySegments({1, 2, 3}, -5, 9);
	// Out-of-range bounds clamp safely.
	QCOMPARE(clampedBounds.size(), std::size_t(1));
	QCOMPARE(clampedBounds[0].first, 0);
	QCOMPARE(clampedBounds[0].last, 2);
	QVERIFY2(validTrajectorySegments({1, 2, 3}, 2, 1).empty(), "reversed bounds are empty");
	QVERIFY2(validTrajectorySegments({0, 0}, -1, 1).empty(), "inactive trains have no trajectory segments");
}

void TestTrajectory::exportCellsFollowActiveBounds() {
	// Export cells preserve active bounds and alignment.
	QCOMPARE(trajectoryExportCells({0, 0, 10, 20, 0, 0}, 2, 3), std::vector<double>({-9999, -9999, 10, 20, -9999, -9999}));
	// Export cells preserve internal gaps and later samples.
	QCOMPARE(trajectoryExportCells({0, 10, -9999, 30, 40, 0}, 0, 5), std::vector<double>({0, 10, -9999, 30, 40, 0}));
	// Inactive export cells are all missing.
	QCOMPARE(trajectoryExportCells({0, 10, 20}, -1, 2), std::vector<double>({-9999, -9999, -9999}));
	// Zero is valid inside active bounds.
	QCOMPARE(trajectoryExportCells({0}, 0, 0), std::vector<double>({0}));
	// Service-leg export keeps leading blanks, bounds, and later spans.
	QCOMPARE(trajectoryExportCells({0, 0, 10, -9999, 30, 40}, 2, 4), std::vector<double>({-9999, -9999, 10, -9999, 30, -9999}));
	// Service-leg export keeps inactive trains blank.
	QCOMPARE(trajectoryExportCells({0, 10, 20}, -1, 2), std::vector<double>({-9999, -9999, -9999}));
}

void TestTrajectory::shiftedExportCellsMapToDeparture() {
	// Compressed export maps active source start to departure time.
	QCOMPARE(shiftedTrajectoryExportCells({0, 10, 20, 30, 40}, 1, 4, 3, 1, 6), std::vector<double>({-9999, -9999, 10, 20, 30, 40}));
	// Compressed export preserves zero, gaps, and later valid spans.
	QCOMPARE(shiftedTrajectoryExportCells({0, 10, -9999, 30, 40}, 0, 4, 2, 0, 6),
		std::vector<double>({-9999, -9999, 0, 10, -9999, 30, 40}));
	// Compressed export blanks samples after End_Time.
	QCOMPARE(shiftedTrajectoryExportCells({10, 20, 30}, 0, 1, 0, 0, 3), std::vector<double>({10, 20, -9999, -9999}));
	// Compressed export keeps inactive source blank.
	QCOMPARE(shiftedTrajectoryExportCells({0, 10, 20}, -1, 2, 0, 0, 2), std::vector<double>({-9999, -9999, -9999}));
	// Compressed export blanks nonfinite source samples.
	QCOMPARE(shiftedTrajectoryExportCells({0, std::numeric_limits<double>::quiet_NaN(), 20}, 0, 2, 0, 0, 2),
		std::vector<double>({0, -9999, 20}));
	// Compressed export blanks short source vectors.
	QCOMPARE(shiftedTrajectoryExportCells({0}, 0, 0, 0, 0, 1), std::vector<double>({0, -9999}));
}

void TestTrajectory::recordsEarliestActiveSample() {
	int earliest = -1;
	earliest = recordEarliestTrajectoryIndex(earliest, 4, false);
	// Inactive train does not record an active sample.
	QCOMPARE(earliest, -1);
	earliest = recordEarliestTrajectoryIndex(earliest, 7, true);
	// First active sample is recorded.
	QCOMPARE(earliest, 7);
	earliest = recordEarliestTrajectoryIndex(earliest, 2, true);
	// Active sample recording is first-write-wins.
	QCOMPARE(earliest, 7);
	// Replicated active sample applies the time offset.
	QCOMPARE(replicatedEarliestTrajectoryIndex(7, 12), 19);
	// Inactive source remains inactive when replicated.
	QCOMPARE(replicatedEarliestTrajectoryIndex(-1, 12), -1);
}

void TestTrajectory::endTimeOverrideIsRestored() {
	int authoritativeEndTime = 1;
	{
		TrajectoryEndTimeOverride temporaryEnd(authoritativeEndTime, 4);
		// Legacy service output sees the temporary end bound.
		QCOMPARE(authoritativeEndTime, 4);
	}
	// Temporary service output bound restores End_Time.
	QCOMPARE(authoritativeEndTime, 1);
}

QTEST_APPLESS_MAIN(TestTrajectory)
#include "test_trajectory.moc"
