#include "app/DispatchController.h"
#include "io/RailMLParser.h"
#include "scene/SceneValidator.h"
#include "simulation/Simulation.h"
#include "simulation/SimulationWorker.h"
#include "util/portability.h"  // localtime_r shim on MSVC
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <thread>
#include <chrono>
#include <memory>
#include <map>

namespace {
void ensureDirectory(const string& path) {
	if (!path.empty()) {
		std::filesystem::create_directories(path);
	}
}

GuiSimulationSnapshot buildGuiSimulationSnapshot(int timestep) {
	GuiSimulationSnapshot snapshot;
	snapshot.timestep = timestep;
	snapshot.totalTimesteps = initial_variables.times;

	snapshot.trains.reserve(static_cast<std::size_t>(numRegions));
	for (int i = 0; i < numRegions; ++i) {
		Train& train = regional_train[i];
		GuiTrainState state;
		state.index = i;
		state.id = train.ID;
		state.type = train.type;
		state.description = train.trainDescription;
		state.routeIndex = train.indexOfRoute;
		const bool validRoute = train.indexOfRoute >= 0 && train.indexOfRoute < static_cast<int>(train_route.size());
		state.reversedDirection = validRoute ? train_route[train.indexOfRoute].reversed_direction : false;
		state.wagonCount = static_cast<int>(train.number_of_wagons);
		state.length = train.train_length;
		state.departureTime = static_cast<int>(train.departure_time);
		state.outOfSimulation = train.OutOfSimulation;
		state.speedKmh = train.speedKmhAt(timestep);
		state.currentOnboardPassengers = train.Current_OnBoard_Passengers;
		state.maxOnboardPassengers = train.MAX_OnBoard_Passengers;

		if (validRoute && timestep >= 0 && timestep < static_cast<int>(train.instant_spatial_position.size())) {
			state.routeAxisPosition = train.instant_spatial_position[static_cast<std::size_t>(timestep)];
			state.wagonHeadPositions.reserve(static_cast<std::size_t>(state.wagonCount + 1));
			state.wagonTailPositions.reserve(static_cast<std::size_t>(state.wagonCount + 1));
			for (int wagon = 0; wagon <= state.wagonCount; ++wagon) {
				state.wagonHeadPositions.push_back(train.trainXPosition(timestep, wagon));
				state.wagonTailPositions.push_back(train.trainXPosition(timestep, wagon + 1));
			}
		}

		if (guiTrainPublishesOccupiedArcs(state)) {
			const int arcCount = std::min(train.Bs.total_arcs,
				static_cast<int>(sizeof(train.Bs.arcs_in_signalling_block_section)
					/ sizeof(train.Bs.arcs_in_signalling_block_section[0])));
			for (int arc = 0; arc < arcCount; ++arc) {
				state.occupiedArcs.push_back({train.Bs.trackLineId,
					train.Bs.arcs_in_signalling_block_section[arc].startNode.X});
			}
		}
		snapshot.trains.push_back(std::move(state));
	}

	auto appendSignal = [&snapshot](const std::string& id, int code, bool reversed) {
		if (!id.empty())
			snapshot.signalStates.push_back({id, code, reversed});
	};
	std::map<std::string, GuiSectionState> sectionStates;
	const int routeCount = std::min(N_Routes, static_cast<int>(train_route.size()));
	for (int routeIndex = 0; routeIndex < routeCount; ++routeIndex) {
		const Route& route = train_route[routeIndex];
		for (int sectionIndex = 0; sectionIndex < route.N_Block_Sections; ++sectionIndex) {
			const Section& section = route.sequence_of_block_sections[sectionIndex];
			const std::string& id = section.ID;
			if (!id.empty()) {
				auto& state = sectionStates[id];
				state.sectionId = id;
				state.prepared = state.prepared || section.code != 0.0;
			}
			if (id.find('/') == std::string::npos) {
				appendSignal(id, static_cast<int>(section.code), route.reversed_direction);
				continue;
			}
			const std::size_t first = id.find("@-");
			const std::size_t middle = id.find("/@", first == std::string::npos ? 0 : first + 1);
			const std::size_t last = id.find("@-", middle == std::string::npos ? 0 : middle + 1);
			if (first != std::string::npos && middle != std::string::npos && last != std::string::npos) {
				appendSignal(id.substr(0, first + 1), static_cast<int>(section.code), route.reversed_direction);
				appendSignal(id.substr(middle + 1, last - middle), static_cast<int>(section.code), route.reversed_direction);
			}
		}
	}
	for (const SimulationIncident& incident : simulationIncidents) {
		if (incident.type != "signal_failure"
			|| timestep < incident.startSeconds || timestep > incident.endSeconds)
			continue;
		for (const std::string& id : incident.resolvedSectionIDs) {
			if (id.empty())
				continue;
			auto& state = sectionStates[id];
			state.sectionId = id;
			state.blocked = true;
		}
	}
	snapshot.sectionStates.reserve(sectionStates.size());
	for (auto& entry : sectionStates)
		snapshot.sectionStates.push_back(std::move(entry.second));

	for (const StationPlatform& platform : AllStationPlatforms) {
		GuiPlatformState state;
		state.stationId = platform.StationID;
		state.platformId = platform.ID;
		state.maxVolume = platform.Max_Passenger_Volume;
		for (const auto& passenger : platform.Current_List_Pax_On_Platform)
			state.passengerIds.push_back(passenger.first);
		for (const Passenger& passenger : AllDailyPassengers) {
			if (!passenger.IsIntheNetwork && passenger.TimeExitedTheNetwork > 0 &&
				timestep >= passenger.TimeExitedTheNetwork && timestep <= passenger.TimeExitedTheNetwork + 5 &&
				platform.StationID == passenger.StationExitedTheNetworkID &&
				platform.ID == passenger.PlatformExitedTheNetworkID)
				state.passengerIds.push_back(passenger.ID);
		}
		snapshot.platforms.push_back(std::move(state));
	}

	for (const Passenger& passenger : AllDailyPassengers) {
		GuiPassengerState state;
		state.id = passenger.ID;
		state.status = passenger.CurrentStatus;
		state.waitingPlatform = passenger.Current_WaitingStationPlatformID;
		state.nextTrain = passenger.Current_Train_To_Wait;
		const auto journey = std::find_if(passenger.Journeys.begin(), passenger.Journeys.end(),
			[&passenger](const Journey& value) { return value.ID == passenger.current_JourneyID; });
		if (journey != passenger.Journeys.end()) {
			const auto trip = std::find_if(journey->Trips.begin(), journey->Trips.end(),
				[&passenger](const Trip& value) { return value.TripID == passenger.current_TripID; });
			if (trip != journey->Trips.end())
				state.nextDestination = trip->Arr_Station_ID;
		}
		snapshot.passengers.push_back(std::move(state));
	}

	const std::size_t messageCount = std::min({VCmsgTimestep.size(), VCmsgTrain.size(), VCmsgText.size()});
	for (std::size_t i = 0; i < messageCount; ++i) {
		if (timestep >= VCmsgTimestep[i] && timestep <= VCmsgTimestep[i] + 40)
			snapshot.virtualCouplingMessages.push_back({VCmsgTimestep[i], VCmsgTrain[i], VCmsgText[i]});
	}
	return snapshot;
}
}

