#include "editors/infrastructure/TopologyFacets.h"

#include "editors/infrastructure/ListFacet.h"
#include "graphics/NetworkPresenter.h"
#include "scene/RuntimeLimits.h"
#include "scene/SceneReferences.h"
#include "scene/SectionInventory.h"

#include <QString>
#include <QStringList>
#include <QTableWidget>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

// The item of the list with the id, or null.
template <typename Item>
const Item* withId(const std::vector<Item>& items, const std::string& id) {
	const auto found = std::find_if(items.begin(), items.end(), [&id](const Item& item) { return item.id == id; });
	return found == items.end() ? nullptr : &*found;
}

// The item of the list in `row`, or null.
template <typename Item>
const Item* inRow(const std::vector<Item>& items, int row) {
	return row >= 0 && row < static_cast<int>(items.size()) ? &items[static_cast<std::size_t>(row)] : nullptr;
}

class TracksFacet final : public ListFacet<SceneTrack> {
public:
	TracksFacet() : ListFacet("tracks", "track", &SceneModel::tracks, {"ID"}) {}
	// Reverse Track turns a track whose arcs run against its X around, so that they run along it.
	std::vector<TaskAction> taskActions() const override {
		return {{QStringLiteral("reverseTrackButton"), QStringLiteral("Reverse Track"),
			QStringLiteral("Swaps the ends of every arc of the selected track and reverses the order of its blocks.")}};
	}
	void runTaskAction(InfrastructurePanel& panel, int index) override {
		if (index == 0)
			reverseTrack(panel);
	}

private:
	// Swaps the ends of the arcs of the track, negates their gradients and reverses its blocks within the places that
	// they hold in scene.blocks, in one edit. Nodes, X, block ids and lengths, and every reference stay as they are, so
	// the routes over the track are only listed, for the user to check against the new block ends.
	static void reverseTrack(InfrastructurePanel& panel) {
		const QString trackText = panel.selectionId();
		const std::string trackId = trackText.toStdString();
		if (sceneTrackChain(panel.model(), trackId).arcs.empty()) {
			panel.showStatus(QStringLiteral("Track '%1' is not one line of arcs. Case Validation names what to fix.").arg(trackText));
			return;
		}
		const bool reversed = panel.edit(QStringLiteral("Reverse track"), [&trackId](SceneModel& scene) {
			for (SceneArc& arc : scene.arcs) {
				if (arc.trackId != trackId)
					continue;
				std::swap(arc.fromNodeId, arc.toNodeId);
				// The gradient is stored along the direction of the track, so it changes sign with it. 0 stays +0.
				if (arc.gradientPercent != 0.0)
					arc.gradientPercent = -arc.gradientPercent;
			}
			std::vector<std::size_t> places;
			for (std::size_t place = 0; place < scene.blocks.size(); ++place)
				if (scene.blocks[place].trackId == trackId)
					places.push_back(place);
			for (std::size_t low = 0, high = places.size(); low + 1 < high; ++low, --high)
				std::swap(scene.blocks[places[low]], scene.blocks[places[high - 1]]);
		});
		if (!reversed)
			return;
		std::unordered_set<std::string> blockIds;
		for (const SceneBlock& block : panel.model().blocks)
			if (block.trackId == trackId)
				blockIds.insert(block.id);
		std::vector<std::string> routes;
		for (const SceneRoute& route : panel.model().routes)
			if (std::any_of(route.blocks.begin(), route.blocks.end(), [&blockIds](const std::string& reference) {
					const std::vector<std::string> components = sceneBlockReferenceComponents(reference);
					return std::any_of(components.begin(), components.end(),
						[&blockIds](const std::string& id) { return blockIds.count(id) != 0; });
				}))
				routes.push_back(route.id);
		if (!routes.empty()) {
			// A box that lists every route can be taller than the screen.
			constexpr std::size_t kMaxListedRoutes = 10;
			QStringList lines;
			for (std::size_t route = 0; route < routes.size() && route < kMaxListedRoutes; ++route)
				lines << QString::fromStdString(routes[route]);
			QString list = lines.join("\n• ");
			if (routes.size() > kMaxListedRoutes)
				list += QStringLiteral("\nand %1 more").arg(static_cast<int>(routes.size() - kMaxListedRoutes));
			panel.prompts().showInfo(&panel, QStringLiteral("Track Reversed"),
				QStringLiteral("Track '%1' is reversed. Check these routes, which run over it:\n• %2").arg(trackText, list));
		}
		panel.showStatus(QStringLiteral("Reversed track '%1'. Undo restores it.").arg(trackText), 8000);
	}

