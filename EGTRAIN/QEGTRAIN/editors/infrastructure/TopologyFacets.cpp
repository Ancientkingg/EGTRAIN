#include "editors/infrastructure/TopologyFacets.h"

#include "editors/infrastructure/ListFacet.h"
#include "graphics/NetworkPresenter.h"
#include "scene/RuntimeLimits.h"
#include "scene/SceneReferences.h"
#include "scene/SectionInventory.h"
#include "scene/TrackPreview.h"

#include <QRegularExpression>
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
#include <utility>
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
	TracksFacet()
		: ListFacet("tracks", "track", &SceneModel::tracks, {"ID", "Layout region", "Level", "Shown on map", "Double switch"}) {}

	// The layout cells come after the id. A track without a view row has no region, and its other cells cannot be edited.
	// The map draws it next to the network, so it counts as shown.
	void fillRows(InfrastructurePanel& panel) override {
		ListFacet::fillRows(panel);
		const SceneModel& model = panel.model();
		QTableWidget& table = *panel.table();
		const bool hasLayout = !stationsByRegion(model).empty();
		for (int row = 0; row < static_cast<int>(model.tracks.size()); ++row) {
			const SceneTrackView* view = viewOf(model, model.tracks[static_cast<std::size_t>(row)].id);
			setCell(table, row, kRegionColumn, view ? QString::number(view->region) : QString());
			if (!hasLayout)
				table.item(row, kRegionColumn)->setToolTip(QStringLiteral("This case has no station layout, so a track cannot be placed on the map."));
			setCell(table, row, kLevelColumn, view ? QString::number(view->level) : QString(), view != nullptr);
			setFlagCell(table, row, kShownColumn, !view || view->visible);
			setFlagCell(table, row, kDoubleSwitchColumn, view && view->doubleSwitch);
			if (!view)
				for (const int column : {kShownColumn, kDoubleSwitchColumn})
					table.item(row, column)->setFlags(table.item(row, column)->flags() & ~Qt::ItemIsUserCheckable);
		}
	}
	// Reverse Track turns a track whose arcs run against its X around, so that they run along it.
	std::vector<TaskAction> taskActions() const override {
		return {{QStringLiteral("reverseTrackButton"), QStringLiteral("Reverse Track"),
					QStringLiteral("Swaps the ends of every arc of the selected track and reverses the order of its blocks.")},
			{QStringLiteral("setXFromDistanceButton"), QStringLiteral("Set X from Distance"),
				QStringLiteral("Sets the X of the nodes of the selected track to the distance along its arcs from the first node, "
							   "taking X and Y as map coordinates in km, and Y to 0.")}};
	}
	void runTaskAction(InfrastructurePanel& panel, int index) override {
		if (index == 0)
			reverseTrack(panel);
		else if (index == 1)
			setXFromDistance(panel);
	}

	// Every region that can place a track, and None for a track without a view row. The region that the edited track has
	// stays in the list even when it cannot place a track.
	std::vector<Choice> choices(const InfrastructurePanel& panel, int row, int column) const override {
		std::vector<Choice> result;
		if (column != kRegionColumn)
			return result;
		result.push_back({QStringLiteral("None"), QString()});
		const SceneTrack* edited = inRow(panel.model().tracks, row);
		const SceneTrackView* own = edited ? viewOf(panel.model(), edited->id) : nullptr;
		for (const auto& [region, stations] : stationsByRegion(panel.model())) {
			if (!regionCanPlaceTracks(panel.model(), region) && !(own && own->region == region))
				continue;
			constexpr std::size_t kShownStations = 4;
			QStringList names;
			for (std::size_t place = 0; place < stations.size() && place < kShownStations; ++place)
				names << QString::fromStdString(stations[place].second);
			QString label = QStringLiteral("%1: %2").arg(region).arg(names.join(QStringLiteral(", ")));
			if (stations.size() > kShownStations)
				label += QStringLiteral("...");
			result.push_back({label, QString::number(region)});
		}
		return result;
	}
	// The region, level, map and double switch cells set or edit the view row of the track, in one edit.
	void commitCell(InfrastructurePanel& panel, int row, int column) override {
		const std::vector<SceneTrack>& tracks = panel.model().tracks;
		if (column == 0 || row >= static_cast<int>(tracks.size())) {
			ListFacet::commitCell(panel, row, column);
			return;
		}
		const std::string trackId = tracks[static_cast<std::size_t>(row)].id;
		const QString value = cellValue(*panel.table()->item(row, column));
		const SceneTrackView* current = viewOf(panel.model(), trackId);
		if (column == kRegionColumn) {
			bool parsed = false;
			const int region = value.toInt(&parsed);
			if (value.isEmpty() ? current == nullptr : current && parsed && current->region == region)
				return;
			if (trackId.empty()) {
				panel.refuseEdit(QStringLiteral("Give the track an ID first"));
				return;
			}
			if (value.isEmpty() && current && (!current->visible || current->doubleSwitch)) {
				panel.refuseEdit(QStringLiteral("A hidden or double switch track needs its layout region. Tick Shown on map and clear "
												"Double switch first, then set the region to None."));
				return;
			}
			if (!value.isEmpty() && !(parsed && stationsByRegion(panel.model()).count(region))) {
				panel.refuseEdit(QStringLiteral("Layout region must be a region of the station layout, or None."));
				return;
			}
			if (!value.isEmpty() && !regionCanPlaceTracks(panel.model(), region)) {
				const std::size_t stations = stationsByRegion(panel.model()).at(region).size();
				panel.refuseEdit(stations == 1 ? QStringLiteral("Region %1 has one station on the layout. A track needs two stations in its region to be placed.").arg(region)
											   : QStringLiteral("Region %1 has %2 stations on the layout, but they do not give two usable places. A track needs two stations at different places in its region to be placed.")
													 .arg(region)
													 .arg(stations));
				return;
			}
			panel.edit(QStringLiteral("Edit track layout"), [trackId, value, region](SceneModel& model) {
				std::vector<SceneTrackView>& views = model.trackViews;
				const auto found = std::find_if(views.begin(), views.end(), [&trackId](const SceneTrackView& view) { return view.trackId == trackId; });
				if (value.isEmpty()) {
					if (found != views.end())
						views.erase(found);
				} else if (found != views.end()) {
					found->region = region;
				} else {
					SceneTrackView view;
					view.trackId = trackId;
					view.region = region;
					views.push_back(view);
				}
			});
			return;
		}
		if (!current)
			return;
		SceneTrackView edited = *current;
		Edit result = Edit::Unchanged;
		if (column == kLevelColumn) {
			bool parsed = false;
			const int level = value.toInt(&parsed);
			if (!parsed)
				result = Edit::Refused;
			else if (level != edited.level) {
				edited.level = level;
				result = Edit::Changed;
			}
		} else if (column == kShownColumn) {
			result = setFlag(value, edited.visible);
		} else if (column == kDoubleSwitchColumn) {
			result = setFlag(value, edited.doubleSwitch);
		}
		if (result == Edit::Changed)
			panel.edit(QStringLiteral("Edit track layout"), [edited](SceneModel& model) {
				for (SceneTrackView& view : model.trackViews)
					if (view.trackId == edited.trackId)
						view = edited;
			});
		else if (result == Edit::Refused)
			panel.refuseEdit(column == kLevelColumn ? QStringLiteral("Level needs a whole number, such as 0 or -1.")
													: refusedValueText(*panel.table(), row, column));
	}

