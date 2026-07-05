#include "app/DispatchController.h"
#include "simulation/SimulationWorker.h"
#include "util/portability.h"  // localtime_r shim on MSVC
#include <filesystem>
#include <regex>
#include <thread>
#include <chrono>

namespace {
void ensureDirectory(const string& path) {
	if (!path.empty()) {
		std::filesystem::create_directories(path);
	}
}
}

// global vars
string TTOrderName;
string TrackLinesName;
string ConnectName, stationName;
// string TrainType1, TrainType2, TrainType3, TrainType4, TrainType5, TrainType6;
string TrainType7, TrainType8, TrainType9, TrainType10, TrainType11, TrainType12, TrainType13, TrainType14;
string TrainType15, TrainType16;
string FileNamesROMA, DWDisturbance, EntDelays;
string Folder_RI_PH;

//-------

extern Logger owl;
// simulation object (global variable)
DispatchController simulation;

void DispatchController::setupEgtrain() {

	/*if (argc <=1) cout<<"ERROR:Only FILENAME SPECIFIED, you need to specify also parameters Informing Interval (s), PH (s), InstanceName, InstanceIndex, DelayDispatchImpl (s)/n/n";

	else{//Assign the right values ot parameters
	Resched_Interval=atoi(argv[1]); PH=atof(argv[2]);  InstanceName=argv[3];  InstanceIndex=atoi(argv[4]); DelayDispatcherImpl=atoi(argv[5]);}*/

	/*OL[1].numTeList=3;
	OL[1].TE[0].trainDescription="1D6000"; OL[1].TE[1].trainDescription="1B3500"; OL[2].TE[3].trainDescription="1D16000";
	OL[1].TE[0].SuccessorID="1B3500"; OL[1].TE[1].SuccessorID="1D16000"; OL[1].TE[2].SuccessorID="None";

	OL[1].Implemented_Order.push_back("1B3500");  OL[1].Implemented_Order.push_back("1D6000"); OL[1].Implemented_Order.push_back("1D16000");
	OL[1].compareImplementedOrderWithRomaSolution("C:/TEMP/",1,0);*/

	/*call ROMA for generating the solution at instant t0 (i.e. the solution based only on the entrance delays when no train is entered yet in the network)*/
	/*callRoma(InstanceName,0,PH);*/

	// Initialize and execute EGTRAIN
	omp_set_num_threads(8);

	/*cout << "Please Insert Number of Track Lines: ...";
	cin >> numTrackLines;
	cout << "\n\n";

	cout << "Please Insert Number of Routes: ...";
	cin >> N_Routes;

	cout << "\n\n";

	cout << "Please Insert Total Simulation Time in seconds [the max supported is 9500 s]:...";
	cin >> times;
	cout << "\n\n";

	cout << "Specify Timetabling Parameters:\n";

	cout << "Please Insert Buffer Time in seconds:...";
	cin >> bufferTime;
	cout << "\n\n";

	cout << "Please Insert Running Time Recovery Time in [%]...";
	cin >> recoveryTimePercentage;
	cout << "\n\n";*/

	TrackLinesName = InputMainFolder + "/TrackLines";
	TTOrderName = InputMainFolder + "/TMS/Timetable Order";
	loadTrackLine((char*)TrackLinesName.c_str());

	// Loading first solution of ROMA
	/*char Resch_Int[20];   sprintf_s(Resch_Int,"%d",Resched_Interval);
	sprintf_s(Init_Time,"%d",0); sprintf_s(Pred_Hor,"%d", (int)PH);

	string Folder_Orders_At_t0=Name_Of_Integ_Folder+"Output_ROMA/instance_"+InstanceName+ "/"+Resch_Int+"-"+Pred_Hor+"/"+Init_Time+"-"+Pred_Hor;    //Folder where the order given by ROMA at instant t0 is contained

	loadAllOrderLists((char*)Folder_Orders_At_t0.c_str());       //Load ROMA Solution given at time t0*/

	extern InitialParameters initial_variables;
	ConnectName = initial_variables.InputMainFolder + "/TrackLines/Connections.txt";
	stationName = InputMainFolder + "/TrackLines/Stations.txt";

	// Open the folder dir and save the names of all trains in the file trainNames.txt
	// system("dir Input_EGTRAIN\\Trains  /b > Input_EGTRAIN\\trainNames.txt");
	string path = initial_variables.InputMainFolder;
	replace(path.begin(), path.end(), '/', '\\'); // replace all occurances of 'x' with 'y'
	string trainNames_command = "dir " + path + "\\Trains /b > " + path + "\\trainNames.txt";
	// std::cout << path << "<<<<<<<<<<<<<<<<<<<<";
	// system((char*)trainNames_command.c_str());

	FileNamesROMA = InputMainFolder + "/TMS/FileNamesROMA.txt";
	DWDisturbance = InputMainFolder + "/TimeTable/Scenarios_DW_Weibull/";
	EntDelays = InputMainFolder + "/TimeTable/Scenarios_Entrance_Delays/";

	loadConnections(numConnections, (char*)ConnectName.c_str()); // Loading switches*/

	loadStations((char*)stationName.c_str()); // Loading stations

	printStations();
	generateAllBlocksFromInputFile((char*)TrackLinesName.c_str(), signalling_block_sections, Blocks); // Generating Block sections

	// generateAllBlocksWithEquiLength (0.500, signalling_block_sections, Blocks);

	setAllConnections(); // Connecting Block sections linked by switches

	setDependenciesBetweenBlocks(); // Create occupation dependencies among connected block sections

	// set mid-signals of double switches as virtual signals
	setVirtualSignals();

	setTrackDetectionSectionBoundariesAndGeoCoordAtSwitchesAndStations(signalling_block_sections, Blocks);

	setGeoCoordinates(signalling_block_sections, Blocks);

	Load_Incidents(InputMainFolder);

	createTds(signalling_block_sections, Blocks, 3, list_of_TDS);

	for (int i = 0; i < Blocks; i++) {
		for (auto it = signalling_block_sections[i].TDS_in_block.begin(); it != signalling_block_sections[i].TDS_in_block.end(); it++) {
			cout << "TDS in Block: " << signalling_block_sections[i].ID << "are: \n";
			cout << (*it)->ID << "\n";
		}
	}

	printAllBlocksId();

	initialiseTopologySequencesForRecife(signalling_block_sections, Blocks, list_of_TDS, All_Topology_Sequences);

	list<TopologySequence>::iterator it = All_Topology_Sequences.begin();

	printInfrastructureFileForRecife(signalling_block_sections, Blocks, All_Topology_Sequences, initial_variables.OutputMainFolder);

	// load single track limits
	loadSingleTrackLimits();

	// load station boundary sections
	loadStationBoundarySections();
}

