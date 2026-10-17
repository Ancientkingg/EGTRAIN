#include "editors/infrastructure/TopologyFacets.h"

#include "editors/infrastructure/ListFacet.h"
#include "scene/SceneReferences.h"

#include <QString>
#include <QTableWidget>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace {

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

class NodesFacet final : public ListFacet<SceneNode> {
public:
	NodesFacet() : ListFacet("nodes", "node", &SceneModel::nodes, {"ID", "Track ID", "X (km)", "Y (km)"}) {}

private:
	void fillRow(QTableWidget& table, int row, const SceneNode& node) const override {
		setCell(table, row, 0, QString::fromStdString(node.id));
		setCell(table, row, 1, QString::fromStdString(node.trackId));
		setCell(table, row, 2, numberText(node.xKm));
		setCell(table, row, 3, numberText(node.yKm));
	}
	Edit editField(SceneNode& node, int column, const QString& value) const override {
		if (column == 1)
			return setText(value, node.trackId);
		if (column == 2)
			return setNumber(value, node.xKm);
		if (column == 3)
			return setNumber(value, node.yKm);
		return Edit::Unchanged;
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

private:
	void fillRow(QTableWidget& table, int row, const SceneConnection& connection) const override {
		setCell(table, row, 0, QString::fromStdString(connection.id));
		setCell(table, row, 1, QString::fromStdString(connection.fromNodeId));
		setCell(table, row, 2, QString::fromStdString(connection.toNodeId));
		setCell(table, row, 3, flagText(connection.hasSpeedLimit));
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