string Folder_RI_PH;

//-------

extern Logger owl;
// simulation object (global variable)
DispatchController simulation;

std::vector<SceneDiagnostic> DispatchController::prepareScene(const SceneModel& scene,
		const std::string& selectedScenarioId, const SceneRunSelection& selectedOccurrences) {
	resetState();
	std::vector<SceneDiagnostic> diagnostics = validateRunnableScene(scene, selectedOccurrences);
	if (hasErrors(diagnostics))
		return diagnostics;

	const std::vector<SceneDiagnostic> infrastructure =
		buildInfrastructureAndSignallingFromScene(scene);
	diagnostics.insert(diagnostics.end(), infrastructure.begin(), infrastructure.end());
	if (hasErrors(diagnostics)) {
		resetState();
		return diagnostics;
	}

	const std::vector<SceneDiagnostic> operations =
		buildOperationsFromScene(scene, selectedScenarioId, selectedOccurrences);
	diagnostics.insert(diagnostics.end(), operations.begin(), operations.end());
	if (hasErrors(diagnostics)) {
		resetState();
		return diagnostics;
	}
	if (initial_variables.RChoice) {
		const bool hasUsablePassengerJourney = std::any_of(AllDailyPassengers.begin(), AllDailyPassengers.end(),
			[](const Passenger& passenger) { return !passenger.Journeys.empty(); });
		if (!hasUsablePassengerJourney) {
			diagnostics.push_back({SceneSeverity::Error, "scene.route_choice.passengers.none",
				"Route choice requires at least one usable passenger journey in the prepared run",
				"passengers.json", "passenger", {}, "passengers", {},
				"Add a passenger journey within the selected run"});
			resetState();
			return diagnostics;
		}
	}

	if (initial_variables.OutputMainFolder.empty())
		initial_variables.OutputMainFolder = "Output";
	ensureDirectory(initial_variables.OutputMainFolder);
	ensureDirectory(initial_variables.OutputMainFolder + "/TrainTrajectories");
	ensureDirectory(initial_variables.OutputMainFolder + "/PassengerStatus");
	ensureDirectory(initial_variables.OutputMainFolder + "/Rescheduling");
	ensureDirectory(initial_variables.OutputMainFolder + "/TEMP");
	ensureDirectory(initial_variables.OutputMainFolder + "/TrainTrajectories/RoutesGenerated");
	Folder_RI_PH = initial_variables.OutputMainFolder + "/TrainTrajectories";
	return diagnostics;
}

