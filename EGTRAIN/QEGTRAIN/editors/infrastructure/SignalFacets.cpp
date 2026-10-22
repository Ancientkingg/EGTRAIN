#include "editors/infrastructure/SignalFacets.h"

#include "editors/infrastructure/ListFacet.h"
#include "scene/SceneIds.h"
#include "scene/SceneReferences.h"
#include "scene/SectionInventory.h"
#include "scene/SignallingLevel.h"
#include "scene/SignallingLevelNames.h"

#include <QComboBox>
#include <QObject>
#include <QSignalBlocker>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QWheelEvent>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace {

// A combo box that ignores the mouse wheel, so the wheel scrolls the table behind it.
class NoWheelComboBox : public QComboBox {
public:
	using QComboBox::QComboBox;

protected:
	void wheelEvent(QWheelEvent* event) override { event->ignore(); }
};

constexpr int kSystemColumn = 3;
constexpr int kTrackColumn = 4;

bool sceneHasTrack(const SceneModel& scene, const std::string& trackId) {
	return std::any_of(scene.tracks.begin(), scene.tracks.end(), [&trackId](const SceneTrack& track) { return track.id == trackId; });
}

// The signalling systems in level order. A stored level that is no system gets one item on top that stands for it.
void populateSignallingSystemCombo(QComboBox* combo, int current) {
	if (!combo)
		return;
	combo->clear();
	if (!isValidSignallingLevel(current)) {
		const bool unchosen = current == kSignallingLevelUnset;
		combo->addItem(unchosen ? QStringLiteral("Choose a signalling system") : QString::fromStdString(signallingLevelName(current)), current);
		combo->setItemData(0,
			unchosen ? QStringLiteral("Not chosen yet")
					 : QString::fromStdString(signallingLevelDescription(current)),
			Qt::ToolTipRole);
	}
	for (int level = levelValue(SignallingLevel::Atb); level <= levelValue(SignallingLevel::Bacc); ++level) {
		combo->addItem(QString::fromStdString(signallingLevelName(level)), level);
		combo->setItemData(combo->count() - 1, QString::fromStdString(signallingLevelDescription(level)), Qt::ToolTipRole);
	}
	combo->setCurrentIndex(combo->findData(current));
}

// All tracks, then each track of the scene with the extent of its blocks. A track that is no track of the scene gets one
// item on top.
void populateSignallingAreaTrackCombo(QComboBox* combo, const SceneModel& scene, const SceneSectionInventory& inventory,
	const std::string& current) {
	if (!combo)
		return;
	combo->clear();
	if (!current.empty() && !sceneHasTrack(scene, current))
		combo->addItem(QStringLiteral("%1 (missing)").arg(QString::fromStdString(current)), QString::fromStdString(current));
	combo->addItem(QStringLiteral("(all tracks)"), QString());
	for (const SceneTrack& track : scene.tracks) {
		const QString id = QString::fromStdString(track.id);
		const SceneBlockExtent extent = sceneBlockExtent(inventory, track.id);
		combo->addItem(extent.found ? QStringLiteral("%1 (blocks %2 to %3 km)")
										  .arg(id, QString::fromStdString(formatSceneSectionCoordinate(extent.startKm)),
											  QString::fromStdString(formatSceneSectionCoordinate(extent.endKm)))
									: QStringLiteral("%1 (no blocks)").arg(id),
			id);
	}
	combo->setCurrentIndex(combo->findData(QString::fromStdString(current)));
}

// The signals are not a list of the model that a ListFacet can name, because Qt makes `signals` a keyword.
class SignalsFacet final : public InfrastructureFacet {
public:
	QString key() const override { return QStringLiteral("signals"); }
	QStringList columns() const override { return {"ID", "Protected section"}; }

