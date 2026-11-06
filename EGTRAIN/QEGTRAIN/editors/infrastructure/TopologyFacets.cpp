#include "editors/infrastructure/TopologyFacets.h"

#include "editors/infrastructure/ListFacet.h"
#include "graphics/NetworkPresenter.h"
#include "scene/SceneReferences.h"

#include <QString>
#include <QTableWidget>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <string>
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

private:
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