// Reset all native runtime state before loading another canonical scene.
void DispatchController::resetState() {
	snapshotMailbox_.take();
	resetNativeInfrastructureState();
	resetNativeOperationsState();
	BlocksOccupied.clear();
	BlocksConnected.clear();
	ETCS_MA.clear();
	AllLocations.clear();
	All_Topology_Sequences.clear();
	signalAspects.clear();
}

std::shared_ptr<const GuiSimulationSnapshot> DispatchController::takeSimulationSnapshot() {
	return snapshotMailbox_.take();
}

void DispatchController::publishSimulationSnapshot(int timestep) {
	auto snapshot = std::make_shared<const GuiSimulationSnapshot>(buildGuiSimulationSnapshot(timestep));
	if (snapshotMailbox_.publish(std::move(snapshot)))
		emit snapshotAvailable();
}

void DispatchController::runSimulation() {

	if (numRegions <= 0) {
		const std::string message = "ERROR: Cannot run simulation because zero trains were loaded.";
		eglogger << message << std::endl;
		std::cerr << message << std::endl;
		return;
	}

	cout << "\n\nSimulating Train Runs...\n\n";

	// Setting departing time of train to the scheduled one
	for (int i = 0; i < numRegions; i++) {

		regional_train[i].departure_time = regional_train[i].scheduled_departure_time;
	}

	// regional_train[1].indexOfRoute = 76;
	// lockSwitchesOnAllConnectedSections(272, 240, 30, train_route[0].sequence_of_block_sections[1], "Rooo", train_route[0].reversed_direction,"None");

	// lockSwitchesWhileTrainTraverses(15700, 15520, 30, train_route[1].sequence_of_block_sections[65], "Mambo", train_route[1], signalling_block_sections, Blocks);

	/*  This piece of commented code is to test the function to block switches in Moving Block
	   elaborateMaOnBlockSectionsWithSwitchDiv(15675, 22, train_route[1].sequence_of_block_sections[66], "Samba", train_route[1]);
	   lockSwitchesWhileTrainTraverses(15675, 15575, 22, train_route[1].sequence_of_block_sections[66], "Samba", train_route[1]);

	   elaborateMaOnBlockSectionsWithSwitchDiv(15575, 22, train_route[1].sequence_of_block_sections[65], "Samba", train_route[1]);
	   lockSwitchesWhileTrainTraverses(15675, 15575, 22, train_route[1].sequence_of_block_sections[65], "Samba", train_route[1]);*/

	// trainSimulation(signalCode1,signalCode2,signalCode3);

	Train_Simulation_Mixed_Signalling_With_Passengers(signalCode1, signalCode2, signalCode3); // Function to Launch EGTRAIN considering passenger flow simulation

	ComputeEnergyConsumptionForAllTrains(regional_train, numRegions);
	ComputeTimetableEnergyConsumption(regional_train, numRegions, initial_variables.OutputMainFolder);

	// Train_Simulation_Integration_With_ROMA(signalCode1,signalCode2,signalCode3);     //Launch EGTRAIN in a closed-loop control with ROMA*/

	clock_t StartRun = clock();

	// TrainSimulationForComputingHW(signalCode1,signalCode2,signalCode3);

	/*//Trials for braking curve
	double Brak;  Brak = regional_train[0].BrakingDistanceFastComputation_PieceWise(23.5332400554126, 0, 40506.6523452644, 41133.1868, train_route[regional_train[0].indexOfRoute].sequence_of_block_sections, train_route[regional_train[0].indexOfRoute].N_Block_Sections);
	double Brak1; Brak1= regional_train[0].BrakingDistanceFastComputation(23.5332400554126, 0, 40506.6523452644, 41133.1868, train_route[regional_train[0].indexOfRoute].sequence_of_block_sections, train_route[regional_train[0].indexOfRoute].N_Block_Sections);
	cout << "Braking distance is " << Brak << " and Classical Computed Braking distance is: " << Brak1<<"\n";
	*/

	double AccDist = 0;

	// AccDist = regional_train[0].AccelerationDistanceFastComputation(25.1688, 25.5097, 16380.6, 16449.9, train_route[regional_train[0].indexOfRoute].sequence_of_block_sections, train_route[regional_train[0].indexOfRoute].N_Block_Sections);

	// AccDist = regional_train[0].AccelerationTimeFollowingMode(20.1688, 22.5097, 16380.6, 16449.9, train_route[regional_train[0].indexOfRoute].sequence_of_block_sections, train_route[regional_train[0].indexOfRoute].N_Block_Sections);

	// cout << "Acceleration distance is " << AccDist << "\n";

	// Function to compute HW under undisturbed service conditions
	// ImprovedTrainSimulationForComputingHW(signalCode1, signalCode2, signalCode3);     //Simulating Train trajectories

	// sorting recorded events of all infrastructure elements in chronological order
	SortRecordedEventsForAllInfrastructureElements(InfraElementsList);

	cout << "Computing Blocking Times....\n";

	// TODO : the following breaks in French case
	// ComputeEnergyConsumptionForAllTrains(regional_train, numRegions);         //Computing EnergyConsumption with and without regenerative braking

	clock_t EndRun = clock();

	double TimeElapsed = 0;

	TimeElapsed = (double)((EndRun - StartRun) / CLOCKS_PER_SEC);
	cout << "TimeElapsed is : " << TimeElapsed;

	// regional_train[0];
	// PrintCompressedTrainPathDiagramTrial(regional_train, numRegions, -171, Folder_RI_PH);

	// PrintTrainPathDiagramToDebug(regional_train, numRegions, Folder_RI_PH);

	// for (int i = 0; i < regional_train[0].numStations; i++) {
	//	cout << regional_train[0].Stations[i].stationName << " " << regional_train[0].Stations[i].dwellTime << "\n";
	// }

	for (int i = 0; i < numRegions; i++)
		regional_train[i].PrintTrajectory();


	Print_Implemented_Order_For_All_OL(Folder_RI_PH);

	// Compute passage times at timetable points
	Compute_TimetablingPoints_For_All_Trains(regional_train, numRegions);

	// Calculate Train Delays
	calculateArrivalDelayAllTrains();
	calculateDelayStatsForAllStations();
	calculateDelayStatsAtStation(Final_Station); // Computing the delay stats at the final station of all the trains
	Compute_Input_Delays();							 // Computing the amount of entrance delays and disturbances set in input in the scenario
							// Print Delay at stations in the right folder
	Print_Station_Delay_Stats(Folder_RI_PH, "pos");

	// Print Out Passenger Delays
	printPassengerTotalJourneyDelay(AllDailyPassengers, initial_variables.OutputMainFolder + "/PassengerStatus");

	for (int j = 0; j < numRegions; j++) {
		eglogger << "Station delay: "
				 << (regional_train[j].numStations > 0
						 ? std::to_string(regional_train[j].StationDelay[regional_train[j].numStations - 1])
						 : "unavailable (no stops)")
				 << std::endl;
	}

	// Calculating positive and negative Train Delays to debug the program
	calculatePosAndNegArrivalDelayAllTrains();
	calculatePosAndNegDelayStatsForAllStations();
	calculatePosAndNegDelayStatsAtStation(Final_Station); // Calculating the pos and neg Delay stats at final station of all trains
																 // Print aggregated results of positive and negative delays at stations
	Print_Station_Delay_Stats(Folder_RI_PH, "pos&neg");

	Print_Computing_Times(Folder_RI_PH); // Printing the total computation time of ROMA and EGTRAIN

	// Printing the trajectories on an Excel Chart and saving them on a PNG image file
	/*Print_Trajectories_As_Image(InstanceName,0,0);*/


	cout << "\n\n Computing Train Blocking Times....\n\n";

	// ComputeBlockingTimesForAllTrains("ETCS2", 7,3,10,Folder_RI_PH,0,0);     //Computing Blocking Time in  ETCS level 2

	// ComputeBlockingTimesETCS3ForAllTrains(7,3,10,Folder_RI_PH, 0,5);     //Computing Blocking Time in ETCS level 3

	// DEVO TESTARE LA NUOVA FUNZIONE PER CONNETTERE I BLOCKING TIMES. PER FARE CIO METTI AL PRIMO TRENO LA ROUTE NUMERO 10 instant_train_energy_consumption CONTROLLA CHE AGLI SWITCH MULTIPLI LA COSA FUNZIONA BENE, CONTROLLA ANCHE CHE IL SETUPTIME TIME APPLICA SOLO SE LO SWITCH NON ERA STATO IMPOSTATO DA UN TRENO PRECEDENTE

	ComputeBlockingTimesInMixedSignallingForAllTrains(5, (3 + bufferTime), 0.5, 50, Folder_RI_PH, 0, recoveryTimePercentage); // Computing Blocking Times in mixed signalling Areas

	PrintTrainPathDiagram(regional_train, numRegions, Folder_RI_PH);

	PrintTrainBlockingTimes(Folder_RI_PH);

	PrintTimetablePoints(Folder_RI_PH);

	// Free this line if you want to compute the headway for each block section
	// ComputeHwMatrixForAllTrainsWithGivenOrder(regional_train,numRegions,Folder_RI_PH,TrainEntranceOrder);

	// print trajectories
	// before in Regional destructor - moved here because vectors are deleted automatically in the destructor and it is no longer possible to use them there
	for (int i = 0; i < numRegions; i++) {
		regional_train[i].PrintTrajectory();
	}
	std::cout << "\n End of Simulation";
}

