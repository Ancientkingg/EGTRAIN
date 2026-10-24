#ifndef CAPACITYANALYSIS_H
#define CAPACITYANALYSIS_H

#include "diagrams/BlockingTimeDiagram.h"

#include <string>
#include <vector>

// Compression of a chain of trains on shared block resources. Every time is in seconds of the run, a share is in
// percent and -1.0 marks a value that is not available.

/// One train of the chain: its occupations and the reference time they are shifted by.
struct CapacityAnalysisTrain {
	/// Runtime occurrence identity, not a canonical service/timetable key.
	std::string runtimeId;
	std::string operatingCode; ///< Operating code of the service
	/// Profile reference is the entry of the train into the analysed blocks (normally
	/// BlockTime's StartRunTime); malformed or unavailable references make the result invalid.
	double profileReferenceTime = -1.0;
	/// Planned time of the reference. Below 0 or not finite: profileReferenceTime is used.
	double scheduledReferenceTime = -1.0;
	std::string referenceLabel;						   ///< Where the reference lies, for the export
	std::string referenceSource;					   ///< Where scheduledReferenceTime came from, for the export
	std::vector<BlockingTimeDiagramInput> occupations; ///< The occupations of the analysed resources
};

/// One pair of occupations that bounds the headway of two trains.
struct CapacityHeadwayEvidence {
	std::string leaderBlockId;	   ///< Block of the leader's occupation
	std::string followerBlockId;   ///< Block of the follower's occupation, which shares a resource with it
	double leaderOffset = 0.0;	   ///< Leader's end of occupation minus its profile reference
	double followerOffset = 0.0;   ///< Follower's start of occupation minus its profile reference
	double candidateHeadway = 0.0; ///< leaderOffset minus followerOffset
};

/// The headway of a leader and a later follower.
struct CapacityPairRow {
	std::string leaderIdentity;		   ///< runtimeId of the leader
	std::string followerIdentity;	   ///< runtimeId of the follower
	std::string leaderOperatingCode;   ///< Operating code of the leader
	std::string followerOperatingCode; ///< Operating code of the follower
	/// Follower's planned reference minus the leader's; -1.0 when a profile reference is not available.
	double scheduledHeadway = -1.0;
	/// Largest candidate headway; -1.0 without a shared resource or a valid profile reference (hasSharedConstraint false).
	/// It is below 0 when the trains enter the analysed blocks at different blocks and the follower reaches the shared
	/// resources later after its reference than the leader leaves them.
	double minimumHeadway = -1.0;
	/// scheduledHeadway minus minimumHeadway; -1.0 without a shared resource or a valid profile reference. A computed
	/// buffer can be negative, and is above scheduledHeadway when minimumHeadway is below 0.
	double buffer = -1.0;
	bool hasSharedConstraint = false; ///< True when the two trains occupy a shared resource and both profile references are valid
	bool adjacent = false;			  ///< True when the follower comes right after the leader in the chain
	/// The occupation pairs whose candidate headway is within kBlockingTimeToleranceSeconds of the largest.
	std::vector<CapacityHeadwayEvidence> governingEvidence;
};

/// An earlier train that fixes the compressed reference of a later one.
struct CapacityCompressionEvidence {
	std::string predecessorIdentity;						///< runtimeId of the earlier train
	double minimumHeadway = 0.0;							///< Minimum headway of the pair
	std::vector<CapacityHeadwayEvidence> governingEvidence; ///< The governing pairs (see CapacityPairRow::governingEvidence)
};

/// One train of the compressed chain.
struct CapacityCompressionRow {
	std::string identity;			  ///< runtimeId of the train
	std::string operatingCode;		  ///< Operating code of the service
	double originalReference = -1.0;  ///< profileReferenceTime of the train
	double scheduledReference = -1.0; ///< Planned reference, or the profile reference without one
	/// The scheduled reference for the first train. For a later one that shares a resource with earlier trains, the largest
	/// of their compressed reference plus their minimum headway, which can lie before the first train's; for one that
	/// shares none, the compressed reference of the first train.
	double compressedReference = -1.0;
	double shift = 0.0; ///< compressedReference minus scheduledReference; 0 when either is not finite
	/// The earlier trains that fix compressedReference.
	std::vector<CapacityCompressionEvidence> governingPredecessors;
};

/// Two compressed occupations of a train and a governing predecessor that touch.
struct CapacityCriticalBlock {
	std::string leaderIdentity;	  ///< runtimeId of the predecessor
	std::string followerIdentity; ///< runtimeId of the follower
	std::string leaderBlockId;	  ///< Block of the predecessor's occupation
	std::string followerBlockId;  ///< Block of the follower's occupation
	/// Follower's start minus the predecessor's end; within kBlockingTimeToleranceSeconds of 0.
	double gap = 0.0;
};

/// The result of analyzeCapacity.
struct CapacityAnalysisResult {
	std::vector<CapacityPairRow> pairs;				 ///< Adjacent rows in supplied order.
	std::vector<CapacityPairRow> allPairs;			 ///< Every earlier/later constraint.
	std::vector<CapacityCompressionRow> compression; ///< One row for each train, in supplied order
	/// The valid occupations of each train, shifted by its compressed minus its profile reference.
	std::vector<std::vector<BlockingTimeDiagramInput>> compressedOccupations;
	std::vector<std::string> trainIdentities;		   ///< runtimeId of each train, in supplied order
	std::vector<std::string> referenceLabels;		   ///< referenceLabel of each train
	std::vector<std::string> referenceSources;		   ///< referenceSource of each train
	std::vector<CapacityCriticalBlock> criticalBlocks; ///< The touching compressed occupations
	/// Compressed reference of the cycle-closing train minus that of the first train; -1.0 without cycleEndIdentity or
	/// when the cycle-closing train shares no resource with an earlier train.
	double cycleTime = -1.0;
	/// The period of the call when it is finite and not below 0, else -1.0.
	double periodSeconds = -1.0;
	/// cycleTime / periodSeconds * 100; -1.0 without a cycle time or a period above 0.
	double cyclePercentage = -1.0;
	/// True when no two compressed occupations of different trains on a shared resource overlap by more than
	/// kBlockingTimeToleranceSeconds.
	bool conflictFree = false;
	/// True with at least two trains, valid profile references and no conflict.
	bool analyzable = false;
	std::string firstIdentity;	  ///< runtimeId of the first train, empty without trains
	std::string cycleEndIdentity; ///< The cycle-closing train; empty when it is missing or is the first train
};

/// Ordered, deterministic capacity chain for one explicit sequence. Inputs are
/// copied; neither train occupations nor canonical timetable data are changed.
CapacityAnalysisResult analyzeCapacity(const std::vector<CapacityAnalysisTrain>& trains,
	double periodSeconds = -1.0, const std::string& cycleEndIdentity = {});

/// The runtime id of the copy of a train one period later: "<runtimeId> (next period)".
std::string capacityNextPeriodIdentity(const std::string& runtimeId);
/// The train one period later: capacityNextPeriodIdentity as its runtime id, the operating code, reference label and
/// reference source of `train`, and its occupations and references moved by `periodSeconds`. A time of an occupation
/// that is not available stays as it is.
CapacityAnalysisTrain capacityNextPeriodTrain(const CapacityAnalysisTrain& train, double periodSeconds);

#endif // CAPACITYANALYSIS_H
