#include "simulation/Simulation.h"
#include "diagrams/RunResults.h"
#include "simulation/PassengerResults.h"
#include "simulation/RunOutputFile.h"
#include "simulation/SimulationRun.h"
#include "util/Log.h"
#include "util/PlaybackProfiler.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <map>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <vector>

using namespace std;

namespace {
bool energySeriesCovers(const std::vector<double>& values, int first, int last) {
	if (first < 0 || last < first || last >= static_cast<int>(values.size()))
		return false;
	for (int index = first; index <= last; ++index) {
		if (!std::isfinite(values[static_cast<std::size_t>(index)]))
			return false;
	}
	return true;
}

bool canComputeTrainEnergy(Train& train) {
	if (train.earliestActiveTrajectoryIndex < 0 || train.End_Time < train.earliestActiveTrajectoryIndex
		|| train.End_Time >= static_cast<int>(train.instant_spatial_position.size())
		|| !std::isfinite(train.departure_time))
		return false;
	// Completed runs can leave a non-finite terminal power sample; the shared
	// energy calculation treats that boundary sample as zero.
	train.sanitizeTerminalPowerSample();
	const int energyStart = static_cast<int>(train.departure_time);
	return energyStart >= 0 && energyStart <= train.End_Time
		&& energySeriesCovers(train.instant_train_power_consumption, energyStart, train.End_Time)
		&& energySeriesCovers(train.instant_train_energy_consumption, energyStart, train.End_Time)
		&& !validTrajectorySegments(train.instant_spatial_position,
			train.earliestActiveTrajectoryIndex, train.End_Time)
				.empty();
}

void calculateDelayStatistics(DelayStats& result, const std::vector<double>& delays,
	const std::vector<double>& consecutive, bool includeNonPositive) {
	result.N_Stopped_Trains = static_cast<int>(delays.size());
	result.N_Delayed_Arr = result.N_Delayed_Arr_3min = result.N_Delayed_Arr_5min = 0;
	result.Av_Arrival_Delay = result.Std_Arrival_Delay = 0;
	result.Perc_Delayed_T = result.Perc_Delayed_T_3min = result.Perc_Delayed_T_5min = 0;
	result.totalArrivalDelay = result.Tot_Consec_Delay = 0;
	result.Max_TotalDelay = result.Max_Cons_Delay = -1;
	if (delays.empty()) {
		result.Av_Arrival_Delay = result.Std_Arrival_Delay = result.Tot_Consec_Delay = -1;
		result.Perc_Delayed_T = result.Perc_Delayed_T_3min = result.Perc_Delayed_T_5min = -1;
		return;
	}

	std::vector<double> population;
	population.reserve(delays.size());
	for (double delay : delays) {
		if (delay > 0) {
			++result.N_Delayed_Arr;
			if (delay > 3 * 60 / timestep)
				++result.N_Delayed_Arr_3min;
			if (delay > 5 * 60 / timestep)
				++result.N_Delayed_Arr_5min;
		}
		if (includeNonPositive || delay > 0)
			population.push_back(delay);
	}

	result.totalArrivalDelay = std::accumulate(delays.begin(), delays.end(), 0.0);
	if (!population.empty()) {
		result.Av_Arrival_Delay =
			std::accumulate(population.begin(), population.end(), 0.0) / population.size();
		if (population.size() > 1) {
			double squaredDifference = 0;
			for (double delay : population)
				squaredDifference += std::pow(delay - result.Av_Arrival_Delay, 2);
			result.Std_Arrival_Delay = std::sqrt(squaredDifference / (population.size() - 1));
		}
	}

	result.Perc_Delayed_T = static_cast<double>(result.N_Delayed_Arr) / delays.size() * 100;
	result.Perc_Delayed_T_3min = static_cast<double>(result.N_Delayed_Arr_3min) / delays.size() * 100;
	result.Perc_Delayed_T_5min = static_cast<double>(result.N_Delayed_Arr_5min) / delays.size() * 100;
	result.Max_TotalDelay = *std::max_element(delays.begin(), delays.end());
	if (!consecutive.empty()) {
		result.Tot_Consec_Delay = std::accumulate(consecutive.begin(), consecutive.end(), 0.0);
		result.Max_Cons_Delay = *std::max_element(consecutive.begin(), consecutive.end());
	}
}

void calculateStationDelayStatistics(SimulationRun& run, const std::string& stationName, DelayStats& result, bool includeNonPositive) {
	std::vector<double> arrivals;
	std::vector<double> consecutive;
	const auto append = [&arrivals, &consecutive, includeNonPositive](const Train& train, int index) {
		const bool recorded = includeNonPositive
			? train.StationArrivals[index] >= 0 && train.ScheduledArrivals[index] >= 0
			: train.StationDelay[index] != -1;
		if (!recorded)
			return;
		arrivals.push_back(train.StationDelay[index]);
		consecutive.push_back(train.StationConsecDelay[index]);
	};

	for (int trainIndex = 0; trainIndex < run.numTrains; ++trainIndex) {
		const Train& train = run.trains[trainIndex];
		if (stationName == kFinalStationName) {
			if (train.numStations > 0)
				append(train, train.numStations - 1);
			continue;
		}
		for (int stationIndex = 0; stationIndex < train.numStations; ++stationIndex) {
			if (train.stationNameForArrivalStats(stationIndex) == stationName)
				append(train, stationIndex);
		}
	}
	calculateDelayStatistics(result, arrivals, consecutive, includeNonPositive);
}
} // namespace

