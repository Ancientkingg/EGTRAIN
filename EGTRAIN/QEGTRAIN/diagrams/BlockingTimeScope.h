#ifndef BLOCKINGTIMESCOPE_H
#define BLOCKINGTIMESCOPE_H

#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/CompletedRunData.h"
#include "diagrams/RouteDiagramCoordinates.h"
#include "scene/SectionLabels.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class QComboBox;
class QFormLayout;
class QLabel;
class QRadioButton;
class QWidget;

struct BlockingTimeScope {
	std::vector<std::string> trainIds;
	std::vector<std::string> blockIds;
	std::vector<int> sectionIndices;
	double startTime = 0.0;
	double endTime = 0.0;
	int routeIndex = -1;
};

/// One visit of a route to a station, as the scope dialogs list it. Positions of blocks count from 0 in route order.
struct RouteStation {
	std::string name;	///< Name of the station, or its id when it has no name
	int firstBlock = 0; ///< The first block that holds the station: a node of the station lies on it, its ends included
	int lastBlock = 0;	///< The last block that holds the station
	int fromBlock = 0;	///< The first block that holds a part of the route after the first node of the station
	int toBlock = 0;	///< The last block that holds a part of the route before the last node of the station
};

// What the scope dialog offers: the routes with their block sections in order and the extent of the run.
struct BlockingTimeScopeChoices {
	struct Route {
		std::string id;
		std::string corridor;
		std::vector<std::string> blockIds;
		std::vector<RouteStation> stations = {}; ///< The stations of the route in route order (routeStationsFrom)
	};
	std::vector<Route> routes;
	double caseEndSeconds = 0.0;
	double stepSeconds = 1.0;
	long long startOffsetSeconds = 0; ///< Clock time of second 0 of the run, in seconds since midnight
	SceneSectionLabels sectionLabels; ///< Names the blocks in the lists; the id is the tooltip of each entry
};

const char* blockingSegmentTypeName(BlockingTimeSegmentStyle style);
const char* blockingSegmentTypeName(const BlockingTimeDiagramSegment& segment);

/// The stations that the path passes, in its order, each with the blocks around it. A node of a station lies on a block
/// when its position on the route lies between the ends of the block, ends included. Nodes of one station that follow each
/// other on the path, with no node of another station between them, are one visit; a station visited again later is
/// listed again. A station whose nodes lie on no block is left out.
std::vector<RouteStation> routeStationsFrom(const std::vector<BlockSectionInput>& blocks, const RouteDiagramPath& path);
/// The route as the scope dialogs offer it: its id, corridor, blocks and stations.
BlockingTimeScopeChoices::Route blockingTimeScopeRouteFrom(const CompletedRoute& route);
/// The blocks, from 0 in route order, of the stretch from station `from` to station `to` of the route, positions in
/// `stations` in either order: every block that lies at least partly between the two stations (from the first node of the
/// one to the last node of the other), where the first and the last station of the route stand for its ends. From a
/// station to itself, the blocks that hold it. The first and the last block of the stretch; the whole route when it has
/// no station.
std::pair<int, int> stationStretchBlocks(const BlockingTimeScopeChoices::Route& route, int from, int to);
/// The stations whose stretch (stationStretchBlocks) is the smallest that holds the blocks `first` to `last`: the stretch
/// of those blocks itself when the stations give it, then the stations nearest the blocks. Of stretches of one length, the
/// earliest From station and the latest To station. Empty when the route has no station or no stretch holds the blocks.
std::optional<std::pair<int, int>> stationsHoldingBlocks(const BlockingTimeScopeChoices::Route& route, int first, int last);
/// A route as the scope dialogs list it: its id with its first and last station, such as "route0: Guingamp to Paimpol",
/// or its only station, then its corridor in brackets when it has one, "route0: Guingamp to Paimpol  (A)". A route
/// without a station shows its id and corridor only, "route5  (A)".
QString blockingTimeScopeRouteText(const BlockingTimeScopeChoices::Route& route);

