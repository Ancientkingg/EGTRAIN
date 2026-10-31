#include "editors/infrastructure/StationFacets.h"

#include "editors/EditorHelpers.h"
#include "editors/infrastructure/ListFacet.h"
#include "editors/infrastructure/SectionRowsFacet.h"
#include "scene/SceneIds.h"
#include "scene/SceneReferences.h"
#include "widgets/CompactDoubleSpinBox.h"

#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QObject>
#include <QSignalBlocker>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

// Set on a number field of a platform while its text has been edited and not yet taken into the model.
constexpr const char kGeometryEditedProperty[] = "platformGeometryEdited";
constexpr int kLengthColumn = 3;
constexpr int kWidthColumn = 4;

// The id that names a platform for the selection: the platforms of different stations may share an id.
std::string platformSelectionKey(const std::string& stationId, const std::string& platformId) {
	return stationId + "\n" + platformId;
}

// The ids in a list that is written with commas. Empty items are dropped.
std::vector<std::string> splitCommaList(const QString& text) {
	std::vector<std::string> result;
	for (const QString& part : text.split(',', Qt::KeepEmptyParts)) {
		const QString trimmed = part.trimmed();
		if (!trimmed.isEmpty())
			result.push_back(trimmed.toStdString());
	}
	return result;
}

QString joinCommaList(const std::vector<std::string>& values) {
	QStringList result;
	for (const std::string& value : values)
		result << QString::fromStdString(value);
	return result.join(", ");
}

// A platform in the model: the index of its station and its place in the platforms of that station.
struct PlatformPlace {
	int station = -1;
	int platform = -1;
};

// The platform in `row` of the table, which lists the platforms of all stations one after the other. The station is -1
// when the row names none.
PlatformPlace platformPlace(const SceneModel& model, int row) {
	int first = 0;
	for (int station = 0; station < static_cast<int>(model.stations.size()); ++station) {
		const int count = static_cast<int>(model.stations[static_cast<std::size_t>(station)].platforms.size());
		if (row >= first && row < first + count)
			return {station, row - first};
		first += count;
	}
	return {};
}

// The length or the width in `column` of the platform in `row` is taken into the model.
void commitGeometry(InfrastructurePanel& panel, int row, int column, double value) {
	if (!panel.canEdit() || panel.facetKey() != QStringLiteral("platforms") || !std::isfinite(value)
		|| (column != kLengthColumn && column != kWidthColumn))
		return;
	const PlatformPlace place = platformPlace(panel.model(), row);
	if (place.station < 0)
		return;
	const std::size_t station = static_cast<std::size_t>(place.station);
	const std::size_t index = static_cast<std::size_t>(place.platform);
	const ScenePlatform& platform = panel.model().stations[station].platforms[index];
	const bool length = column == kLengthColumn;
	if (length ? platform.hasLength && platform.lengthM == value : platform.hasWidth && platform.widthM == value)
		return;
	panel.editInPlace(QStringLiteral("Edit platform"), [station, index, length, value](SceneModel& model) {
		ScenePlatform& edited = model.stations[station].platforms[index];
		if (length) {
			edited.hasLength = true;
			edited.lengthM = value;
		} else {
			edited.hasWidth = true;
			edited.widthM = value;
		}
	});
}