// Updated Function to Calculate the arrival delay at each station for each train
void calculateArrivalDelayAllTrains(SimulationRun& run) {
	for (int j = 0; j < run.numTrains; j++) {
		// Determine the actual arrivals at the train stations
		run.trains[j].Determine_Actual_Station_Arrivals();
		// Compute the arrival delays
		run.trains[j].computeArrivalDelaysAtStations();
	}
}

// Function to calculate the train delay statistics for a single station instant_spatial_position
void calculateDelayStatsAtStation(SimulationRun& run, Stations& S) {
	calculateStationDelayStatistics(run, S.stationName, S, false);
}

void calculateDelayStatsAtFinalStops(SimulationRun& run) {
	calculateStationDelayStatistics(run, kFinalStationName, run.finalStation, false);
}

// Function to Calculate the positive and negative delays stats at station instant_spatial_position
void calculatePosAndNegDelayStatsAtStation(SimulationRun& run, Stations& S) {
	calculateStationDelayStatistics(run, S.stationName, S, true);
}

void calculatePosAndNegDelayStatsAtFinalStops(SimulationRun& run) {
	calculateStationDelayStatistics(run, kFinalStationName, run.finalStation, true);
}

// Function to Compute the amount of Disturbances set as input: Entrance delays, Cumulative disturbances to dwell times and Total delays (sum of entrance delays and disturbances to dwell times)
void Compute_Input_Delays(SimulationRun& run) {
	std::vector<double> delays(run.numTrains);
	for (int trainIndex = 0; trainIndex < run.numTrains; ++trainIndex)
		delays[trainIndex] = run.trains[trainIndex].TotalInputDelays;
	calculateDelayStatistics(run.totalInputDelays, delays, {}, false);
	for (int trainIndex = 0; trainIndex < run.numTrains; ++trainIndex)
		delays[trainIndex] = run.trains[trainIndex].EntranceDelay;
	calculateDelayStatistics(run.entranceInputDelays, delays, {}, false);
	for (int trainIndex = 0; trainIndex < run.numTrains; ++trainIndex)
		delays[trainIndex] = run.trains[trainIndex].TotalInputDelays
			- run.trains[trainIndex].EntranceDelay;
	calculateDelayStatistics(run.disturbanceInput, delays, {}, false);
}

// Function to calculate the train delay statistics for all the station considered in the network
void calculateDelayStatsForAllStations(SimulationRun& run) {
	for (int s = 0; s < run.infra.numStations; s++) {
		calculateDelayStatsAtStation(run, run.infra.stations[s]);
	}
}

