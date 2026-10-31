#include "app/RunOutcome.h"

#include <QString>
#include <QTest>

namespace {

QString discardName(RunDiscard discard) {
	switch (discard) {
		case RunDiscard::None:
			return QStringLiteral("None");
		case RunDiscard::SceneChanged:
			return QStringLiteral("SceneChanged");
		case RunDiscard::Failed:
			return QStringLiteral("Failed");
		case RunDiscard::Stopped:
			return QStringLiteral("Stopped");
		case RunDiscard::NoResults:
			return QStringLiteral("NoResults");
	}
	return QStringLiteral("unknown");
}

} // namespace

class TestRunOutcome : public QObject {
	Q_OBJECT

private slots:
	void classifiesRunEnd_data();
	void classifiesRunEnd();
};

void TestRunOutcome::classifiesRunEnd_data() {
	QTest::addColumn<bool>("failed");
	QTest::addColumn<bool>("stopRequested");
	QTest::addColumn<bool>("lastStageCompleted");
	QTest::addColumn<bool>("sceneChanged");
	QTest::addColumn<bool>("hasTrajectories");
	QTest::addColumn<QString>("discard");
	QTest::addColumn<bool>("stopped");

	// failed, stop requested, last stage completed, scene changed, trajectories; discard, stopped
	QTest::newRow("completed") << false << false << true << false << true << "None" << false;
	QTest::newRow("stop after the last stage") << false << true << true << false << true << "None" << false;
	QTest::newRow("scene changed") << false << false << true << true << true << "SceneChanged" << false;
	QTest::newRow("scene changed and stopped") << false << true << false << true << true << "SceneChanged" << true;
	QTest::newRow("scene changed and failed") << true << false << false << true << true << "SceneChanged" << false;
	QTest::newRow("failed") << true << false << false << false << true << "Failed" << false;
	QTest::newRow("failed while stopping") << true << true << false << false << true << "Failed" << false;
	QTest::newRow("stopped by the user") << false << true << false << false << true << "Stopped" << true;
	QTest::newRow("stopped without trajectories") << false << true << false << false << false << "Stopped" << true;
	QTest::newRow("no trajectories") << false << false << true << false << false << "NoResults" << false;
}

// The results are kept, or discarded for the first reason that applies, and a stop counts only before the last stage
// of a run that did not fail.
void TestRunOutcome::classifiesRunEnd() {
	QFETCH(bool, failed);
	QFETCH(bool, stopRequested);
	QFETCH(bool, lastStageCompleted);
	QFETCH(bool, sceneChanged);
	QFETCH(bool, hasTrajectories);
	QFETCH(QString, discard);
	QFETCH(bool, stopped);
	RunEnd end;
	end.failed = failed;
	end.stopRequested = stopRequested;
	end.lastStageCompleted = lastStageCompleted;
	end.sceneChangedDuringRun = sceneChanged;
	end.hasTrajectories = hasTrajectories;
	const RunOutcome outcome = classifyRunOutcome(end);
	QCOMPARE(discardName(outcome.discard), discard);
	QCOMPARE(outcome.stopped, stopped);
	QCOMPARE(outcome.resultsKept(), discard == QLatin1String("None"));
}

QTEST_APPLESS_MAIN(TestRunOutcome)
#include "test_runoutcome.moc"
