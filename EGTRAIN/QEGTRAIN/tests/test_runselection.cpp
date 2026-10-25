#include "app/RunSelection.h"

#include "scene/RuntimeLimits.h"

#include <QTest>

#include <string>
#include <vector>

static SceneServiceOccurrence occurrence(const char* serviceId, int number) {
	SceneServiceOccurrence value;
	value.serviceId = serviceId;
	value.occurrence = number;
	return value;
}

// S1 runs once. S2 runs three times, every 600 s. S3 repeats every 1800 s, so it runs once in 1000 s and twice in
// 3600 s. All enter at time 0. Over 1000 s the scene has 5 occurrences, and 4 of them enter inside the period: the
// third run of S2 enters at 1200 s.
static SceneModel runScene() {
	SceneModel scene;
	SceneService s1;
	s1.id = "S1";
	SceneService s2;
	s2.id = "S2";
	s2.hasRepeat = true;
	s2.hasRepeatCount = true;
	s2.repeatCount = 3;
	s2.headwaySeconds = 600.0;
	SceneService s3;
	s3.id = "S3";
	s3.hasRepeat = true;
	s3.headwaySeconds = 1800.0;
	scene.services = {s1, s2, s3};
	return scene;
}

constexpr double kPeriod = 1000.0;

class TestRunSelection : public QObject {
	Q_OBJECT

private slots:
	void startsEmpty();
	void followsScenarioEdits();
	void findsSelectedScenario();
	void countsOccurrences();
	void excludesOccurrences();
	void prunesExclusions();
	void followsServiceRenames();
	void keepsExclusionsOutOfOversizedScene();
};

void TestRunSelection::startsEmpty() {
	RunSelection selection;
	// a new selection has no scenario, no applied scenario, no exclusion and no edited scenario
	QVERIFY(selection.scenarioId().empty());
	QVERIFY(selection.appliedScenarioId().empty());
	QVERIFY(!selection.hasExclusions());
	QVERIFY(!selection.isScenarioModified("a"));
}

void TestRunSelection::followsScenarioEdits() {
	RunSelection selection;
	selection.selectScenario("a");
	selection.applySelectedScenario();
	selection.selectScenario("b");
	// selecting another scenario leaves the scenario of the latest run as it was
	QCOMPARE(selection.scenarioId(), "b");
	QCOMPARE(selection.appliedScenarioId(), "a");
	selection.clearAppliedScenario();
	// clearing the applied scenario keeps the selected one
	QVERIFY(selection.appliedScenarioId().empty());
	QCOMPARE(selection.scenarioId(), "b");

	selection.markScenarioModified("a");
	selection.markScenarioModified("b");
	selection.scenarioRenamed("a", "c");
	// renaming selects the new id and forgets that the old id was edited
	QCOMPARE(selection.scenarioId(), "c");
	QVERIFY(!selection.isScenarioModified("a"));
	QVERIFY(selection.isScenarioModified("b"));
	selection.markScenarioModified("c");
	selection.scenarioDeleted("c", "b");
	// deleting a scenario selects the next one and forgets that the deleted one was edited
	QCOMPARE(selection.scenarioId(), "b");
	QVERIFY(!selection.isScenarioModified("c"));
	QVERIFY(selection.isScenarioModified("b"));
	selection.clearModifiedScenarios();
	QVERIFY2(!selection.isScenarioModified("b"), "the edited scenarios can be cleared");
}

void TestRunSelection::findsSelectedScenario() {
	SceneModel scene;
	const SceneModel& readOnly = scene;
	RunSelection selection;
	// a scene without scenarios has no selected scenario, and looking for it adds none
	QVERIFY(selection.selectedScenario(readOnly) == nullptr);
	QVERIFY(scene.scenarios.empty());
	SceneScenario first;
	first.id = "a";
	SceneScenario second;
	second.id = "b";
	scene.scenarios = {first, second};
	scene.defaultScenarioId = "b";
	// a selection that names no scenario gives the default scenario
	QCOMPARE(selection.selectedScenario(readOnly), &scene.scenarios[1]);
	QCOMPARE(selection.selectedScenario(scene), &scene.scenarios[1]);
	selection.selectScenario("a");
	// the selected scenario is the one with the selected id
	QCOMPARE(selection.selectedScenario(readOnly), &scene.scenarios[0]);
	QCOMPARE(selection.selectedScenario(scene), &scene.scenarios[0]);
	selection.selectScenario("gone");
	// a selected id that is not in the scene gives the default scenario
	QCOMPARE(selection.selectedScenario(readOnly), &scene.scenarios[1]);

	SceneModel empty;
	// asking for the scenario for an edit adds the baseline scenario to a scene without scenarios
	QVERIFY(selection.selectedScenario(empty) != nullptr);
	QCOMPARE(empty.scenarios.size(), std::size_t(1));
	QCOMPARE(empty.scenarios[0].id, "baseline");
}

void TestRunSelection::countsOccurrences() {
	const SceneModel scene = runScene();
	// the scene has 5 occurrences over the period and 4 of them enter inside it
	QCOMPARE(sceneTotalOccurrences(scene, kPeriod), 5);
	QCOMPARE(sceneInPeriodOccurrences(scene, kPeriod), 4);
	// a longer period adds the second run of S3
	QCOMPARE(sceneTotalOccurrences(scene, 3600.0), 6);
	QCOMPARE(sceneInPeriodOccurrences(scene, 3600.0), 6);
	// an empty scene has no occurrence
	QCOMPARE(sceneTotalOccurrences(SceneModel(), kPeriod), 0);
	QCOMPARE(sceneInPeriodOccurrences(SceneModel(), kPeriod), 0);
}