// Function to calculate positive and negative train delay statistics for all stations
void calculatePosAndNegDelayStatsForAllStations(SimulationRun& run) {
	for (int s = 0; s < run.infra.numStations; s++) {
		calculatePosAndNegDelayStatsAtStation(run, run.infra.stations[s]);
	}
}

// Function to calculate positive an negative delays of trains for all the trains considered in the simulation
void calculatePosAndNegArrivalDelayAllTrains(SimulationRun& run) {
	for (int j = 0; j < run.numTrains; j++) {
		run.trains[j].Compute_Pos_And_Neg_Arrival_Delays_At_Stations();
	}
}

// Function to compute Energy consumption for all the trains in the network
void ComputeEnergyConsumptionForAllTrains(Train* Trains, int numTrains) {
	for (int i = 0; i < numTrains; i++) {
		Trains[i].TotalEnergyConsumed = 0;
		Trains[i].TotalEnergyConsWithRegBrak = 0;
		Trains[i].TotalEnergySubstationRequest = 0;
		Trains[i].TotalEnergySubstRequestWithRegBrak = 0;
		if (!canComputeTrainEnergy(Trains[i]))
			continue;
		// Compute the Energy Consumption for all the trains in the network
		Trains[i].TotalEnergyConsumptionWithAndWithoutRegBraking(0.8, 0.7); // we are using as default an efficiency of 0.8 for the substation and 0.7 for regenerative braking (but these values are actually a feature of the substation and the train respectively)
	}
}

// Function to Compute the Energy Consumption for the Timetable
void ComputeTimetableEnergyConsumption(Train* Trains, int numTrains, string OutputFolder) {
	double TotalEnergyConsumed = 0, TotalEnergyConsWithRegBraking = 0, TotalEnergySubstationRequest = 0, TotalEnergySubstRequestWithRegBraking = 0;

	string OutputPerTrainFileName;
	OutputPerTrainFileName = OutputPerTrainFileName + OutputFolder + "/EnergyConsumptionPerTrain.txt";
	ofstream OutputPerTrain = openRunOutputFile(OutputPerTrainFileName, ios::binary);
	OutputPerTrain << "TrainID TotEnergyConsumed[KWh] TotEnergyConsumedWithRegen[KWh] TotEnergyRequestAtSubst[KWh] TotEnergyRequestAtSubstWithRegen[KWh]\n";

	for (int i = 0; i < numTrains; i++) {
		// Printing out the Energy consumed Measure of Performance by Train
		OutputPerTrain << Trains[i].trainDescription << " " << energyMJKWh(Trains[i].TotalEnergyConsumed) << " " << energyMJKWh(Trains[i].TotalEnergyConsWithRegBrak) << " " << energyMJKWh(Trains[i].TotalEnergySubstationRequest) << " " << energyMJKWh(Trains[i].TotalEnergySubstRequestWithRegBrak) << "\n";

		TotalEnergyConsumed = TotalEnergyConsumed + Trains[i].TotalEnergyConsumed;
		TotalEnergyConsWithRegBraking = TotalEnergyConsWithRegBraking + Trains[i].TotalEnergyConsWithRegBrak;
		TotalEnergySubstationRequest = TotalEnergySubstationRequest + Trains[i].TotalEnergySubstationRequest;
		TotalEnergySubstRequestWithRegBraking = TotalEnergySubstRequestWithRegBraking + Trains[i].TotalEnergySubstRequestWithRegBrak;
	}
	// close the output file
	closeRunOutputFile(OutputPerTrain, OutputPerTrainFileName);
	// Printing the results aggregated over the entire network
	string OutputFileName;
	OutputFileName = OutputFileName + OutputFolder + "/TotalEnergyConsumption.txt";
	ofstream Output = openRunOutputFile(OutputFileName, ios::binary);

	Output << "TotalEnergyConsumed[KWh] TotalEnergyConsumedWithRegenerativeBraking[KWh] TotalEnergyRequestAtSubst[KWh] TotalEnergyRequestAtSubstWithRegBraking\n";
	Output << energyMJKWh(TotalEnergyConsumed) << " " << energyMJKWh(TotalEnergyConsWithRegBraking) << " " << energyMJKWh(TotalEnergySubstationRequest) << " " << energyMJKWh(TotalEnergySubstRequestWithRegBraking) << "\n";
	closeRunOutputFile(Output, OutputFileName);
}

