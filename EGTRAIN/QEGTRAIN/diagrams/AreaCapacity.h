#pragma once

#include "diagrams/CapacityAnalysis.h"

#include <string>
#include <vector>

// Occupation of a set of resources by a set of trains.
//
// Every time is in seconds, every percentage is in percent (the unit of
// CapacityAnalysisResult::cyclePercentage) and -1.0 marks a value that is not
// available (the sentinel of CapacityAnalysisResult).
//
// The analysis area is only the list AreaCapacityInput::resourceIds of one
// call. It is not a signalling area, reads no signalling level and is stored
// nowhere. A resource is a block id as BlockingTimeDiagramInput::blockId holds
// it. An occupation belongs to a resource when shareBlockingTimeResource is
// true for the two ids. A composite block id therefore counts for every
// resource it shares a component with (once in each such row and once in the
// envelope), so the caller lists plain block ids. A resource id that is itself
// a composite id matches in the same way, so two rows can count the same
// occupation. Occupations are not clipped to a time window: the caller chooses
// the trains of the period and the period is only the denominator.

struct AreaCapacityTrain {
	CapacityAnalysisTrain train;
	// Route::reversed_direction of the route of the train. The chain keeps the
	// trains that have the value of the first selected train.
	bool reversedDirection = false;
};

struct AreaCapacityInput {
	// Empty ids, "None" and ids that repeat an earlier entry are ignored.
	std::vector<std::string> resourceIds;
	// The trains of the analysis in chain order. The module never reorders them.
	std::vector<AreaCapacityTrain> selected;
	// Every other train of the run. It may hold trains that touch nothing of the area.
	std::vector<AreaCapacityTrain> otherTraffic;
	// Valid when it is finite and above 0.
	double periodSeconds = -1.0;
	// The cycle-closing occurrence of the chain, as for analyzeCapacity.
	std::string cycleEndIdentity;
};

// One row per resource of the area, computed from the usable occupations of
// the selected trains that belong to the resource.
struct AreaCapacityResource {
	std::string resourceId;
	// Selected trains with at least one such occupation.
	int trainCount = 0;
	// Such occupations; a repeat on the same resource counts again.
	int occupationCount = 0;
	// Sum of endOccTime minus startOccTime over those occupations.
	double occupationSeconds = 0.0;
	// Length of the union of those occupations; overlapping or touching ones count once.
	double unionSeconds = 0.0;
	// occupationSeconds / period * 100; -1.0 without a valid period.
	double occupationPercentage = -1.0;
	// True for the rows whose occupationSeconds is the largest (within
	// kBlockingTimeToleranceSeconds, ties all count) and above 0.
	bool governing = false;
};

// What a note reports. Each kind names the ids that it fills in
// AreaCapacityNote; the other ids are empty.
enum class AreaCapacityNoteKind {
	// No resource id is left. Nothing else is computed.
	EmptyScope,
	// The period is not finite or not above 0. Every percentage is -1.0.
	InvalidPeriod,
	// trainId repeats the runtimeId of an earlier entry of selected or
	// otherTraffic. The repeating entry is ignored.
	DuplicateIdentity,
	// No selected train is left.
	NoSelectedTraffic,
	// trainId has an occupation on resourceId that fails
	// validBlockingTimeDiagramInput. It is used nowhere else.
	IncompleteOccupation,
	// trainId has two or more usable occupations on resourceId.
	RepeatedResource,
	// trainId has another reversedDirection than the first selected train. It
	// is left out of the chain and still counts in the rows and the envelope.
	OpposingTrain,
	// trainId, a train of the chain, has a profile reference that is not
	// finite or is below 0.
	InvalidReference,
	// The chain holds exactly one train.
	FewerThanTwoTrains,
	// The adjacent chain trains trainId and otherTrainId share no resource.
	// Not reported when one of them has an invalid reference.
	NoSharedResource,
	// The chain holds at least two trains and its compressed occupations overlap.
	ChainConflict,
	// trainId is in otherTraffic and has an occupation in the area, usable or not.
	UnselectedTraffic
};

struct AreaCapacityNote {
	AreaCapacityNoteKind kind = AreaCapacityNoteKind::EmptyScope;
	std::string trainId;
	std::string otherTrainId;
	std::string resourceId;
};

struct AreaCapacityResult {
	// The period of the input when it is valid, else -1.0.
	double periodSeconds = -1.0;
	// In the order of AreaCapacityInput::resourceIds.
	std::vector<AreaCapacityResource> resources;
	// Latest endOccTime minus earliest startOccTime over all usable occupations
	// of the selected trains, idle gaps included; -1.0 without such an
	// occupation. This is the formula of the removed area calculation, reported
	// for comparison only.
	double envelopeSeconds = -1.0;
	// envelopeSeconds / period * 100; -1.0 without a value or a valid period.
	double envelopePercentage = -1.0;
	// analyzeCapacity for the selected trains that run in the direction of the first one.
	CapacityAnalysisResult chain;
	// True when some row has trainCount above 0 and no note of the kinds
	// UnselectedTraffic, IncompleteOccupation or DuplicateIdentity exists. It
	// says only that no train of the supplied lists that touches the area is
	// missing from the selection and that no occupation in the area is
	// unusable. When it is false the figures count only the selected traffic.
	bool complete = false;
	// In the order of the steps of the calculation.
	std::vector<AreaCapacityNote> notes;
};

AreaCapacityResult analyzeAreaCapacity(const AreaCapacityInput& input);