	void fillRow(QTableWidget& table, int row, const SceneTrack& track) const override {
		setCell(table, row, 0, QString::fromStdString(track.id));
	}
	Edit editField(SceneTrack&, int, const QString&) const override { return Edit::Unchanged; }
	SceneTrack newItem(const SceneModel&, const std::string& id) const override { return {id}; }
	void rewriteReferences(SceneModel& model, const std::string& oldId, const std::string& newId) const override {
		sceneRewriteTrackReferences(model, oldId, newId);
	}
};

// The station of a node is the one that the Details dock names for it (previewStationMemberships): the station of a
// platform that lists the node, or a station without platforms at the position of the node.
class NodesFacet final : public ListFacet<SceneNode> {
public:
	NodesFacet() : ListFacet("nodes", "node", &SceneModel::nodes, {"ID", "Station", "Track ID", "X (km)", "Y (km)"}) {}
	Capabilities capabilities() const override { return TrackRowFilter; }

	// The name of the station of the node, which cannot be edited, comes after the id.
	void fillRows(InfrastructurePanel& panel) override {
		ListFacet::fillRows(panel);
		const std::vector<SceneNode>& nodes = panel.model().nodes;
		const std::map<std::string, PreviewStationMembership> stations = previewStationMemberships(panel.model());
		for (int row = 0; row < static_cast<int>(nodes.size()); ++row) {
			const auto station = stations.find(nodes[static_cast<std::size_t>(row)].id);
			setCell(*panel.table(), row, kStationColumn,
				station == stations.end() ? QString() : QString::fromStdString(station->second.name), false);
		}
	}
	// Add Station... adds a station with one platform at the selected node.
	std::vector<TaskAction> taskActions() const override {
		return {{QStringLiteral("addStationAtNodeButton"), QStringLiteral("Add Station..."),
			QStringLiteral("Adds a station with one platform at the selected node.")}};
	}
	void runTaskAction(InfrastructurePanel& panel, int index) override {
		if (index == 0)
			addStationAtNode(panel);
	}
	bool rowOnTrack(const InfrastructurePanel& panel, int row, const std::string& trackId) const override {
		const SceneNode* node = inRow(panel.model().nodes, row);
		return node && node->trackId == trackId;
	}
	// The track is one of the tracks of the scene.
	std::vector<Choice> choices(const InfrastructurePanel& panel, int, int column) const override {
		std::vector<Choice> result;
		if (column == kTrackColumn)
			for (const SceneTrack& track : panel.model().tracks)
				result.push_back({QString::fromStdString(track.id), QString::fromStdString(track.id)});
		return result;
	}

private:
	// Asks for a name and adds the station and its platform in one edit. It refuses, before it asks, a node that has a
	// station and a case that has the most stations that a run takes.
	static void addStationAtNode(InfrastructurePanel& panel) {
		const QString nodeText = panel.selectionId();
		const std::string nodeId = nodeText.toStdString();
		const SceneModel& model = panel.model();
		const std::map<std::string, PreviewStationMembership> stations = previewStationMemberships(model);
		const auto member = stations.find(nodeId);
		if (member != stations.end()) {
			panel.showStatus(QStringLiteral("Node '%1' already belongs to station '%2'. Add a platform to that station under Platforms.")
					.arg(nodeText, QString::fromStdString(member->second.name)));
			return;
		}
		if (model.stations.size() >= RuntimeLimits::kNativeMaxStations) {
			panel.showStatus(QStringLiteral("The case already has %1 stations, the most a run takes.")
					.arg(static_cast<int>(RuntimeLimits::kNativeMaxStations)));
			return;
		}
		const std::optional<QString> answer = panel.prompts().askText(
			&panel, QStringLiteral("Add Station"), QStringLiteral("Name of the new station at node %1:").arg(nodeText));
		if (!answer)
			return;
		const QString name = answer->trimmed();
		if (name.isEmpty()) {
			panel.showStatus(QStringLiteral("Give the station a name."));
			return;
		}
		if (name.contains('\t') || name.contains('\n') || name.contains('\r')) {
			panel.showStatus(QStringLiteral("A station name cannot hold a tab or a line break."));
			return;
		}
		const std::string stationId = name.toStdString();
		if (std::any_of(model.stations.begin(), model.stations.end(),
				[&stationId](const SceneStation& station) { return station.id == stationId || station.name == stationId; })) {
			panel.showStatus(QStringLiteral("Station '%1' already exists. Add a platform to it under Platforms.").arg(name));
			return;
		}
		panel.setSelectionId(nodeText);
		const bool added = panel.edit(QStringLiteral("Add station"), [&](SceneModel& scene) {
			SceneStation station;
			station.id = stationId;
			station.name = stationId;
			ScenePlatform platform;
			platform.id = stationId + ".platform.1";
			platform.nodeIds = {nodeId};
			station.platforms.push_back(std::move(platform));
			scene.stations.push_back(std::move(station));
		});
		if (added)
			panel.showStatus(QStringLiteral("Added station '%1' at node '%2'. Undo removes it.").arg(name, nodeText), 8000);
	}