private:
	static constexpr int kRegionColumn = 1;
	static constexpr int kLevelColumn = 2;
	static constexpr int kShownColumn = 3;
	static constexpr int kDoubleSwitchColumn = 4;

	static const SceneTrackView* viewOf(const SceneModel& model, const std::string& trackId) {
		const auto found = std::find_if(model.trackViews.begin(), model.trackViews.end(),
			[&trackId](const SceneTrackView& view) { return view.trackId == trackId; });
		return found == model.trackViews.end() ? nullptr : &*found;
	}

	// For each region that the station views define, its stations as (position km, station id) by position.
	static std::map<int, std::vector<std::pair<double, std::string>>> stationsByRegion(const SceneModel& model) {
		std::map<int, std::vector<std::pair<double, std::string>>> regions;
		for (const SceneStationView& view : model.stationViews)
			for (const auto& [region, positionKm] : view.regions)
				regions[region].emplace_back(positionKm, view.stationId);
		for (auto& entry : regions)
			std::stable_sort(entry.second.begin(), entry.second.end(),
				[](const auto& left, const auto& right) { return left.first < right.first; });
		return regions;
	}

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
		if (!routes.empty())
			panel.prompts().showInfo(&panel, QStringLiteral("Track Reversed"),
				QStringLiteral("Track '%1' is reversed.").arg(trackText) + listToCheck(QStringLiteral("Check these routes, which run over it:"), routes));
		panel.showStatus(QStringLiteral("Reversed track '%1'. Undo restores it.").arg(trackText), 8000);
	}

	// The X that the first node of the track gets. It is the one place that fixes the start of the chainage.
	static constexpr double kStartXKm = 0.0;

	// Sets X of the nodes of the track to the distance along its arcs from the first node, taking the old X and Y as map
	// coordinates, and Y to 0, in one edit. A station with a position that sat on a platform node of the track follows the
	// node. Block lengths, signalling areas, connections, section references and stations without platforms stay as they
	// are, so a box lists the blocks and the stations to check.
	static void setXFromDistance(InfrastructurePanel& panel) {
		const QString trackText = panel.selectionId();
		const std::string trackId = trackText.toStdString();
		const SceneTrackChain chain = sceneTrackChain(panel.model(), trackId);
		if (chain.nodes.empty()) {
			panel.showStatus(QStringLiteral("Track '%1' is not one line of arcs. Case Validation names what to fix.").arg(trackText));
			return;
		}
		std::vector<std::string> nodeIds;
		std::vector<double> oldX;
		std::vector<double> newX;
		bool unchanged = true;
		for (const SceneNode* node : chain.nodes) {
			if (!std::isfinite(node->xKm) || !std::isfinite(node->yKm)) {
				panel.showStatus(QStringLiteral("Node '%1' has no number in X or Y. Set it first.")
						.arg(QString::fromStdString(node->id)));
				return;
			}
			const double x = newX.empty() ? kStartXKm
										  : newX.back() + std::hypot(node->xKm - chain.nodes[newX.size() - 1]->xKm, node->yKm - chain.nodes[newX.size() - 1]->yKm);
			unchanged = unchanged && node->xKm == x && node->yKm == 0.0;
			nodeIds.push_back(node->id);
			oldX.push_back(node->xKm);
			newX.push_back(x);
		}
		if (unchanged) {
			panel.showStatus(QStringLiteral("Track '%1' already has X as the distance along its arcs.").arg(trackText));
			return;
		}
		const auto indexOf = [&nodeIds](const std::string& id) -> std::optional<std::size_t> {
			const auto found = std::find(nodeIds.begin(), nodeIds.end(), id);
			if (found == nodeIds.end())
				return std::nullopt;
			return static_cast<std::size_t>(found - nodeIds.begin());
		};
		// The place in the chain of the first platform node of the station whose old X is its position, if any.
		const auto followedNode = [&](const SceneStation& station) -> std::optional<std::size_t> {
			if (!station.hasPosition)
				return std::nullopt;
			for (const ScenePlatform& platform : station.platforms)
				for (const std::string& id : platform.nodeIds) {
					const std::optional<std::size_t> place = indexOf(id);
					if (place && std::abs(oldX[*place] - station.positionKm) <= RuntimeLimits::kNativeCoordinateTolerance)
						return place;
				}
			return std::nullopt;
		};
		// Stations to check: a position that no node of the track gave a new X to, next to a platform node of the track, or
		// a station without platforms at the old X of a node of the track. Found before the edit changes the positions.
		std::vector<std::string> stationsToCheck;
		for (const SceneStation& station : panel.model().stations) {
			if (!station.hasPosition || followedNode(station))
				continue;
			const bool onTrack = std::any_of(station.platforms.begin(), station.platforms.end(),
				[&indexOf](const ScenePlatform& platform) {
					return std::any_of(platform.nodeIds.begin(), platform.nodeIds.end(),
						[&indexOf](const std::string& id) { return indexOf(id).has_value(); });
				});
			const bool atNode = station.platforms.empty()
				&& std::any_of(oldX.begin(), oldX.end(), [&station](double x) {
					   return std::abs(x - station.positionKm) <= RuntimeLimits::kNativeCoordinateTolerance;
				   });
			if (onTrack || atNode)
				stationsToCheck.push_back(station.id);
		}
		const bool edited = panel.edit(QStringLiteral("Set X from distance"), [&](SceneModel& scene) {
			for (SceneNode& node : scene.nodes) {
				const std::optional<std::size_t> place = node.trackId == trackId ? indexOf(node.id) : std::nullopt;
				if (!place)
					continue;
				node.xKm = newX[*place];
				node.yKm = 0.0;
			}
			for (SceneStation& station : scene.stations)
				if (const std::optional<std::size_t> place = followedNode(station))
					station.positionKm = newX[*place];
		});
		if (!edited)
			return;
		std::vector<std::string> blocksToCheck;
		const SceneSectionInventory inventory = buildSceneSectionInventory(panel.model());
		for (const SceneSectionDescriptor& section : inventory.sections) {
			if (section.firstTrackId != trackId || section.connectionDerived)
				continue;
			QStringList coverage;
			if (section.trackCoverageGap)
				coverage << QStringLiteral("gap");
			if (section.layoutOverflow)
				coverage << QStringLiteral("overflow");
			if (section.clippedToTrackEnd)
				coverage << QStringLiteral("clipped");
			if (!coverage.isEmpty())
				blocksToCheck.push_back(
					(section.sourceBlockId + " (" + coverage.join(QStringLiteral(", ")).toStdString() + ")"));
		}
		if (!blocksToCheck.empty() || !stationsToCheck.empty()) {
			double blockSumKm = 0.0;
			for (const SceneBlock& block : panel.model().blocks)
				if (block.trackId == trackId)
					blockSumKm += block.lengthKm;
			QString text = QStringLiteral("Track '%1' is now %2 km long; its blocks add up to %3 km.")
							   .arg(trackText, QString::number(newX.back(), 'f', 3), QString::number(blockSumKm, 'f', 3));
			text += listToCheck(QStringLiteral("Blocks to check:"), blocksToCheck);
			text += listToCheck(QStringLiteral("Stations whose position to check:"), stationsToCheck);
			text += QStringLiteral("\n\nCase Validation lists the references that the new X values break.");
			panel.prompts().showInfo(&panel, QStringLiteral("X Set from Distance"), text);
		}
		panel.showStatus(QStringLiteral("Set X of %1 nodes of track '%2' from the distance along its arcs. Undo restores them.")
							 .arg(nodeIds.size())
							 .arg(trackText),
			8000);
	}

	// A heading and one bullet for each of the first ten names, then "and N more"; empty for no names. A box that lists
	// every name can be taller than the screen.
	static QString listToCheck(const QString& heading, const std::vector<std::string>& names) {
		if (names.empty())
			return {};
		constexpr std::size_t kMaxListed = 10;
		QStringList lines;
		for (std::size_t name = 0; name < names.size() && name < kMaxListed; ++name)
			lines << QString::fromStdString(names[name]);
		QString list = lines.join("\n• ");
		if (names.size() > kMaxListed)
			list += QStringLiteral("\nand %1 more").arg(static_cast<int>(names.size() - kMaxListed));
		return QStringLiteral("\n\n%1\n• %2").arg(heading, list);
	}

	void fillRow(QTableWidget& table, int row, const SceneTrack& track) const override {
		setCell(table, row, 0, QString::fromStdString(track.id));
	}
	Edit editField(SceneTrack&, int, const QString&) const override { return Edit::Unchanged; }
	SceneTrack newItem(const SceneModel&, const std::string& id) const override { return {id}; }
	void rewriteReferences(SceneModel& model, const std::string& oldId, const std::string& newId) const override {
		sceneRewriteTrackReferences(model, oldId, newId);
	}
	// A deleted track takes its view row with it.
	void removeReferences(SceneModel& model, const std::string& id) const override {
		std::vector<SceneTrackView>& views = model.trackViews;
		views.erase(std::remove_if(views.begin(), views.end(), [&id](const SceneTrackView& view) { return view.trackId == id; }), views.end());
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
		// The legacy export refuses a station id with whitespace, so a run of whitespace in the name becomes one underscore.
		const std::string stationName = name.toStdString();
		const std::string stationId = QString(name).replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral("_")).toStdString();
		if (std::any_of(model.stations.begin(), model.stations.end(), [&stationId, &stationName](const SceneStation& station) {
				return station.id == stationId || station.id == stationName || station.name == stationId || station.name == stationName;
			})) {
			panel.showStatus(QStringLiteral("Station '%1' already exists. Add a platform to it under Platforms.").arg(name));
			return;
		}
		panel.setSelectionId(nodeText);
		const bool added = panel.edit(QStringLiteral("Add station"), [&](SceneModel& scene) {
			SceneStation station;
			station.id = stationId;
			station.name = stationName;
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