// Function to Compute the Arrival and Departure times of at all the timetabling points along their own route
void Compute_TimetablingPoints_For_All_Trains(Train* Trains, int numTrains) {
	for (int i = 0; i < numTrains; i++) {
		Trains[i].ComputeTimetablingPoints();
	}
}

// Function to Print all the trajectories
void PrintTrainPathDiagram(const SimulationRun& run, Train* S, int N_S, string FolderName) {
	string FileName;
	FileName = FolderName + "/TrainPathDiagram.txt";
	ofstream FileOutput = openRunOutputFile(FileName, ios::binary);
	FileOutput << "Train/Time ";
	for (int t = 0; t < run.settings.times; t++) {
		FileOutput << t * timestep << " ";
	}
	FileOutput << "\n";
	for (int i = 0; i < N_S; i++) {
		FileOutput << S[i].trainDescription << " ";
		const auto exportCells = trajectoryExportCells(S[i].instant_spatial_position,
			S[i].earliestActiveTrajectoryIndex, S[i].End_Time);
		for (int t = 0; t < run.settings.times; t++) {
			const double position = t < static_cast<int>(exportCells.size()) ? exportCells[t] : -9999;
			if (position == -9999) {
				FileOutput << -9999 << " ";
			} else if (run.infra.routes[S[i].indexOfRoute].reversed_direction == 0) {
				FileOutput << position << " ";
			} else {
				FileOutput << run.infra.routes[S[i].indexOfRoute].OriginalRefReversedRoute - position << " ";
			}
		}
		FileOutput << "\n";
	}

	closeRunOutputFile(FileOutput, FileName);
}

// Function to print the files with the computing time of ROMA and EGTRAIN for each combination RI-PH
void Print_Computing_Times(const SimulationRun& run, string FolderName) {
	string FileName;
	FileName = FileName + FolderName + "/Computing_Times.txt";
	ofstream Comp_Times = openRunOutputFile(FileName);
	Comp_Times << "TOT_Comp_Time_EGTRAIN[s]" << " " << "TOT_Comp_Time_ROMA" << " " << "TOT_COMP_TIME" << "\n";
	Comp_Times << run.compTimeEgtrain << " " << run.compTimeRoma << " " << run.compTimeEgtrain + run.compTimeRoma << "\n";
	closeRunOutputFile(Comp_Times, FileName);
}

namespace {
// A step reports the progress of the run once in this many steps.
constexpr int kProgressEverySteps = 100;
// The margin of the ends of authority that the trains report at levels 3 and 4, in metres.
constexpr double kEndOfAuthorityMarginMetres = 50;
} // namespace

TrainMovementOrder::TrainMovementOrder(const SimulationRun& run) : dueOrder(run.numTrains) {
	std::iota(dueOrder.begin(), dueOrder.end(), 0);
	std::stable_sort(dueOrder.begin(), dueOrder.end(), [&](int a, int b) {
		return run.trains[a].departure_time < run.trains[b].departure_time;
	});
}

