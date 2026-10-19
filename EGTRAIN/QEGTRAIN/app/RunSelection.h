#ifndef RUNSELECTION_H
#define RUNSELECTION_H

// SceneModel has a member named signals, which the Qt keyword hides. Include the scene model before any Qt header,
// or through editors/SceneDocument.h.
#include "scene/SceneModel.h"

#include <set>
#include <string>

/// How many generated service occurrences the scene has over `durationSeconds` (seconds from the base time of the
/// scene). The count stops at the largest int.
int sceneTotalOccurrences(const SceneModel& model, double durationSeconds);
/// How many generated service occurrences enter the simulation at or after second 0 and before `durationSeconds`. The
/// count stops at the largest int.
int sceneInPeriodOccurrences(const SceneModel& model, double durationSeconds);

/// What the next run uses of the scene, apart from the scene itself: the scenario that is selected, the service
/// occurrences that are left out, and the scenarios that have been edited since the scene was saved. It also keeps the
/// scenario of the run whose results are shown. The editors change it, and run control reads it. Occurrences are
/// numbered from 1 within their service, and a duration is in seconds from the base time of the scene.
class RunSelection {
public:
	/// The id of the scenario that the editors show and the next run uses. It may be empty or name no scenario of
	/// the scene; selectedScenario() then returns the default scenario.
	const std::string& scenarioId() const { return m_scenarioId; }
	/// Selects the scenario with the id `id`.
	void selectScenario(const std::string& id) { m_scenarioId = id; }
	/// The selected scenario of `model`, or its default scenario when the model has none with the selected id. This
	/// overload adds the baseline scenario to a model without scenarios.
	SceneScenario* selectedScenario(SceneModel& model) const;
	/// As the overload above, but null for a model without scenarios.
	const SceneScenario* selectedScenario(const SceneModel& model) const;
	/// The scenario that the latest run used; empty while no run's results are kept.
	const std::string& appliedScenarioId() const { return m_appliedScenarioId; }
	/// The run that is being prepared uses the selected scenario.
	void applySelectedScenario() { m_appliedScenarioId = m_scenarioId; }
	/// No run's results are kept.
	void clearAppliedScenario() { m_appliedScenarioId.clear(); }
	/// The scenario got another id: it stays selected, and the old id no longer counts as edited.
	void scenarioRenamed(const std::string& oldId, const std::string& newId);
	/// The scenario is gone and `nextId` is selected in its place.
	void scenarioDeleted(const std::string& deletedId, const std::string& nextId);

	/// The scenario `id` was edited since the scene was saved.
	bool isScenarioModified(const std::string& id) const { return m_modifiedScenarioIds.count(id) > 0; }
	/// Records that the scenario `id` was edited.
	void markScenarioModified(const std::string& id) { m_modifiedScenarioIds.insert(id); }
	/// No scenario counts as edited, as after a save.
	void clearModifiedScenarios() { m_modifiedScenarioIds.clear(); }

	/// The occurrences that a run leaves out. All others are selected.
	const SceneRunSelection& excluded() const { return m_excluded; }
	/// At least one occurrence is left out.
	bool hasExclusions() const { return !m_excluded.empty(); }
	/// The run leaves `occurrence` out.
	bool isExcluded(const SceneServiceOccurrence& occurrence) const { return m_excluded.count(occurrence) > 0; }
	/// Leaves `occurrence` out of the run or takes it in again.
	void setExcluded(const SceneServiceOccurrence& occurrence, bool excluded);
	/// Selects every occurrence.
	void includeAll() { m_excluded.clear(); }
	/// Leaves out every occurrence of the scene over `durationSeconds`.
	void excludeAll(const SceneModel& model, double durationSeconds);
	/// Forgets exclusions of services that are gone and of occurrences beyond the count of their service.
	void pruneExcluded(const SceneModel& model, double durationSeconds);
	/// The exclusions of a service follow it to its new id.
	void serviceRenamed(const std::string& oldId, const std::string& newId);

	/// The occurrences that the run selects, as the scene builder takes them. The result is empty when nothing is left
	/// out and when the scene has more occurrences than a run can expand; the builder reads an empty selection as every
	/// occurrence.
	SceneRunSelection selectedOccurrences(const SceneModel& model, double durationSeconds) const;
	/// How many occurrences over `durationSeconds` are selected.
	int selectedCount(const SceneModel& model, double durationSeconds) const;
	/// How many of the selected occurrences enter the simulation at or after second 0 and before `durationSeconds`.
	int selectedInPeriodCount(const SceneModel& model, double durationSeconds) const;

private:
	std::string m_scenarioId;
	std::string m_appliedScenarioId;
	std::set<std::string> m_modifiedScenarioIds;
	SceneRunSelection m_excluded;
};

#endif // RUNSELECTION_H
