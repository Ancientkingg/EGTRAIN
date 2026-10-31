#include "editors/EditorRules.h"

#include "editors/EditorHelpers.h"
#include "scene/RuntimeLimits.h"
#include "scene/SceneIds.h"
#include "scene/SceneValidator.h"
#include "scene/SectionInventory.h"

#include <QHash>

#include <algorithm>
#include <cstddef>
#include <set>
#include <utility>

namespace {

// The text of a signal failure target in the list: a signal by its id and the label of the section it protects, and a
// block by the label of its section.
QString signalFailureTargetText(const SceneModel& model, const SceneSectionLabels& labels, const std::string& target) {
	for (const auto& signal : sceneSignals(model)) {
		if (signal.id != target)
			continue;
		if (signal.protectedSection.empty())
			return QString::fromStdString(target);
		return QStringLiteral("%1 (%2)").arg(QString::fromStdString(target), QString::fromStdString(labels.label(signal.protectedSection)));
	}
	return QString::fromStdString(labels.label(target));
}

} // namespace

bool PassengerImportMerge::rejects(const std::string& passengerId) const {
	return !passengerId.empty() && std::find(rejected.begin(), rejected.end(), passengerId) != rejected.end();
}

PassengerImportMerge mergeImportedPassengers(const SceneModel& model, const ScenePassengerImportResult& imported, const std::string& sourcePath) {
	std::set<std::string> passengerIds;
	std::set<std::string> journeyIds;
	std::set<std::string> legIds;
	for (const auto& passenger : model.passengers) {
		passengerIds.insert(passenger.id);
		for (const auto& journey : passenger.journeys) {
			journeyIds.insert(journey.id);
			for (const auto& leg : journey.legs)
				legIds.insert(leg.id);
		}
	}
	PassengerImportMerge merge;
	for (const auto& passenger : imported.passengers) {
		bool collision = passenger.id.empty() || passengerIds.count(passenger.id) > 0;
		std::set<std::string> candidateJourneyIds;
		std::set<std::string> candidateLegIds;
		for (const auto& journey : passenger.journeys) {
			if (journey.id.empty() || journeyIds.count(journey.id) > 0
				|| !candidateJourneyIds.insert(journey.id).second)
				collision = true;
			for (const auto& leg : journey.legs) {
				if (leg.id.empty() || legIds.count(leg.id) > 0
					|| !candidateLegIds.insert(leg.id).second)
					collision = true;
			}
		}
		if (collision) {
			merge.rejected.push_back(passenger.id);
			continue;
		}
		passengerIds.insert(passenger.id);
		journeyIds.insert(candidateJourneyIds.begin(), candidateJourneyIds.end());
		legIds.insert(candidateLegIds.begin(), candidateLegIds.end());
		merge.accepted.push_back(passenger);
	}
	merge.report = imported.report;
	if (!merge.rejected.empty()) {
		const int accepted = static_cast<int>(merge.accepted.size());
		const int rejected = static_cast<int>(merge.rejected.size());
		merge.report.push_back({"passengers.editor", sourcePath, accepted + rejected, accepted, rejected, 0});
	}
	return merge;
}

std::vector<std::string> signalFailureTargets(const SceneModel& model) {
	std::vector<std::string> targets;
	std::set<std::string> seen;
	const auto add = [&targets, &seen](const std::string& target) {
		if (!target.empty() && seen.insert(target).second)
			targets.push_back(target);
	};
	for (const auto& signal : sceneSignals(model))
		add(signal.id);
	for (const auto& block : model.blocks)
		add(block.id);
	return targets;
}