// Resets all simulation state so setupEgtrain() can be called again for a different case study.
void DispatchController::resetState() {
	// Counters
	numTrackLines = 0;
	Blocks = 0;
	N_Routes = 0;
	numRegions = 0;
	numConnections = 0;
	numStations = 0;
	N_OrderLists = 0;
	numAllStationPlatforms = 0;
	numAllDailyPassengers = 0;
	N_Train = 0;
	N_TrainD = 0;

	// String path globals (ensure clean state even if setupEgtrain accumulator fix ever reverts)
	TrackLinesName.clear();
	TTOrderName.clear();
	ConnectName.clear();
	stationName.clear();
	FileNamesROMA.clear();
	DWDisturbance.clear();
	EntDelays.clear();

	// std:: containers
	list_of_TDS.clear();
	InfraElementsList.clear();
	train_route.clear();
	AllStationPlatforms.clear();
	AllDailyPassengers.clear();
	BlocksOccupied.clear();
	BlocksConnected.clear();
	ETCS_MA.clear();
	AllLocations.clear();
	All_Topology_Sequences.clear();
	stationBoundarySections.clear();
	signalAspects.clear();
	VCmsgTimestep.clear();
	VCmsgTrain.clear();
	VCmsgText.clear();
	simulationIncidents.clear();

	// Fixed-size C arrays. Reset to default-constructed state.
	for (int i = 0; i < 268; i++)
		blockSets[i] = BlockSet();
	for (int i = 0; i < 708; i++)
		connections[i] = Connections();
	for (int i = 0; i < 6000; i++)
		signalling_block_sections[i] = Section();
	for (int i = 0; i < Max_N_Reg; i++)
		regional_train[i] = Regional();
	// stale regions/regionX from the previous case corrupt the GUI layout
	// interpolation of the next one; readStationInfo only appends
	for (int i = 0; i < 95; i++)
		StationArray[i] = Stations();
}