void DispatchController::Train_Simulation_Mixed_Signalling_With_Passengers(double v1, double v2, double v3) {
	nlohmann::json jsmsg;

	for (int t = 0; t < initial_variables.times; t++) {
		// pause/stop/speed from GUI
		if (auto* sw = SimulationWorker::active()) {
			while (sw->isPauseRequested() && !sw->isStopRequested())
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			if (sw->isStopRequested())
				break;
			if (int delay = sw->delayMs())
				std::this_thread::sleep_for(std::chrono::milliseconds(delay));
		}
		clock_t startEGTRAIN = clock(); // EGTRAIN start time
		std::cout << "\r Time of simulation is " << t;
		// logger.Log(" clock at" + to_string(t));

		// you can comment it if you are simulating something different
		/*if (t < 430)
			regional_train[0].max_train_speed = 16;
		else regional_train[0].max_train_speed = 33.61;*/

		// Simulate entrance process of passengers on the railway network according to route choice
		checkJourneyStartForAllPassengers(t, initial_variables.startingSimulationTime, AllDailyPassengers);

		// This function will update the list of all waiting passengers at all platforms in the network at every instant
		// To reduce the computation time it is instead recommended that the function to update the list of waiting passengers at platform is only used in the functional "Simulate_Train_Passenger_Interaction"
		// In that case  please comment the line below and uncomment the corresponding function Update_List_Passengers_Waiting_At_Platform in that function
		if (initial_variables.PAX_GUI) {
			Update_List_Passengers_Waiting_At_ALL_Platforms(AllStationPlatforms, AllDailyPassengers);
		}

		// Simulate train movement at each simulation step
		for (int j = 0; j < numRegions; j++) {
			// cout << j << "++++----/n";

			// Rigos added this to see if it works
			// regional_train[j].Trajectory_Block_Section_Free_Flow(t, v1, v2, v3);
			// The one that run Rafael is the following
			regional_train[j].trajectoryComputationIncludingMovingBlock(t, v1, v2, v3); // originally we shall call the function Trajectory_Block_Section_Free_Flow
			regional_train[j].recordEarliestActiveTrajectoryIndex(t);
			regional_train[j].recordStationPassagesAtTime(t);

			// check if train arrived at destination or departed from origin
			regional_train[j].checkTrainArrDep(j, t);
		}

		// cout << "Time is : " << t << endl;
		// for (int n = 0; n < numRegions; n++) {

		//	cout << "Train is " << regional_train[n].trainDescription << " at x: " << regional_train[n].trainXPosition(t) <<" and speed: " <<regional_train[n].instant_train_speed[t] * 3.6 <<" km/h  "<< endl;
		//}

		// Here we prepare the Traffic State data
		for (int n = 0; n < numRegions; n++) {

			// The interaction Trains and passengers and corresponding dwell time dependency at platform is simulated here in a non multi-thread fashion
			// Simulating interaction with the passengers and changing dwell times according to the chosen passenger-dependent dwell time function
			Simulate_Train_Passenger_Interactions(t, initial_variables.startingSimulationTime, regional_train[n], AllDailyPassengers, AllStationPlatforms);

			// round to 2 decimals
			// cout << "n=" << n << " and train desc is " << regional_train[n].trainDescription << "\n";
			double xPosition = std::ceil(regional_train[n].trainXPosition(t) * 100.0) / 100.0;
			double trainSpeed = std::ceil((regional_train[n].instant_train_speed[t] * 3.6) * 100.0) / 100.0;
			jsmsg["trains"][regional_train[n].trainDescription]["km-point"] = xPosition;
			jsmsg["trains"][regional_train[n].trainDescription]["speed"] = trainSpeed;

			jsmsg["trains"][regional_train[n].trainDescription]["BlockOccupied"] = regional_train[n].Bs.ID;
			jsmsg["trains"][regional_train[n].trainDescription]["lastOccTime"] = regional_train[n].ComputeLastOccupationTime_real_time(t, regional_train[n].Bs.ID, 10);
			jsmsg["trains"][regional_train[n].trainDescription]["direction"] = train_route[regional_train[n].indexOfRoute].reversed_direction;

			for (int j = 0; j < regional_train[n].Bs.total_arcs; j++) {
				if ((xPosition < regional_train[n].Bs.arcs_in_signalling_block_section[j].endNode.tdsbGeoCoordX * 1000) && (xPosition >= regional_train[n].Bs.arcs_in_signalling_block_section[j].startNode.tdsbGeoCoordX * 1000)) { // Selection of the right Arc of the Block Section
					owl << "99Train " << regional_train[n].trainDescription << " is in blocksection " << regional_train[n].Bs.ID << " so it is in TDS " << regional_train[n].Bs.arcs_in_signalling_block_section[j].startNode.tdsbId << " and next is " << regional_train[n].Bs.arcs_in_signalling_block_section[j].endNode.tdsbId << std::endl;
					owl << "+++++" << xPosition << regional_train[n].Bs.arcs_in_signalling_block_section[j].startNode.tdsbGeoCoordX << " , " << regional_train[n].Bs.arcs_in_signalling_block_section[j].endNode.tdsbGeoCoordX << std::endl;
				}
			}
			jsmsg["trains"][regional_train[n].trainDescription]["depTime"] = regional_train[n].departure_time;
			if (regional_train[n].departure_time <= t) {
				char c = '-';
				int index = regional_train[n].Bs.ID.find(c);

				jsmsg["trains"][regional_train[n].trainDescription]["trackID"] = regional_train[n].Bs.ID.substr(index + 1, regional_train[n].Bs.ID.length() - index - 2);
				jsmsg["trains"][regional_train[n].trainDescription]["inArea"] = 1;
			} else
				jsmsg["trains"][regional_train[n].trainDescription]["inArea"] = 0;

			// train_route[regional_train[n].indexOfRoute].sequence_of_block_sections[train_route[regional_train[n].indexOfRoute].N_Block_Sections-1].ID ;
			// regional_train[n].direction; // probably this is the direction
			// regional_train->TimetablePoints
			//

			// for (auto it = jsmsg["trains"].begin(); it != jsmsg["trains"].end(); ++it)
			//{
			//	std::cout << it.key() << " : " << it.value() << std::endl;
			// }
			// regional_train[n].BlockTime
			// td::cout<<"train desc is " << regional_train[n].trainDescription <<" Bs: "<< regional_train[n].Bs.ID << "\n";
			// regional_train[n].ComputeBlockingTimesInMixedSignallingAreas(5, (3 + bufferTime), 0.5, 50, 0,1);
			// std::cout << "StartOccTime " << regional_train[n].trainDescription << " Start: " <<  regional_train[n].BlockTime[regional_train[n].N_BlockTimeComplete].StartOccTime << "\n";
			// std::cout << "EndOccTime " << regional_train[n].trainDescription << " End: " << regional_train[n].BlockTime[regional_train[n].N_BlockTimeComplete].EndOccTime << "\n";
			// regional_train[n].ComputeBlockingTimeForSingleLocation
			// train_route[n].sequence_of_block_sections
			// std::cout << regional_train[n].Bs.arcs_in_signalling_block_section->startNode.ID << "<<<<---- - \n\n";
			// regional_train[n].ComputeBlockingTimes("Conventional", 5, (3 + bufferTime), 0.5, 50, 0);
			// std::cout << "train desc222 is " << regional_train[n].BlockTime[9].StartOccTime;
			// std::cout << xPosition<<"<<<<-----\n\n";

			int Previous_Block_Index = 0;
			Section SBs;
			if (t > 0 && t >= regional_train[n].departure_time) {
				for (int h = 0; h < train_route[regional_train[n].indexOfRoute].N_Block_Sections; h++) {
					if ((regional_train[n].instant_spatial_position[t - 1] < train_route[regional_train[n].indexOfRoute].sequence_of_block_sections[h].end_node.X * 1000) &&
						(regional_train[n].instant_spatial_position[t - 1] >= train_route[regional_train[n].indexOfRoute].sequence_of_block_sections[h].start_node.X * 1000)) {
						SBs = train_route[regional_train[n].indexOfRoute].sequence_of_block_sections[h];

						if (h == 0)
							Previous_Block_Index = h;
						else {
							Previous_Block_Index = h - 1;
							break;
						}
						// regional_train[n].ComputeBlockingTimeForSingleLocation_real_time(Previous_Block_Index, "Conventional", regional_train[n].scheduled_departure_time, 5,(3 + bufferTime), 0.5, 0,1);
					}
					// if ((regional_train[n].instant_spatial_position[t - 1] <= train_route[regional_train[n].indexOfRoute].sequence_of_block_sections[Previous_Block_Index].start_node.X * 1000) && (regional_train[n].instant_spatial_position[t] > train_route[regional_train[n].indexOfRoute].sequence_of_block_sections[Previous_Block_Index].start_node.X * 1000)) {
					// BlockTime[N_BlockSections].StartRunTime = t - 1;
					//	std::cout << "Occupation itme : " << t - 1;

					//	}
				}
			}
		}

		// Print Passenger Status after the simulation
		printCurrentPassengerStatus(t, initial_variables.startingSimulationTime, AllDailyPassengers, (initial_variables.OutputMainFolder + "/PassengerStatus"));

		jsmsg["time"] = t;

		// for the ZeroMQbroker
		if (initial_variables.TSM) {
			const std::string xml = trafficStateMonitoring_xml(jsmsg);
			std::cout << "\n\n Sending the following Traffic State XML file" << std::endl
					  << xml << std::flush;
			send_external_state(jsmsg, xml, "tcp://127.0.0.1:5555");
		}

		if (initial_variables.RChoice) {
			const nlohmann::json route_choice_json = routeChoicePayload(AllDailyPassengers, t);
			const std::string xml = routeChoice_xml(route_choice_json);
			std::cout << "\n\n Sending the following Route Choice XML file" << std::endl;
			std::cout << xml << std::flush;
			send_external_state(route_choice_json, xml, "tcp://127.0.0.1:5556");
		}

		ETCS_MA.clear(); // Clear the list containing all the Movement Authorities given to the trains at the previous instant

		Occupy_Block_Sections_Of_Route(t); // Fill in the lists Blocks_Occupied and BlocksConnected

		// Occupy failed sections and give them an End of Authority so both
		// aspect-driven and moving-block trains react to the incident
		Apply_Signal_Failures_Mixed_Signalling(t);

		// Only for level>=3
		ReportAllTrainPositionsToRBC(t, 50);

		// Predict_And_Check_Decoupling_MA_For_All_Train_in_Following_Mode(t);  // Predict and check the Predict_MA_To_DecoupleAt for all trains which are in following mode

		// function to protect all station areas
		protectStationAreas(t);

		releaseMixedSignallingSystem(); // Release Blocks connected with the one really occupied by a train

		activateMixedSignallingSystem(); // Apply the rules of the signalling system for all the Blocks contained

		unlockDoubleSwitches(); // unlock double switches (otherwise trains stop in the middle of double switches)

		for (int i = 0; i < numRegions; i++) {
			regional_train[i].unlockSingleTrack(
				train_route[regional_train[i].indexOfRoute].sequence_of_block_sections,
				train_route[regional_train[i].indexOfRoute].N_Block_Sections,
				t);
		} // unlock occupied single tracks

		// update output signalling file with aspect changes
		// saveSignalAspectChanges(t, "Input_EGTRAIN/Rescheduling");

		// showElementInEtcsMa(t);   //Printing the MAs
		/*showElement(t,BlocksOccupied);*/
		BlocksOccupied.clear();	 // Clear the list BlocksOccupied
		BlocksConnected.clear(); // Clear the list BlocksConnected

		/*debugFunctionBlockCodes(t,"@2-B2@-1.314000/@3-B0@-1.339000",train_route[0]);*/

		Detect_Implemented_Order_For_All_OL(); // Detect The order Implemented for all the OLs in the network

		clock_t endEGTRAIN = clock();																// variable that sets the time in which EGTRAIN ends
		Comp_Time_EGTRAIN = Comp_Time_EGTRAIN + double(endEGTRAIN - startEGTRAIN) / CLOCKS_PER_SEC; // computing the cumulated computation time of EGTRAIN

		publishSimulationSnapshot(t);
	}
}

void DispatchController::printLastTrainServicePathDiagram() {
	// print last services
	for (int i = 0; i < numRegions; i++) {
		// print only trains running during simulation (departure_time >= initial_variables.times indicate that trains were not used, e.g. if only sprinters were used, IC are not printed)
		if (regional_train[i].departure_time < initial_variables.times) {
			if (!regional_train[i].OutOfSimulation) {
				TrajectoryEndTimeOverride temporaryEnd(regional_train[i].End_Time, initial_variables.times - 1);
				regional_train[i].printTrainServicePathDiagram(initial_variables.OutputMainFolder + "/TrainTrajectories", -1); // -1 indicates no next service
			} else {
				regional_train[i].printTrainServicePathDiagram(initial_variables.OutputMainFolder + "/TrainTrajectories", -1);
			}
		}
	}
}

void DispatchController::setVectorSizesFromInput(int vec_size) {
	for (int i = 0; i < numRegions; i++) {
		// define vector sizes with length of simulation from user input
		regional_train[i].setTrainVectorSizesFromInput(vec_size);
	}
}
