#include "scene/SceneReferences.h"

#include "scene/SectionInventory.h"
#include "scene/SectionReference.h"

#include <algorithm>

namespace {

std::string quoted(const std::string& text) {
	return "'" + text + "'";
}

void replaceId(std::string& value, const std::string& oldId, const std::string& newId) {
	if (value == oldId)
		value = newId;
}

} // namespace

std::vector<std::string> sceneBlockReferenceComponents(const std::string& reference) {
	std::vector<std::string> components;
	for (const std::string& part : sectionReferenceParts(reference))
		if (const std::optional<SectionReferencePart> parsed = parseSectionReferencePart(part))
			components.push_back(parsed->blockId);
	return components;
}

std::string sceneRewriteBlockReference(const std::string& reference, const std::string& oldId,
	const std::string& newId) {
	if (reference == oldId)
		return newId;

	std::string rewritten;
	for (std::string part : sectionReferenceParts(reference)) {
		const std::optional<SectionReferencePart> parsed = parseSectionReferencePart(part);
		// The block id starts after the '@' of a wrapped part.
		if (parsed && parsed->blockId == oldId)
			part.replace(part.front() == '@' ? 1 : 0, oldId.size(), newId);
		if (!rewritten.empty())
			rewritten += '/';
		rewritten += part;
	}
	return rewritten;
}

