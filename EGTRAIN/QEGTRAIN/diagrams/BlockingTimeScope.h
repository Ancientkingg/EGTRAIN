#ifndef BLOCKINGTIMESCOPE_H
#define BLOCKINGTIMESCOPE_H

#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/RouteDiagramCoordinates.h"

#include <map>
#include <string>
#include <vector>

// Blocking-time inputs of the finished run. Most functions here read the run from the simulation
// globals regional_train, numRegions, train_route, initial_variables and timestep, not from arguments.

struct BlockingTimeScope {
	std::vector<std::string> trainIds;
	std::vector<std::string> blockIds;
	std::vector<int> sectionIndices;
	double startTime = 0.0;
	double endTime = 0.0;
	int routeIndex = -1;
};

const char* blockingSegmentTypeName(BlockingTimeSegmentStyle style);
const char* blockingSegmentTypeName(const BlockingTimeDiagramSegment& segment);

BlockingTimeScope defaultBlockingTimeScope();
std::vector<BlockingTimeDiagramSegment> buildAllBlockingTimeSegments();
std::vector<BlockingTimeDiagramSegment> projectBlockingSegments(
	const std::vector<BlockingTimeDiagramSegment>& segments,
	const std::map<std::string, RouteDiagramProjection>& projections, int& omitted,
	std::vector<BlockingTimeDiagramSegment>* unmapped = nullptr);
std::vector<BlockingTimePlannedReference> buildBlockingTimePlannedReferences(
	const BlockingTimeScope& scope, const std::map<int, RouteDiagramPath>& paths,
	const std::map<int, RouteDiagramProjection>& projections, int& omitted);

#endif