// prepares the simulation
void DispatchController::prepareSimulation() {

	/*******************************************************************************************************************************

					   Creating Order List for reordering Trains at main reordering locations on the infrastructure

	*******************************************************************************************************************************/
	// Test to debug the respect order list with virtual signals
	// signalling_block_sections[145].end_node.virtualSignal = true;
	// signalling_block_sections[2].start_node.virtualSignal = true;

	// Load Ordered lists of trains at junctions or nodes where reordering is possible
	N_OrderLists = 0;

	// loadAllOrderLists((char*)TTOrderName.c_str());      //Load Order given by the timetable for each checkpoint in which reordering is possible

	loadAllOrderListsUpgraded((char*)TTOrderName.c_str());

	// Set_RespectOrder_And_Index_OrderList_Upgraded_Improved();

	// Set_RespectOrder_And_Index_OrderList_Upgraded_Improved_With_JunctionType();

	// Set_OL0_as_OL1();   //Setting OL at the departure equal to the OL at the first Checkpoint where the reordering is performed

	// If you want to take dynamic decisions (i.e. trains pass as they arrive at the CPs) use this part of the code below which deletes List for OL1, 2, and 3,
	/*
	for (int i = 1; i < N_OrderLists; i++) {
		for (int j = 0; j < OL[i].numTeList; j++) {
			OL[i].TE[j].trainDescription = "None";
		}
	}
	*/

	/*//Entirely disable OL
	for(int i=0;i<N_OrderLists;i++){
	for (int j=0;j<OL[i].numTeList;j++){
	OL[i].TE[j].trainDescription="None";
	}
	}*/

	// Print OL lists on the screen
	for (int i = 0; i < N_OrderLists; i++) {
		cout << "OL " << i << "\n";
		for (int j = 0; j < OL[i].numTeList; j++) {
			cout << OL[i].TE[j].trainDescription << "\n";
		}
	}

	// Verifications on stations belonging to signalling_block_sections with SwitchDiv=1
	for (int i = 0; i < Blocks; i++) {
		if (signalling_block_sections[i].start_node.numConnections > 0) {
			eglogger << "Block Section: " << signalling_block_sections[i].ID << "has initial Node: " << signalling_block_sections[i].start_node.tdsbId << "Which is a non diverging switch or a switch" << std::endl;
		}
		if (signalling_block_sections[i].withSwitchDiv == 1) {
			for (int j = 0; j < signalling_block_sections[i].total_arcs; j++) {
				if (signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.numConnections > 0) {
					if ((signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.X != signalling_block_sections[i].XEndSwitch) && (signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.X != signalling_block_sections[i].XStartSwitch)) {
						eglogger << signalling_block_sections[i].ID << " " << " and tdsbId: " << signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.tdsbId << " and position on signalling_block_sections: " << signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.X << std::endl;
					}
				}
			}
		}
	}

	// Verifications on stations belonging to signalling_block_sections with SwitchDiv=1
	for (int i = 0; i < Blocks; i++) {
		if (signalling_block_sections[i].withSwitchDiv == 1) {
			for (int j = 0; j < signalling_block_sections[i].total_arcs; j++) {
				if (signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.stationName.empty() != 1)
					eglogger << "station " << signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.stationName << "belongs to signalling_block_sections with SwitchDiv = 1: " << signalling_block_sections[i].ID << " " << signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.stationName << " and tdsbId : " << signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.tdsbId << " and position on signalling_block_sections : " << std::to_string(signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.X) << std::endl;
			}
		}
	}

	for (int i = 0; i < Blocks; i++) {
		for (int j = 0; j < signalling_block_sections[i].total_arcs; j++) {
			if (signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.stationName.empty() != 1)
				eglogger << "station " << signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.stationName << " and platform ID is " << signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.stationPlatformId << "\n";
		}
	}

	// Verification on congruency between TDBS of signalling_block_sections whose start_node and end_node are switches
	for (int i = 0; i < Blocks; i++) {
		if (signalling_block_sections[i].start_node.numConnections > 0) {
			eglogger << "Switch at Starting Node of signalling_block_sections: " << signalling_block_sections[i].ID << " " << signalling_block_sections[i].start_node.tdsbId << " " << signalling_block_sections[i].arcs_in_signalling_block_section[0].startNode.tdsbId << std::endl;
		}

		for (int j = 0; j < signalling_block_sections[i].total_arcs; j++)
			if ((signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.numConnections > 0) && (signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.X == signalling_block_sections[i].end_node.X)) {
				eglogger << "Switch At Ending Node of signalling_block_sections: " << signalling_block_sections[i].ID << " " << signalling_block_sections[i].end_node.tdsbId << " " << signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.tdsbId << std::endl;
			}
	}

	// Verification on Station which are also switches
	for (int i = 0; i < Blocks; i++) {
		for (int j = 0; j < signalling_block_sections[i].total_arcs; j++)
			if ((signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.numConnections > 0) && (signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.stationName.empty() != 1)) {
				eglogger << signalling_block_sections[i].ID << " " << signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.tdsbId << "\n";
			}
	}

	// Verification of start_node of signalling_block_sections which coincide with XEnd of switches
	for (int i = 0; i < Blocks; i++) {
		if (signalling_block_sections[i].withSwitchDiv == 1) {
			if (signalling_block_sections[i].start_node.X == signalling_block_sections[i].XEndSwitch) {
				eglogger << "Block Section:" << signalling_block_sections[i].ID << "has Switch Ending at its starting Node\n";
			}
		}
	}

	for (int i = 0; i < signalling_block_sections[562].total_nodes; i++) {
		if (signalling_block_sections[562].nodelist_of_nodes_in_signalling_section[i].numConnections > 0) {
			// why?
			// cout << signalling_block_sections[562].ID << " " << signalling_block_sections[562].nodelist_of_nodes_in_signalling_section[i].ID << " " << signalling_block_sections[562].nodelist_of_nodes_in_signalling_section[i].numConnections << " " << signalling_block_sections[562].nodelist_of_nodes_in_signalling_section[i].X << "\n";
		}
	}

	// writeRouteFilesFromTracks(InputMainFolder +"/Routes/RouteToWrite.txt", signalling_block_sections, Blocks, InputMainFolder + "/Routes");

	// writeAllRoutes(InputMainFolder + "/RoutesToWrite", InputMainFolder + "/Routes", signalling_block_sections, Blocks);

	string FileNetworkAreas;
	FileNetworkAreas = InputMainFolder + "/TrackLines/AreasCaseStudy.txt";

	InitializeAllNetworkAreas(FileNetworkAreas, signalling_block_sections, Blocks);

	setUpAllRoutes(); // Set Up routes of trains

	// set corridor of routes
	loadRouteCorridors();

	// set virtual signals on routes
	setRouteVirtualSignals();

	loadAllJoinedRoutes(InputMainFolder + "/RoutesToWrite", train_route, N_Routes);

	// Verification of Routes
	for (int i = 0; i < N_Routes; i++) {
		verifyRouteValidity(train_route[i], i);
	}

	setListAllInfrastructureElementsFromRoutes(train_route, N_Routes);

	/*for (int i = 0; i < train_route[76].N_Block_Sections; i++) {
		for (int j = 0; j < train_route[76].sequence_of_block_sections[i].total_arcs; j++) {

			if (train_route[76].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].end_node.stationName.empty() != 1)
				cout << "Station on Route 76: " << train_route[76].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].end_node.stationName << "\n";
		}
	}*/

	// Verification on Routes TDSB

	/* Rigos I do not know why this is here
	if (train_route[5].reversed_direction == 1) {
		for (int i = 0; i < train_route[5].N_Block_Sections; i++) {
			for (int j = 0; j < train_route[5].sequence_of_block_sections[i].total_arcs; j++) {
				if (train_route[5].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.tdsbId.empty() != 1)
					cout << train_route[5].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.tdsbId << "\n";
			}
			if (i == train_route[5].N_Block_Sections - 1) {
				cout << train_route[5].sequence_of_block_sections[i].arcs_in_signalling_block_section[train_route[5].sequence_of_block_sections[i].total_arcs - 1].endNode.tdsbId << "\n";
			}
		}
	}
	*/

	/* print routes
	for (int i = 0; i < N_Routes; i++) {
		if (train_route[i].reversed_direction == 1) {
			for (int h = 0; h < train_route[i].N_Block_Sections; h++) {
				for (int m = 0; m < train_route[i].sequence_of_block_sections[h].total_arcs; m++) {
					if (train_route[i].sequence_of_block_sections[h].arcs_in_signalling_block_section[m].startNode.numConnections > 0) {
						cout << train_route[i].ID << " " << train_route[i].sequence_of_block_sections[h].ID << " " << train_route[i].sequence_of_block_sections[h].arcs_in_signalling_block_section[m].startNode.X << " " << train_route[i].sequence_of_block_sections[h].arcs_in_signalling_block_section[m].startNode.numConnections << "\n";
					}
				}
			}
		}
	}*/

	if (train_route[0].N_Block_Sections > 0) {
		for (int i = 0; i < train_route[0].N_Block_Sections; i++) {
			if (train_route[0].sequence_of_block_sections[i].ID == "@2-B1@") {
				for (int j = 0; j < train_route[0].sequence_of_block_sections[i].total_arcs; j++) {
					if (train_route[0].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].startNode.numConnections > 0)
						cout << " It is a start_node Node " << train_route[0].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].startNode.ID << " " << train_route[0].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].startNode.X << " " << train_route[0].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].startNode.numConnections << "\n";
					if (train_route[0].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.numConnections > 0)
						cout << " It is a end_node Node " << train_route[0].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.ID << " " << train_route[0].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.X << " " << train_route[0].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.numConnections << "\n";
				}
			}
		}
	}

	// Print All of the routes generated in the folder Input_EGTRAIN/RoutesGenerated
	printAllRoutes("C:/Users/hermes/Desktop/DigitalRailwayMML/Input_EGTRAIN");

	/*train_route[61].sequence_of_block_sections[28].N_ETCS3BrakingPoints=1;
	train_route[61].sequence_of_block_sections[28].ETCS3BrakingPoints[0]=14.2;*/

	// string TrainType;
	// TrainType1 = InputMainFolder + "/Trains/AltWtl.TXT";
	// TrainType2 = InputMainFolder + "/Trains/FarnWtl.TXT";

	// Loading train characteristics: Physical/Mechanical and TimeTable
	LoadAllTrainFiles(InputMainFolder);
	if (numRegions <= 0) {
		const std::string message = "ERROR: Zero trains loaded. Check trainNames.txt entries and train file paths in " + InputMainFolder;
		eglogger << message << std::endl;
		std::cerr << message << std::endl;
	}
	// loadTrainType((char*)TrainType1.c_str(), numRegions);
	// loadTrainType((char*)TrainType2.c_str(), numRegions);

	// set vector sizes used in Train class to the length of the simulation
	setVectorSizesFromInput(initial_variables.times);

	// Converting train names in the ones used by ROMA
	// nameTrainDescriptionAsRomaTool((char*)FileNamesROMA.c_str(),0);

	// Changing the departure times of the trains in the UP and DOWN Direction in order to let them fit in one hour timetable
	changeTrainDepartureTimesForHourlyTimetabling(regional_train, numRegions);

	/*Load_Dwell_Time_Disturbed_Scenario(DWDisturbance, InstanceIndex);*/
	Load_Entrance_Delay_Disturbed_Scenario(EntDelays, "Guingamp", InstanceIndex);
	Load_Entrance_Delay_Disturbed_Scenario(EntDelays, "Paimpol", InstanceIndex);

	// for (int i = 0; i < numRegions; i++) {
	//	cout << regional_train[i].trainDescription << " " << regional_train[i].indexOfRoute << "\n";
	// }

	for (int train = 0; train < numRegions; train++) {
		regional_train[train].Initialise_Gibson_Dwell_Time_Parameters(7, 0.32, 18.23, 0.564, 4.838, 22.24, 0.04, 0.562);
	}

	Initialise_All_Station_Platforms(AllStationPlatforms, numAllStationPlatforms, signalling_block_sections, Blocks, regional_train, numRegions, train_route, 100, 2.5);

	initialiseAllDailyPassengersFromDailyActivitySchedule(initial_variables.InputMainFolder + "/Passengers/DAS_FrenchCaseStudy.csv", AllDailyPassengers, numAllDailyPassengers);

	updatePassengerRouteChoice(initial_variables.InputMainFolder + "/Passengers/RouteChoiceFC_EQ1.csv", AllDailyPassengers, numAllDailyPassengers);

	assignPlatformsToTripsInJourney(AllDailyPassengers, AllStationPlatforms);

	double dwelltime = regional_train[2].computePaxDependentDwellTimeAtStations(70, 42, 0.8, 5.631, 0.000, 0.815, 0.564, 0.016, 2.012, 0.086, 0.562);


	string Folder_Instance;
	Folder_Instance = Folder_Instance + Name_Of_Integ_Folder + initial_variables.OutputMainFolder + "/" + "instance_" + InstanceName + "/"; // create folder of the instance
	ensureDirectory(Folder_Instance);
	Folder_RI_PH = Folder_RI_PH + Name_Of_Integ_Folder + initial_variables.OutputMainFolder + "/TrainTrajectories"; // Create Folder RI-PH which will collect train trajectories
	// cout << Folder_RI_PH << "\n";
	ensureDirectory(Folder_RI_PH);

	for (int i = 0; i < 200; i++) {
		if (signalling_block_sections[i].GeoXBegNode == signalling_block_sections[i].GeoXEndNode)
			break;
		else {

			for (int j = 0; j < 20; j++) {
				if (signalling_block_sections[i].arcs_in_signalling_block_section[j].length == 0)
					break;
				else {
					/*
					std::cout << "ID of Arc: " << signalling_block_sections[i].arcs_in_signalling_block_section[j].ID <<
						"length: " << std::to_string(signalling_block_sections[i].arcs_in_signalling_block_section[j].length) <<
						" startNode: " << signalling_block_sections[i].arcs_in_signalling_block_section[j].startNode.X <<
						" startNode ID : " << signalling_block_sections[i].arcs_in_signalling_block_section[j].startNode.ID <<
						"endNode: " << signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.X <<
						" endNode ID : " << signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.ID <<
						"\n\n";*/
				}
			}
		}
	}
}