std::vector<std::string> sceneDirectDeleteConsumers(const SceneModel& model, const std::string& facet,
	const std::string& id, const std::string& scope) {
	std::vector<std::string> consumers;
	const auto add = [&consumers](const std::string& description) {
		if (!description.empty() && std::find(consumers.begin(), consumers.end(), description) == consumers.end())
			consumers.push_back(description);
	};
	const auto sectionUsesBlock = [&id](const SceneSectionInventory& inventory, const std::string& reference) {
		const std::vector<std::string> components = sceneBlockReferenceComponents(reference);
		if (std::find(components.begin(), components.end(), id) != components.end())
			return true;
		const SceneSectionDescriptor* section = inventory.resolve(reference);
		return section && (section->sourceBlockId == id || section->firstBlockId == id || section->secondBlockId == id);
	};
	const auto sectionUsesConnection = [](const SceneSectionInventory& inventory, const std::string& connectionId,
										   const std::string& reference) {
		const SceneSectionDescriptor* section = inventory.resolve(reference);
		return section && section->sourceConnectionId == connectionId;
	};

	if (facet == "tracks") {
		for (const auto& node : model.nodes)
			if (node.trackId == id)
				add("node " + quoted(node.id));
		for (const auto& arc : model.arcs)
			if (arc.trackId == id)
				add("arc " + quoted(arc.id));
		for (const auto& block : model.blocks)
			if (block.trackId == id)
				add("block " + quoted(block.id));
		for (const auto& area : model.signallingAreas)
			if (area.trackId == id)
				add("signalling area " + quoted(area.id));
	} else if (facet == "nodes") {
		for (const auto& arc : model.arcs)
			if (arc.fromNodeId == id || arc.toNodeId == id)
				add("arc " + quoted(arc.id) + " endpoint");
		for (const auto& connection : model.connections)
			if (connection.fromNodeId == id || connection.toNodeId == id)
				add("connection " + quoted(connection.id) + " endpoint");
		for (const auto& station : model.stations)
			for (const auto& platform : station.platforms)
				if (std::find(platform.nodeIds.begin(), platform.nodeIds.end(), id) != platform.nodeIds.end())
					add("platform " + quoted(platform.id) + " at station " + quoted(station.id));
	} else if (facet == "blocks" || facet == "connections") {
		const SceneSectionInventory inventory = buildSceneSectionInventory(model);
		const auto usesSection = [&](const std::string& reference) {
			return facet == "blocks" ? sectionUsesBlock(inventory, reference)
									 : sectionUsesConnection(inventory, id, reference);
		};
		for (const auto& route : model.routes)
			for (const auto& token : route.blocks)
				if (usesSection(token)) {
					add("route " + quoted(route.id) + " section " + quoted(token));
					break;
				}
		for (const auto& signal : model.signals)
			if (usesSection(signal.protectedSection))
				add("signal " + quoted(signal.id) + " protected section " + quoted(signal.protectedSection));
		for (const auto& scenario : model.scenarios)
			for (const auto& incident : scenario.incidents)
				if (incident.type == "signal_failure" && usesSection(incident.target))
					add("signal-failure incident " + quoted(incident.id) + " in scenario " + quoted(scenario.id));
		for (const auto& dependency : model.blockDependencies)
			if (usesSection(dependency.block) || usesSection(dependency.dependsOn))
				add("block dependency " + quoted(dependency.block) + " -> " + quoted(dependency.dependsOn));
		for (const auto& restriction : model.singleTrackRestrictions)
			if (usesSection(restriction.startBlock) || usesSection(restriction.endBlock)
				|| usesSection(restriction.protectedStartBlock) || usesSection(restriction.protectedEndBlock))
				add("single-track restriction " + quoted(restriction.startBlock) + " -> " + quoted(restriction.endBlock));
		for (const auto& boundary : model.stationBoundaries)
			if (usesSection(boundary.entranceBlock) || (boundary.hasExitBlock && usesSection(boundary.exitBlock)))
				add("station boundary " + quoted(boundary.entranceBlock) + " -> " + quoted(boundary.exitBlock));
	} else if (facet == "stations") {
		for (const auto& service : model.services)
			for (const auto& stop : service.stops)
				if (stop.stationId == id)
					add("service " + quoted(service.id) + " stop");
		for (const auto& scenario : model.scenarios)
			for (const auto& delay : scenario.entranceDelays)
				if (delay.stationId == id)
					add("entrance delay for service " + quoted(delay.serviceId) + " in scenario " + quoted(scenario.id));
		for (const auto& passenger : model.passengers)
			for (const auto& journey : passenger.journeys) {
				if (journey.originStationId == id || journey.destinationStationId == id)
					add("passenger " + quoted(passenger.id) + " journey " + quoted(journey.id));
				for (const auto& leg : journey.legs)
					if (leg.originStationId == id || leg.destinationStationId == id)
						add("passenger " + quoted(passenger.id) + " leg " + quoted(leg.id));
			}
	} else if (facet == "platforms") {
		for (const auto& service : model.services)
			for (const auto& stop : service.stops)
				if (stop.stationId == scope && stop.platformId == id)
					add("service " + quoted(service.id) + " stop at station " + quoted(scope));
	} else if (facet == "signals") {
		for (const auto& scenario : model.scenarios)
			for (const auto& incident : scenario.incidents)
				if (incident.type == "signal_failure" && incident.target == id)
					add("signal-failure incident " + quoted(incident.id) + " in scenario " + quoted(scenario.id));
	} else if (facet == "routes") {
		for (const auto& service : model.services)
			if (service.route == id)
				add("service " + quoted(service.id));
	} else if (facet == "train_unit") {
		for (const auto& composition : model.compositions)
			if (std::find(composition.units.begin(), composition.units.end(), id) != composition.units.end())
				add("composition " + quoted(composition.id));
	} else if (facet == "composition") {
		for (const auto& service : model.services)
			if (service.composition == id)
				add("service " + quoted(service.id));
	} else if (facet == "service") {
		for (const auto& scenario : model.scenarios) {
			for (const auto& incident : scenario.incidents)
				if (incident.type == "train_breakdown" && incident.target == id)
					add("breakdown incident " + quoted(incident.id) + " in scenario " + quoted(scenario.id));
			for (const auto& delay : scenario.entranceDelays)
				if (delay.serviceId == id)
					add("entrance delay in scenario " + quoted(scenario.id));
		}
		for (const auto& passenger : model.passengers)
			for (const auto& journey : passenger.journeys)
				for (const auto& leg : journey.legs)
					if (leg.serviceId == id)
						add("passenger " + quoted(passenger.id) + " leg " + quoted(leg.id));
	}
	return consumers;
}

void sceneRewriteTrackReferences(SceneModel& model, const std::string& oldId, const std::string& newId) {
	for (auto& node : model.nodes)
		replaceId(node.trackId, oldId, newId);
	for (auto& arc : model.arcs)
		replaceId(arc.trackId, oldId, newId);
	for (auto& block : model.blocks)
		replaceId(block.trackId, oldId, newId);
	for (auto& area : model.signallingAreas)
		replaceId(area.trackId, oldId, newId);
}

void sceneRewriteNodeReferences(SceneModel& model, const std::string& oldId, const std::string& newId) {
	for (auto& arc : model.arcs) {
		replaceId(arc.fromNodeId, oldId, newId);
		replaceId(arc.toNodeId, oldId, newId);
	}
	for (auto& connection : model.connections) {
		replaceId(connection.fromNodeId, oldId, newId);
		replaceId(connection.toNodeId, oldId, newId);
	}
	for (auto& station : model.stations)
		for (auto& platform : station.platforms)
			for (auto& nodeId : platform.nodeIds)
				replaceId(nodeId, oldId, newId);
}

