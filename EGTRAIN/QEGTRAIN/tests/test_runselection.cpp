#include "app/RunSelection.h"

#include "simulation/RuntimeLimits.h"

#include <iostream>
#include <string>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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

int main() {
	bool ok = true;

	{
		RunSelection selection;
		ok &= expect(selection.scenarioId().empty() && selection.appliedScenarioId().empty() && !selection.hasExclusions()
				&& !selection.isScenarioModified("a"),
			"a new selection has no scenario, no applied scenario, no exclusion and no edited scenario");
	}

	{
		RunSelection selection;
		selection.selectScenario("a");
		selection.applySelectedScenario();
		selection.selectScenario("b");
		ok &= expect(selection.scenarioId() == "b" && selection.appliedScenarioId() == "a",
			"selecting another scenario leaves the scenario of the latest run as it was");
		selection.clearAppliedScenario();
		ok &= expect(selection.appliedScenarioId().empty() && selection.scenarioId() == "b",
			"clearing the applied scenario keeps the selected one");

		selection.markScenarioModified("a");
		selection.markScenarioModified("b");
		selection.scenarioRenamed("a", "c");
		ok &= expect(selection.scenarioId() == "c" && !selection.isScenarioModified("a") && selection.isScenarioModified("b"),
			"renaming selects the new id and forgets that the old id was edited");
		selection.markScenarioModified("c");
		selection.scenarioDeleted("c", "b");
		ok &= expect(selection.scenarioId() == "b" && !selection.isScenarioModified("c") && selection.isScenarioModified("b"),
			"deleting a scenario selects the next one and forgets that the deleted one was edited");
		selection.clearModifiedScenarios();
		ok &= expect(!selection.isScenarioModified("b"), "the edited scenarios can be cleared");
	}

	{
		const SceneModel scene = runScene();
		ok &= expect(sceneTotalOccurrences(scene, kPeriod) == 5 && sceneInPeriodOccurrences(scene, kPeriod) == 4,
			"the scene has 5 occurrences over the period and 4 of them enter inside it");
		ok &= expect(sceneTotalOccurrences(scene, 3600.0) == 6 && sceneInPeriodOccurrences(scene, 3600.0) == 6,
			"a longer period adds the second run of S3");
		ok &= expect(sceneTotalOccurrences(SceneModel(), kPeriod) == 0 && sceneInPeriodOccurrences(SceneModel(), kPeriod) == 0,
			"an empty scene has no occurrence");
	}

	{
		const SceneModel scene = runScene();
		RunSelection selection;
		ok &= expect(selection.selectedOccurrences(scene, kPeriod).empty() && selection.selectedCount(scene, kPeriod) == 5
				&& selection.selectedInPeriodCount(scene, kPeriod) == 4,
			"with nothing left out the selection is empty, as the builder reads it, and every occurrence counts");

		selection.setExcluded(occurrence("S2", 2), true);
		ok &= expect(selection.hasExclusions() && selection.isExcluded(occurrence("S2", 2)) && !selection.isExcluded(occurrence("S2", 1)),
			"an occurrence can be left out");
		const SceneRunSelection selected = selection.selectedOccurrences(scene, kPeriod);
		ok &= expect(selected.size() == 4 && selected.count(occurrence("S2", 2)) == 0 && selected.count(occurrence("S2", 3)) == 1
				&& selected.count(occurrence("S1", 1)) == 1 && selected.count(occurrence("S3", 1)) == 1,
			"the selection holds every occurrence except the ones that are left out");
		ok &= expect(selection.selectedCount(scene, kPeriod) == 4 && selection.selectedInPeriodCount(scene, kPeriod) == 3,
			"the counts leave out the excluded occurrence");
		selection.setExcluded(occurrence("S2", 3), true);
		ok &= expect(selection.selectedCount(scene, kPeriod) == 3 && selection.selectedInPeriodCount(scene, kPeriod) == 3,
			"an excluded occurrence that enters after the period does not lower the count in the period");

		selection.setExcluded(occurrence("S2", 2), false);
		selection.setExcluded(occurrence("S2", 3), false);
		ok &= expect(!selection.hasExclusions() && selection.selectedOccurrences(scene, kPeriod).empty(),
			"including the occurrences again leaves nothing excluded");

		selection.setExcluded(occurrence("gone", 1), true);
		ok &= expect(selection.selectedCount(scene, kPeriod) == 5, "an exclusion of a service that does not exist is not counted");

		selection.excludeAll(scene, kPeriod);
		ok &= expect(selection.excluded().size() == 5 && selection.selectedCount(scene, kPeriod) == 0
				&& selection.selectedInPeriodCount(scene, kPeriod) == 0 && selection.selectedOccurrences(scene, kPeriod).empty(),
			"excluding all leaves 5 occurrences out and none selected");
		selection.includeAll();
		ok &= expect(!selection.hasExclusions() && selection.selectedCount(scene, kPeriod) == 5, "including all clears the exclusions");
	}

	{
		const SceneModel scene = runScene();
		RunSelection selection;
		for (const SceneServiceOccurrence& value : {occurrence("S1", 1), occurrence("S2", 4), occurrence("S2", 0),
				 occurrence("gone", 1), occurrence("S3", 2)})
			selection.setExcluded(value, true);
		selection.pruneExcluded(scene, kPeriod);
		ok &= expect(selection.excluded() == SceneRunSelection({occurrence("S1", 1)}),
			"pruning forgets services that are gone and occurrences beyond the count of their service");
		selection.setExcluded(occurrence("S3", 2), true);
		selection.pruneExcluded(scene, 3600.0);
		ok &= expect(selection.excluded() == SceneRunSelection({occurrence("S1", 1), occurrence("S3", 2)}),
			"an occurrence stays when the period is long enough for it");
	}

	{
		RunSelection selection;
		selection.setExcluded(occurrence("S2", 1), true);
		selection.setExcluded(occurrence("S2", 3), true);
		selection.setExcluded(occurrence("S1", 1), true);
		selection.serviceRenamed("S2", "S2b");
		ok &= expect(selection.excluded() == SceneRunSelection({occurrence("S1", 1), occurrence("S2b", 1), occurrence("S2b", 3)}),
			"renaming a service moves its exclusions to the new id and keeps the occurrence numbers");
		selection.serviceRenamed("S1", "S1");
		selection.serviceRenamed("other", "S9");
		ok &= expect(selection.excluded() == SceneRunSelection({occurrence("S1", 1), occurrence("S2b", 1), occurrence("S2b", 3)}),
			"renaming to the same id, or a service without exclusions, changes nothing");
	}

	{
		SceneModel scene = runScene();
		scene.services[1].repeatCount = RuntimeLimits::kMaxExpandedTrains + 1;
		RunSelection selection;
		selection.setExcluded(occurrence("S1", 1), true);
		ok &= expect(sceneTotalOccurrences(scene, kPeriod) > RuntimeLimits::kMaxExpandedTrains
				&& selection.selectedOccurrences(scene, kPeriod).empty()
				&& selection.selectedInPeriodCount(scene, kPeriod) == sceneInPeriodOccurrences(scene, kPeriod),
			"a scene with more occurrences than a run can expand keeps the exclusions out of the selection");
	}

	if (!ok)
		return 1;
	std::cout << "all run selection tests passed\n";
	return 0;
}