IncidentTargetChoices incidentTargetChoices(const SceneModel& model, const SceneIncident* incident, double occurrenceDuration) {
	IncidentTargetChoices choices;
	std::vector<IncidentTargetChoice>& entries = choices.entries;
	const auto add = [&entries](const QString& text, const QString& target, std::optional<int> occurrence = std::nullopt) {
		entries.push_back({text, target, occurrence, QString()});
		return static_cast<int>(entries.size()) - 1;
	};
	add(QStringLiteral("Choose target"), QString());
	int current = 0;
	const bool signalFailure = incident && incident->type == "signal_failure";
	if (incident) {
		const QString target = QString::fromStdString(incident->target);
		bool found = false;
		if (signalFailure) {
			const SceneSectionLabels labels = sceneSectionLabels(buildSceneSectionInventory(model));
			for (const std::string& value : signalFailureTargets(model))
				add(signalFailureTargetText(model, labels, value), QString::fromStdString(value));
			const auto shown = std::find_if(entries.begin(), entries.end(), [&target](const IncidentTargetChoice& entry) { return entry.target == target; });
			current = shown == entries.end() ? -1 : static_cast<int>(shown - entries.begin());
			if (current < 0 && !target.isEmpty())
				current = add(QStringLiteral("%1 (missing)").arg(target), target);
		} else {
			const bool singular = incident->hasOccurrence || incident->occurrence != 1;
			if (!singular && !incident->target.empty()) {
				const bool exists = std::any_of(model.services.begin(), model.services.end(),
					[&](const SceneService& service) { return service.id == incident->target; });
				current = add((exists ? QStringLiteral("All runs of %1") : QStringLiteral("%1 (missing)")).arg(target), target, 0);
			}
			int shown = 0;
			for (const SceneService& service : model.services) {
				const int count = std::max(0, sceneServiceOccurrenceCount(service, occurrenceDuration));
				for (int occurrence = 1; occurrence <= count && shown < RuntimeLimits::kMaxExpandedTrains; ++occurrence, ++shown) {
					const int index = add(generatedServiceLabel(service, occurrence), QString::fromStdString(service.id), occurrence);
					if (singular && service.id == incident->target && occurrence == incident->occurrence) {
						current = index;
						found = true;
					}
				}
				if (shown >= RuntimeLimits::kMaxExpandedTrains)
					break;
			}
			if (singular && !found && !incident->target.empty()) {
				for (const SceneService& service : model.services) {
					if (service.id != incident->target)
						continue;
					const int count = std::max(0, sceneServiceOccurrenceCount(service, occurrenceDuration));
					if (incident->occurrence < 1 || incident->occurrence > count)
						break;
					current = add(generatedServiceLabel(service, incident->occurrence), QString::fromStdString(service.id), incident->occurrence);
					found = true;
					break;
				}
			}
		}
		if (!signalFailure && !incident->target.empty()
			&& ((incident->hasOccurrence || incident->occurrence != 1) ? !found : current == 0)) {
			current = add(QStringLiteral("%1 #%2 (missing)").arg(target).arg(incident->occurrence), target,
				incident->hasOccurrence || incident->occurrence != 1 ? incident->occurrence : 0);
		}
	}
	QHash<QString, int> labelCounts;
	for (const IncidentTargetChoice& entry : entries)
		++labelCounts[entry.text];
	for (IncidentTargetChoice& entry : entries) {
		const QString label = entry.text;
		const int occurrence = entry.occurrence.value_or(0);
		const QString identity = entry.target.isEmpty() ? QString()
			: occurrence > 0							? QString("service %1, run %2").arg(entry.target).arg(occurrence)
														: QString("service %1, all runs").arg(entry.target);
		if (labelCounts.value(label) > 1 && !identity.isEmpty())
			entry.text = QString("%1 (%2)").arg(label, identity);
		// A signal or block shows its id, a train its text and, on a line of its own, its service and run.
		entry.toolTip = signalFailure ? entry.target : label;
		if (!signalFailure && !identity.isEmpty())
			entry.toolTip += QStringLiteral("\n") + identity.left(1).toUpper() + identity.mid(1);
	}
	choices.current = std::max(0, current);
	return choices;
}

ScenarioImport prepareScenarioImport(const SceneModel& model, SceneScenario scenario) {
	ScenarioImport prepared;
	prepared.requestedId = scenario.id;
	const std::string uniqueId = sceneUniqueScenarioId(model, scenario.id);
	prepared.idTaken = uniqueId != scenario.id;
	scenario.id = uniqueId;
	for (auto& incident : scenario.incidents) {
		const std::string uniqueIncident = sceneUniqueIncidentId(model, incident.id);
		if (uniqueIncident != incident.id) {
			incident.id = uniqueIncident;
			++prepared.renamedIncidents;
		}
	}
	SceneModel candidate = model;
	candidate.scenarios.push_back(scenario);
	const std::string prefix = "scenarios[" + std::to_string(candidate.scenarios.size() - 1) + "]";
	for (const auto& diagnostic : validateScene(candidate)) {
		if (diagnostic.severity == SceneSeverity::Error && diagnostic.path.rfind(prefix, 0) == 0)
			prepared.errors.push_back(diagnostic);
	}
	prepared.scenario = std::move(scenario);
	return prepared;
}