void runSimulationStep(SimulationRun& run, int t, TrainMovementOrder& order) {
	const InitialParameters& settings = run.settings;
	QEGTRAIN_PROFILE_SCOPE("worker/playback_step/compute", "worker", "");
	clock_t startEGTRAIN = clock(); // EGTRAIN start time
	if (t % kProgressEverySteps == 0) {
		EG_LOG(AppRun, Info) << "\r Time of simulation is " << t;
	}
	// Within the step the trains reuse the results that their TrainStepCache holds of this step and the step before
	for (Train& train : run.trains)
		train.stepCache.beginStep(t);

	{
		QEGTRAIN_PROFILE_SCOPE("worker/playback_step/compute/passenger_entry_platform_refresh", "worker",
			"worker/playback_step/compute");
		// The passengers whose journey starts enter the network; with the passenger GUI the waiting lists of the platforms are refreshed
		enterPassengersAndRefreshPlatforms(run, t);
	}

	{
		QEGTRAIN_PROFILE_SCOPE("worker/playback_step/compute/train_movement", "worker",
			"worker/playback_step/compute");
		// Simulate train movement at each simulation step
		// Every train keeps its place in the order of the trains of the run, except that the trains that are due and still
		// waiting to enter take the places of those trains in the order in which they are due. Of several trains
		// waiting at the entry of a route the one due first then enters first.
		std::vector<int>& movementOrder = order.movementOrder;
		movementOrder.resize(run.numTrains);
		std::iota(movementOrder.begin(), movementOrder.end(), 0);
		const auto dueAndWaiting = [&](int j) { return !run.trains[j].CanEnter && t >= run.trains[j].departure_time; };
		auto nextDue = order.dueOrder.begin();
		for (int& place : movementOrder) {
			if (!dueAndWaiting(place))
				continue;
			while (!dueAndWaiting(*nextDue))
				++nextDue;
			place = *nextDue++;
		}
		for (const int j : movementOrder) {
			run.trains[j].trajectoryComputationIncludingMovingBlock(t, signalCode1, signalCode2, signalCode3);
			run.trains[j].recordEarliestActiveTrajectoryIndex(t);
			run.trains[j].recordStationPassagesAtTime(t);

			// check if train arrived at destination or departed from origin
			run.trains[j].checkTrainArrDep(t);
		}
	}

	{
		QEGTRAIN_PROFILE_SCOPE("worker/playback_step/compute/train_passenger_state_payload", "worker",
			"worker/playback_step/compute");
		// The interaction of trains and passengers, with the passenger-dependent dwell times at the platforms
		simulateTrainPassengerInteractions(run, t);
	}

	{
		QEGTRAIN_PROFILE_SCOPE("worker/playback_step/compute/infrastructure_signalling_cleanup", "worker",
			"worker/playback_step/compute");
		run.etcsMa.clear(); // Clear the list containing all the Movement Authorities given to the trains at the previous instant

		Occupy_Block_Sections_Of_Route(run, t); // Fill in the lists Blocks_Occupied and BlocksConnected

		// Occupy failed sections and give them an End of Authority so both
		// aspect-driven and moving-block trains react to the incident
		Apply_Signal_Failures_Mixed_Signalling(run, t);

		// Held single-track sections give trains of the other direction an End of Authority at levels 3 and 4
		Apply_Single_Track_Authorities_Mixed_Signalling(run);

		// Only levels 3 and 4
		ReportAllTrainPositionsToRBC(run, t, kEndOfAuthorityMarginMetres);

		// function to protect all station areas
		protectStationAreas(run, t);

		releaseMixedSignallingSystem(run); // Release Blocks connected with the one really occupied by a train

		activateMixedSignallingSystem(run); // Apply the rules of the signalling system for all the Blocks contained

		unlockDoubleSwitches(run); // unlock double switches (otherwise trains stop in the middle of double switches)

		run.blocksOccupied.clear();	 // Clear the list of the occupied blocks
		run.blocksConnected.clear(); // Clear the list of the connected blocks
	}
	for (Train& train : run.trains)
		train.stepCache.endStep();

	clock_t endEGTRAIN = clock();																	// variable that sets the time in which EGTRAIN ends
	run.compTimeEgtrain = run.compTimeEgtrain + double(endEGTRAIN - startEGTRAIN) / CLOCKS_PER_SEC; // computing the cumulated computation time of EGTRAIN
}