	static constexpr int kStationColumn = 1;
	static constexpr int kTrackColumn = 2;

	void fillRow(QTableWidget& table, int row, const SceneNode& node) const override {
		setCell(table, row, 0, QString::fromStdString(node.id));
		setCell(table, row, kTrackColumn, QString::fromStdString(node.trackId));
		setCell(table, row, 3, numberText(node.xKm));
		setCell(table, row, 4, numberText(node.yKm));
	}
	Edit editField(SceneNode& node, int column, const QString& value) const override {
		if (column == kTrackColumn)
			return setText(value, node.trackId);
		if (column == 3)
			return setNumber(value, node.xKm);
		if (column == 4)
			return setNumber(value, node.yKm);
		return Edit::Unchanged;
	}
	QString refusal(const SceneModel& model, int column, const QString& value) const override {
		if (column != kTrackColumn || withId(model.tracks, value.toStdString()))
			return {};
		return missingIdsText(QStringLiteral("Track"), {value});
	}
	// A node goes on the first track, one kilometre after the last node on it.
	SceneNode newItem(const SceneModel& model, const std::string& id) const override {
		const std::string trackId = model.tracks.empty() ? std::string() : model.tracks.front().id;
		double x = 0.0;
		for (const SceneNode& node : model.nodes)
			if (node.trackId == trackId && std::isfinite(node.xKm))
				x = std::max(x, node.xKm + 1.0);
		return {id, trackId, x, 0.0};
	}
	void rewriteReferences(SceneModel& model, const std::string& oldId, const std::string& newId) const override {
		sceneRewriteNodeReferences(model, oldId, newId);
	}
};

class ArcsFacet final : public ListFacet<SceneArc> {
public:
	ArcsFacet()
		: ListFacet("arcs", "arc", &SceneModel::arcs,
			  {"ID", "Track ID", "From node", "To node", "Curvature radius (m)", "Gradient (rise/length)", "Speed limit (m/s)"}) {}
	Capabilities capabilities() const override { return TrackRowFilter; }

	bool rowOnTrack(const InfrastructurePanel& panel, int row, const std::string& trackId) const override {
		const SceneArc* arc = inRow(panel.model().arcs, row);
		return arc && arc->trackId == trackId;
	}
	// An end is one of the nodes on the track of the arc, named with its position, such as "B0.node.41 (2.520 km)".
	std::vector<Choice> choices(const InfrastructurePanel& panel, int row, int column) const override {
		std::vector<Choice> result;
		const SceneArc* arc = inRow(panel.model().arcs, row);
		if (!arc || (column != 2 && column != 3))
			return result;
		for (const SceneNode& node : panel.model().nodes) {
			if (node.trackId != arc->trackId)
				continue;
			const QString id = QString::fromStdString(node.id);
			result.push_back({QStringLiteral("%1 (%2 km)").arg(id, QString::number(node.xKm, 'f', 3)), id});
		}
		return result;
	}

private:
	void fillRow(QTableWidget& table, int row, const SceneArc& arc) const override {
		setCell(table, row, 0, QString::fromStdString(arc.id));
		setCell(table, row, 1, QString::fromStdString(arc.trackId));
		setCell(table, row, 2, QString::fromStdString(arc.fromNodeId));
		setCell(table, row, 3, QString::fromStdString(arc.toNodeId));
		setCell(table, row, 4, numberText(arc.curvatureRadiusM));
		setCell(table, row, 5, numberText(arc.gradientPercent));
		setCell(table, row, 6, numberText(arc.speedLimitMs));
	}
	Edit editField(SceneArc& arc, int column, const QString& value) const override {
		if (column == 1)
			return setText(value, arc.trackId);
		if (column == 2)
			return setText(value, arc.fromNodeId);
		if (column == 3)
			return setText(value, arc.toNodeId);
		if (column == 4)
			return setNumber(value, arc.curvatureRadiusM);
		if (column == 5)
			return setNumber(value, arc.gradientPercent);
		if (column == 6)
			return setNumber(value, arc.speedLimitMs);
		return Edit::Unchanged;
	}
	QString refusal(const SceneModel& model, int column, const QString& value) const override {
		if ((column != 2 && column != 3) || withId(model.nodes, value.toStdString()))
			return {};
		return missingIdsText(QStringLiteral("Node"), {value});
	}
	// An arc goes on the first track, from the node after the last node that the arcs of the track end at to the next one.
	SceneArc newItem(const SceneModel& model, const std::string& id) const override {
		const std::string trackId = model.tracks.empty() ? std::string() : model.tracks.front().id;
		std::vector<std::string> nodeIds;
		for (const SceneNode& node : model.nodes)
			if (node.trackId == trackId)
				nodeIds.push_back(node.id);
		std::string from;
		std::string to;
		if (nodeIds.size() >= 2) {
			std::size_t index = 0;
			for (const SceneArc& arc : model.arcs)
				if (arc.trackId == trackId && index + 1 < nodeIds.size())
					++index;
			from = nodeIds[index];
			to = nodeIds[std::min(index + 1, nodeIds.size() - 1)];
		}
		return {id, trackId, from, to, 0.0, 0.0, 20.0};
	}
};