void sceneRewriteBlockReferences(SceneModel& model, const std::string& oldId, const std::string& newId) {
	const auto rewrite = [&oldId, &newId](std::string& reference) {
		reference = sceneRewriteBlockReference(reference, oldId, newId);
	};
	for (auto& route : model.routes)
		for (auto& reference : route.blocks)
			rewrite(reference);
	for (auto& dependency : model.blockDependencies) {
		rewrite(dependency.block);
		rewrite(dependency.dependsOn);
	}
	for (auto& restriction : model.singleTrackRestrictions) {
		rewrite(restriction.startBlock);
		rewrite(restriction.endBlock);
		rewrite(restriction.protectedStartBlock);
		rewrite(restriction.protectedEndBlock);
	}
	for (auto& boundary : model.stationBoundaries) {
		rewrite(boundary.entranceBlock);
		if (boundary.hasExitBlock)
			rewrite(boundary.exitBlock);
	}
	for (auto& scenario : model.scenarios)
		for (auto& incident : scenario.incidents)
			if (incident.type == "signal_failure")
				rewrite(incident.target);
	for (auto& signal : model.signals)
		if (!signal.protectedSection.empty())
			rewrite(signal.protectedSection);
}

void sceneRewriteStationReferences(SceneModel& model, const std::string& oldId, const std::string& newId) {
	for (auto& service : model.services)
		for (auto& stop : service.stops)
			replaceId(stop.stationId, oldId, newId);
	for (auto& scenario : model.scenarios)
		for (auto& delay : scenario.entranceDelays)
			replaceId(delay.stationId, oldId, newId);
	for (auto& passenger : model.passengers)
		for (auto& journey : passenger.journeys) {
			replaceId(journey.originStationId, oldId, newId);
			replaceId(journey.destinationStationId, oldId, newId);
			for (auto& leg : journey.legs) {
				replaceId(leg.originStationId, oldId, newId);
				replaceId(leg.destinationStationId, oldId, newId);
			}
		}
}

void sceneRewritePlatformReferences(SceneModel& model, const std::string& stationId, const std::string& oldId,
	const std::string& newId) {
	for (auto& service : model.services)
		for (auto& stop : service.stops)
			if (stop.stationId == stationId)
				replaceId(stop.platformId, oldId, newId);
}

void sceneMovePlatformReferences(SceneModel& model, const std::string& platformId,
	const std::string& fromStationId, const std::string& toStationId) {
	for (auto& service : model.services)
		for (auto& stop : service.stops)
			if (stop.stationId == fromStationId && stop.platformId == platformId)
				stop.stationId = toStationId;
}

void sceneRewriteSignalReferences(SceneModel& model, const std::string& oldId, const std::string& newId) {
	for (auto& scenario : model.scenarios)
		for (auto& incident : scenario.incidents)
			if (incident.type == "signal_failure")
				replaceId(incident.target, oldId, newId);
}

void sceneRewriteRouteReferences(SceneModel& model, const std::string& oldId, const std::string& newId) {
	for (auto& service : model.services)
		replaceId(service.route, oldId, newId);
}

void sceneRewriteTrainUnitReferences(SceneModel& model, const std::string& oldId, const std::string& newId) {
	for (auto& composition : model.compositions)
		for (auto& unitId : composition.units)
			replaceId(unitId, oldId, newId);
}

void sceneRewriteCompositionReferences(SceneModel& model, const std::string& oldId, const std::string& newId) {
	for (auto& service : model.services)
		replaceId(service.composition, oldId, newId);
}

void sceneRewriteServiceReferences(SceneModel& model, const std::string& oldId, const std::string& newId) {
	for (auto& scenario : model.scenarios) {
		for (auto& delay : scenario.entranceDelays)
			replaceId(delay.serviceId, oldId, newId);
		for (auto& incident : scenario.incidents)
			if (incident.type == "train_breakdown")
				replaceId(incident.target, oldId, newId);
	}
	for (auto& passenger : model.passengers)
		for (auto& journey : passenger.journeys)
			for (auto& leg : journey.legs)
				replaceId(leg.serviceId, oldId, newId);
}

void sceneRewriteScenarioReferences(SceneModel& model, const std::string& oldId, const std::string& newId) {
	replaceId(model.defaultScenarioId, oldId, newId);
}

const SceneTrainUnit* sceneTrainUnitById(const SceneModel& model, const std::string& id) {
	for (const SceneTrainUnit& unit : model.trainUnits) {
		if (unit.id == id)
			return &unit;
	}
	return nullptr;
}