namespace {
// The blocking times of a run: the setup time, the release time before the buffer time is added, the sight and reaction
// time and the absolute running time supplement in seconds, and the safety margin in metres.
constexpr double kBlockingSetupTimeSeconds = 5;
constexpr double kBlockingReleaseTimeSeconds = 3;
constexpr double kBlockingSightReactionTimeSeconds = 0.5;
constexpr double kBlockingRunningTimeSupplementSeconds = 0;
constexpr double kBlockingSafetyMarginMetres = 50;
} // namespace

bool runSimulationPostprocessing(SimulationRun& run, const std::function<bool()>& stopRequested) {
	const InitialParameters& settings = run.settings;
	const std::string trajectoryFolder = settings.OutputMainFolder + "/TrainTrajectories";

	ComputeEnergyConsumptionForAllTrains(run.trains.data(), run.numTrains);
	ComputeTimetableEnergyConsumption(run.trains.data(), run.numTrains, settings.OutputMainFolder);

	if (stopRequested())
		return false;

	// sorting recorded events of all infrastructure elements in chronological order
	SortRecordedEventsForAllInfrastructureElements(run.infra.infraEvents);

	if (stopRequested())
		return false;

	// Compute passage times at timetable points
	Compute_TimetablingPoints_For_All_Trains(run.trains.data(), run.numTrains);

	// Calculate Train Delays
	calculateArrivalDelayAllTrains(run);
	calculateDelayStatsForAllStations(run);
	calculateDelayStatsAtFinalStops(run); // Computing the delay stats at the final station of all the trains
	Compute_Input_Delays(run);			  // Computing the amount of entrance delays and disturbances set in input in the scenario
										  // Print Delay at stations in the right folder
	Print_Station_Delay_Stats(run, trajectoryFolder, "pos");

	// Print Out Passenger Delays
	printPassengerTotalJourneyDelay(run.passengers, settings.OutputMainFolder + "/PassengerStatus");
	printPassengerResultFiles(run, settings.OutputMainFolder + "/PassengerStatus");

	// Calculating positive and negative Train Delays to debug the program
	calculatePosAndNegArrivalDelayAllTrains(run);
	calculatePosAndNegDelayStatsForAllStations(run);
	calculatePosAndNegDelayStatsAtFinalStops(run); // Calculating the pos and neg Delay stats at final station of all trains
												   // Print aggregated results of positive and negative delays at stations
	Print_Station_Delay_Stats(run, trajectoryFolder, "pos&neg");

	Print_Computing_Times(run, trajectoryFolder); // Printing the total computation time of ROMA and EGTRAIN


	if (stopRequested())
		return false;

	EG_LOG(AppRun, Info) << "Computing blocking times...";

	ComputeBlockingTimesInMixedSignallingForAllTrains(run, kBlockingSetupTimeSeconds, kBlockingReleaseTimeSeconds + settings.bufferTime,
		kBlockingSightReactionTimeSeconds, kBlockingSafetyMarginMetres, trajectoryFolder, kBlockingRunningTimeSupplementSeconds,
		settings.recoveryTimePercentage); // Computing Blocking Times in mixed signalling Areas

	// The dense path diagram is part of the detailed trajectories.
	if (settings.exportDetailedTrajectories)
		PrintTrainPathDiagram(run, run.trains.data(), run.numTrains, trajectoryFolder);

	PrintTimetablePoints(run, trajectoryFolder);

	// print trajectories
	// before in Regional destructor - moved here because vectors are deleted automatically in the destructor and it is no longer possible to use them there
	if (stopRequested())
		return false;
	// The per-train files and their folder are part of the detailed trajectories.
	if (settings.exportDetailedTrajectories) {
		const std::string temporaryFolder = settings.OutputMainFolder + "/TEMP";
		std::error_code error;
		std::filesystem::create_directories(pathFromUtf8(temporaryFolder), error);
		if (error)
			throw std::runtime_error("Cannot create the folder " + temporaryFolder + ": " + error.message());
		for (int i = 0; i < run.numTrains; i++) {
			if (stopRequested())
				return false;
			run.trains[i].PrintTrajectory();
		}
	}
	return true;
}