// launches and runs the simulation
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

	// Train_Simulation_Mixed_Signalling(signalCode1, signalCode2, signalCode3);  //Launch EGTRAIN without ROMA

	Train_Simulation_Mixed_Signalling_With_Passengers(signalCode1, signalCode2, signalCode3); // Function to Launch EGTRAIN considering passenger flow simulation

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
		eglogger << "Station delay: " << std::to_string(regional_train[j].StationDelay[regional_train[j].numStations - 1]) << std::endl;
	}

	printTimetableGraph();
	printCommonTimetableGraph();
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

extern ZMQ_channel channel;
// Function to simulate traffic in networks with a mixed signalling system (e.g. conventional, mixed to ETCS L1, L2 and or L3)
/**Function to Simulate traffic within the observation periodand without interactions wth the traffic management system*/
void DispatchController::Train_Simulation_Mixed_Signalling(double v1, double v2, double v3) {
	// logger.Log("Starting Train_Simulation_Mixed_Signalling");
	nlohmann::json jsmsg, route_choice_json;

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
/////////////////////////////////////channel.zmq_communication(to_string(t));

// logger.Log(" clock at" + to_string(t));

/*#pragma omp parallel
{
#pragma omp for*/

// you can comment it if you are simulating something different
/*if (t < 430)
	regional_train[0].max_train_speed = 16;
else regional_train[0].max_train_speed = 33.61;*/

// print time update to file
// dispatchingTool->printTimeUpdateMsg(t, initial_variables.InputMainFolder+ "/Rescheduling");

// load dispatching decisions
// dispatchingTool->loadDispDecisions(initial_variables.InputMainFolder + "/Rescheduling", t);

// Upload for each simulation step: train characteristics
//  MULTI-THREAD
#pragma omp parallel for
		for (int j = 0; j < numRegions; j++) {
// cout << j << "++++----/n";

// only one thread writing to file at a time
#pragma omp critical(trainservicepathwrite)
			{
				// implement dispatching decisions
				//	regional_train[j].executeDispDecisions(t);
			}
			// Rigos added this to see if it works
			// regional_train[j].Trajectory_Block_Section_Free_Flow(t, v1, v2, v3);
			// The one that run Rafael is the following
			regional_train[j].trajectoryComputationIncludingMovingBlock(t, v1, v2, v3); // originally we shall call the function Trajectory_Block_Section_Free_Flow
			regional_train[j].recordStationPassagesAtTime(t);

			// Function to debug the OL list
			bool trialbool = 0;
			trialbool = regional_train[1].Train_Must_Stop_For_OL_Order(1, 0);

// only one thread writing to file at a time
#pragma omp critical(arrdepwrite)
			{
				// check if train arrived at destination or departed from origin
				regional_train[j].checkTrainArrDep(j, t);
			}
		}

		//}
		// cout << "Time is : " << t << endl;
		// for (int n = 0; n < numRegions; n++) {

		//	cout << "Train is " << regional_train[n].trainDescription << " at x: " << regional_train[n].trainXPosition(t) <<" and speed: " <<regional_train[n].instant_train_speed[t] * 3.6 <<" km/h  "<< endl;
		//}

		// Here we prepare the Traffic State data
		for (int n = 0; n < numRegions; n++) {

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
			if (t >= regional_train[n].departure_time) {
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
		jsmsg["time"] = t;

		// Here we prepare the Route choice data
		for (int n = 0; n < 100; n++) { // for all ACTIVE passengers
			std::string station_names[10] = {"Guingamp", "Gourland", "Tregonnau_Squiffiec", "Brelidy_Plouec",
											 "Pontrieux_halte", "Pontrieux", "Frynaudour", "Traou_Nez", "Lancerf", "Paimpol"};
			int random_station = rand() % 10;
			int random_passenger = rand() % 1000;

			route_choice_json["passengers"][std::to_string(random_passenger) + "--1.0"]["origin"] = station_names[random_station];
			route_choice_json["passengers"][std::to_string(random_passenger) + "--1.0"]["destination"] = station_names[rand() % 10];
			route_choice_json["passengers"][std::to_string(random_passenger) + "--1.0"]["departure_time"] = t + random_passenger;
		}
		route_choice_json["time"] = t;
		// for the ZeroMQbroker
		// comm(jsmsg);
		if (initial_variables.TSM) {
			std::cout << "\n\n Sending the following Traffic State XML file" << std::endl
					  << trafficStateMonitoring_xml(jsmsg) << std::flush;
			std::thread(send_traffic_state5555, jsmsg, trafficStateMonitoring_xml(jsmsg)).detach();
		}

		if (initial_variables.RChoice) {
			std::cout << "\n\n Sending the following Route Choice XML file" << std::endl;
			std::cout << routeChoice_xml(route_choice_json) << std::flush;
			std::thread(send_traffic_state5556, route_choice_json, routeChoice_xml(route_choice_json)).detach();
		}

		ETCS_MA.clear(); // Clear the list containing all the Movement Authorities given to the trains at the previous instant

		Occupy_Block_Sections_Of_Route(t); // Fill in the lists Blocks_Occupied and BlocksConnected

		// Only for level>=3
		// ReportAllTrainPositionsToRBC(t, 50);    //Reporting the positions of the trains to the RBC considering a safety Margin of 50 metres

		// Predict_And_Check_Decoupling_MA_For_All_Train_in_Following_Mode(t);  // Predict and check the Predict_MA_To_DecoupleAt for all trains which are in following mode

		// dynamic platform allocation
		//////////////////////for (int i = 0; i < numRegions; i++) { dispatchingTool->checkArrivalPlatform(i, t); }

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

		// send signal to update GUI
		emit(iterationFinished(t));
	}
}


void DispatchController::Train_Simulation_Mixed_Signalling_With_Passengers(double v1, double v2, double v3) {
	// logger.Log("Starting Train_Simulation_Mixed_Signalling");
	nlohmann::json jsmsg, route_choice_json;

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
		/////////////////////////////////////channel.zmq_communication(to_string(t));

		// logger.Log(" clock at" + to_string(t));

		/*#pragma omp parallel
		{
		#pragma omp for*/

		// you can comment it if you are simulating something different
		/*if (t < 430)
			regional_train[0].max_train_speed = 16;
		else regional_train[0].max_train_speed = 33.61;*/

		// print time update to file
		// dispatchingTool->printTimeUpdateMsg(t, initial_variables.InputMainFolder+ "/Rescheduling");

		// load dispatching decisions
		// dispatchingTool->loadDispDecisions(initial_variables.InputMainFolder + "/Rescheduling", t);

		// Simulate entrance process of passengers on the railway network according to route choice
		checkJourneyStartForAllPassengers(t, initial_variables.startingSimulationTime, AllDailyPassengers);

		// This function will update the list of all waiting passengers at all platforms in the network at every instant
		// To reduce the computation time it is instead recommended that the function to update the list of waiting passengers at platform is only used in the functional "Simulate_Train_Passenger_Interaction"
		// In that case  please comment the line below and uncomment the corresponding function Update_List_Passengers_Waiting_At_Platform in that function
		if (initial_variables.PAX_GUI) {
			Update_List_Passengers_Waiting_At_ALL_Platforms(AllStationPlatforms, AllDailyPassengers);
		}

		// Simulate train movement at each simulation step
		//  MULTI-THREAD
#pragma omp parallel for
		for (int j = 0; j < numRegions; j++) {
			// cout << j << "++++----/n";

			// only one thread writing to file at a time
#pragma omp critical(trainservicepathwrite)
			{
				// implement dispatching decisions
				//	regional_train[j].executeDispDecisions(t);
			}
			// Rigos added this to see if it works
			// regional_train[j].Trajectory_Block_Section_Free_Flow(t, v1, v2, v3);
			// The one that run Rafael is the following
			regional_train[j].trajectoryComputationIncludingMovingBlock(t, v1, v2, v3); // originally we shall call the function Trajectory_Block_Section_Free_Flow
			regional_train[j].recordStationPassagesAtTime(t);

			// only one thread writing to file at a time
#pragma omp critical(arrdepwrite)
			{
				// check if train arrived at destination or departed from origin
				regional_train[j].checkTrainArrDep(j, t);
			}
		}

		//}
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
			if (t >= regional_train[n].departure_time) {
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

		// Here we prepare the Route choice data
		for (int n = 0; n < 100; n++) { // for all ACTIVE passengers
			std::string station_names[10] = {"Guingamp", "Gourland", "Tregonnau_Squiffiec", "Brelidy_Plouec",
											 "Pontrieux_halte", "Pontrieux", "Frynaudour", "Traou_Nez", "Lancerf", "Paimpol"};
			int random_station = rand() % 10;
			int random_passenger = rand() % 1000;

			route_choice_json["passengers"][std::to_string(random_passenger) + "--1.0"]["origin"] = station_names[random_station];
			route_choice_json["passengers"][std::to_string(random_passenger) + "--1.0"]["destination"] = station_names[rand() % 10];
			route_choice_json["passengers"][std::to_string(random_passenger) + "--1.0"]["departure_time"] = t + random_passenger;
		}
		route_choice_json["time"] = t;
		// for the ZeroMQbroker
		// comm(jsmsg);
		if (initial_variables.TSM) {
			std::cout << "\n\n Sending the following Traffic State XML file" << std::endl
					  << trafficStateMonitoring_xml(jsmsg) << std::flush;
			std::thread(send_traffic_state5555, jsmsg, trafficStateMonitoring_xml(jsmsg)).detach();
		}

		if (initial_variables.RChoice) {
			std::cout << "\n\n Sending the following Route Choice XML file" << std::endl;
			std::cout << routeChoice_xml(route_choice_json) << std::flush;
			std::thread(send_traffic_state5556, route_choice_json, routeChoice_xml(route_choice_json)).detach();
		}

		ETCS_MA.clear(); // Clear the list containing all the Movement Authorities given to the trains at the previous instant

		Occupy_Block_Sections_Of_Route(t); // Fill in the lists Blocks_Occupied and BlocksConnected

		// Only for level>=3
		// ReportAllTrainPositionsToRBC(t, 50);    //Reporting the positions of the trains to the RBC considering a safety Margin of 50 metres

		// Predict_And_Check_Decoupling_MA_For_All_Train_in_Following_Mode(t);  // Predict and check the Predict_MA_To_DecoupleAt for all trains which are in following mode

		// dynamic platform allocation
		//////////////////////for (int i = 0; i < numRegions; i++) { dispatchingTool->checkArrivalPlatform(i, t); }

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

		// send signal to update GUI
		emit(iterationFinished(t));
	}
}

void DispatchController::printLastTrainServicePathDiagram() {
	// print last services
	for (int i = 0; i < numRegions; i++) {
		// print only trains running during simulation (departure_time >= initial_variables.times indicate that trains were not used, e.g. if only sprinters were used, IC are not printed)
		if (regional_train[i].departure_time < initial_variables.times) {
			regional_train[i].End_Time = initial_variables.times - 1;													   // set End_Time to print last leg correctly
			regional_train[i].printTrainServicePathDiagram(initial_variables.OutputMainFolder + "/TrainTrajectories", -1); // -1 indicates no next service
		}
	}
}

void DispatchController::printTimetableGraph() {

	string strReplace = "%HERE%";

	std::stringstream ss;

	// make sure the output directory exists (it is otherwise only created on the PAX path)
	ensureDirectory(initial_variables.OutputMainFolder + "/TrainTrajectories");

	// the HTML timetable graph needs a template; some case studies (e.g. Netherlands) do not ship one
	{
		ifstream templateCheck(initial_variables.InputMainFolder + "/GUI/TimetableGraph.html");
		if (!templateCheck) {
			cout << "Skipping HTML timetable graph: no template at "
				 << initial_variables.InputMainFolder << "/GUI/TimetableGraph.html" << endl;
			return;
		}
	}

	if (!initial_variables.Service_lines.empty()) { // check whether we have a list of service lines
		for (auto& it : initial_variables.Service_lines) {
			string strNew = "";
			ifstream filein(initial_variables.InputMainFolder + "/GUI/TimetableGraph.html");						   // File to read from
			ofstream fileout(initial_variables.OutputMainFolder + "/TrainTrajectories/TimetableGraph" + it + ".html"); // Temporary file
			time_t time0 = time(0);
			time_t production_time = time0;

			// prepare time points
			std::vector<std::string> timetable;
			for (int t = 0; t < initial_variables.times; t++) {
				std::string timepoint = "";
				struct tm ltm, prtm, stime;
				time_t now;
				localtime_r(&time0, &ltm);
				now = time0 - ((ltm.tm_hour * 3600) + (ltm.tm_min * 60) + ltm.tm_sec) + initial_variables.startingSimulationTime;
				time_t simulation_time = now + t;
				localtime_r(&simulation_time, &stime);
				char simTime[100]; // simulation time
				strftime(simTime, 50, "%Y-%m-%d %H:%M:%S", &stime);
				timepoint = "'" + timepoint + simTime + "',";

				timetable.push_back(timepoint);
			}

			std::string train_part_template = "{	name: '%trainName%', \
										hovertemplate:'km - point:%{y}' ,\
										x : time_points,\
										y : %position% ,\
										mode : 'lines+markers',\
										connectgaps : true},";

			for (int i = 0; i < numRegions; i++) {
				std::string train_part = train_part_template;
				std::string train_name = regional_train[i].trainDescription;
				std::vector<std::string> position_y;

				if (!regional_train[i].trainDescription.rfind(it + '-', 0)) {

					for (int t = 0; t < initial_variables.times; t++) {

						// end of leg
						if ((regional_train[i].instant_spatial_position[t] == -9999) || (t == regional_train[i].End_Time + 1)) {
							;
						} else if (t >= regional_train[i].prevIntendedDepTime) {
							if (regional_train[i].instant_spatial_position[t] == 0) {
								position_y.push_back(",");
							}

							// non-reversed route (same with/without jump)
							else if (!train_route[regional_train[i].indexOfRoute].reversed_direction) {
								position_y.push_back(to_string(regional_train[i].instant_spatial_position[t]) + ",");
								// fileout << regional_train[i].instant_spatial_position[t] << "\t";
							}
							// reversed route without jump
							else if (train_route[regional_train[i].indexOfRoute].diffRegionsJumpX.first == 0) {
								position_y.push_back(to_string(train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute - regional_train[i].instant_spatial_position[t]) + ",");
								// fileout << (train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute - regional_train[i].instant_spatial_position[t]) << "\t";
							}
							// reversed route with jump
							else {
								position_y.push_back(to_string((train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute - regional_train[i].instant_spatial_position[t]) - (train_route[regional_train[i].indexOfRoute].diffRegionsJumpX.first * 1000)) + ",");

								// fileout << ((train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute - regional_train[i].instant_spatial_position[t]) - (train_route[regional_train[i].indexOfRoute].diffRegionsJumpX.first * 1000)) << "\t";
							}
						}

						else {
							position_y.push_back(",");
							// fileout << "\t"; // empty cells before start of service
						}
					}
				}
				// replace train name
				size_t pos = train_part.find("%trainName%");
				train_part.replace(pos, 11, train_name);

				// replace time array
				// pos = train_part.find("%time_points%");

				// replace position array
				pos = train_part.find("%position%");
				std::stringstream spos;
				for (auto it = position_y.begin(); it != position_y.end(); it++) {
					if (it != position_y.begin()) {
						spos << " ";
					}
					spos << *it;
				}
				train_part.replace(pos, 10, "[" + spos.str() + "]");

				strNew = strNew + train_part;
			}

			// Stations
			std::string station_positions = "tickvals: [";
			std::string station_names = "ticktext: [";

			std::string virtual_word = "virtual";
			for (int j = 0; j < numStations; j++) {
				if (StationArray[j].stationName.find(virtual_word) == std::string::npos) {
					if (std::count(initial_variables.Line_stations[it].begin(), initial_variables.Line_stations[it].end(), StationArray[j].stationName)) {
						station_positions = station_positions + to_string(1000 * StationArray[j].X) + ",";
						station_names = station_names + "'" + StationArray[j].stationName + "',";
					}
				}
			}

			station_positions = station_positions + "],";
			station_names = station_names + "],";

			string strTemp;
			// bool found = false;
			while (std::getline(filein, strTemp))
			// while (filein >> strTemp)
			{
				if (strTemp == strReplace) {
					strTemp = strNew;
					// found = true;
				}

				if (strTemp == "%time_points%") {
					for (auto it = timetable.begin(); it != timetable.end(); it++) {
						if (it != timetable.begin()) {
							ss << " ";
						}
						ss << *it;
					}
					strTemp = ss.str();
					// found = true;
				}

				if (strTemp == "%stationData%") {
					strTemp = station_positions + "\n" + station_names;
					// found = true;
				}
				strTemp += " \n";
				fileout << strTemp;

				// if(found) break;
			}
			fileout.close();

			// for (vector<std::string>::iterator iter = initial_variables.Service_lines.begin();
			//	iter < initial_variables.Service_lines.end();
			//	iter++) {

			//}
		}
	} else {
		string strNew = "";
		ifstream filein(initial_variables.InputMainFolder + "/GUI/TimetableGraph.html"); // File to read from

		ofstream fileout(initial_variables.OutputMainFolder + "/TrainTrajectories/TimetableGraph.html"); // Temporary file
		if (!filein || !fileout) {
			cout << "Error opening files in printTimetableGraph!" << endl;
		}
		time_t time0 = time(0);
		time_t production_time = time0;
		for (int i = 0; i < numRegions; i++) {
		std:
			string train_part = "{	name: '%trainName%', \
					x : %time%,\
					y : %position% ,\
					mode : 'lines+markers',\
					connectgaps : true\
				},";
			std::string train_name = regional_train[i].trainDescription;
			std::vector<std::string> timetable;
			std::vector<std::string> position_y;

			for (int t = 0; t < initial_variables.times; t++) {
				std::string timepoint = "";
				struct tm ltm, prtm, stime;
				time_t now;
				localtime_r(&time0, &ltm);
				now = time0 - ((ltm.tm_hour * 3600) + (ltm.tm_min * 60) + ltm.tm_sec) + initial_variables.startingSimulationTime;
				time_t simulation_time = now + t;
				localtime_r(&simulation_time, &stime);
				char simTime[100]; // simulation time
				strftime(simTime, 50, "%Y-%m-%d %H:%M:%S", &stime);
				timepoint = "'" + timepoint + simTime + "',";

				timetable.push_back(timepoint);
				// end of leg
				if ((regional_train[i].instant_spatial_position[t] == -9999) || (t == regional_train[i].End_Time + 1)) {
					;
				} else if (t >= regional_train[i].prevIntendedDepTime) {
					if (regional_train[i].instant_spatial_position[t] == 0) {
						position_y.push_back(",");
					}

					// non-reversed route (same with/without jump)
					else if (!train_route[regional_train[i].indexOfRoute].reversed_direction) {
						position_y.push_back(to_string(regional_train[i].instant_spatial_position[t]) + ",");
						// fileout << regional_train[i].instant_spatial_position[t] << "\t";
					}
					// reversed route without jump
					else if (train_route[regional_train[i].indexOfRoute].diffRegionsJumpX.first == 0) {
						position_y.push_back(to_string(train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute - regional_train[i].instant_spatial_position[t]) + ",");
						// fileout << (train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute - regional_train[i].instant_spatial_position[t]) << "\t";
					}
					// reversed route with jump
					else {
						position_y.push_back(to_string((train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute - regional_train[i].instant_spatial_position[t]) - (train_route[regional_train[i].indexOfRoute].diffRegionsJumpX.first * 1000)) + ",");

						// fileout << ((train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute - regional_train[i].instant_spatial_position[t]) - (train_route[regional_train[i].indexOfRoute].diffRegionsJumpX.first * 1000)) << "\t";
					}
				} else {
					position_y.push_back(",");
					// fileout << "\t"; // empty cells before start of service
				}
			}

			// replace train name
			size_t pos = train_part.find("%trainName%");
			train_part.replace(pos, 11, train_name);
			// replace time array
			pos = train_part.find("%time%");
			std::stringstream ss;
			for (auto it = timetable.begin(); it != timetable.end(); it++) {
				if (it != timetable.begin()) {
					ss << " ";
				}
				ss << *it;
			}
			train_part.replace(pos, 6, "[" + ss.str() + "]");
			// replace position array
			pos = train_part.find("%position%");
			std::stringstream spos;
			for (auto it = position_y.begin(); it != position_y.end(); it++) {
				if (it != position_y.begin()) {
					spos << " ";
				}
				spos << *it;
			}
			train_part.replace(pos, 10, "[" + spos.str() + "]");

			strNew = strNew + train_part;
		}

		// Stations
		std::string station_positions = "tickvals: [";
		std::string station_names = "ticktext: [";

		std::string virtual_word = "virtual";
		for (int j = 0; j < numStations; j++) {
			if (StationArray[j].stationName.find(virtual_word) == std::string::npos) {
				station_positions = station_positions + to_string(1000 * StationArray[j].X) + ",";
				station_names = station_names + "'" + StationArray[j].stationName + "',";
			}
		}
		station_positions = station_positions + "],";
		station_names = station_names + "],";

		string strTemp;
		// bool found = false;
		while (std::getline(filein, strTemp))
		// while (filein >> strTemp)
		{
			if (strTemp == strReplace) {
				strTemp = strNew;
				// found = true;
			}

			if (strTemp == "%stationData%") {
				strTemp = station_positions + "\n" + station_names;
				// found = true;
			}
			strTemp += " \n";
			fileout << strTemp;

			// if(found) break;
		}
		fileout.close();
	}
}

void DispatchController::printCommonTimetableGraph() {

	string strReplace = "%HERE%";

	std::stringstream ss;
	double limit_l = 36600.0;
	double limit_r = 52000.0;

	string strNew = "";
	ifstream filein(initial_variables.InputMainFolder + "/GUI/TimetableGraph.html");					   // File to read from
	ofstream fileout(initial_variables.OutputMainFolder + "/TrainTrajectories/commonTimetableGraph.html"); // Temporary file
	time_t time0 = time(0);
	time_t production_time = time0;

	// prepare time points
	std::vector<std::string> timetable;
	for (int t = 0; t < initial_variables.times; t++) {
		std::string timepoint = "";
		struct tm ltm, prtm, stime;
		time_t now;
		localtime_r(&time0, &ltm);
		now = time0 - ((ltm.tm_hour * 3600) + (ltm.tm_min * 60) + ltm.tm_sec) + initial_variables.startingSimulationTime;
		time_t simulation_time = now + t;
		localtime_r(&simulation_time, &stime);
		char simTime[100]; // simulation time
		strftime(simTime, 50, "%Y-%m-%d %H:%M:%S", &stime);
		timepoint = "'" + timepoint + simTime + "',";

		timetable.push_back(timepoint);
	}

	std::string train_part_template = "{	name: '%trainName%', \
									hovertemplate:'km - point:%{y}' ,\
									x : time_points,\
									y : %position% ,\
									mode : 'lines+markers',\
									connectgaps : true},";

	for (int i = 0; i < numRegions; i++) {
		std::string train_part = train_part_template;
		std::string train_name = regional_train[i].trainDescription;
		std::vector<std::string> position_y;

		if (regional_train[i].trainDescription.rfind("F-", 0)) {

			for (int t = 0; t < initial_variables.times; t++) {

				// end of leg

				if ((regional_train[i].instant_spatial_position[t] == -9999) || (t == regional_train[i].End_Time + 1)) {
					;
				} else if (t >= regional_train[i].prevIntendedDepTime) {
					if (regional_train[i].instant_spatial_position[t] == 0) {
						position_y.push_back(",");
					}
					// non-reversed route (same with/without jump)
					else if (!train_route[regional_train[i].indexOfRoute].reversed_direction) {
						if ((regional_train[i].instant_spatial_position[t] >= limit_l) && (regional_train[i].instant_spatial_position[t] <= limit_r)) {

							position_y.push_back(to_string(regional_train[i].instant_spatial_position[t]) + ",");
						} else
							position_y.push_back(",");
					}
					// reversed route without jump
					else if (train_route[regional_train[i].indexOfRoute].diffRegionsJumpX.first == 0) {
						int abs_position = train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute - regional_train[i].instant_spatial_position[t];
						if ((abs_position >= limit_l) && (abs_position <= limit_r)) {
							position_y.push_back(to_string(abs_position) + ",");
							// fileout << (train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute - regional_train[i].instant_spatial_position[t]) << "\t";
						} else
							position_y.push_back(",");
					}
					// reversed route with jump
					else {
						int abs_position = (train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute - regional_train[i].instant_spatial_position[t]) - (train_route[regional_train[i].indexOfRoute].diffRegionsJumpX.first * 1000);
						if ((abs_position >= limit_l) && (abs_position <= limit_r)) {

							position_y.push_back(to_string(abs_position) + ",");

							// fileout << ((train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute - regional_train[i].instant_spatial_position[t]) - (train_route[regional_train[i].indexOfRoute].diffRegionsJumpX.first * 1000)) << "\t";
						} else
							position_y.push_back(",");
					}

				}

				else {
					position_y.push_back(",");
					// fileout << "\t"; // empty cells before start of service
				}
			}

			// replace train name
			size_t pos = train_part.find("%trainName%");
			train_part.replace(pos, 11, train_name);

			// replace time array
			// pos = train_part.find("%time_points%");

			// replace position array
			pos = train_part.find("%position%");
			std::stringstream spos;
			for (auto it = position_y.begin(); it != position_y.end(); it++) {
				if (it != position_y.begin()) {
					spos << " ";
				}
				spos << *it;
			}
			train_part.replace(pos, 10, "[" + spos.str() + "]");

			strNew = strNew + train_part;
		}
	}

	// Stations
	std::string station_positions = "tickvals: [";
	std::string station_names = "ticktext: [";

	std::string virtual_word = "virtual";
	for (int j = 0; j < numStations; j++) {
		if (StationArray[j].stationName.find(virtual_word) == std::string::npos) {
			if (std::count(initial_variables.Line_stations["common"].begin(), initial_variables.Line_stations["common"].end(), StationArray[j].stationName)) {
				station_positions = station_positions + to_string(1000 * StationArray[j].X) + ",";
				station_names = station_names + "'" + StationArray[j].stationName + "',";
			}
		}
	}

	station_positions = station_positions + "],";
	station_names = station_names + "],";

	string strTemp;
	// bool found = false;
	while (std::getline(filein, strTemp))
	// while (filein >> strTemp)
	{
		if (strTemp == strReplace) {
			strTemp = strNew;
			// found = true;
		}

		if (strTemp == "%time_points%") {
			for (auto it = timetable.begin(); it != timetable.end(); it++) {
				if (it != timetable.begin()) {
					ss << " ";
				}
				ss << *it;
			}
			strTemp = ss.str();
			// found = true;
		}

		if (strTemp == "%stationData%") {
			strTemp = station_positions + "\n" + station_names;
			// found = true;
		}
		strTemp += " \n";
		fileout << strTemp;

		// if(found) break;
	}
	fileout.close();

	// for (vector<std::string>::iterator iter = initial_variables.Service_lines.begin();
	//	iter < initial_variables.Service_lines.end();
	//	iter++) {

	//}
}

// set vector sizes with length of simulation from user input
void DispatchController::setVectorSizesFromInput(int vec_size) {
	for (int i = 0; i < numRegions; i++) {
		// define vector sizes with length of simulation from user input
		regional_train[i].setTrainVectorSizesFromInput(vec_size);
	}
}
