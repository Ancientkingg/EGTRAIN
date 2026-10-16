#ifndef BLOCKINGTIMESCOPE_H
#define BLOCKINGTIMESCOPE_H

#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/RouteDiagramCoordinates.h"

#include <map>
#include <string>
#include <vector>

class QWidget;

// Blocking-time inputs of the finished run. buildAllBlockingTimeSegments and buildBlockingTimePlannedReferences read
// the run from the simulation globals regional_train, numRegions and train_route, not from arguments.

struct BlockingTimeScope {
	std::vector<std::string> trainIds;
	std::vector<std::string> blockIds;
	std::vector<int> sectionIndices;
	double startTime = 0.0;
	double endTime = 0.0;
	int routeIndex = -1;
};

// What the scope dialog offers: the routes with their block sections in order, the trains with the route each runs on
// and the extent of the run.
struct BlockingTimeScopeChoices {
	struct Route {
		std::string id;
		std::string corridor;
		std::vector<std::string> blockIds;
	};
	struct Train {
		std::string id;
		int routeIndex = -1;
		std::string routeId;
	};
	std::vector<Route> routes;
	std::vector<Train> trains;
	double caseEndSeconds = 0.0;
	double stepSeconds = 1.0;
};

const char* blockingSegmentTypeName(BlockingTimeSegmentStyle style);
const char* blockingSegmentTypeName(const BlockingTimeDiagramSegment& segment);

// The scope of the whole run.
BlockingTimeScope defaultBlockingTimeScope(double caseEndSeconds);
// Asks for a route, a range of its blocks and an interval. On accept the scope holds the choice and the trains of the
// route. False when the dialog is cancelled or the interval is empty.
bool chooseBlockingTimeScope(QWidget* parent, const BlockingTimeScopeChoices& choices, BlockingTimeScope& scope);

std::vector<BlockingTimeDiagramSegment> buildAllBlockingTimeSegments();
std::vector<BlockingTimeDiagramSegment> projectBlockingSegments(
	const std::vector<BlockingTimeDiagramSegment>& segments,
	const std::map<std::string, RouteDiagramProjection>& projections, int& omitted,
	std::vector<BlockingTimeDiagramSegment>* unmapped = nullptr);
std::vector<BlockingTimePlannedReference> buildBlockingTimePlannedReferences(
	const BlockingTimeScope& scope, const std::map<int, RouteDiagramPath>& paths,
	const std::map<int, RouteDiagramProjection>& projections, int& omitted);

#endif
