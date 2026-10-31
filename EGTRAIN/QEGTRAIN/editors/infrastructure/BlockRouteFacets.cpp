#include "editors/infrastructure/BlockRouteFacets.h"

#include "editors/CountText.h"
#include "editors/infrastructure/ListFacet.h"
#include "editors/infrastructure/SectionRowsFacet.h"
#include "scene/SceneIds.h"
#include "scene/SceneReferences.h"
#include "scene/SectionInventory.h"

#include <QComboBox>
#include <QListWidget>
#include <QSignalBlocker>
#include <QString>
#include <QStringList>
#include <QTableWidget>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace {

// The blocks on the track that the filter of the panel names. The cells of a block come with the extent of its section.
class BlocksFacet final : public InfrastructureFacet {
public:
	QString key() const override { return QStringLiteral("blocks"); }
	Capabilities capabilities() const override { return TrackFilter | InsertAndMove; }
	QStringList columns() const override {
		return {"ID", "Track ID", "Length (km)", "Order", "Start km", "End km", "Coverage"};
	}

	void fillRows(InfrastructurePanel& panel) override {
		const SceneModel& model = panel.model();
		QTableWidget& table = *panel.table();
		const QString selectedTrack = panel.blockTrackFilterCombo()->currentData().toString();
		const SceneSectionInventory inventory = buildSceneSectionInventory(model);
		table.setRowCount(0);
		for (int modelIndex = 0; modelIndex < static_cast<int>(model.blocks.size()); ++modelIndex) {
			const SceneBlock& block = model.blocks[static_cast<std::size_t>(modelIndex)];
			if (QString::fromStdString(block.trackId) != selectedTrack)
				continue;
			const int row = table.rowCount();
			table.insertRow(row);
			panel.blockRowModelIndices().push_back(modelIndex);
			setCell(table, row, 0, QString::fromStdString(block.id));
			setCell(table, row, 1, QString::fromStdString(block.trackId));
			setCell(table, row, 2, numberText(block.lengthKm));
			setCell(table, row, 3, QString::number(row + 1), false);
			const auto section = std::find_if(inventory.sections.begin(), inventory.sections.end(),
				[&block](const SceneSectionDescriptor& candidate) { return candidate.sourceBlockId == block.id; });
			if (section == inventory.sections.end()) {
				for (int column = 4; column <= 6; ++column)
					setCell(table, row, column, QStringLiteral("unavailable"), false);
				continue;
			}
			setCell(table, row, 4, numberText(section->startKm), false);
			setCell(table, row, 5, numberText(section->endKm), false);
			QStringList coverage;
			if (section->trackCoverageGap)
				coverage << QStringLiteral("gap");
			if (section->layoutOverflow)
				coverage << QStringLiteral("overflow");
			if (section->clippedToTrackEnd)
				coverage << QStringLiteral("clipped");
			if (coverage.isEmpty())
				coverage << QStringLiteral("complete");
			setCell(table, row, 6, coverage.join(QStringLiteral(", ")), false);
		}
	}

