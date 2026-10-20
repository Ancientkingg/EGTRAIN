#include "app/RunSelection.h"

#include "scene/RuntimeLimits.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// The most occurrences that a run expands into trains.
constexpr int kMaxOccurrences = RuntimeLimits::kMaxExpandedTrains;

int occurrenceCount(const SceneService& service, double durationSeconds) {
	return std::max(0, sceneServiceOccurrenceCount(service, durationSeconds));
}

} // namespace

int sceneTotalOccurrences(const SceneModel& model, double durationSeconds) {
	int total = 0;
	for (const SceneService& service : model.services) {
		const int count = occurrenceCount(service, durationSeconds);
		if (count > std::numeric_limits<int>::max() - total)
			return std::numeric_limits<int>::max();
		total += count;
	}
	return total;
}

int sceneInPeriodOccurrences(const SceneModel& model, double durationSeconds) {
	long long total = 0;
	for (const SceneService& service : model.services) {
		total += sceneServiceInWindowCount(service, durationSeconds);
		if (total >= std::numeric_limits<int>::max())
			return std::numeric_limits<int>::max();
	}
	return static_cast<int>(total);
}

SceneScenario* RunSelection::selectedScenario(SceneModel& model) const {
	for (SceneScenario& scenario : model.scenarios) {
		if (scenario.id == m_scenarioId)
			return &scenario;
	}
	return defaultScenario(model);
}

const SceneScenario* RunSelection::selectedScenario(const SceneModel& model) const {
	for (const SceneScenario& scenario : model.scenarios) {
		if (scenario.id == m_scenarioId)
			return &scenario;
	}
	return defaultScenario(model);
}

void RunSelection::scenarioRenamed(const std::string& oldId, const std::string& newId) {
	m_scenarioId = newId;
	m_modifiedScenarioIds.erase(oldId);
}

void RunSelection::scenarioDeleted(const std::string& deletedId, const std::string& nextId) {
	m_modifiedScenarioIds.erase(deletedId);
	m_scenarioId = nextId;
}

void RunSelection::setExcluded(const SceneServiceOccurrence& occurrence, bool excluded) {
	if (excluded)
		m_excluded.insert(occurrence);
	else
		m_excluded.erase(occurrence);
}

void RunSelection::excludeAll(const SceneModel& model, double durationSeconds) {
	m_excluded.clear();
	for (const SceneService& service : model.services) {
		const int count = occurrenceCount(service, durationSeconds);
		for (int occurrence = 1; occurrence <= count; ++occurrence) {
			SceneServiceOccurrence value;
			value.serviceId = service.id;
			value.occurrence = occurrence;
			m_excluded.insert(value);
		}
	}
}

void RunSelection::pruneExcluded(const SceneModel& model, double durationSeconds) {
	for (auto it = m_excluded.begin(); it != m_excluded.end();) {
		const auto service = std::find_if(model.services.begin(), model.services.end(),
			[&](const SceneService& candidate) { return candidate.id == it->serviceId; });
		if (service == model.services.end() || it->occurrence < 1
			|| it->occurrence > sceneServiceOccurrenceCount(*service, durationSeconds))
			it = m_excluded.erase(it);
		else
			++it;
	}
}

void RunSelection::serviceRenamed(const std::string& oldId, const std::string& newId) {
	if (oldId == newId)
		return;
	SceneRunSelection migrated;
	for (const SceneServiceOccurrence& value : m_excluded) {
		SceneServiceOccurrence replacement = value;
		if (replacement.serviceId == oldId)
			replacement.serviceId = newId;
		migrated.insert(replacement);
	}
	m_excluded.swap(migrated);
}

SceneRunSelection RunSelection::selectedOccurrences(const SceneModel& model, double durationSeconds) const {
	SceneRunSelection selection;
	if (m_excluded.empty())
		return selection;
	if (sceneTotalOccurrences(model, durationSeconds) > kMaxOccurrences)
		return selection;
	for (const SceneService& service : model.services) {
		const int count = occurrenceCount(service, durationSeconds);
		for (int occurrence = 1; occurrence <= count; ++occurrence) {
			SceneServiceOccurrence value;
			value.serviceId = service.id;
			value.occurrence = occurrence;
			if (!isExcluded(value))
				selection.insert(value);
		}
	}
	return selection;
}

int RunSelection::selectedCount(const SceneModel& model, double durationSeconds) const {
	int excluded = 0;
	for (const SceneServiceOccurrence& value : m_excluded) {
		const auto service = std::find_if(model.services.begin(), model.services.end(),
			[&](const SceneService& candidate) { return candidate.id == value.serviceId; });
		if (service != model.services.end() && value.occurrence >= 1
			&& value.occurrence <= sceneServiceOccurrenceCount(*service, durationSeconds))
			++excluded;
	}
	return std::max(0, sceneTotalOccurrences(model, durationSeconds) - excluded);
}

int RunSelection::selectedInPeriodCount(const SceneModel& model, double durationSeconds) const {
	const int configured = sceneTotalOccurrences(model, durationSeconds);
	if (m_excluded.empty() || configured > kMaxOccurrences)
		return sceneInPeriodOccurrences(model, durationSeconds);
	int selected = 0;
	for (const SceneService& service : model.services) {
		const int count = occurrenceCount(service, durationSeconds);
		for (int occurrence = 1; occurrence <= count; ++occurrence) {
			const SceneServiceOccurrence value{service.id, occurrence};
			const double entry = sceneServiceScheduledEntry(service, occurrence);
			if (!isExcluded(value) && std::isfinite(entry) && entry >= 0.0 && entry < durationSeconds)
				++selected;
		}
	}
	return selected;
}