class ConnectionsFacet final : public ListFacet<SceneConnection> {
public:
	ConnectionsFacet()
		: ListFacet("connections", "connection", &SceneModel::connections,
			  {"ID", "From node", "To node", "Has speed limit", "Speed limit (m/s)"}) {}
	Capabilities capabilities() const override { return TrackRowFilter; }

	// A connection lies on the tracks of both its ends.
	bool rowOnTrack(const InfrastructurePanel& panel, int row, const std::string& trackId) const override {
		const SceneConnection* connection = inRow(panel.model().connections, row);
		if (!connection)
			return false;
		for (const std::string* end : {&connection->fromNodeId, &connection->toNodeId}) {
			const SceneNode* node = withId(panel.model().nodes, *end);
			if (node && node->trackId == trackId)
				return true;
		}
		return false;
	}
	// An end is any node, named with its track, such as "B0.node.41 (B0)".
	std::vector<Choice> choices(const InfrastructurePanel& panel, int, int column) const override {
		std::vector<Choice> result;
		if (column != 1 && column != 2)
			return result;
		for (const SceneNode& node : panel.model().nodes) {
			const QString id = QString::fromStdString(node.id);
			result.push_back({QStringLiteral("%1 (%2)").arg(id, QString::fromStdString(node.trackId)), id});
		}
		return result;
	}

private:
	void fillRow(QTableWidget& table, int row, const SceneConnection& connection) const override {
		setCell(table, row, 0, QString::fromStdString(connection.id));
		setCell(table, row, 1, QString::fromStdString(connection.fromNodeId));
		setCell(table, row, 2, QString::fromStdString(connection.toNodeId));
		setFlagCell(table, row, 3, connection.hasSpeedLimit);
		setCell(table, row, 4, numberText(connection.speedLimitMs));
	}
	Edit editField(SceneConnection& connection, int column, const QString& value) const override {
		if (column == 1)
			return setText(value, connection.fromNodeId);
		if (column == 2)
			return setText(value, connection.toNodeId);
		if (column == 3)
			return setFlag(value, connection.hasSpeedLimit);
		if (column == 4)
			return setNumber(value, connection.speedLimitMs);
		return Edit::Unchanged;
	}
	QString refusal(const SceneModel& model, int column, const QString& value) const override {
		if ((column != 1 && column != 2) || withId(model.nodes, value.toStdString()))
			return {};
		return missingIdsText(QStringLiteral("Node"), {value});
	}
	// A connection joins the first two nodes.
	SceneConnection newItem(const SceneModel& model, const std::string& id) const override {
		std::string from;
		std::string to;
		if (model.nodes.size() >= 2) {
			from = model.nodes[0].id;
			to = model.nodes[1].id;
		}
		return {id, from, to, false, 0.0};
	}
};

} // namespace

std::vector<std::unique_ptr<InfrastructureFacet>> makeTopologyFacets() {
	std::vector<std::unique_ptr<InfrastructureFacet>> facets;
	facets.push_back(std::make_unique<TracksFacet>());
	facets.push_back(std::make_unique<NodesFacet>());
	facets.push_back(std::make_unique<ArcsFacet>());
	facets.push_back(std::make_unique<ConnectionsFacet>());
	return facets;
}