// A field for the length or the width of a platform. The model takes the value when the field is stepped or left, and
// not on every key.
QDoubleSpinBox* makeGeometryField(InfrastructurePanel& panel, int row, int column, double value, const char* objectName,
	const QString& toolTip) {
	auto* edit = new CompactDoubleSpinBox(panel.table());
	edit->setObjectName(objectName);
	edit->setAccessibleName(toolTip);
	edit->setToolTip(toolTip);
	edit->setRange(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
	edit->setDecimals(std::numeric_limits<double>::max_digits10);
	edit->setSingleStep(1.0);
	edit->setKeyboardTracking(false);
	{
		const QSignalBlocker blocker(edit);
		edit->setValue(value);
	}
	QObject::connect(edit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &panel,
		[&panel, row, column](double next) { commitGeometry(panel, row, column, next); });
	if (QLineEdit* textEdit = edit->findChild<QLineEdit*>()) {
		QObject::connect(textEdit, &QLineEdit::textEdited, edit,
			[edit] { edit->setProperty(kGeometryEditedProperty, edit->hasAcceptableInput()); });
	}
	QObject::connect(edit, &QAbstractSpinBox::editingFinished, &panel, [&panel, edit, row, column] {
		if (!edit->property(kGeometryEditedProperty).toBool())
			return;
		edit->setProperty(kGeometryEditedProperty, false);
		commitGeometry(panel, row, column, edit->value());
	});
	return edit;
}

class StationsFacet final : public ListFacet<SceneStation> {
public:
	StationsFacet() : ListFacet("stations", "station", &SceneModel::stations, {"ID", "Name", "Has position", "Position km"}) {}

private:
	void fillRow(QTableWidget& table, int row, const SceneStation& station) const override {
		setCell(table, row, 0, QString::fromStdString(station.id));
		setCell(table, row, 1, QString::fromStdString(station.name));
		setCell(table, row, 2, flagText(station.hasPosition));
		setCell(table, row, 3, numberText(station.positionKm));
	}
	Edit editField(SceneStation& station, int column, const QString& value) const override {
		if (column == 1)
			return setText(value, station.name);
		if (column == 2)
			return setFlag(value, station.hasPosition);
		if (column == 3)
			return setNumber(value, station.positionKm);
		return Edit::Unchanged;
	}
	// A station is named after its id.
	SceneStation newItem(const SceneModel&, const std::string& id) const override {
		SceneStation station;
		station.id = id;
		station.name = id;
		return station;
	}
	void rewriteReferences(SceneModel& model, const std::string& oldId, const std::string& newId) const override {
		sceneRewriteStationReferences(model, oldId, newId);
	}
};

class PlatformsFacet final : public InfrastructureFacet {
public:
	QString key() const override { return QStringLiteral("platforms"); }
	QStringList columns() const override { return {"Station ID", "ID", "Nodes", "Length (m)", "Width (m)"}; }

	void fillRows(InfrastructurePanel& panel) override {
		const SceneModel& model = panel.model();
		QTableWidget& table = *panel.table();
		int count = 0;
		for (const SceneStation& station : model.stations)
			count += static_cast<int>(station.platforms.size());
		table.setRowCount(count);
		int row = 0;
		for (const SceneStation& station : model.stations) {
			for (const ScenePlatform& platform : station.platforms) {
				setCell(table, row, 0, QString::fromStdString(station.id));
				setCell(table, row, 1, QString::fromStdString(platform.id));
				setCell(table, row, 2, joinCommaList(platform.nodeIds));
				table.setCellWidget(row, kLengthColumn,
					makeGeometryField(panel, row, kLengthColumn, platform.hasLength ? platform.lengthM : kDefaultPlatformLengthM,
						"platformLengthEdit",
						QStringLiteral("Sets the passenger capacity. Default 100 m.")));
				table.setCellWidget(row, kWidthColumn,
					makeGeometryField(panel, row, kWidthColumn, platform.hasWidth ? platform.widthM : kDefaultPlatformWidthM,
						"platformWidthEdit",
						QStringLiteral("Sets the passenger capacity. Default 2.5 m.")));
				++row;
			}
		}
	}

	// A platform is named by its station and its id.
	QString selectionId(const QTableWidget& table, int row) const override {
		const QTableWidgetItem* station = table.item(row, 0);
		const QTableWidgetItem* platform = table.item(row, 1);
		if (station && platform)
			return QString::fromStdString(platformSelectionKey(station->text().toStdString(), platform->text().toStdString()));
		return InfrastructureFacet::selectionId(table, row);
	}

	void commitCell(InfrastructurePanel& panel, int row, int column) override {
		const PlatformPlace place = platformPlace(panel.model(), row);
		if (place.station < 0)
			return;
		const SceneModel& model = panel.model();
		const std::size_t stationIndex = static_cast<std::size_t>(place.station);
		const std::size_t platformIndex = static_cast<std::size_t>(place.platform);
		const SceneStation& station = model.stations[stationIndex];
		const ScenePlatform& platform = station.platforms[platformIndex];
		const QString value = panel.table()->item(row, column)->text().trimmed();
		if (column == 0) {
			// The platform moves to the station that the text names.
			const std::string targetId = value.toStdString();
			const auto target = std::find_if(model.stations.begin(), model.stations.end(),
				[&targetId](const SceneStation& candidate) { return candidate.id == targetId; });
			if (target == model.stations.end()) {
				panel.refuseEdit(QStringLiteral("Platform Station ID must name an existing station"));
				return;
			}
			if (&*target == &station)
				return;
			const std::string platformId = platform.id;
			if (std::any_of(target->platforms.begin(), target->platforms.end(),
					[&platformId](const ScenePlatform& other) { return other.id == platformId; })) {
				panel.refuseEdit(QStringLiteral("Platform ID already exists on the target station"));
				return;
			}
			const std::string sourceId = station.id;
			const std::size_t targetIndex = static_cast<std::size_t>(target - model.stations.begin());
			panel.setSelectionId(QString::fromStdString(platformSelectionKey(targetId, platformId)));
			panel.edit(QStringLiteral("Edit platform"), [&](SceneModel& edited) {
				std::vector<ScenePlatform>& from = edited.stations[stationIndex].platforms;
				ScenePlatform moved = std::move(from[platformIndex]);
				from.erase(from.begin() + place.platform);
				edited.stations[targetIndex].platforms.push_back(std::move(moved));
				sceneMovePlatformReferences(edited, platformId, sourceId, targetId);
			});
		} else if (column == 1) {
			const std::string oldId = platform.id;
			const std::string newId = value.toStdString();
			if (oldId == newId)
				return;
			const bool taken = std::any_of(station.platforms.begin(), station.platforms.end(),
				[&newId](const ScenePlatform& other) { return other.id == newId; });
			if (newId.empty() || taken) {
				panel.refuseEdit(QStringLiteral("IDs must be unique and not empty"));
				return;
			}
			const std::string stationId = station.id;
			panel.setSelectionId(QString::fromStdString(platformSelectionKey(stationId, newId)));
			panel.edit(QStringLiteral("Rename platform"), [&](SceneModel& edited) {
				edited.stations[stationIndex].platforms[platformIndex].id = newId;
				sceneRewritePlatformReferences(edited, stationId, oldId, newId);
			});
		} else if (column == 2) {
			std::vector<std::string> nodes = splitCommaList(value);
			if (platform.nodeIds == nodes)
				return;
			panel.edit(QStringLiteral("Edit platform"),
				[&](SceneModel& edited) { edited.stations[stationIndex].platforms[platformIndex].nodeIds = std::move(nodes); });
		}
	}

	// A platform goes to the first station.
	void add(InfrastructurePanel& panel) override {
		const SceneModel& model = panel.model();
		if (model.stations.empty()) {
			panel.showStatus(QStringLiteral("Add a station before adding a platform"));
			return;
		}
		const std::string id = sceneUniqueInfrastructureId(model, "platform", "platforms");
		panel.setSelectionId(QString::fromStdString(platformSelectionKey(model.stations.front().id, id)));
		panel.edit(QStringLiteral("Add platform"), [&id](SceneModel& edited) { edited.stations.front().platforms.push_back({id, {}}); });
	}

	void remove(InfrastructurePanel& panel) override {
		const PlatformPlace place = platformPlace(panel.model(), panel.table()->currentRow());
		if (place.station < 0)
			return;
		const std::size_t stationIndex = static_cast<std::size_t>(place.station);
		const SceneStation& station = panel.model().stations[stationIndex];
		const std::string id = station.platforms[static_cast<std::size_t>(place.platform)].id;
		if (id.empty())
			return;
		if (!panel.confirmDelete(QString::fromStdString(id), sceneDirectDeleteConsumers(panel.model(), "platforms", id, station.id)))
			return;
		panel.setSelectionId(QString());
		panel.edit(QStringLiteral("Delete platform"), [stationIndex, place](SceneModel& edited) {
			std::vector<ScenePlatform>& platforms = edited.stations[stationIndex].platforms;
			platforms.erase(platforms.begin() + place.platform);
		});
	}

	// A number field that has been typed in and not left yet takes its value into the model.
	void commitPendingEdits(InfrastructurePanel& panel) override {
		QTableWidget& table = *panel.table();
		std::vector<std::tuple<int, int, double>> pending;
		for (int row = 0; row < table.rowCount(); ++row) {
			for (const int column : {kLengthColumn, kWidthColumn}) {
				auto* edit = qobject_cast<QDoubleSpinBox*>(table.cellWidget(row, column));
				if (!hasEditorFocus(edit) || !edit->property(kGeometryEditedProperty).toBool())
					continue;
				edit->setProperty(kGeometryEditedProperty, false);
				edit->interpretText();
				pending.emplace_back(row, column, edit->value());
			}
		}
		for (const auto& [row, column, value] : pending)
			commitGeometry(panel, row, column, value);
	}
};

class StationBoundariesFacet final : public SectionRowsFacet<SceneStationBoundary> {
public:
	StationBoundariesFacet()
		: SectionRowsFacet("station_boundaries", "station boundary", &SceneModel::stationBoundaries, {"Entrance", "Exit", "Direction"}) {}

private:
	void fillRow(InfrastructurePanel& panel, const SceneSectionInventory& inventory, int row,
		const SceneStationBoundary& boundary) const override {
		addSectionCombo(panel, inventory, row, 0, boundary.entranceBlock, "boundaryEntranceCombo",
			storeIn(&SceneStationBoundary::entranceBlock));
		// Choosing no exit clears the flag and the section together.
		addSectionCombo(
			panel, inventory, row, 1, boundary.hasExitBlock ? boundary.exitBlock : std::string(), "boundaryExitCombo",
			[](SceneStationBoundary& item, const std::string& section) {
				const bool hasExit = !section.empty();
				if (item.hasExitBlock == hasExit && (!hasExit || item.exitBlock == section))
					return false;
				item.hasExitBlock = hasExit;
				item.exitBlock = hasExit ? section : std::string();
				return true;
			},
			true, QStringLiteral("(none)"))
			->setToolTip(QStringLiteral("Exit section, or (none)"));
		setCell(*panel.table(), row, 2, flagText(boundary.direction));
	}
	std::string rowTitle(const SceneStationBoundary& boundary) const override { return boundary.entranceBlock; }
	Edit editField(SceneStationBoundary& boundary, int column, const QString& value) const override {
		return column == 2 ? setFlag(value, boundary.direction) : Edit::Unchanged;
	}
};

} // namespace

std::vector<std::unique_ptr<InfrastructureFacet>> makeStationFacets() {
	std::vector<std::unique_ptr<InfrastructureFacet>> facets;
	facets.push_back(std::make_unique<StationsFacet>());
	facets.push_back(std::make_unique<PlatformsFacet>());
	facets.push_back(std::make_unique<StationBoundariesFacet>());
	return facets;
}