void TestRunSelection::excludesOccurrences() {
	const SceneModel scene = runScene();
	RunSelection selection;
	// with nothing left out the selection is empty, as the builder reads it, and every occurrence counts
	QVERIFY(selection.selectedOccurrences(scene, kPeriod).empty());
	QCOMPARE(selection.selectedCount(scene, kPeriod), 5);
	QCOMPARE(selection.selectedInPeriodCount(scene, kPeriod), 4);

	selection.setExcluded(occurrence("S2", 2), true);
	// an occurrence can be left out
	QVERIFY(selection.hasExclusions());
	QVERIFY(selection.isExcluded(occurrence("S2", 2)));
	QVERIFY(!selection.isExcluded(occurrence("S2", 1)));
	const SceneRunSelection selected = selection.selectedOccurrences(scene, kPeriod);
	// the selection holds every occurrence except the ones that are left out
	QCOMPARE(selected.size(), std::size_t(4));
	QCOMPARE(selected.count(occurrence("S2", 2)), std::size_t(0));
	QCOMPARE(selected.count(occurrence("S2", 3)), std::size_t(1));
	QCOMPARE(selected.count(occurrence("S1", 1)), std::size_t(1));
	QCOMPARE(selected.count(occurrence("S3", 1)), std::size_t(1));
	// the counts leave out the excluded occurrence
	QCOMPARE(selection.selectedCount(scene, kPeriod), 4);
	QCOMPARE(selection.selectedInPeriodCount(scene, kPeriod), 3);
	selection.setExcluded(occurrence("S2", 3), true);
	// an excluded occurrence that enters after the period does not lower the count in the period
	QCOMPARE(selection.selectedCount(scene, kPeriod), 3);
	QCOMPARE(selection.selectedInPeriodCount(scene, kPeriod), 3);

	selection.setExcluded(occurrence("S2", 2), false);
	selection.setExcluded(occurrence("S2", 3), false);
	// including the occurrences again leaves nothing excluded
	QVERIFY(!selection.hasExclusions());
	QVERIFY(selection.selectedOccurrences(scene, kPeriod).empty());

	selection.setExcluded(occurrence("gone", 1), true);
	// an exclusion of a service that does not exist is not counted
	QCOMPARE(selection.selectedCount(scene, kPeriod), 5);

	selection.excludeAll(scene, kPeriod);
	// excluding all leaves 5 occurrences out and none selected
	QCOMPARE(selection.excluded().size(), std::size_t(5));
	QCOMPARE(selection.selectedCount(scene, kPeriod), 0);
	QCOMPARE(selection.selectedInPeriodCount(scene, kPeriod), 0);
	QVERIFY(selection.selectedOccurrences(scene, kPeriod).empty());
	selection.includeAll();
	// including all clears the exclusions
	QVERIFY(!selection.hasExclusions());
	QCOMPARE(selection.selectedCount(scene, kPeriod), 5);
}

void TestRunSelection::prunesExclusions() {
	const SceneModel scene = runScene();
	RunSelection selection;
	for (const SceneServiceOccurrence& value : {occurrence("S1", 1), occurrence("S2", 4), occurrence("S2", 0),
			 occurrence("gone", 1), occurrence("S3", 2)})
		selection.setExcluded(value, true);
	selection.pruneExcluded(scene, kPeriod);
	QVERIFY2(selection.excluded() == SceneRunSelection({occurrence("S1", 1)}),
		"pruning forgets services that are gone and occurrences beyond the count of their service");
	selection.setExcluded(occurrence("S3", 2), true);
	selection.pruneExcluded(scene, 3600.0);
	QVERIFY2(selection.excluded() == SceneRunSelection({occurrence("S1", 1), occurrence("S3", 2)}),
		"an occurrence stays when the period is long enough for it");
}

void TestRunSelection::followsServiceRenames() {
	RunSelection selection;
	selection.setExcluded(occurrence("S2", 1), true);
	selection.setExcluded(occurrence("S2", 3), true);
	selection.setExcluded(occurrence("S1", 1), true);
	selection.serviceRenamed("S2", "S2b");
	QVERIFY2(selection.excluded() == SceneRunSelection({occurrence("S1", 1), occurrence("S2b", 1), occurrence("S2b", 3)}),
		"renaming a service moves its exclusions to the new id and keeps the occurrence numbers");
	selection.serviceRenamed("S1", "S1");
	selection.serviceRenamed("other", "S9");
	QVERIFY2(selection.excluded() == SceneRunSelection({occurrence("S1", 1), occurrence("S2b", 1), occurrence("S2b", 3)}),
		"renaming to the same id, or a service without exclusions, changes nothing");
}

void TestRunSelection::keepsExclusionsOutOfOversizedScene() {
	SceneModel scene = runScene();
	scene.services[1].repeatCount = RuntimeLimits::kMaxExpandedTrains + 1;
	RunSelection selection;
	selection.setExcluded(occurrence("S1", 1), true);
	// a scene with more occurrences than a run can expand keeps the exclusions out of the selection
	QCOMPARE_GT(sceneTotalOccurrences(scene, kPeriod), RuntimeLimits::kMaxExpandedTrains);
	QVERIFY(selection.selectedOccurrences(scene, kPeriod).empty());
	QCOMPARE(selection.selectedInPeriodCount(scene, kPeriod), sceneInPeriodOccurrences(scene, kPeriod));
}

QTEST_APPLESS_MAIN(TestRunSelection)
#include "test_runselection.moc"
