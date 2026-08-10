#ifndef CAPACITYANALYSIS_H
#define CAPACITYANALYSIS_H

#include "diagrams/BlockingTimeDiagram.h"

#include <string>
#include <vector>

struct CapacityAnalysisTrain {
	// Runtime occurrence identity, not a canonical service/timetable key.
	std::string runtimeId;
	std::string operatingCode;
	// Profile reference is the selected section entry (normally BlockTime's
	// StartRunTime); malformed or unavailable references make the result invalid.
	double profileReferenceTime = -1.0;
	double scheduledReferenceTime = -1.0;
	std::string referenceLabel;
	std::string referenceSource;
	std::vector<BlockingTimeDiagramInput> occupations;
};

struct CapacityHeadwayEvidence {
	std::string leaderBlockId;
	std::string followerBlockId;
	double leaderOffset = 0.0;
	double followerOffset = 0.0;
	double candidateHeadway = 0.0;
};

struct CapacityPairRow {
	std::string leaderIdentity;
	std::string followerIdentity;
	std::string leaderOperatingCode;
	std::string followerOperatingCode;
	double scheduledHeadway = -1.0;
	double minimumHeadway = -1.0;
	double buffer = -1.0;
	bool hasSharedConstraint = false;
	bool adjacent = false;
	std::vector<CapacityHeadwayEvidence> governingEvidence;
};

struct CapacityCompressionEvidence {
	std::string predecessorIdentity;
	double minimumHeadway = 0.0;
	std::vector<CapacityHeadwayEvidence> governingEvidence;
};

struct CapacityCompressionRow {
	std::string identity;
	std::string operatingCode;
	double originalReference = -1.0;
	double scheduledReference = -1.0;
	double compressedReference = -1.0;
	double shift = 0.0;
	std::vector<CapacityCompressionEvidence> governingPredecessors;
};

struct CapacityCriticalBlock {
	std::string leaderIdentity;
	std::string followerIdentity;
	std::string leaderBlockId;
	std::string followerBlockId;
	double gap = 0.0;
};

struct CapacityAnalysisResult {
	std::vector<CapacityPairRow> pairs;      // Adjacent rows in supplied order.
	std::vector<CapacityPairRow> allPairs;   // Every earlier/later constraint.
	std::vector<CapacityCompressionRow> compression;
	std::vector<std::vector<BlockingTimeDiagramInput>> compressedOccupations;
	std::vector<std::string> trainIdentities;
	std::vector<std::string> referenceLabels;
	std::vector<std::string> referenceSources;
	std::vector<CapacityCriticalBlock> criticalBlocks;
	double cycleTime = -1.0;
	double periodSeconds = -1.0;
	double cyclePercentage = -1.0;
	bool conflictFree = false;
	bool analyzable = false;
	std::string firstIdentity;
	std::string cycleEndIdentity;
};

// Ordered, deterministic capacity chain for one explicit sequence. Inputs are
// copied; neither train occupations nor canonical timetable data are changed.
CapacityAnalysisResult analyzeCapacity(const std::vector<CapacityAnalysisTrain>& trains,
	 double periodSeconds = -1.0, const std::string& cycleEndIdentity = {});

#endif // CAPACITYANALYSIS_H