	void commitCell(InfrastructurePanel& panel, int row, int column) override {
		const int index = modelIndex(panel, row);
		if (index < 0)
			return;
		const std::vector<SceneBlock>& blocks = panel.model().blocks;
		const std::size_t place = static_cast<std::size_t>(index);
		const QString value = panel.table()->item(row, column)->text().trimmed();
		if (column == 0) {
			const std::string oldId = blocks[place].id;
			const std::string newId = value.toStdString();
			if (oldId == newId)
				return;
			if (!idCanReplaceRow(blocks, index, newId)) {
				panel.refuseEdit(QStringLiteral("IDs must be unique and not empty"));
				return;
			}
			panel.setSelectionId(value);
			panel.edit(QStringLiteral("Rename block"), [place, &oldId, &newId](SceneModel& model) {
				model.blocks[place].id = newId;
				sceneRewriteBlockReferences(model, oldId, newId);
			});
			return;
		}
		SceneBlock edited = blocks[place];
		Edit edit = Edit::Unchanged;
		if (column == 1)
			edit = setText(value, edited.trackId);
		else if (column == 2)
			edit = setNumber(value, edited.lengthKm);
		if (edit == Edit::Refused) {
			panel.refuseEdit();
			return;
		}
		if (edit != Edit::Changed)
			return;
		const auto apply = [place, &edited](SceneModel& model) { model.blocks[place] = edited; };
		if (column == 2) {
			panel.edit(QStringLiteral("Edit block"), apply);
			return;
		}
		// The block moves to another track, and the filter shows that track from now on.
		if (!panel.editInPlace(QStringLiteral("Edit block"), apply))
			return;
		panel.refreshBlockTrackFilter();
		{
			const QSignalBlocker blocker(panel.blockTrackFilterCombo());
			panel.blockTrackFilterCombo()->setCurrentIndex(panel.blockTrackFilterCombo()->findData(value));
		}
		panel.refreshTable();
	}

	// A block goes at the end of the model, on the track that the filter names, which must be a track of the scene.
	void add(InfrastructurePanel& panel) override {
		const SceneModel& model = panel.model();
		const std::string selectedTrack = panel.blockTrackFilterCombo()->currentData().toString().toStdString();
		const auto track = std::find_if(model.tracks.begin(), model.tracks.end(),
			[&selectedTrack](const SceneTrack& candidate) { return candidate.id == selectedTrack; });
		if (selectedTrack.empty() || track == model.tracks.end()) {
			panel.showStatus(QStringLiteral("Select a valid track before adding a block"));
			return;
		}
		const std::string id = sceneUniqueInfrastructureId(model, "block", "blocks");
		const SceneBlock block{id, track->id, 1.0};
		panel.setSelectionId(QString::fromStdString(id));
		panel.edit(QStringLiteral("Add block"), [&block](SceneModel& edited) { edited.blocks.push_back(block); });
	}

	void remove(InfrastructurePanel& panel) override {
		const int index = modelIndex(panel, panel.table()->currentRow());
		if (index < 0)
			return;
		const std::string id = panel.model().blocks[static_cast<std::size_t>(index)].id;
		if (id.empty())
			return;
		if (!panel.confirmDelete(QString::fromStdString(id), sceneDirectDeleteConsumers(panel.model(), "blocks", id)))
			return;
		panel.setSelectionId(QString());
		panel.edit(QStringLiteral("Delete block"), [index](SceneModel& model) { model.blocks.erase(model.blocks.begin() + index); });
	}

	// The new block takes the place of the selected one, on its track.
	void insert(InfrastructurePanel& panel) override {
		const int index = modelIndex(panel, panel.table()->currentRow());
		if (index < 0)
			return;
		const SceneModel& model = panel.model();
		const SceneBlock block{sceneUniqueInfrastructureId(model, "block", "blocks"), model.blocks[static_cast<std::size_t>(index)].trackId, 1.0};
		panel.setSelectionId(QString::fromStdString(block.id));
		panel.edit(QStringLiteral("Insert block"),
			[index, &block](SceneModel& edited) { edited.blocks.insert(edited.blocks.begin() + index, block); });
	}

	// The selected block swaps its place with its neighbour on the track.
	void move(InfrastructurePanel& panel, int offset) override {
		const int row = panel.table()->currentRow();
		const int index = modelIndex(panel, row);
		const int neighbor = modelIndex(panel, row + offset);
		if (index < 0 || neighbor < 0)
			return;
		const std::size_t from = static_cast<std::size_t>(index);
		const std::size_t to = static_cast<std::size_t>(neighbor);
		panel.setSelectionId(QString::fromStdString(panel.model().blocks[from].id));
		panel.edit(QStringLiteral("Move block"), [from, to](SceneModel& model) { std::swap(model.blocks[from], model.blocks[to]); });
	}

private:
	// The place in the model of the block in `row` of the table, or -1 when the row names none.
	static int modelIndex(InfrastructurePanel& panel, int row) {
		const std::vector<int>& rows = panel.blockRowModelIndices();
		if (row < 0 || row >= static_cast<int>(rows.size()))
			return -1;
		const int index = rows[static_cast<std::size_t>(row)];
		return index >= 0 && index < static_cast<int>(panel.model().blocks.size()) ? index : -1;
	}
};