	void fillRows(InfrastructurePanel& panel) override {
		const std::vector<SceneSignal>& items = sceneSignals(panel.model());
		QTableWidget& table = *panel.table();
		table.setRowCount(static_cast<int>(items.size()));
		const SceneSectionInventory inventory = buildSceneSectionInventory(panel.model());
		for (int row = 0; row < table.rowCount(); ++row) {
			const SceneSignal& item = items[static_cast<std::size_t>(row)];
			setCell(table, row, 0, QString::fromStdString(item.id));
			auto* combo = new QComboBox(&table);
			combo->setObjectName(QStringLiteral("protectedSectionCombo"));
			combo->setAccessibleName(QStringLiteral("Protected section for %1").arg(QString::fromStdString(item.id)));
			combo->setToolTip(QStringLiteral("Choose the block or switch section protected by this signal"));
			{
				const QSignalBlocker blocker(combo);
				InfrastructurePanel::populateSectionCombo(combo, inventory, item.protectedSection, true, QStringLiteral("(unbound)"));
			}
			QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), &panel, [&panel, row, combo](int) {
				std::vector<SceneSignal>& current = sceneSignals(panel.mutableModel());
				if (row < 0 || row >= static_cast<int>(current.size()))
					return;
				const std::string section = combo->currentData().toString().toStdString();
				SceneSignal& edited = current[static_cast<std::size_t>(row)];
				if (edited.protectedSection == section)
					return;
				edited.protectedSection = section;
				panel.entityEdited(QStringLiteral("Edit signal"));
			});
			table.setCellWidget(row, 1, combo);
		}
	}

	void commitCell(InfrastructurePanel& panel, int row, int column) override {
		std::vector<SceneSignal>& items = sceneSignals(panel.mutableModel());
		if (column != 0 || row >= static_cast<int>(items.size()))
			return;
		SceneSignal& item = items[static_cast<std::size_t>(row)];
		const std::string newId = panel.table()->item(row, column)->text().trimmed().toStdString();
		if (item.id == newId)
			return;
		if (!idCanReplaceRow(items, row, newId)) {
			panel.refuseEdit(QStringLiteral("IDs must be unique and not empty"));
			return;
		}
		const std::string oldId = item.id;
		item.id = newId;
		sceneRewriteSignalReferences(panel.mutableModel(), oldId, newId);
		panel.setSelectionId(QString::fromStdString(newId));
		panel.entityChanged(QStringLiteral("Rename signal"));
	}

	void add(InfrastructurePanel& panel) override {
		SceneModel& model = panel.mutableModel();
		const std::string id = sceneUniqueInfrastructureId(model, "signal", "signals");
		sceneSignals(model).push_back({id, {}});
		panel.setSelectionId(QString::fromStdString(id));
		panel.entityChanged(QStringLiteral("Add signal"));
	}

	void remove(InfrastructurePanel& panel) override {
		std::vector<SceneSignal>& items = sceneSignals(panel.mutableModel());
		const int row = panel.table()->currentRow();
		if (row < 0 || row >= static_cast<int>(items.size()))
			return;
		const std::string id = items[static_cast<std::size_t>(row)].id;
		if (id.empty())
			return;
		if (!panel.confirmDelete(QString::fromStdString(id), sceneDirectDeleteConsumers(panel.model(), "signals", id)))
			return;
		items.erase(items.begin() + row);
		panel.setSelectionId(QString());
		panel.entityChanged(QStringLiteral("Delete signal"));
	}
};

class SignallingAreasFacet final : public ListFacet<SceneSignallingArea> {
public:
	SignallingAreasFacet()
		: ListFacet("signalling_areas", "signalling area", &SceneModel::signallingAreas,
			  {"ID", "Start km", "End km", "Signalling system", "Track"}) {}

	// The system and the track are combos, which the table keeps while the other cells are edited.
	void fillRows(InfrastructurePanel& panel) override {
		ListFacet::fillRows(panel);
		const std::vector<SceneSignallingArea>& areas = panel.model().signallingAreas;
		const SceneSectionInventory inventory = buildSceneSectionInventory(panel.model());
		for (int row = 0; row < static_cast<int>(areas.size()); ++row) {
			const SceneSignallingArea& area = areas[static_cast<std::size_t>(row)];
			const QString id = QString::fromStdString(area.id);
			panel.table()->setCellWidget(row, kSystemColumn, makeSystemCombo(panel, row, id, area.level));
			panel.table()->setCellWidget(row, kTrackColumn, makeTrackCombo(panel, inventory, row, id, area.trackId));
		}
	}

private:
	void fillRow(QTableWidget& table, int row, const SceneSignallingArea& area) const override {
		setCell(table, row, 0, QString::fromStdString(area.id));
		setCell(table, row, 1, numberText(area.startKm));
		setCell(table, row, 2, numberText(area.endKm));
	}
	Edit editField(SceneSignallingArea& area, int column, const QString& value) const override {
		if (column == 1)
			return setNumber(value, area.startKm);
		if (column == 2)
			return setNumber(value, area.endKm);
		return Edit::Unchanged;
	}
	// A new area covers the blocks of the network and has no system yet.
	SceneSignallingArea newItem(const SceneModel& model, const std::string& id) const override {
		const SceneBlockExtent extent = sceneBlockExtent(buildSceneSectionInventory(model), "");
		return {id, extent.startKm, extent.endKm, kSignallingLevelUnset, {}};
	}
	std::string idBase() const override { return "signalling-area"; }