/// The rows Route / corridor, Stretch, From and To of the two scope dialogs, and what they choose. The route list names
/// each route by blockingTimeScopeRouteText. Stretch chooses between Stations and Blocks: From station and To station
/// list the stations of the route (stationStretchBlocks gives their blocks), From block and To block list its blocks as
/// "1: B0 0.000 to 0.230 km" with the short block id as tooltip. Stations is offered for a route with two stations or more. A From
/// entry after the To entry moves the To entry to it, and a To entry before the From entry moves the From entry. A new
/// route takes all of its blocks, from its first to its last station with Stations. Blocks shows the blocks that the
/// stations chose, and Stations shows the stations that stationsHoldingBlocks gives for the blocks.
class ScopeStretchChoice : public QObject {
	Q_OBJECT

public:
	/// Adds the rows to `form`, with the widgets in `parent`. With `allRoutes` the route list starts with "All routes",
	/// which takes every block of every route. The widgets are named `name` + "Route", "Stations", "Blocks", "FromBlock"
	/// and "ToBlock", whichever kind of entry the last two list.
	ScopeStretchChoice(std::vector<BlockingTimeScopeChoices::Route> routes, SceneSectionLabels labels, bool allRoutes,
		QFormLayout* form, QWidget* parent, const QString& name);

	/// The chosen route as an index into the routes, -1 for all routes.
	int routeIndex() const;
	/// The id of the chosen route, empty for all routes.
	QString routeId() const;
	/// The positions of the blocks of the stretch in route order, from 0; empty for all routes.
	std::vector<int> sectionIndices() const;
	/// The runtime section ids of the blocks of the stretch in route order; empty for all routes.
	QStringList sectionIds() const;
	/// Chooses the route at `index` with its blocks `indices` (positions in either order, the range from the lowest to the
	/// highest), or all its blocks when there are none: as stations when the stations of the route give that stretch, else
	/// as blocks. An index of no route chooses the first entry of the route list.
	void choose(int index, const std::vector<int>& indices);

signals:
	/// The user chose another route or stretch: the id of the route and the section ids of the stretch, as routeId and
	/// sectionIds give them.
	void stretchChanged(const QString& routeId, const QStringList& sectionIds);
	/// The pointer or the arrow keys are on an entry of the open route, From or To list: the chosen route and stretch as
	/// stretchChanged gives them, and the section ids of the entry: all blocks of a route, the blocks that hold a station (RouteStation
	/// firstBlock to lastBlock) or the one block; none for "All routes".
	void entryHovered(const QString& routeId, const QStringList& sectionIds, const QStringList& entrySectionIds);
	/// The lists hold other entries, of another route or another kind of stretch, so the dialog can widen for them.
	void entriesChanged();

private:
	const BlockingTimeScopeChoices::Route* route() const;
	bool showsStations() const;
	void refill();
	void chooseRoute();
	void chooseKind(bool stations);
	void chooseEntry(bool from, int index);
	QStringList idsOf(int first, int last) const;
	void hover(const QComboBox* list, int index);

	std::vector<BlockingTimeScopeChoices::Route> m_routes;
	SceneSectionLabels m_labels;
	QComboBox* m_route = nullptr;
	QRadioButton* m_stations = nullptr;
	QRadioButton* m_blocks = nullptr;
	QLabel* m_fromLabel = nullptr;
	QLabel* m_toLabel = nullptr;
	QComboBox* m_from = nullptr;
	QComboBox* m_to = nullptr;
	bool m_preferStations = true; ///< Stations unless the user chose Blocks; a route of fewer stations shows blocks
	int m_firstBlock = 0;		  ///< The stretch of the chosen route
	int m_lastBlock = -1;
	int m_fromStation = 0; ///< The stations of the stretch while Stations is shown
	int m_toStation = 0;
};

/// Called with the route and stretch rows of a scope dialog once the dialog is built and before it shows, so that a caller
/// can read the opening stretch and connect to the signals.
using ScopeStretchObserver = std::function<void(ScopeStretchChoice&)>;

// The scope of the whole run.
BlockingTimeScope defaultBlockingTimeScope(double caseEndSeconds);
/// Asks for a route, a stretch of it (ScopeStretchChoice) and an interval as clock times of the run, from and to a second
/// of the run. It opens on the route and blocks of the scope when the choices have its route, else on all routes, and on
/// its interval. On accept the scope holds the choice and no train list, so it takes every train that occupies a block of
/// the stretch, whatever route the train runs on. False when the dialog is cancelled or the interval is empty.
bool chooseBlockingTimeScope(QWidget* parent, const BlockingTimeScopeChoices& choices, BlockingTimeScope& scope,
	const ScopeStretchObserver& observer = {});

std::vector<BlockingTimeDiagramSegment> projectBlockingSegments(
	const std::vector<BlockingTimeDiagramSegment>& segments,
	const std::map<std::string, RouteDiagramProjection>& projections, int& omitted,
	std::vector<BlockingTimeDiagramSegment>* unmapped = nullptr);

#endif