class RoutesFacet final : public ListFacet<SceneRoute> {
public:
	RoutesFacet() : ListFacet("routes", "route", &SceneModel::routes, {"ID", "Sections", "Has corridor", "Corridor", "Reversed"}) {}
	Capabilities capabilities() const override { return RouteSections; }

	void addSection(InfrastructurePanel& panel) override {
		const int routeRow = selectedRoute(panel);
		const QComboBox* catalog = panel.routeSectionCatalogCombo();
		if (routeRow < 0 || catalog->currentIndex() < 0)
			return;
		const std::string section = catalog->currentData().toString().toStdString();
		if (section.empty())
			return;
		if (changeSections(panel, routeRow, QStringLiteral("Add route section"),
				[&section](std::vector<std::string>& sections) { sections.push_back(section); }))
			panel.routeSectionListWidget()->setCurrentRow(panel.routeSectionListWidget()->count() - 1);
	}

	void removeSection(InfrastructurePanel& panel) override {
		const int routeRow = selectedRoute(panel);
		const int sectionRow = panel.routeSectionListWidget()->currentRow();
		if (routeRow < 0 || sectionRow < 0 || sectionRow >= sectionCount(panel, routeRow))
			return;
		if (changeSections(panel, routeRow, QStringLiteral("Remove route section"),
				[sectionRow](std::vector<std::string>& sections) { sections.erase(sections.begin() + sectionRow); }))
			panel.routeSectionListWidget()->setCurrentRow(std::min(sectionRow, panel.routeSectionListWidget()->count() - 1));
	}

	void moveSection(InfrastructurePanel& panel, int offset) override {
		const int routeRow = selectedRoute(panel);
		const int sectionRow = panel.routeSectionListWidget()->currentRow();
		const int targetRow = sectionRow + offset;
		const int count = routeRow < 0 ? 0 : sectionCount(panel, routeRow);
		if (routeRow < 0 || sectionRow < 0 || sectionRow >= count || targetRow < 0 || targetRow >= count)
			return;
		if (changeSections(panel, routeRow, QStringLiteral("Move route section"), [sectionRow, targetRow](std::vector<std::string>& sections) {
				std::swap(sections[static_cast<std::size_t>(sectionRow)], sections[static_cast<std::size_t>(targetRow)]);
			}))
			panel.routeSectionListWidget()->setCurrentRow(targetRow);
	}

private:
	void fillRow(QTableWidget& table, int row, const SceneRoute& route) const override {
		setCell(table, row, 0, QString::fromStdString(route.id));
		setCell(table, row, 1,
			route.blocks.empty()
				? QStringLiteral("-")
				: countText(static_cast<long long>(route.blocks.size()), QStringLiteral("section"), QStringLiteral("sections")),
			false);
		setCell(table, row, 2, flagText(route.hasCorridor));
		setCell(table, row, 3, QString::fromStdString(route.corridor));
		setCell(table, row, 4, flagText(route.reversed));
	}
	Edit editField(SceneRoute& route, int column, const QString& value) const override {
		if (column == 2)
			return setFlag(value, route.hasCorridor);
		if (column == 3)
			return setText(value, route.corridor);
		if (column == 4)
			return setFlag(value, route.reversed);
		return Edit::Unchanged;
	}
	SceneRoute newItem(const SceneModel&, const std::string& id) const override { return {id, {}, false, {}, false}; }
	void rewriteReferences(SceneModel& model, const std::string& oldId, const std::string& newId) const override {
		sceneRewriteRouteReferences(model, oldId, newId);
	}

