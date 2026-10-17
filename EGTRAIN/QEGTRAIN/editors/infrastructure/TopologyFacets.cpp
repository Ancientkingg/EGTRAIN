#include "editors/infrastructure/TopologyFacets.h"

#include "editors/InfrastructurePanel.h"
#include "scene/SceneIds.h"
#include "scene/SceneReferences.h"

#include <QString>
#include <QTableWidget>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace {

// Enough digits for the text of a number to give the number back.
constexpr int kNumberDigits = std::numeric_limits<double>::max_digits10;

QString numberText(double value) {
	return QString::number(value, 'g', kNumberDigits);
}

QString flagText(bool value) {
	return value ? QStringLiteral("true") : QStringLiteral("false");
}

// What came of the text of an edited cell.
enum class Edit {
	Unchanged,
	Changed,
	// The text is no value of the field. The table shows the model again.
	Refused,
	// The text is no id that the entity can have, because it is empty or another entity has it.
	UnsafeId,
};

Edit setText(const QString& text, std::string& target) {
	const std::string value = text.toStdString();
	if (target == value)
		return Edit::Unchanged;
	target = value;
	return Edit::Changed;
}

Edit setNumber(const QString& text, double& target) {
	bool parsed = false;
	const double number = text.toDouble(&parsed);
	if (!parsed || !std::isfinite(number))
		return Edit::Refused;
	if (target == number)
		return Edit::Unchanged;
	target = number;
	return Edit::Changed;
}

Edit setFlag(const QString& text, bool& target) {
	const QString lower = text.toLower();
	if (lower != QStringLiteral("true") && lower != QStringLiteral("false"))
		return Edit::Refused;
	const bool flag = lower == QStringLiteral("true");
	if (target == flag)
		return Edit::Unchanged;
	target = flag;
	return Edit::Changed;
}

// Whether the item in `row` can take `id`: it is not empty, and no other item has it.
template <typename Item>
bool idCanReplaceRow(const std::vector<Item>& items, int row, const std::string& id) {
	if (id.empty())
		return false;
	for (int index = 0; index < static_cast<int>(items.size()); ++index)
		if (index != row && items[static_cast<std::size_t>(index)].id == id)
			return false;
	return true;
}

// A facet over one list of the model, whose items have an id and are listed in the order of the list. The column
// of the id is the first.
template <typename Item>
class ListFacet : public InfrastructureFacet {
public:
	QString key() const override { return m_key; }
	QStringList columns() const override { return m_columns; }

	void fillRows(InfrastructurePanel& panel) override {
		const std::vector<Item>& items = panel.model().*m_list;
		QTableWidget& table = *panel.table();
		table.setRowCount(static_cast<int>(items.size()));
		for (int row = 0; row < table.rowCount(); ++row)
			fillRow(table, row, items[static_cast<std::size_t>(row)]);
	}

	void commitCell(InfrastructurePanel& panel, int row, int column) override {
		std::vector<Item>& items = panel.mutableModel().*m_list;
		if (row >= static_cast<int>(items.size()))
			return;
		const QString value = panel.table()->item(row, column)->text().trimmed();
		Item& item = items[static_cast<std::size_t>(row)];
		Edit edit = Edit::Unchanged;
		if (column == 0) {
			const std::string newId = value.toStdString();
			if (item.id != newId) {
				if (idCanReplaceRow(items, row, newId)) {
					const std::string oldId = item.id;
					item.id = newId;
					rewriteReferences(panel.mutableModel(), oldId, newId);
					panel.setSelectionId(value);
					edit = Edit::Changed;
				} else {
					edit = Edit::UnsafeId;
				}
			}
		} else {
			edit = editField(item, column, value);
		}
		if (edit == Edit::Changed)
			panel.entityChanged((column == 0 ? QStringLiteral("Rename %1") : QStringLiteral("Edit %1")).arg(m_noun));
		else if (edit == Edit::UnsafeId)
			panel.refuseEdit(QStringLiteral("Infrastructure IDs must be non-empty and unique"));
		else if (edit == Edit::Refused)
			panel.refuseEdit();
	}

	void add(InfrastructurePanel& panel) override {
		SceneModel& model = panel.mutableModel();
		const std::string id = sceneUniqueInfrastructureId(model, m_noun.toStdString(), m_key.toStdString());
		(model.*m_list).push_back(newItem(model, id));
		panel.setSelectionId(QString::fromStdString(id));
		panel.entityChanged(QStringLiteral("Add %1").arg(m_noun));
	}

	void remove(InfrastructurePanel& panel) override {
		std::vector<Item>& items = panel.mutableModel().*m_list;
		const int row = panel.table()->currentRow();
		if (row < 0 || row >= static_cast<int>(items.size()))
			return;
		const std::string id = items[static_cast<std::size_t>(row)].id;
		if (id.empty())
			return;
		if (!panel.confirmDelete(QString::fromStdString(id), sceneDirectDeleteConsumers(panel.model(), m_key.toStdString(), id)))
			return;
		items.erase(items.begin() + row);
		panel.setSelectionId(QString());
		panel.entityChanged(QStringLiteral("Delete %1").arg(m_noun));
	}

protected:
	// `comboKey` is the value of the combo, `noun` names one entity and is the base of the ids of new ones.
	ListFacet(const char* comboKey, const char* noun, std::vector<Item> SceneModel::* list, QStringList headers)
		: m_key(QString::fromLatin1(comboKey)), m_noun(QString::fromLatin1(noun)), m_list(list), m_columns(std::move(headers)) {}

	// Puts the cells of the entity in `row`.
	virtual void fillRow(QTableWidget& table, int row, const Item& item) const = 0;
	// Takes the text of a cell other than the id into the item.
	virtual Edit editField(Item& item, int column, const QString& value) const = 0;
	// A new item with this id, as the model is now.
	virtual Item newItem(const SceneModel& model, const std::string& id) const = 0;
	// What refers to the item that was renamed.
	virtual void rewriteReferences(SceneModel&, const std::string&, const std::string&) const {}

private:
	QString m_key;
	QString m_noun;
	std::vector<Item> SceneModel::* m_list;
	QStringList m_columns;
};

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