	static QComboBox* makeSystemCombo(InfrastructurePanel& panel, int row, const QString& areaId, int level) {
		auto* combo = new NoWheelComboBox(panel.table());
		combo->setObjectName(QStringLiteral("signallingAreaSystemCombo"));
		combo->setAccessibleName(QStringLiteral("Signalling system for %1").arg(areaId));
		combo->setToolTip(QStringLiteral("Choose the signalling system of the sections inside this area"));
		combo->setFocusPolicy(Qt::StrongFocus);
		populateSignallingSystemCombo(combo, level);
		QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), &panel, [&panel, row, combo](int) {
			std::vector<SceneSignallingArea>& areas = panel.mutableModel().signallingAreas;
			if (row < 0 || row >= static_cast<int>(areas.size()))
				return;
			const int chosen = combo->currentData().toInt();
			SceneSignallingArea& edited = areas[static_cast<std::size_t>(row)];
			if (edited.level == chosen)
				return;
			edited.level = chosen;
			// The item that stood for a level that is no system is no longer needed.
			{
				const QSignalBlocker blocker(combo);
				for (int index = combo->count() - 1; index >= 0; --index)
					if (!isValidSignallingLevel(combo->itemData(index).toInt()))
						combo->removeItem(index);
				combo->setCurrentIndex(combo->findData(chosen));
			}
			panel.entityEdited(QStringLiteral("Edit signalling area"));
		});
		return combo;
	}

	static QComboBox* makeTrackCombo(InfrastructurePanel& panel, const SceneSectionInventory& inventory, int row, const QString& areaId,
		const std::string& trackId) {
		auto* combo = new NoWheelComboBox(panel.table());
		combo->setObjectName(QStringLiteral("signallingAreaTrackCombo"));
		combo->setAccessibleName(QStringLiteral("Track for %1").arg(areaId));
		combo->setToolTip(QStringLiteral("Choose the track this area applies to, or all tracks"));
		combo->setFocusPolicy(Qt::StrongFocus);
		populateSignallingAreaTrackCombo(combo, panel.model(), inventory, trackId);
		QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), &panel, [&panel, row, combo](int) {
			std::vector<SceneSignallingArea>& areas = panel.mutableModel().signallingAreas;
			if (row < 0 || row >= static_cast<int>(areas.size()))
				return;
			const std::string chosen = combo->currentData().toString().toStdString();
			SceneSignallingArea& edited = areas[static_cast<std::size_t>(row)];
			if (edited.trackId == chosen)
				return;
			edited.trackId = chosen;
			// The item that stood for a track that is no track of the scene is no longer needed.
			{
				const QSignalBlocker blocker(combo);
				for (int index = combo->count() - 1; index >= 0; --index) {
					const std::string itemTrack = combo->itemData(index).toString().toStdString();
					if (!itemTrack.empty() && !sceneHasTrack(panel.model(), itemTrack))
						combo->removeItem(index);
				}
				combo->setCurrentIndex(combo->findData(QString::fromStdString(chosen)));
			}
			panel.entityEdited(QStringLiteral("Edit signalling area"));
		});
		return combo;
	}
};

} // namespace

std::vector<std::unique_ptr<InfrastructureFacet>> makeSignalFacets() {
	std::vector<std::unique_ptr<InfrastructureFacet>> facets;
	facets.push_back(std::make_unique<SignalsFacet>());
	facets.push_back(std::make_unique<SignallingAreasFacet>());
	return facets;
}