	// The selected row when it is a route, or -1.
	static int selectedRoute(const InfrastructurePanel& panel) {
		const int row = panel.table()->currentRow();
		return row >= 0 && row < static_cast<int>(panel.model().routes.size()) ? row : -1;
	}

	// The number of sections of the route in `routeRow`.
	static int sectionCount(const InfrastructurePanel& panel, int routeRow) {
		return static_cast<int>(panel.model().routes[static_cast<std::size_t>(routeRow)].blocks.size());
	}

	// Changes the sections of the route in `routeRow` with `change`. The table lists the routes again, which fills the list
	// of sections. False when the scene is not open for edits.
	static bool changeSections(InfrastructurePanel& panel, int routeRow, const QString& label,
		const std::function<void(std::vector<std::string>&)>& change) {
		const std::size_t index = static_cast<std::size_t>(routeRow);
		panel.setSelectionId(QString::fromStdString(panel.model().routes[index].id));
		return panel.edit(label, [index, &change](SceneModel& model) { change(model.routes[index].blocks); });
	}
};

class BlockDependenciesFacet final : public SectionRowsFacet<SceneBlockDependency> {
public:
	BlockDependenciesFacet()
		: SectionRowsFacet("block_dependencies", "block dependency", &SceneModel::blockDependencies, {"Block", "Depends on"}) {}

private:
	void fillRow(InfrastructurePanel& panel, const SceneSectionInventory& inventory, int row,
		const SceneBlockDependency& dependency) const override {
		addSectionCombo(panel, inventory, row, 0, dependency.block, "dependencyBlockCombo", storeIn(&SceneBlockDependency::block))
			->setToolTip(QStringLiteral("The block that depends on another section"));
		addSectionCombo(panel, inventory, row, 1, dependency.dependsOn, "dependencyDependsOnCombo",
			storeIn(&SceneBlockDependency::dependsOn))
			->setToolTip(QStringLiteral("The section it depends on"));
	}
	std::string rowTitle(const SceneBlockDependency& dependency) const override { return dependency.block; }
};

class SingleTrackRestrictionsFacet final : public SectionRowsFacet<SceneSingleTrackRestriction> {
public:
	SingleTrackRestrictionsFacet()
		: SectionRowsFacet("single_track_restrictions", "single-track restriction", &SceneModel::singleTrackRestrictions,
			  {"Start", "End", "Protected start", "Protected end"}) {}

private:
	void fillRow(InfrastructurePanel& panel, const SceneSectionInventory& inventory, int row,
		const SceneSingleTrackRestriction& restriction) const override {
		addSectionCombo(panel, inventory, row, 0, restriction.startBlock, "restrictionStartCombo",
			storeIn(&SceneSingleTrackRestriction::startBlock));
		addSectionCombo(panel, inventory, row, 1, restriction.endBlock, "restrictionEndCombo",
			storeIn(&SceneSingleTrackRestriction::endBlock));
		addSectionCombo(panel, inventory, row, 2, restriction.protectedStartBlock, "restrictionProtectedStartCombo",
			storeIn(&SceneSingleTrackRestriction::protectedStartBlock));
		addSectionCombo(panel, inventory, row, 3, restriction.protectedEndBlock, "restrictionProtectedEndCombo",
			storeIn(&SceneSingleTrackRestriction::protectedEndBlock));
	}
	std::string rowTitle(const SceneSingleTrackRestriction& restriction) const override { return restriction.startBlock; }
};

} // namespace

std::vector<std::unique_ptr<InfrastructureFacet>> makeBlockRouteFacets() {
	std::vector<std::unique_ptr<InfrastructureFacet>> facets;
	facets.push_back(std::make_unique<BlocksFacet>());
	facets.push_back(std::make_unique<RoutesFacet>());
	facets.push_back(std::make_unique<BlockDependenciesFacet>());
	facets.push_back(std::make_unique<SingleTrackRestrictionsFacet>());
	return facets;
}
