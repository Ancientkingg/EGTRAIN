#include "simulation/RollingStock.h"

#include <thread>
#include <chrono>
#include <cstdio>
#include <algorithm>

// GUI - Virtual Coupling notifications
vector<int> VCmsgTimestep;
vector<string> VCmsgTrain;
vector<string> VCmsgText;
QList<QGraphicsItemGroup*> VCmsgItems;
// --------------

int N_OrderLists = initial_variables.num_OrderLists; // This is the number of OrderLists that have to be respected at critical nodes

OrderList::OrderList() {
	Node_X = -10000;
	numTeList = 0;
	LastEnteredTrain = "None";
	BlockID = "None";
	Is_DivergingJunction = Is_MergingJunction = 0;
}

OrderList OL[20];

BlockingTimes::BlockingTimes() {
	StartOccTime = -1;
	EndOccTime = -1;
	length = 0;
	StartRunTime = EndRunTime = StartApproachTime = EndApproachTime = StartClearTime = EndClearTime = -1;
	setupTime = sightReacTime = ApproachTime = RunTime = clearingTime = ReleaseTime = -1;
	RunTimeMargin = -1;
	PosStart = -1;
	PosEnd = -1;
	IsComplete = true;
	NextBlockID = "None";
	ConnectedBlockingTimeID = "None";
	stationName = "None";
	trainDescription = "None";
	GeoPosStart = GeoPosEnd = -1;
	PosConnectedBlockingTime = -1;
	SpeedPreviousTrain = -1;
	SignallingLevel = -99999999;
	LocationWithSwitch = false;
	SwitchName = "None";
	InfraElementInPositionForTrain = true;
	IsAlreadyUniformedToConnectedSwitch = false;
	IsEndOfDivSwitchBeingStartOfADivSwitch = false;
	NamePreviousTrain = "None";
	NextBlockIDPreviousTrain = "None";
}

int numRegions = 0; /*Number of Speed ranges in the characteristic Tractive effort-speed curve of trains, Number of Trains loaded for the simulation( N_Train+N_TrainD)*/

int N_Train, N_TrainD; /*Number of Trains with even path, Number of Trains with odd path*/

// const int Max_N_Reg = 150;

double VirtualQ[Max_N_Reg], VirtualQD[Max_N_Reg];

Train::Train() {
	velocityIntervals = temp = stop = counter = counter2 = CounterFollowingMode = BrakStep = IsTrainCoupling = 0;
	End_Time = (int)((initial_variables.times - 1) / timestep);
	numStations = 0;
	Xobmin = Vobmin = 0;
	// instant_train_energy_consumption[0] = 0; // moved to function that sets the size of vectors from user input (at this point vector size is zero, can't set value)
	brakingPoint = 0;
	delayed = 0;
	RunStartTime = 0;
	CanEnter = false;
	direction = 0;
	Final_Delay = TotalInputDelays = 0;
	N_Station_Stopped = 0;
	indexOfRoute = 0;
	OutOfSimulation = false;
	GradientExceptionInBraking = false;
	IsTrainStoppedForEoA = false;
	type = "None";
	ETCS3StoppingPoint = -1;
	Start_Node_X = -10000;
	EntranceDelay = 0;
	N_BlockSections = 0;
	N_BlockTimeComplete = 0;
	N_ConflictingTrains = 0;
	IsTrainInFollowingMode = IsTrainDecoupling = IsInUnintentionalDecoupling = false;
	LeadingTrainInFollowingMode = "None";
	CurrentServiceStopPlatform = "None";
	XCurrentServiceStop = -1;
	TotalEnergyConsumed = 0;
	TotalEnergySubstationRequest = 0;
	TotalEnergyConsWithRegBrak = 0;
	TotalEnergySubstRequestWithRegBrak = 0;
	EnergyForAuxiliaries = 0;
	numOverlaps = 0;
	MAX_OnBoard_Passengers = 300 + 300 * number_of_wagons; // By default it is considered that a carriage can transport maximum 300 passengers so it is 300 pax for the traction unit and 300 pax for each carriage / wagon
	Current_OnBoard_Passengers = 0;						   // It is considered that the train starts with no passengers onboard
	for (int i = 0; i < 40; i++) {
		StationArrivals[i] = -1;
		StationArrivalNames[i] = "None";
		StationDelay[i] = -1;
		StationConsecDelay[i] = -1;
		StationDisturbance[i] = 0;
	}

	for (int i = 0; i < 100; i++) {
		for (int j = 0; j < 1000; j++) {
			HwMatrix[i][j] = -999999;
		}
	}

	prevIntendedDepTime = 0; // initialized as 0 to print from t=0 in the 1st service
	reservedPlatform = -1;

	for (int p = 0; p < 8; p++) {
		this->GibsonDwellTimeParameters[p] = -1;
	}
}

// Function to compute dwell times based on the interaction with passengers
// By default the dwell time is computed based on the microscopic dwell time model by Fernandez et al. (2007) which extends the model by Gibson et al. (1989)
double Train::computePaxDependentDwellTimeAtStations(int N_BoardPax, int N_AlightPax, double PlatformOccupancyRate, float beta0, float beta1, float beta2, float beta3, float beta4, float beta5, float beta6, float beta7) {
	// define parameters delta 1, delta 2 and delta 3
	double delta1 = 0, delta2 = 0, delta3 = 0;
	// delta1 measures the congestion at the platform if the platform occupancy rate is higher than 0.65 than delta1=1
	if (PlatformOccupancyRate > 0.65)
		delta1 = 1;

	// delta 3 measures the degree of congestion on board of the train. It becomes 1 if the onboard occupancy rate is larger than 0.7
	double OnboardOccupancyrate = this->Current_OnBoard_Passengers / this->MAX_OnBoard_Passengers;

	if (OnboardOccupancyrate > 0.7)
		delta3 = 1;

	// Split the boarding and alighting passengers equally among the coaches / wagons
	int N_cars = number_of_wagons + 1; // the one represents the power unit
	list<int> BoardingPassengersInACar;
	list<int> AlightingPassengersInACar;

	if (N_cars > 1) {
		int CounterBoardPax = 0, CounterAlightingPax = 0;

		for (int i = 0; i < N_cars - 1; i++) {
			int averageBoardingPassengers = (int)(N_BoardPax / N_cars);
			BoardingPassengersInACar.push_back(averageBoardingPassengers);
			CounterBoardPax = CounterBoardPax + averageBoardingPassengers;

			int averageAlightingPassengers = (int)(N_AlightPax / N_cars);
			AlightingPassengersInACar.push_back(averageAlightingPassengers);
			CounterAlightingPax = CounterAlightingPax + averageAlightingPassengers;
		}
		// Now identifying the number of alighting and boarding passengers in the last car

		int BoardingPaxLastCar = N_BoardPax - CounterBoardPax;
		BoardingPassengersInACar.push_back(BoardingPaxLastCar); // Adding the board passengers of the last car to the list

		int AlightingPaxLastCar = N_AlightPax - CounterAlightingPax;
		AlightingPassengersInACar.push_back(AlightingPaxLastCar); // Adding the alighting passengers of the last car to the list

	}

	else { // if there is only one car then all passengers will aligth and board from the only car
		BoardingPassengersInACar.push_back(N_BoardPax);
		AlightingPassengersInACar.push_back(N_AlightPax);
	}

	// To apply the dwell time model by Gibson it is required to have the number of people boarding and alighting at each door
	// We consider that train cars have 2 doors per car + 2 extra doors which pertain to the traction units
	int N_Doors_In_a_Car = 2;
	int Total_N_Doors_In_Train = N_Doors_In_a_Car * number_of_wagons + N_Doors_In_a_Car;

	// defining list of passengers boarding and alighting from each door in a car
	// they are defined by randomly drawin the number of passenger boarding or alighting from a door and deriving the remaining of that car from the randomly drawn number
	list<int> BoardPaxFromDoor;
	list<int> AlightPaxFromDoor;

	if (BoardingPassengersInACar.empty() != 1) {
		for (list<int>::iterator BoardInCar = BoardingPassengersInACar.begin(); BoardInCar != BoardingPassengersInACar.end(); BoardInCar++) {
			NumberGenerator N;
			// The number of people boarding from the first door is drawn according to a Gaussian with mean equal to the total number of passenger boarding from that car/2 and standard deviation being 20% of the mean.
			int Nboarding_FirstDoorInACar = (int)N.getGaussianFloat((*BoardInCar / 2), (*BoardInCar / 2 * 0.20));
			int Nboarding_SecondDoorInACar = *BoardInCar - Nboarding_FirstDoorInACar;

			BoardPaxFromDoor.push_back(Nboarding_FirstDoorInACar);
			BoardPaxFromDoor.push_back(Nboarding_SecondDoorInACar);
		}
	}
	if (AlightingPassengersInACar.empty() != 1) {
		for (list<int>::iterator AlightFromCar = AlightingPassengersInACar.begin(); AlightFromCar != AlightingPassengersInACar.end(); AlightFromCar++) {
			NumberGenerator N;
			// The number of people alighting from the first door is drawn according to a Gaussian with mean equal to the total number of passenger aligthing from that car/2 and standard deviation being 20% of the mean.
			int NAlight_FirstDoorInACar = (int)N.getGaussianFloat((*AlightFromCar / 2), (*AlightFromCar / 2 * 0.20));
			int NAlight_SecondDoorInACar = *AlightFromCar - NAlight_FirstDoorInACar;

			AlightPaxFromDoor.push_back(NAlight_FirstDoorInACar);
			AlightPaxFromDoor.push_back(NAlight_SecondDoorInACar);
		}
	}

	double Max_door_alight_board_time = -9999;
	list<double> Door_alight_board_time;

	int DoorInCarCounter = 0; // This counts the number of the car in the train. It is needed to check wheter there are more than 15 pboarding passengers on a car as this will set delta2 to 1

	list<int>::iterator B_Pax_Car = BoardingPassengersInACar.begin();
	list<int>::iterator A_Pax_Door = AlightPaxFromDoor.begin();

	for (int door = 0; door < Total_N_Doors_In_Train; door++) {
		double BoardProcessTime = 0, AlightProcessTime = 0;

		// Computing the board process time
		int delta2 = 0;
		if (*B_Pax_Car > 15)
			delta2 = 1; // set delta2 to 1 if the number of passenger boarding from the car is larger than 15

		BoardProcessTime = beta2 + beta3 * delta1 + beta4 * delta2;
		AlightProcessTime = beta5 * exp(-beta6 * (*A_Pax_Door)) + beta7 * delta3;

		double Door_a_b_time = BoardProcessTime + AlightProcessTime;

		Door_alight_board_time.push_back(Door_a_b_time);
		A_Pax_Door++; // Advancing the iterator over the alighting doors of one element

		DoorInCarCounter++;
		if (DoorInCarCounter == 2) { // if both doors of a car have been considered then advance the iterator on the boarding passengers per car (B_Pax_Car) to the next car
			B_Pax_Car++;
			DoorInCarCounter = 0; // and reset the car DoorinCarcounter to 0
		}
	}

	// Identifying the max door boarding alighting time
	if (Door_alight_board_time.empty() != 1) {
		for (list<double>::iterator door_time = Door_alight_board_time.begin(); door_time != Door_alight_board_time.end(); door_time++) {
			if (*door_time > Max_door_alight_board_time) {
				Max_door_alight_board_time = *door_time;
			}
		}
	}

	// finally computing the overall dwell time at the stop
	double Total_Dwell_Time = beta0 + beta1 * delta1 + Max_door_alight_board_time;

	// and returning it as output of the function
	return Total_Dwell_Time;
}

Regional::Regional() {
	g = 9.81;
	ID = 0;
	mass_of_traction_unit = 0;
	mass_of_a_wagon = 0;
	number_of_wagons = 0;
	max_train_speed = 0;
	max_train_decelaration = 1.3;
	frontal_wagon_area = 1.45;
	resistanceCoefficient = 0;
	Jerk = 0;
	train_length = 0;
	massPerWagonAxle = mass_of_a_wagon * number_of_wagons;
	massFactor = (1.09 * mass_of_traction_unit + 1.06 * massPerWagonAxle) / (mass_of_traction_unit + massPerWagonAxle);
	total_train_mass = mass_of_traction_unit + massPerWagonAxle;
}

Regional regional_train[Max_N_Reg];
Regional P, PD; /*This train is a Proof Train to measure the simulated Headway for each different Signalling Layout
				P is the Proof Train on the Even Track, while PD is the proof Train on the Odd track*/

void compareImplementedOrderWithRomaSolutionForAllOl(string FolderName, int t) {
	for (int i = 1; i < N_OrderLists; i++) {
		OL[i].compareImplementedOrderWithRomaSolution(FolderName, i, t);
	}
}

// Function to compute the Blocking Times in ETCS level 3 for all trains
void ComputeBlockingTimesETCS3ForAllTrains(double SetupTime, double ReleaseTime, double SightReacTime, string OutputFolder, double AbsRTSupplement, double PercentRTSupplement) {
	for (int i = 0; i < numRegions; i++) {

		// regional_train[i].ComputeBlockTimeETCS3(SetupTime,ReleaseTime,SightReacTime,AbsRTSupplement,PercentRTSupplement);
		regional_train[i].ComputeBlockTimeETCS3(SetupTime, ReleaseTime, SightReacTime, AbsRTSupplement, PercentRTSupplement);
	}

	// Print the Files
	PrintTrainBlockingTimes(OutputFolder);
}

// Function to compute blocking times of trains in ETCS L2 or conventional signalling
void ComputeBlockingTimesForAllTrains(string SignallingType, double SetupTime, double ReleaseTime, double SightReacTime, string OutputFolder, double AbsRTSupplement, double PercRTSupplement) {
	for (int i = 0; i < numRegions; i++) {

		regional_train[i].ComputeBlockingTimes(SignallingType, SetupTime, ReleaseTime, SightReacTime, AbsRTSupplement, PercRTSupplement);
	}

	// Print the Files
	PrintTrainBlockingTimes(OutputFolder);
}

// Function to compute Blocking Times of trains in mixed signalling areas
void ComputeBlockingTimesInMixedSignallingForAllTrains(double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, string OutputFolder, double AbsRTSupplement, double PercRTSupplement) {
	for (int i = 0; i < numRegions; i++) {

		regional_train[i].ComputeBlockingTimesInMixedSignallingAreas(SetupTime, ReleaseTime, SightReacTime, SafetyMargin, AbsRTSupplement, PercRTSupplement);
	}

	// Print the Files
	PrintTrainBlockingTimes(OutputFolder);
}

// Function that determines the correct departure headway to avoid conflicts (with this function we put blockingTime A always below Blocking Time blockSets)
double ComputeDepartureTimesToSolveConflicts(BlockingTimes A, BlockingTimes blockSets, double DepTimeTrain1, double DepTimeTrain2) {
	double ShiftedDepTime = -1;
	double overlap = -1;
	// with this function we move always train A below train blockSets
	overlap = blockSets.EndOccTime - A.StartOccTime;
	ShiftedDepTime = DepTimeTrain1 + overlap; // Add the overlap from the arrival distance

	return ShiftedDepTime;
}

double ComputeHWForLocationToDepartureTime(BlockingTimes A, BlockingTimes blockSets, double DepTimeTrain1, double DepTimeTrain2) {
	double Hw = -1;
	double overlap = -1;
	double DepartureTimeDistance = abs(DepTimeTrain1 - DepTimeTrain2);

	if (DepTimeTrain1 <= DepTimeTrain2) {	 // if train A enters the block before train blockSets
		if (A.EndOccTime > blockSets.StartOccTime) { // if the blocks overlap
			overlap = abs(A.EndOccTime - blockSets.StartOccTime);
			Hw = DepartureTimeDistance + overlap; // add the overlap to their arrival distance
		} else {								  // if the blocks do not overlap
			overlap = abs(A.EndOccTime - blockSets.StartOccTime);
			Hw = DepartureTimeDistance - overlap; // Subtract the overlap from the arrival distance
		}
	}

	else {									 // if train A departs after train blockSets
		if (blockSets.EndOccTime > A.StartOccTime) { // if the blocks overlap
			overlap = abs(blockSets.EndOccTime - A.StartOccTime);
			Hw = DepartureTimeDistance + overlap; // add the overlap to their arrival distance
		} else {								  // if the blocks do not overlap
			overlap = abs(blockSets.EndOccTime - A.StartOccTime);
			Hw = DepartureTimeDistance - overlap; // Subtract the overlap from the arrival distance
		}
	}
	return Hw;
}

// Function to Compute the Headway with another on a block section or a location
double ComputeHwForLocation(BlockingTimes A, BlockingTimes blockSets) {
	double Hw = -1;
	double overlap = -1;
	double ArrivalTimeDistance = abs(A.StartRunTime - blockSets.StartRunTime);

	if (A.StartRunTime <= blockSets.StartRunTime) {	 // if train A enters the block before train blockSets
		if (A.EndOccTime > blockSets.StartOccTime) { // if the blocks overlap
			overlap = abs(A.EndOccTime - blockSets.StartOccTime);
			Hw = ArrivalTimeDistance + overlap; // add the overlap to their arrival distance
		} else {								// if the blocks do not overlap
			overlap = abs(A.EndOccTime - blockSets.StartOccTime);
			Hw = ArrivalTimeDistance - overlap; // Subtract the overlap from the arrival distance
		}
	}

	else {									 // if train A enters the block after train blockSets
		if (blockSets.EndOccTime > A.StartOccTime) { // if the blocks overlap
			overlap = abs(blockSets.EndOccTime - A.StartOccTime);
			Hw = ArrivalTimeDistance + overlap; // add the overlap to their arrival distance
		} else {								// if the blocks do not overlap
			overlap = abs(blockSets.EndOccTime - A.StartOccTime);
			Hw = ArrivalTimeDistance - overlap; // Subtract the overlap from the arrival distance
		}
	}
	return Hw;
}

// Function to Compute the Headway with another on a block section or a location (in this function we move blocking time blockSets always below blocking time A)
double ComputeHwForLocationByShiftingBBelowA(BlockingTimes A, BlockingTimes blockSets) {
	double Hw = -1;
	double overlap = -1;
	// With this function we move always train A above train blockSets
	overlap = A.EndOccTime - blockSets.StartOccTime;
	Hw = abs(blockSets.StartRunTime - A.StartRunTime + overlap);

	return Hw;
}

// Function to Compute the Headway with another on a block section or a location (in this function we move blocking time blockSets always below blocking time A)
double ComputeShiftAtTimetablePointsByShiftingBBelowA(BlockingTimes A, BlockingTimes blockSets, double timeB) {
	double ShiftedTimeB = -1;
	double overlap = -1;
	// With this function we move always train A above train blockSets
	overlap = A.EndOccTime - blockSets.StartOccTime;
	ShiftedTimeB = timeB + overlap;

	return ShiftedTimeB;
}

// Debug Activate Signalling Function
void Debug_Activate_Signalling() {
	for (int t = 825; t < initial_variables.times; t++) {

		Occupy_Block_Sections_Of_Route(t); // Fill in the lists Blocks_Occupied and BlocksConnected

		releaseBlockConnections();  // Release Blocks connected with the one really occupied by a train
		activateSignallingSystem(); // Apply the rules of the signalling system for all the Blocks contained
		BlocksOccupied.clear();		  // Clear the list BlocksOccupied
		BlocksConnected.clear();	  // Clear the list BlocksConnected
	}
}

void DetectConflictsForAllTrains(Train* Trains, int numTrains) {
	for (int i = 0; i < numTrains; i++) {
		Trains[i].DetectConflictsWithPreviousDepartingTrains(Trains, numTrains);
	}
}

// Debugging Function
void Function_To_Debug_Occupation_Train(Train T, Section* BS, int Blocks) {
	for (int i = 825; i < T.End_Time; i++) {
		for (int h = 0; h < Blocks; h++) {
			if (((T.instant_spatial_position[i] - T.train_length) < BS[h].end_node.X * 1000) && ((T.instant_spatial_position[i] - T.train_length) >= BS[h].start_node.X * 1000)) {
				int Prev_Block = h - 1;
				if (h == 0)
					Prev_Block = 0;
				occupyBlockAndConnected(BS[h], BS[Prev_Block], (T.instant_spatial_position[i] - T.train_length), (T.instant_spatial_position[i - 1] - T.train_length));
				// if the Block Section has a switch in diverging position
				if (BS[h].withSwitchDiv == true)
					activateBlocksWithSwitchesDiv(BS[h], BS[Prev_Block].trackLineId, (T.instant_spatial_position[i] - T.train_length));
			}
			if ((T.instant_spatial_position[i] < BS[h].end_node.X * 1000) && (T.instant_spatial_position[i] >= BS[h].start_node.X * 1000)) {
				int Prev_Block = h - 1;
				if (h == 0)
					Prev_Block = 0;
				occupyBlockAndConnected(BS[h], BS[Prev_Block], (T.instant_spatial_position[i]), (T.instant_spatial_position[i - 1]));
				if (BS[h].withSwitchDiv == true) {
					activateBlocksWithSwitchesDiv(BS[h], BS[Prev_Block].trackLineId, T.instant_spatial_position[i]);
					break;
				}
			}
		}
		showElement(i, BlocksConnected);
		BlocksConnected.clear();
		BlocksOccupied.clear();
	}
}

void LoadAllTrainFiles(string MainFolder) {
	string DirName, ListOfFiles;
	ListOfFiles = ListOfFiles + MainFolder + "/trainNames.txt";

	ifstream InputFile;
	InputFile.open((char*)ListOfFiles.c_str(), ios::binary);
	if (!InputFile)
		cout << "ERROR: Train File Names impossible to open\n\n";
	else {
		while (InputFile) {

			string TrainTypeName;
			InputFile >> TrainTypeName;
			size_t found = TrainTypeName.find("TXT");
			size_t found2 = TrainTypeName.find("txt");
			if ((found != string::npos) || (found2 != string::npos)) { // if the file has the word txt in then open it otherwise not since it is a folder or another type of file
				string FileToOpen;
				FileToOpen = FileToOpen + MainFolder + "/Trains/" + TrainTypeName;
				loadTrainType((char*)FileToOpen.c_str(), numRegions);
			}
		}
	}
	InputFile.close(); // Close the file;
}

// Function to Load All The OrderLists referred to Critical Nodes in the Infrastructure
void loadAllOrderLists(char* FolderName) {
	for (int i = 0; i < N_OrderLists; i++) {
		char OL_ID[20];
		snprintf(OL_ID, sizeof(OL_ID), "%d", i);
		string FileName;
		FileName = FileName + FolderName + "/OL" + OL_ID + ".txt";
		OL[i].Load_OrderList((char*)FileName.c_str());
	}
}

// Function to Load All The OrderLists referred to Critical Nodes in the Infrastructure
void loadAllOrderListsUpgraded(char* FolderName) {
	for (int i = 0; i < N_OrderLists; i++) {
		char OL_ID[20];
		snprintf(OL_ID, sizeof(OL_ID), "%d", i);
		string FileName;
		FileName = FileName + FolderName + "/OL" + OL_ID + ".txt";
		OL[i].Load_OrderList_Upgraded((char*)FileName.c_str());
	}
}

// Fuction to load data of a train Type. Train Type involve the same category (Intercity, Regional), the same Route, and the same stops on that Route
void loadTrainType(char* Train_File, int& numRegions) {
	int N_Reg_Previous = numRegions; // Number of trains before that this category was added
	int N_Train_Type = 0;		// Number of Trains of this Type
	int N_FILEROWS = 1000;
	int N_FileRows = 0; // NumBlocksinB is the number of block section in Path blockSets
	string* Matr;		// Defining the matrix which will collect the data from the file FileName

	Matr = new string[N_FILEROWS]; // Allocating Matr dynamically

	/*cout<<"Insert the path of text file with Block Section Characteristics [Position, Length(m)]: ";  cin.get(blockname,99);*/
	ifstream Traininput;
	Traininput.open(Train_File, ios::binary);
	if (!Traininput) {
		cout << "Error 71 :impossible to open the train file" << Train_File << "\n";
	}

	while (Traininput) {
		Traininput >> Matr[N_FileRows];
		N_FileRows++;
	}
	N_FileRows = N_FileRows - 1;

	Traininput.close(); // Closing Block Section Data File
						// cin.ignore();
						// Initialize trains of the Category
	int Headway = atoi(Matr[2].c_str());		   // this is the headway of the train of this type
	double DepFirst_Train = atof(Matr[1].c_str()); // this is the departure time of the first train of this type
												   // The real part of the code is commented below, so this is just for the work for NR
	/*double numTrains=(3600-DepFirst_Train)/Headway;
	if ((numTrains-(int)numTrains)>0){N_Train_Type=(int)numTrains+1;}
	else N_Train_Type=(int)numTrains;*/

	// Defining the total number of trains belonging to this Type
	/*double numTrains=(times-DepFirst_Train)/Headway;
	if ((numTrains-(int)numTrains)>0){N_Train_Type=(int)numTrains+1;}
	else N_Train_Type=(int)numTrains;*/

	// Defining all the trains running in 1 hour considering that the first train departs at instant 0
	double numTrains = (initial_variables.times / Headway); //(3600 / Headway);
	if ((numTrains - (int)numTrains) > 0) {
		N_Train_Type = (int)numTrains + 1;
	} else
		N_Train_Type = (int)numTrains;

	// N_Train_Type=1;
	// Increase the Number of Total train on the Network
	numRegions = numRegions + N_Train_Type;

	for (int i = 0; i < N_FileRows; i++) {
		// cout << Matr[i] << "\n";
	}

	// Load Physical Characteristic of Trains
	int i = 0, j = 0;
	double* Matr1;
	Matr1 = new double[9];
	/*cout<<"Insert the path of Regional Train main data file[mass_of_traction_unit(Kg),mass_of_a_wagon(Kg),number_of_wagons,max_train_speed(m/s),max_train_decelaration(m/s2),frontal_wagon_area(m2),resistanceCoefficient,Jerk(m/s3),Length(m)]: "; cin.get(filename,99);*/

	ifstream File1;
	File1.open((char*)(initial_variables.InputMainFolder + Matr[4]).c_str(), ios::binary);
	if (!File1) {
		cout << "Error 1[loadTrainType]: Impossible to open train physical characteristics file 1 :" << initial_variables.InputMainFolder + "/" + Matr[4].c_str() << "\n";
	}

	for (int i = 0; i < 9; i++) {
		File1 >> Matr1[i];
	}

	File1.close();

	// cin.ignore();

	i = j = 0; // Resetting indexes to 0

	// Load Characteristic Traction-Speed curve
	int VINT = 20;
	int vinterv = 0;
	double** Matr2;

	Matr2 = new double*[VINT];
	for (int i = 0; i < VINT; i++) {
		Matr2[i] = new double[5];
	}

	ifstream File2;
	/*cout<<"Insert the path of Regional Traction Unit Characteristic data file [Vlb(m/s),Vub(m/s),C0,C1,C2]: "; cin.get(name,99);*/

	File2.open((char*)(initial_variables.InputMainFolder + Matr[5]).c_str(), ios::binary);
	if (!File2) {
		cout << "Error 2 [Load_TrainType]: Impossible to open train physical characteristics file 2" << initial_variables.InputMainFolder + Matr[5].c_str() << "\n";
	}

	while (File2) {
		File2 >> Matr2[i][j];
		j++;
		vinterv++;
		if (j > 4) {
			i++;
			j = 0;
		}
	}

	vinterv = (vinterv - 1) / 5;

	File2.close();

	// cin.ignore();

	i = j = 0; // Resetting indices to 0

	// Load Timetable of Trains
	int n_station = 100;
	int NumbStat = 0;
	string** TimeTab;
	TimeTab = new string*[n_station];
	for (int i = 0; i < n_station; i++) {
		TimeTab[i] = new string[4];
	}

	/*cout<< "Insert Line Timetable file path for Even Train Runs[ Station ID, Scheduled Dwell Time (s)]: ";  cin.get(TimeTabname,99);*/

	ifstream File4;

	File4.open((char*)(initial_variables.InputMainFolder + Matr[6]).c_str(), ios::binary);

	if (!File4) {
		cout << "ERROR 3[loadTrainType]: Impossible to open file " << initial_variables.InputMainFolder + Matr[6].c_str() << "\n";
	}

	while (File4) {
		File4 >> TimeTab[i][j];
		j++;
		NumbStat++;
		if (j > 3) {
			i++;
			j = 0;
		}
	}

	NumbStat = (NumbStat - 1) / 4; // Assuming the dimension of Timetable file i.e. the total number of Stations of the Line

	File4.close();
	// cin.ignore();

	/****************************************Setting All features of the trains of this Type*************************************************************/
	for (int i = 0; i < N_Train_Type; i++) {
		// Setting Physical parameters
		regional_train[i + N_Reg_Previous].ID = i + 1;
		regional_train[i + N_Reg_Previous].type = Matr[0];
		regional_train[i + N_Reg_Previous].indexOfRoute = atoi(Matr[3].c_str());
		regional_train[i + N_Reg_Previous].mass_of_traction_unit = Matr1[0];
		regional_train[i + N_Reg_Previous].mass_of_a_wagon = Matr1[1];
		regional_train[i + N_Reg_Previous].number_of_wagons = Matr1[2];
		regional_train[i + N_Reg_Previous].max_train_speed = Matr1[3];
		regional_train[i + N_Reg_Previous].max_train_decelaration = Matr1[4];
		regional_train[i + N_Reg_Previous].frontal_wagon_area = Matr1[5];
		regional_train[i + N_Reg_Previous].resistanceCoefficient = Matr1[6];
		regional_train[i + N_Reg_Previous].Jerk = Matr1[7];
		regional_train[i + N_Reg_Previous].train_length = Matr1[8];
		regional_train[i + N_Reg_Previous].massPerWagonAxle = regional_train[i + N_Reg_Previous].mass_of_a_wagon * regional_train[i + N_Reg_Previous].number_of_wagons;
		regional_train[i + N_Reg_Previous].massFactor = (1.09 * regional_train[i + N_Reg_Previous].mass_of_traction_unit + 1.06 * regional_train[i + N_Reg_Previous].massPerWagonAxle) / (regional_train[i + N_Reg_Previous].mass_of_traction_unit + regional_train[i + N_Reg_Previous].massPerWagonAxle);
		regional_train[i + N_Reg_Previous].total_train_mass = regional_train[i + N_Reg_Previous].mass_of_traction_unit + regional_train[i + N_Reg_Previous].massPerWagonAxle;
		// Setting trainDescription
		char IDChar[20];
		snprintf(IDChar, sizeof(IDChar), "%d", (int)regional_train[i + N_Reg_Previous].ID);
		regional_train[i + N_Reg_Previous].trainDescription = regional_train[i + N_Reg_Previous].type + "-" + IDChar;

		// Setting Train Start_Node_X

		regional_train[i + N_Reg_Previous].TrainRouteID = train_route[regional_train[i + N_Reg_Previous].indexOfRoute].ID;
		regional_train[i + N_Reg_Previous].Start_Node_X = train_route[regional_train[i + N_Reg_Previous].indexOfRoute].x_of_start_node;

		// Setting Traction Unit Characteristics
		regional_train[i + N_Reg_Previous].velocityIntervals = vinterv;

		for (int h = 0; h < regional_train[i + N_Reg_Previous].velocityIntervals; h++) {
			regional_train[i + N_Reg_Previous].Vlb[h] = Matr2[h][0];
			regional_train[i + N_Reg_Previous].Vub[h] = Matr2[h][1];
			regional_train[i + N_Reg_Previous].C0[h] = Matr2[h][2];
			regional_train[i + N_Reg_Previous].C1[h] = Matr2[h][3];
			regional_train[i + N_Reg_Previous].C2[h] = Matr2[h][4];
		}

		// Setting Stations and Dwell Times
		regional_train[i + N_Reg_Previous].numStations = NumbStat;
		regional_train[i + N_Reg_Previous].Stations = new Node[regional_train[i + N_Reg_Previous].numStations];

		for (int s = 0; s < regional_train[i + N_Reg_Previous].numStations; s++) {
			regional_train[i + N_Reg_Previous].Stations[s].stationName = TimeTab[s][0];		  // gets the station name where train stops
			regional_train[i + N_Reg_Previous].StationArrivalNames[s] = TimeTab[s][0];
			regional_train[i + N_Reg_Previous].Stations[s].dwellTime = atof(TimeTab[s][1].c_str()); // gets the dwell time for that station
			regional_train[i + N_Reg_Previous].Stations[s].StopTime = regional_train[i + N_Reg_Previous].Stations[s].dwellTime / timestep;
			if (i == 0) {
				regional_train[i + N_Reg_Previous].ScheduledArrivals[s] = atof(TimeTab[s][2].c_str());
				regional_train[i + N_Reg_Previous].ScheduledDepartures[s] = atof(TimeTab[s][3].c_str());
			} else {
				regional_train[i + N_Reg_Previous].ScheduledArrivals[s] = regional_train[i + N_Reg_Previous - 1].ScheduledArrivals[s] + Headway;
				regional_train[i + N_Reg_Previous].ScheduledDepartures[s] = regional_train[i + N_Reg_Previous - 1].ScheduledDepartures[s] + Headway;
				if (regional_train[i + N_Reg_Previous - 1].ScheduledArrivals[s] == -1)
					regional_train[i + N_Reg_Previous].ScheduledArrivals[s] = -1;
				if (regional_train[i + N_Reg_Previous - 1].ScheduledDepartures[s] == -1)
					regional_train[i + N_Reg_Previous].ScheduledDepartures[s] = -1;
			}
		}
		// Setting Headways and Departure Times
		if (i == 0) {
			regional_train[i + N_Reg_Previous].departure_time = DepFirst_Train;
		} else {
			regional_train[i + N_Reg_Previous].departure_time = regional_train[i + N_Reg_Previous - 1].departure_time + Headway;
		}
		// Setting the departure time in the original timetable
		regional_train[i + N_Reg_Previous].scheduled_departure_time = regional_train[i + N_Reg_Previous].departure_time;
	}

	cout << "\rLoading Train : " << Matr[0];
	// Deleting all Matrices ad Arrays allocated dynamically
	delete[] Matr; // Deleting Matr
	for (int i = 0; i < VINT; i++)
		delete Matr2[i]; // Deleting Matr2
	delete Matr2;

	// Deleting TimeTab
	delete[] TimeTab;
}

// Function to Rename trains according to the ROMA denomination: the trains in the ROMA file must be ordered according to the timetable departure
// the train renamed must depart or pass from the Node with abscissa Node_X
void nameTrainDescriptionAsRomaTool(char* FileNamesROMA, double Node_X) {
	string* Matr;
	int N_FILEROWS = 1000;
	int N_FileRows = 0;
	Matr = new string[N_FILEROWS]; // Allocating Matr dynamically
	ifstream input;
	input.open(FileNamesROMA, ios::binary);
	if (!input) {
		cout << "Error 58 :impossible to open the file\n";
	} else {
		cout << "Train Names from the ROMA tool loaded\n";
	}

	while (input) {
		input >> Matr[N_FileRows];
		N_FileRows++;
	}
	N_FileRows = N_FileRows - 1;

	input.close(); // Closing Block Section Data File
				   // Creating the list of Train Event like in the function
	list<TrainEvent> Train_Order_in_X;
	for (int i = 0; i < numRegions; i++) {
		if (regional_train[i].Start_Node_X == Node_X * 1000) {
			TrainEvent TEL;
			TEL.trainDescription = regional_train[i].trainDescription;
			TEL.Time = regional_train[i].departure_time;
			Train_Order_in_X.push_back(TEL);
		} else {
			for (int t = 1; t < regional_train[i].End_Time; t++) {
				if ((regional_train[i].instant_spatial_position[t - 1] <= Node_X * 1000) && (regional_train[i].instant_spatial_position[t] >= Node_X * 1000)) {
					TrainEvent TEL;
					TEL.trainDescription = regional_train[i].trainDescription;
					TEL.Time = t;
					Train_Order_in_X.push_back(TEL);
				}
			}
		}
	}

	// Check if the size of the list is equal to the number of trains written in the ROMA names file
	if (Train_Order_in_X.size() != N_FileRows)
		cout << "Error: Verify the number of train names contained in the ROMA name file or the number of trains passing or departing from Node " << Node_X << "\n";
	else {
		cout << "Number of Trains in the ROMA name File equal to the number of trains departing or passing from Node " << Node_X << "\n";
		// Ordering the TrainEvent List chronologically
		Train_Order_in_X.sort(orderTrainEvents);
		int counter = 0;
		for (list<TrainEvent>::iterator it = Train_Order_in_X.begin(); it != Train_Order_in_X.end(); it++) {
			counter++;
			for (unsigned int i = 0; i < Train_Order_in_X.size(); i++) {
				if (regional_train[i].trainDescription == it->trainDescription) {
					regional_train[i].trainDescription = Matr[counter - 1];
					break;
				}
			}
		}
	}
	delete[] Matr; // Deleting Matr
}
extern Logger owl;
// Function to Determine for each Route the Block Sections that are occupied by trains
//(This Function Fill in the list BlocksOccupied)
void Occupy_Block_Sections_Of_Route(int i) {
	for (int j = 0; j < numRegions; j++) {
		if ((regional_train[j].trainDescription == "B-Farum-HojeTaastrup_1-1") || (regional_train[j].trainDescription == "B-HojeTaastrup-Farum_2-1"))
			owl << "Train : " << regional_train[j].trainDescription << std::endl;
		regional_train[j].Det_Section_Occupied_By_Train(i, train_route[regional_train[j].indexOfRoute].sequence_of_block_sections, train_route[regional_train[j].indexOfRoute].N_Block_Sections);
	}
}

// Function to order blocking times by the StartOccTime
bool OrderByStartOccTime(BlockingTimes A, BlockingTimes blockSets) {
	if (A.StartOccTime <= blockSets.StartOccTime) {
		return true;
	} else {
		return false;
	}
}

// Function to Identify the point where following train under Virtual Coupling shall decpouple from the leading train
void Predict_And_Check_Decoupling_MA_For_All_Train_in_Following_Mode(int t) {
	for (int j = 0; j < numRegions; j++) {
		regional_train[j].Predict_And_Check_Validity_Of_MA_To_Split_At(t, regional_train, numRegions);
	}
}

// Function to Print out the blocking times of all the Trains
void PrintTimetablePoints(string MainFolder) {
	string FileName;
	FileName = FileName + MainFolder + "/TimetablePoints.txt";
	ofstream OutputFile;
	OutputFile.open((char*)FileName.c_str(), ios::binary);

	for (int i = 0; i < numRegions; i++) {
		OutputFile << regional_train[i].trainDescription << " " << regional_train[i].type << "\n";

		if (regional_train[i].TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = regional_train[i].TimetablePoints.begin(); j != regional_train[i].TimetablePoints.end(); j++) {
				OutputFile << j->SuccessorID << " ";
			}
		}
		OutputFile << "\n";

		if (regional_train[i].TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = regional_train[i].TimetablePoints.begin(); j != regional_train[i].TimetablePoints.end(); j++) {
				OutputFile << j->Position << " ";
			}
		}

		OutputFile << "\n";

		if (regional_train[i].TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = regional_train[i].TimetablePoints.begin(); j != regional_train[i].TimetablePoints.end(); j++) {
				OutputFile << j->Position << " ";
			}
		}

		OutputFile << "\n";

		if (regional_train[i].TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = regional_train[i].TimetablePoints.begin(); j != regional_train[i].TimetablePoints.end(); j++) {
				OutputFile << j->Time << " ";
			}
		}
		OutputFile << "\n";

		if (regional_train[i].TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = regional_train[i].TimetablePoints.begin(); j != regional_train[i].TimetablePoints.end(); j++) {
				OutputFile << j->Time2 << " ";
			}
		}
		OutputFile << "\n";
	}
}

// Function to Print out the blocking times of all the Trains
void PrintTrainBlockingTimes(string MainFolder) {
	string FileName;
	FileName = FileName + MainFolder + "/BlockingTimes.txt";
	ofstream OutputFile;
	OutputFile.open((char*)FileName.c_str(), ios::binary);

	for (int i = 0; i < numRegions; i++) {
		OutputFile << regional_train[i].trainDescription << "\n";

		for (int j = 0; j < regional_train[i].N_BlockTimeComplete; j++) {
			OutputFile << regional_train[i].BlockTime[j].BlockID << " ";
		}
		OutputFile << "\n";

		for (int j = 0; j < regional_train[i].N_BlockTimeComplete; j++) {
			/*if (train_route[regional_train[i].indexOfRoute].reversed_direction==0)
			OutputFile<<regional_train[i].BlockTime[j].PosStart<<" ";
			else
			OutputFile<<train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute-regional_train[i].BlockTime[j].PosEnd<<" ";*/
			OutputFile << regional_train[i].BlockTime[j].GeoPosStart << " ";
		}

		OutputFile << "\n";

		for (int j = 0; j < regional_train[i].N_BlockTimeComplete; j++) {
			/*if (train_route[regional_train[i].indexOfRoute].reversed_direction==0)
			OutputFile<<regional_train[i].BlockTime[j].PosEnd<<" ";
			else
			OutputFile<<train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute-regional_train[i].BlockTime[j].PosStart<<" ";*/
			OutputFile << regional_train[i].BlockTime[j].GeoPosEnd << " ";
		}

		OutputFile << "\n";

		for (int j = 0; j < regional_train[i].N_BlockTimeComplete; j++) {
			OutputFile << regional_train[i].BlockTime[j].StartOccTime << " ";
		}
		OutputFile << "\n";

		for (int j = 0; j < regional_train[i].N_BlockTimeComplete; j++) {
			OutputFile << regional_train[i].BlockTime[j].EndOccTime << " ";
		}
		OutputFile << "\n";
	}
}

// This Function is to Print out the LastEnteredTrain of the list OL
void Print_Out_OL_LastEntry(OrderList OL, int t) {
	ofstream output;
	string LastEntryName;
	LastEntryName = LastEntryName + InputMainFolder + "/Routes/OL_LastEntry.txt";
	output.open((char*)LastEntryName.c_str(), ios::app);
	output << t << " " << OL.LastEnteredTrain << "\n";
	output.close();
}

// Function to determine the scheduled sequence of train departure from a certain Node and print it on a text file
void Print_Scheduled_Dep_Order_In_Node(double Node_X) { // Node_X must be expressed in Km
	list<TrainEvent> Train_Order_in_X;
	for (int i = 0; i < numRegions; i++) {
		if (regional_train[i].Start_Node_X == Node_X * 1000) {
			TrainEvent TEL;
			TEL.trainDescription = regional_train[i].trainDescription;
			TEL.Time = regional_train[i].departure_time;
			Train_Order_in_X.push_back(TEL);
		} else {
			for (int t = 1; t < regional_train[i].End_Time; t++) {
				if ((regional_train[i].instant_spatial_position[t - 1] <= Node_X * 1000) && (regional_train[i].instant_spatial_position[t] >= Node_X * 1000)) {
					TrainEvent TEL;
					TEL.trainDescription = regional_train[i].trainDescription;
					TEL.Time = t;
					Train_Order_in_X.push_back(TEL);
				}
			}
		}
	}
	// Ordering the TrainEvent List chronologically
	Train_Order_in_X.sort(orderTrainEvents);
	ofstream ScheduledTrainOrder;
	string FileName;

	char nodex[20];
	snprintf(nodex, sizeof(nodex), "%f", Node_X);
	FileName = FileName + InputMainFolder + "/TrackLines/ScheduledOrder_NodeX_" + nodex + ".txt";
	ScheduledTrainOrder.open((char*)FileName.c_str());
	ScheduledTrainOrder << Node_X << "\n";
	list<TrainEvent>::iterator it;
	if (Train_Order_in_X.empty() != 1) {
		for (it = Train_Order_in_X.begin(); it != Train_Order_in_X.end(); it++)
			ScheduledTrainOrder << it->trainDescription << "\n";
	}
}

// Print EventTimes in the Format for TU Delft Timetabling tool
void Print_Timetabling_Point_TUDelft_format(string MainFolder) {

	int IDEvent1 = 0, IDEvent2 = 0;
	double TimeEvent1 = 0, TimeEvent2 = 0;
	double ProcessTimeEvents_1_2 = 0;
	TrainEvent LastWrittenEvent; // this TrainList memorises the information relative to the last event written down

	// Initialising the File of the Events
	string FileOutName;
	FileOutName = FileOutName + MainFolder + "/Events.csv";

	ofstream FILEOUTPUT;

	FILEOUTPUT.open((char*)FileOutName.c_str(), ios::binary);

	FILEOUTPUT << "event_id,line_id,hourlyrunno,direction,segmentno,station_abbr,farstation_abbr,event_type,time,track\n";

	// Initialising the File of the Processes
	string FileProcessOUT;
	FileProcessOUT = FileProcessOUT + MainFolder + "/Processes.csv";
	ofstream FILEPROCESS;
	FILEPROCESS.open((char*)FileProcessOUT.c_str(), ios::binary);

	FILEPROCESS << "from_event_id,to_event_id,mindur,scheddur,maxdur,process_type\n";

	int EventCounter = 0; // This is the Event Counter

	// Filling in the events
	for (int i = 0; i < numRegions; i++) {

		if (regional_train[i].TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator t = regional_train[i].TimetablePoints.begin(); t != regional_train[i].TimetablePoints.end(); t++) {

				string StationAbbr, FarStationArrivalEvent, FarStationDepartureEvent, Event_TypeArrival, Event_TypeDeparture;
				int TimeArrival, TimeDeparture;

				// specifying Station Abbr
				StationAbbr = t->SuccessorID;
				list<TrainEvent>::iterator BEG = regional_train[i].TimetablePoints.begin(); // Defining the position of the first station
				list<TrainEvent>::iterator END = regional_train[i].TimetablePoints.end();
				END--; // Defining the position of the last station
				// Defining the FarStation
				if ((t != BEG) && (t != END)) {
					list<TrainEvent>::iterator StationBefore = t;
					StationBefore--; // Setting Station before t
					FarStationArrivalEvent = StationBefore->SuccessorID;

					list<TrainEvent>::iterator StationAfter = t;
					StationAfter++; // Setting the station after t
					FarStationDepartureEvent = StationAfter->SuccessorID;
				}

				else if (t == BEG) {
					FarStationArrivalEvent = "START";
					list<TrainEvent>::iterator StationAfter = t;
					if (t != END) {

						StationAfter++;										  // Increase the pointer only if current station is not the last one
						FarStationDepartureEvent = StationAfter->SuccessorID; // In this case the Far Station Departure Event becomes the next station
					}

					else { // else put END as Far Station since the train departs from the last station to go to END
						FarStationDepartureEvent = "END";
					}

				}

				else if (t == END) {
					FarStationDepartureEvent = "END";
					list<TrainEvent>::iterator StationBefore = t;
					if (t != BEG) {
						StationBefore--; // Take the he Station Before t if t is not also the beginning
						FarStationArrivalEvent = StationBefore->SuccessorID;
					}

					else { // The train will depart from Start if t is also the first Station
						FarStationArrivalEvent = "START";
					}
				}

				// Assigning the type of event
				if (t->Time == t->Time2) {
					Event_TypeArrival = "2-arrthru";
					Event_TypeDeparture = "1-depthru";
				} else {
					Event_TypeArrival = "3-arr";
					Event_TypeDeparture = "0-dep";
				}

				// Assigning the times
				TimeArrival = (int)t->Time;
				TimeDeparture = (int)t->Time2;

				// Writing down the arrival event
				if (FarStationArrivalEvent != "START") {

					EventCounter++; // Increase the event counter of one unit
					FILEOUTPUT << EventCounter << "," << t->trainDescription << "," << 1 << "," << 1 << "," << 1 << "," << StationAbbr << "," << FarStationArrivalEvent << "," << Event_TypeArrival << "," << TimeArrival << ", \n";

					if ((t->trainDescription == LastWrittenEvent.trainDescription) && (LastWrittenEvent.Position != -10000)) { // IF this is an event of the same train and the last written Event has been already initialised

						if (StationAbbr == LastWrittenEvent.SuccessorID) { // if the station of this event is the same of the previous event
							if (TimeArrival == LastWrittenEvent.Time) {	   // if the times are the same then it is a thru process
								FILEPROCESS << LastWrittenEvent.Position << "," << EventCounter << "," << TimeArrival - LastWrittenEvent.Time << ", , ," << "thru" << "\n";
							} else { // else the process is a dwell
								FILEPROCESS << LastWrittenEvent.Position << "," << EventCounter << "," << TimeArrival - LastWrittenEvent.Time << ", , ," << "dwell" << "\n";
							}
						} else { // if the Station of the current event is different from the station of the previous event then the process is a run
							FILEPROCESS << LastWrittenEvent.Position << "," << EventCounter << "," << TimeArrival - LastWrittenEvent.Time << ", , ," << "run" << "\n";
						}
					}

					// Setting the process times
					LastWrittenEvent.trainDescription = t->trainDescription;
					LastWrittenEvent.SuccessorID = StationAbbr;
					LastWrittenEvent.Time = TimeArrival;
					LastWrittenEvent.Position = EventCounter;
				}

				// Writing down the departure event
				if (FarStationDepartureEvent != "END") {

					EventCounter++; // Increase the event counter of one unit
					FILEOUTPUT << EventCounter << "," << t->trainDescription << "," << 1 << "," << 1 << "," << 1 << "," << StationAbbr << "," << FarStationDepartureEvent << "," << Event_TypeDeparture << "," << TimeDeparture << " \n";

					if ((t->trainDescription == LastWrittenEvent.trainDescription) && (LastWrittenEvent.Position != -10000)) { // IF this is an event of the same train and the last written Event has been already initialised

						if (StationAbbr == LastWrittenEvent.SuccessorID) { // if the station of this event is the same of the previous event
							if (TimeDeparture == LastWrittenEvent.Time) {  // if the times are the same then it is a thru process
								FILEPROCESS << LastWrittenEvent.Position << "," << EventCounter << "," << TimeDeparture - LastWrittenEvent.Time << ", , ," << "thru" << "\n";
							} else { // else the process is a dwell
								FILEPROCESS << LastWrittenEvent.Position << "," << EventCounter << "," << TimeDeparture - LastWrittenEvent.Time << ", , ," << "dwell" << "\n";
							}
						} else { // if the Station of the current event is different from the station of the previous event then the process is a run
							FILEPROCESS << LastWrittenEvent.Position << "," << EventCounter << "," << TimeDeparture - LastWrittenEvent.Time << ", , ," << "run" << "\n";
						}
					}

					// Setting the process times
					LastWrittenEvent.trainDescription = t->trainDescription;
					LastWrittenEvent.SuccessorID = StationAbbr;
					LastWrittenEvent.Time = TimeDeparture;
					LastWrittenEvent.Position = EventCounter;
				}
			}
		}
	}
}

// Function to report the position of the trains in ETCS Level 3
void ReportAllTrainPositionsToRBC(int i, double ETCS3SafetyMargin) {
	for (int j = 0; j < numRegions; j++) {
		regional_train[j].ReportPositionToRBC(i, train_route[regional_train[j].indexOfRoute].sequence_of_block_sections, train_route[regional_train[j].indexOfRoute].N_Block_Sections, ETCS3SafetyMargin);
	}
}

// Function to Reset the Lists that will be updated at each instant
void resetOlToUpdate() {
	for (int i = 0; i < N_OrderLists; i++) {
		for (int j = 0; j < OL[i].numTeList; j++) {
			OL[i].TE[j].trainDescription = "None";
		}
	}
}

// Function to Assign to OL0 the same order of OL1
void Set_OL0_as_OL1() {
	for (int j = 0; j < OL[0].numTeList; j++) {
		OL[0].TE[j].trainDescription = "None";
	}
	OL[0].numTeList = OL[1].numTeList; // Setting numTeList of OL0 and OL1 as equal
	for (int j = 0; j < OL[0].numTeList; j++) {
		OL[0].TE[j].trainDescription = OL[1].TE[j].trainDescription;
	}
}

// Function to Set the Critical Nodes In which a special order has to be respected and Link these nodes to the respective OrderLists OL
void setRespectOrderAndIndexOrderList() {
	for (int i = 0; i < N_OrderLists; i++) {
		for (int j = 0; j < numTrackLines; j++) {
			if (blockSets[j].member[0].startNode.X == OL[i].Node_X) {
				blockSets[j].member[0].startNode.respectOrder = true;
				blockSets[j].member[0].startNode.indexOrderList = i;
			}
			for (int h = 0; h < blockSets[j].len; h++) {
				if (blockSets[j].member[h].endNode.X == OL[i].Node_X) {
					blockSets[j].member[h].endNode.respectOrder = true;
					blockSets[j].member[h].endNode.indexOrderList = i;
				}
			}
		}
	}
}

// This is the function to set the departure sequence of trains at checkpoint nodes of a block section
void Set_RespectOrder_And_Index_OrderList_Upgraded() {
	for (int i = 0; i < N_OrderLists; i++) {
		for (int j = 0; j < Blocks; j++) {
			if ((signalling_block_sections[j].ID == OL[i].BlockID) && (signalling_block_sections[j].start_node.X == OL[i].Node_X)) { // Here we are assigning the train sequence to Block Section when Routes have not been defined yet. So we do not need to distinguish between reversed and not reversed block sections because Block sections nodes all have absolute coordinates and not relative. So the code can be written jut for the absolute coordinates of Block Section nodes
				signalling_block_sections[j].start_node.respectOrder = true;
				signalling_block_sections[j].start_node.indexOrderList = i;
				// Assigning the same things to the beginning Node of the first arcs_in_signalling_block_section (i.e. arcs_in_signalling_block_section[0]) of signalling_block_sections[j]
				signalling_block_sections[j].arcs_in_signalling_block_section[0].startNode.respectOrder = true;
				signalling_block_sections[j].arcs_in_signalling_block_section[0].startNode.indexOrderList = i;
			}
			for (int h = 0; h < signalling_block_sections[j].total_arcs; h++) {
				if ((signalling_block_sections[j].ID == OL[i].BlockID) && (signalling_block_sections[j].arcs_in_signalling_block_section[h].endNode.X == OL[i].Node_X)) {
					signalling_block_sections[j].arcs_in_signalling_block_section[h].endNode.respectOrder = true;
					signalling_block_sections[j].arcs_in_signalling_block_section[h].endNode.indexOrderList = i;
				}
			}
		}
	}
}

// This is the function to set the departure sequence of trains at checkpoint nodes of a block section
void Set_RespectOrder_And_Index_OrderList_Upgraded_Improved() {
	for (int i = 0; i < N_OrderLists; i++) {
		for (int j = 0; j < Blocks; j++) {
			if (signalling_block_sections[j].start_node.X == OL[i].Node_X) { // Here we are assigning the train sequence to Block Section when Routes have not been defined yet. So we do not need to distinguish between reversed and not reversed block sections because Block sections nodes all have absolute coordinates and not relative. So the code can be written jut for the absolute coordinates of Block Section nodes
				signalling_block_sections[j].start_node.respectOrder = true;
				signalling_block_sections[j].start_node.indexOrderList = i;
				// Assigning the same things to the beginning Node of the first arcs_in_signalling_block_section (i.e. arcs_in_signalling_block_section[0]) of signalling_block_sections[j]
				signalling_block_sections[j].arcs_in_signalling_block_section[0].startNode.respectOrder = true;
				signalling_block_sections[j].arcs_in_signalling_block_section[0].startNode.indexOrderList = i;
			}

			if (signalling_block_sections[j].end_node.X == OL[i].Node_X) { // Here we are assigning the train sequence to Block Section when Routes have not been defined yet. So we do not need to distinguish between reversed and not reversed block sections because Block sections nodes all have absolute coordinates and not relative. So the code can be written jut for the absolute coordinates of Block Section nodes
				signalling_block_sections[j].end_node.respectOrder = true;
				signalling_block_sections[j].end_node.indexOrderList = i;
			}

			for (int h = 0; h < signalling_block_sections[j].total_arcs; h++) {
				if (signalling_block_sections[j].arcs_in_signalling_block_section[h].endNode.X == OL[i].Node_X) {
					signalling_block_sections[j].arcs_in_signalling_block_section[h].endNode.respectOrder = true;
					signalling_block_sections[j].arcs_in_signalling_block_section[h].endNode.indexOrderList = i;
				}
			}
		}
	}
}

// This is the function to set the departure sequence of trains at checkpoint nodes of a block section considering whether the OrderList refers to a Merging or a diverging Junction.
//  Whether the OL refers to a Merging or Diverging Junction shall be evaluated looking at the shape of the junction following the Km progressive in the same direction of
// IMPORTANT NOTE: When applying the function below the OL point should be coinciding with the last switch of a merging junction or the very first switch of a diverging junction (looking at the infrastructure layout when following the same abscissa direction which was used to intialise the infrastructure)

void Set_RespectOrder_And_Index_OrderList_Upgraded_Improved_With_JunctionType() {
	for (int i = 0; i < N_OrderLists; i++) {
		for (int j = 0; j < Blocks; j++) {
			if (OL[i].Is_MergingJunction == 1) {
				if ((signalling_block_sections[j].start_node.X < OL[i].Node_X) && (signalling_block_sections[j].end_node.X >= OL[i].Node_X)) {
					// if the entry signal is a normal signal then set that signal to reference point where the Order List shild be respected
					if (signalling_block_sections[j].start_node.virtualSignal == 0) {
						// Assigning the respect order conditions to the initial Node of this block section which has the OL Node X inside its length. This means that the OL Order will be respected at the entry signal of this block section or ( which is the same) at the exist signal of the previous block sections
						// Respect order cannot be assigned to virtual signals as those should not exist in reality and trains cannot wait in front of a double switch for being reodered
						signalling_block_sections[j].start_node.respectOrder = true;
						signalling_block_sections[j].start_node.indexOrderList = i;
						// Assigning the same things to the beginning Node of the first arcs_in_signalling_block_section (i.e. arcs_in_signalling_block_section[0]) of signalling_block_sections[j]
						signalling_block_sections[j].arcs_in_signalling_block_section[0].startNode.respectOrder = true;
						signalling_block_sections[j].arcs_in_signalling_block_section[0].startNode.indexOrderList = i;
						double SignalNodeID = signalling_block_sections[j].arcs_in_signalling_block_section[0].startNode.ID;
						double SignalNodeX = signalling_block_sections[j].arcs_in_signalling_block_section[0].startNode.X;
						string SignalNodeTDSB_ID = signalling_block_sections[j].arcs_in_signalling_block_section[0].startNode.tdsbId;
						// Check now for all the block sections which end with the such a signal they might be either block sections on the same trackline of the identified block or not
						// To those block sections we need to assign the respect order conditions to the end Node of the last Arc of the block
						for (int k = 0; k < Blocks; k++) {
							int N_TotarcsINBlock = signalling_block_sections[k].total_arcs;
							// Impose the respect order also the exit signal of block sections whose exit signals coincides with the entry signal of the block section identified in the piece of code above, simply because the exit signal of this block section and the entry signal of the previous block section are the same signal
							if ((signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.ID == SignalNodeID) && (signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.X == SignalNodeX) && (signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.tdsbId == SignalNodeTDSB_ID)) {
								// This is imposed to the end Node of the last Arc of the block section
								signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.respectOrder = true;
								signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.indexOrderList = i;
								// As well as to the last Node of the block section itself
								signalling_block_sections[k].end_node.respectOrder = true;
								signalling_block_sections[k].end_node.indexOrderList = i;
							}
							// Impose also to respect the order at all of those blocks whose starting Node coincides with the identified non-virtual signal
							if ((signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.ID == SignalNodeID) && (signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.X == SignalNodeX) && (signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.tdsbId == SignalNodeTDSB_ID)) {
								// This is imposed to the end Node of the last Arc of the block section
								signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.respectOrder = true;
								signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.indexOrderList = i;
								// As well as to the last Node of the block section itself
								signalling_block_sections[k].start_node.respectOrder = true;
								signalling_block_sections[k].start_node.indexOrderList = i;
							}
						}

					} else { // if instead the entry signal of that block section is a virtual signal then the Order List should be respected at the start signal of the previous block section ( i.e. the one having the virtual signal as exit signal)

						double VirtualSignalID = signalling_block_sections[j].start_node.ID;
						double VirtualSignalX = signalling_block_sections[j].start_node.X;
						// Check if the Virtual signal also have a tdsbId which is different than "None". In that case also the condition of equal on the tdsbId should be put in the if condition below
						string VirtualSignalTDSB_ID = signalling_block_sections[j].start_node.tdsbId;

						// defining the Signal ID and Signal Node X of the non-virtual entry signal of the block section ending with the identified Virtual Signal
						double SignalNodeID = -1;
						double SignalNodeX = -1;
						string SignalNodeTDSB_ID = "None";
						// Iterate through the block section to find the block section which has a diverging switch and has the exit signal equal to the Virtual Signal identified
						for (int z = 0; z < Blocks; z++) {

							if ((signalling_block_sections[z].end_node.X == VirtualSignalX) && (signalling_block_sections[z].end_node.ID == VirtualSignalID) && (signalling_block_sections[z].end_node.tdsbId == VirtualSignalTDSB_ID) && (signalling_block_sections[z].withSwitchDiv == 1)) {

								signalling_block_sections[z].start_node.respectOrder = true;
								signalling_block_sections[z].start_node.indexOrderList = i;
								// Assigning the same things to the beginning Node of the first arcs_in_signalling_block_section (i.e. arcs_in_signalling_block_section[0]) of signalling_block_sections[z]
								signalling_block_sections[z].arcs_in_signalling_block_section[0].startNode.respectOrder = true;
								signalling_block_sections[z].arcs_in_signalling_block_section[0].startNode.indexOrderList = i;
								SignalNodeID = signalling_block_sections[z].arcs_in_signalling_block_section[0].startNode.ID;
								SignalNodeX = signalling_block_sections[z].arcs_in_signalling_block_section[0].startNode.X;
								SignalNodeTDSB_ID = signalling_block_sections[z].arcs_in_signalling_block_section[0].startNode.tdsbId;
								break; // break the for loop of iterator z iterating through the Blocks
							}
						}
						// if a real non-virtual entry signal of the block section ending with the identified Virtual Signal exists
						// then assign the Respect order also to all of the previous block sections which exit with such a non-virtual signal (i.e. having exit signal coinciding with such a non-virtual signal)
						if ((SignalNodeID != -1) && (SignalNodeX != -1) && (SignalNodeTDSB_ID != "None")) {
							// Check now for all the block sections which end with the such a non non-virtual signal Signal Node which might be either block sections on the same trackline of the identified block or not
							// To those block sections we need to assign the respect order conditions to the end Node of the last Arc of the block
							for (int k = 0; k < Blocks; k++) {
								int N_TotarcsINBlock = signalling_block_sections[k].total_arcs;
								// Impose the respect order also the exit signal of block sections whose exit signals coincides with the entry signal of the block section identified in the piece of code above, simply because the exit signal of this block section and the entry signal of the previous block section are the same signal
								if ((signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.ID == SignalNodeID) && (signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.X == SignalNodeX) && (signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.tdsbId == SignalNodeTDSB_ID)) {
									// This is imposed to the end Node of the last Arc of the block section
									signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.respectOrder = true;
									signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.indexOrderList = i;
									// As well as to the last Node of the block section itself
									signalling_block_sections[k].end_node.respectOrder = true;
									signalling_block_sections[k].end_node.indexOrderList = i;
								}
								// Impose also to respect the order at all of those blocks whose starting Node coincides with the identified non-virtual signal
								if ((signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.ID == SignalNodeID) && (signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.X == SignalNodeX) && (signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.tdsbId == SignalNodeTDSB_ID)) {
									// This is imposed to the end Node of the last Arc of the block section
									signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.respectOrder = true;
									signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.indexOrderList = i;
									// As well as to the last Node of the block section itself
									signalling_block_sections[k].start_node.respectOrder = true;
									signalling_block_sections[k].start_node.indexOrderList = i;
								}
							}
						}
					}
				}

			}

			// If the OL order List refers to a Merging Junction ( looking at the the infrastructure layout when following teh progressive direction by which the infrastructure is defined in EGTRAIN)
			// Then the order should be assigned to the exit signal of those block section which have the OL location inside their lengths, as well as to the entry signal of the immediately adjacent block section ( whose entry signals coincide with the exist signal of the identified block)
			else if (OL[i].Is_DivergingJunction == 1) {
				if ((signalling_block_sections[j].start_node.X < OL[i].Node_X) && (signalling_block_sections[j].end_node.X >= OL[i].Node_X)) {

					if (signalling_block_sections[j].end_node.virtualSignal == 0) {
						// Assigning the respect order conditions to the end Node (i.e. the exit signal) of this block section which has the OL Node X inside its length. This means that the OL Order will be respected at the entry signal of this block section or ( which is the same) at the exist signal of the previous block sections
						// Respect order cannot be assigned to virtual signals as those should not exist in reality and trains cannot wait in front of a double switch for being reodered
						signalling_block_sections[j].end_node.respectOrder = true;
						signalling_block_sections[j].end_node.indexOrderList = i;
						// Assigning the same things to the end Node of the last Arc of the block section
						int N_TotarcsINBlock = signalling_block_sections[j].total_arcs;
						signalling_block_sections[j].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.respectOrder = true;
						signalling_block_sections[j].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.indexOrderList = i;
						double SignalNodeID = signalling_block_sections[j].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.ID;
						double SignalNodeX = signalling_block_sections[j].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.X;
						string SignalNodeTDSB_ID = signalling_block_sections[j].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.tdsbId;
						// Check now for all the block sections which start with the such a signal they might be either block sections on the same trackline of the identified block or not
						// To those block sections we need to assign the respect order conditions to the first Node of the first Arc of the block
						for (int k = 0; k < Blocks; k++) {

							// Impose the respect order also the entry signal of block sections whose entry signals coincides with the exit signal of the block section identified in the piece of code above, simply because the exit signal of this block section and the entry signal of the previous block section are the same signal
							if ((signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.ID == SignalNodeID) && (signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.X == SignalNodeX) && (signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.tdsbId == SignalNodeTDSB_ID)) {
								// This is imposed to the end Node of the last Arc of the block section
								signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.respectOrder = true;
								signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.indexOrderList = i;
								// As well as to the start Node (i.e. the actual entry signal Node) of the block section itself
								signalling_block_sections[k].start_node.respectOrder = true;
								signalling_block_sections[k].start_node.indexOrderList = i;
							}

							int N_TotarcsINBlock = signalling_block_sections[k].total_arcs;
							// Impose the respect order also the exit signal of block sections whose exit signals coincides with the entry signal of the block section identified in the piece of code above, simply because the exit signal of this block section and the entry signal of the previous block section are the same signal
							if ((signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.ID == SignalNodeID) && (signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.X == SignalNodeX) && (signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.tdsbId == SignalNodeTDSB_ID)) {
								// This is imposed to the end Node of the last Arc of the block section
								signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.respectOrder = true;
								signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.indexOrderList = i;
								// As well as to the last Node of the block section itself
								signalling_block_sections[k].end_node.respectOrder = true;
								signalling_block_sections[k].end_node.indexOrderList = i;
							}
						}
					}

					else { // if instead the exit signal of that block section is a virtual signal then the Order List should be respected at the exit signal of the next block section ( i.e. the one having the identified virtual signal as entry signal)

						double VirtualSignalID = signalling_block_sections[j].end_node.ID;
						double VirtualSignalX = signalling_block_sections[j].end_node.X;
						// if the Virtual Signal has a tdsbId then we also need to use its tdsbId in the if condition below otherwise only the two features below could suffice (although some issue might arise with nodes having the same X and ID on other tracklines)
						string VirtualSignalTDSB_ID = signalling_block_sections[j].end_node.tdsbId;

						// defining the Signal ID and Signal Node X of the non-virtual exit signal of the block section starting with the identified Virtual Signal
						double SignalNodeID = -1;
						double SignalNodeX = -1;
						string SignalNodeTDSB_ID = "None";

						// Iterate through the block section to find the block section which has a diverging switch and has the exit signal equal to the Virtual Signal identified
						for (int z = 0; z < Blocks; z++) {

							if ((signalling_block_sections[z].start_node.X == VirtualSignalX) && (signalling_block_sections[z].start_node.ID == VirtualSignalID) && (signalling_block_sections[z].start_node.tdsbId == VirtualSignalTDSB_ID) && (signalling_block_sections[z].withSwitchDiv == 1)) {

								signalling_block_sections[z].end_node.respectOrder = true;
								signalling_block_sections[z].end_node.indexOrderList = i;

								// Assigning the same things to the end Node of the last arcs_in_signalling_block_section (i.e. arcs_in_signalling_block_section[NTotarcsInBlock-1]) of signalling_block_sections[z]

								int N_TotarcsINBlock = signalling_block_sections[z].total_arcs;
								signalling_block_sections[z].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.respectOrder = true;
								signalling_block_sections[z].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.indexOrderList = i;
								SignalNodeID = signalling_block_sections[z].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.ID;
								SignalNodeX = signalling_block_sections[z].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.X;
								SignalNodeTDSB_ID = signalling_block_sections[z].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.tdsbId;
								break; // break the for loop of iterator z iterating through the Blocks
							}
						}
						// if a real non-virtual exit signal of the block section starting with the identified Virtual Signal exists
						// then assign the Respect order also to all of the next block sections which start with such a non-virtual signal (i.e. having entry signal coinciding with such a non-virtual signal)
						if ((SignalNodeID != -1) && (SignalNodeX != -1) && (SignalNodeTDSB_ID != "None")) {
							// Check now for all the block sections which start with the such a non-virtual signal which might be either block sections on the same trackline of the identified block or not
							// To those block sections we need to assign the respect order conditions to the start Node of the first Arc of the block
							for (int k = 0; k < Blocks; k++) {

								// Impose the respect order also the entry signal of block sections whose entry signals coincides with the exit signal of the block section identified in the piece of code above, simply because the exit signal of this block section and the entry signal of the previous block section are the same signal
								if ((signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.ID == SignalNodeID) && (signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.X == SignalNodeX) && (signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.tdsbId == SignalNodeTDSB_ID)) {
									// This is imposed to the end Node of the last Arc of the block section
									signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.respectOrder = true;
									signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.indexOrderList = i;
									// As well as to the start Node (i.e. the actual entry signal Node) of the block section itself
									signalling_block_sections[k].start_node.respectOrder = true;
									signalling_block_sections[k].start_node.indexOrderList = i;
								}

								int N_TotarcsINBlock = signalling_block_sections[k].total_arcs;
								// Impose the respect order also the exit signal of block sections whose exit signals coincides with the entry signal of the block section identified in the piece of code above, simply because the exit signal of this block section and the entry signal of the previous block section are the same signal
								if ((signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.ID == SignalNodeID) && (signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.X == SignalNodeX) && (signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.tdsbId == SignalNodeTDSB_ID)) {
									// This is imposed to the end Node of the last Arc of the block section
									signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.respectOrder = true;
									signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.indexOrderList = i;
									// As well as to the last Node of the block section itself
									signalling_block_sections[k].end_node.respectOrder = true;
									signalling_block_sections[k].end_node.indexOrderList = i;
								}
							}
						}
					}
				}
			}
		}
	}
}

// check train arrival/departure at/from destination/origin
void Train::checkTrainArrDep(int trainIdx, int t) {
	// add X to last station if needed (initialized as 0 in the beginning)
	if (Stations[numStations - 1].X == 0) {
		// find station position
		for (int j = 0; j < numStations; j++) {
			if (Stations[numStations - 1].stationName == StationArray[j].stationName) {
				Stations[numStations - 1].X = StationArray[j].X;
				break;
			}
		}
	}

	// add X to fisrt station if needed (initialized as 0 in the beginning)
	if (Stations[0].X == 0) {
		// find station position
		for (int j = 0; j < numStations; j++) {
			if (Stations[0].stationName == StationArray[j].stationName) {
				Stations[0].X = StationArray[j].X;
				break;
			}
		}
	}

	// train position
	double X = trainXPosition(t);

	// check arrivals and departures
	// for (int a = 0; a < numStations; a++) {
	//	if (std::fabs(X- Stations[a].X) < 0.001)
	//	{
	// cout << "\n<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n " <<
	//	trainDescription << " TRAIN at " << Stations[numStations - 1].stationName << " at t = " << t << endl;
	//	}
	//}
	// train stopped at last station
	if (((std::fabs(X - Stations[numStations - 1].X) < 0.001) || (std::fabs(std::fabs(X - Stations[numStations - 1].X) - train_length / 1000) < 0.001)) && (t >= 2) && (instant_train_speed[t - 2] != 0) && (instant_train_speed[t - 1] == 0) && (instant_train_speed[t] == 0)) { // position tolerance of 1m
		printTrainArrDepMsg(Stations[numStations - 1].stationName, "arr", trainIdx, t, initial_variables.InputMainFolder + "/Rescheduling");
		cout << "\n<<< " << trainDescription << " ARRIVED at " << Stations[numStations - 1].stationName << " at t = " << t << " - " << simulationTime(t, initial_variables.startingSimulationTime) << endl;
	}
	// train departing from origin
	else if (((std::fabs(X - Stations[0].X) < 0.001) || (std::fabs(std::fabs(X - Stations[0].X) - train_length / 1000) < 0.001)) && (t >= 1) && (instant_train_speed[t - 1] == 0) && (instant_train_speed[t] != 0)) { // position tolerance of 1m
		// printTrainArrDepMsg(Stations[0].stationName, "dep", trainIdx, t, "Input_EGTRAIN/Rescheduling");
		cout << "\n>>> " << trainDescription << " DEPARTED from " << Stations[0].stationName << " at t = " << t << " - " << simulationTime(t, initial_variables.startingSimulationTime) << endl;
	}
}

// check and execute dispatching decisions
void Train::executeDispDecisions(int t) {
	// check any existing dispatching decisions
	for (auto it = dispDecisions.begin(); it != dispDecisions.end();) {
		// DWELL MESSAGE
		if (it->messageType == "dwell") {
			// implement decision
			implementDwell(*it);

			// remove implemented decision from vector
			it = dispDecisions.erase(it);
		}
		// DISP MESSAGE
		else if (it->messageType == "disp") {
			// check possible departure time (the time the train can actually depart respecting dwell and shunting)
			int possibleDepTime = INT_MAX;							   // initialize as maximum int value to not trigger if condition unless value is changed
			int shuntingTime = 5 * 60;								   // using 5 min as shunting time
			int stoppedTime = Stations[numStations - 1].StepStopped + 1; // + 1sec to account that last update was at t-1

			// check if it is the intended time to departure
			if (t >= it->intendedDepTime) {
				// train continues (needs just dwell time)
				if (train_route[indexOfRoute].reversed_direction == train_route[it->routeID].reversed_direction) { // compare directions of both previous and new route
					// dwell time respected
					if (stoppedTime > Stations[numStations - 1].StopTime) { // train can only depart 1sec after time set for departure
						// depart after dwell time
						possibleDepTime = t;
					}
				}
				// train goes back (needs time for shunting)
				else {
					// shunting time respected
					if (stoppedTime > shuntingTime) { // train can only depart 1sec after time set for departure
						// depart after shunting time
						possibleDepTime = t;
					}
				}
			}

			// check if train can leave (interlocking area not occupied by train entering station)
			bool canLeaveStation = true;
			if (t >= possibleDepTime) {
				std::vector<std::string> stationsToProtect = {"Alm", "Asd", "Asdz"};
				if (std::find(stationsToProtect.begin(), stationsToProtect.end(), Stations[numStations - 1].stationName) != stationsToProtect.end()) {
					for (int tr = 0; tr < numRegions; tr++) {
						if (regional_train[tr].type == type && regional_train[tr].ID == ID) {
							continue;
						}
						if (regional_train[tr].Stations[regional_train[tr].numStations - 1].stationName == Stations[numStations - 1].stationName && regional_train[tr].reservedPlatform != -1 && regional_train[tr].CanEnter == true) {
							// other train not yet at platform (2 meter margin)
							if (std::abs(regional_train[tr].trainXPosition(t - 1) - trainXPosition(t - 1)) > 0.002) {
								canLeaveStation = false;
								break;
							}
						}
					}
				}
			}

			// check if it is time to implement decision
			if (t >= possibleDepTime && canLeaveStation == true) {
				// change intendedDepTime of decision to possibleDepTime
				it->intendedDepTime = possibleDepTime;

				// implement decision
				implementDisp(*it);

				// remove implemented decision from vector
				it = dispDecisions.erase(it);
			} else {
				// next element (increment only when no erase occurs)
				++it;
			}
		}
	}
}

// implement init message
void Train::implementInitMsg(DispatchDecision decision) {
	std::string stationDep = decision.stationDep;
	int routeID = decision.routeID;
	int startTime = decision.startTime; // time to enter the simulation and go to the platform waiting

	// add station
	Node* stations = new Node[1];
	stations[0].dwellTime = 60;
	stations[0].StopTime = stations[0].dwellTime / timestep;
	stations[0].stationName = stationDep;

	// find station position
	for (int j = 0; j < numStations; j++) {
		if (stations[0].stationName == StationArray[j].stationName) {
			stations[0].X = StationArray[j].X;
			break;
		}
	}

	// change stations
	numStations = 1;
	Stations = stations;

	// change route
	int prevIndex_Of_Route = indexOfRoute;
	indexOfRoute = routeID;
	TrainRouteID = train_route[routeID].ID;
	Start_Node_X = train_route[routeID].x_of_start_node;
	cacheStationPositions();

	// set train position
	if (train_route[indexOfRoute].reversed_direction) {
		std::fill_n(instant_spatial_position.begin(), instant_spatial_position.size(), train_route[indexOfRoute].OriginalRefReversedRoute);
	} else {
		std::fill_n(instant_spatial_position.begin(), instant_spatial_position.size(), train_route[indexOfRoute].x_of_start_node * 1000);
	}

	// change timetable
	departure_time = startTime;
	scheduled_departure_time = startTime;

	// update dispLineID
	dispLineID = decision.lineID;

	// update arrival platform
	arrivalPlatform = decision.arrivalPlatform;

	// book platform
	reservedPlatform = decision.arrivalPlatform;

	// set departure time (huge to force the train to wait at station)
	ScheduledDepartures[0] = 10000;
}

// implement disp message from dispatching tool
void Train::implementDisp(DispatchDecision decision) {
	std::string stationArr = decision.stationArr;
	int routeID = decision.routeID;
	std::string timetableFile = decision.timetableFile;
	int intendedDepTime = decision.intendedDepTime;

	// keep current train position
	double X = trainXPosition(intendedDepTime - 1);

	// set end time as next intended departure (train will be at same location from now until then - waiting at station)
	End_Time = intendedDepTime - 1; // finish the leg right before starting next leg

	// print train service path diagram before next service
	printTrainServicePathDiagram(initial_variables.OutputMainFolder + "/TrainTrajectories", routeID);

	// variables to change stations
	int noStations = 0; // counter/index
	std::vector<Node> stationsVec;

	// read timetable from file
	std::ifstream myfile_timetable(initial_variables.InputMainFolder + "/Timetable/" + timetableFile);

	// open file
	if (myfile_timetable.is_open()) {
		std::string line;

		// read stations line by line
		while (getline(myfile_timetable, line)) {
			std::string stationName, dwTString, depTimeString, arrTimeString;
			double dwellTime;
			float depTime, arrTime;

			// ignore empty lines (in the end of the file)
			if (line.empty()) {
				continue;
			}

			// parse line
			std::istringstream lineStream(line);
			getline(lineStream, stationName, '\t');
			getline(lineStream, dwTString, '\t');
			getline(lineStream, arrTimeString, '\t');
			getline(lineStream, depTimeString, '\t');

			// conversion to int
			dwellTime = std::stod(dwTString);
			arrTime = std::stof(arrTimeString);
			depTime = std::stof(depTimeString);

			// save stop details
			Node stop;
			stop.stationName = stationName;

			// custom dwell time
			if (noStations != 0 && !decision.dwellTimes.empty() && noStations <= decision.dwellTimes.size()) {
				stop.dwellTime = decision.dwellTimes[noStations - 1];
			}
			// default dwell time
			else {
				stop.dwellTime = dwellTime;
			}
			stop.StopTime = stop.dwellTime / timestep;

			// keep stopped time for current station
			if (noStations == 0) {
				stop.StepStopped = Stations[numStations - 1].StepStopped; // noStations can be used as index
			} else {
				stop.StepStopped = 0; // noStations can be used as index
			}

			// add to vector
			stationsVec.push_back(stop);

			// save scheduled arr times
			ScheduledArrivals[noStations] = arrTime; // noStations can be used as index

			// save scheduled dep times
			ScheduledDepartures[noStations] = depTime; // noStations can be used as index

			// increase index/counter
			noStations++;
		}
		// close file
		myfile_timetable.close();
	} else {
		// error opening the file
		std::cout << "Unable to open file with timetable. Rescheduling decision not implemented.\n";

		// discard changes
		return;
	}

	// build sequence of stations
	Node* stations = new Node[noStations];
	for (int i = 0; i < noStations; i++) {
		stations[i].dwellTime = stationsVec[i].dwellTime;
		stations[i].StopTime = stationsVec[i].StopTime;
		stations[i].stationName = stationsVec[i].stationName;
		stations[i].StepStopped = stationsVec[i].StepStopped;

		// find station position
		for (int j = 0; j < numStations; j++) {
			if (stations[i].stationName == StationArray[j].stationName) {
				stations[i].X = StationArray[j].X;
				break;
			}
		}
	}

	// change stations
	numStations = noStations;
	Stations = stations;

	// route does not change
	if (routeID == indexOfRoute) {
		std::fill_n(instant_spatial_position.begin(), instant_spatial_position.size(), instant_spatial_position[intendedDepTime - 1]);
		cacheStationPositions();
	}
	// change to new route
	else {
		// change route
		int prevIndex_Of_Route = indexOfRoute;
		indexOfRoute = routeID;
		TrainRouteID = train_route[routeID].ID;
		Start_Node_X = train_route[routeID].x_of_start_node;
		cacheStationPositions();

		// update train position in the new route
		if (train_route[indexOfRoute].reversed_direction) {
			instant_spatial_position[intendedDepTime] = train_route[indexOfRoute].OriginalRefReversedRoute - (X * 1000);

			if (train_route[indexOfRoute].reversed_direction != train_route[prevIndex_Of_Route].reversed_direction) {
				instant_spatial_position[intendedDepTime] += train_length; // add train_length to update position of head
			}

			std::fill_n(instant_spatial_position.begin(), instant_spatial_position.size(), instant_spatial_position[intendedDepTime]);
		} else {
			instant_spatial_position[intendedDepTime] = X * 1000;

			if (train_route[indexOfRoute].reversed_direction != train_route[prevIndex_Of_Route].reversed_direction) {
				instant_spatial_position[intendedDepTime] += train_length; // add train_length to update position of head
			}

			std::fill_n(instant_spatial_position.begin(), instant_spatial_position.size(), instant_spatial_position[intendedDepTime]);
		}

		// update 1st station boolean in the new route using cached index
		if (stationBlockSection[0] >= 0 && stationArc[0] >= 0) {
			train_route[indexOfRoute].sequence_of_block_sections[stationBlockSection[0]].arcs_in_signalling_block_section[stationArc[0]].endNode.station = 1;
		}
	}

	// change timetable
	departure_time = intendedDepTime;
	scheduled_departure_time = intendedDepTime;

	// update dispLineID
	dispLineID = decision.lineID;

	// update arrival platform
	arrivalPlatform = decision.arrivalPlatform;

	// release platform
	reservedPlatform = -1;

	// store intendedDepTime (used to avoid printing train path diagram always from t=0)
	prevIntendedDepTime = intendedDepTime;
}

// implement dwell message from dispatching tool
void Train::implementDwell(DispatchDecision decision) {
	// update intermediate station dwell times
	for (int i = 1; i < (numStations - 1); i++) {
		Stations[i].dwellTime = decision.dwellTimes[i - 1];
		Stations[i].StopTime = decision.dwellTimes[i - 1] / timestep;
	}
}

// print train service path diagram (append to file)
void Train::printTrainServicePathDiagram(std::string FolderName, int nextServiceRouteID) {
	std::string FileName = FolderName + "/TrainServicePathDiagram.txt";
	std::ofstream FileOutput;

	// avoid different corridor for initial service (entering station for the first time)
	if (prevIntendedDepTime == 0 && nextServiceRouteID != -1 && train_route[indexOfRoute].corridor != train_route[nextServiceRouteID].corridor && train_route[indexOfRoute].reversed_direction == train_route[nextServiceRouteID].reversed_direction) {
		indexOfRoute = nextServiceRouteID;
	}

	FileOutput.open((char*)FileName.c_str(), std::ios::binary | std::ios::app); // append

	FileOutput << trainDescription << "\t" << dispLineID << "\t" << train_route[indexOfRoute].reversed_direction << "\t" << train_route[indexOfRoute].corridor << "\t";

	const int activeFirst = earliestActiveTrajectoryIndex < 0
			? -1
			: std::max(earliestActiveTrajectoryIndex, prevIntendedDepTime);
	const auto exportCells = trajectoryExportCells(instant_spatial_position, activeFirst, End_Time);
	for (int t = 0; t < initial_variables.times; t++) {
		const double position = t < static_cast<int>(exportCells.size()) ? exportCells[t] : -9999;
		if (position != -9999) {
			// non-reversed route (same with/without jump)
			if (!train_route[indexOfRoute].reversed_direction) {
				FileOutput << position << "\t";
			}
			// reversed route without jump
			else if (train_route[indexOfRoute].diffRegionsJumpX.first == 0) {
				FileOutput << (train_route[indexOfRoute].OriginalRefReversedRoute - position) << "\t";
			}
			// reversed route with jump
			else {
				FileOutput << ((train_route[indexOfRoute].OriginalRefReversedRoute - position) - (train_route[indexOfRoute].diffRegionsJumpX.first * 1000)) << "\t";
			}
		} else {
			FileOutput << "\t";
		}
	}
	FileOutput << std::endl;

	FileOutput.close();
}

// print train arrival at terminal station (append to file)
void Train::printTrainArrDepMsg(std::string stationName, std::string msgType, int trainIdx, int t, std::string FolderName) {
	std::string FileName = FolderName + "/EGTRAINOutput.txt";
	std::ofstream FileOutput;

	FileOutput.open((char*)FileName.c_str(), std::ios::binary | std::ios::app); // append

	if (FileOutput.is_open()) {
		FileOutput << "egtrain," << stationName << "," << msgType << "," << dispLineID << "," << trainIdx << "," << t << "\n";

		FileOutput.close();

		// temporary solution to avoid file conflicts
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	} else // error opening file
	{
		std::cout << "Error2 opening file to write arr/dep message\n";
	}
}

// computes train X position (works with routes crossing different regions)
// used to get real X position (in routes with 2 regions, there are jumps when getting X from the position in the route directly)
// wagon = 0 by default to return head position
double Train::trainXPosition(int t, int wagon /*= 0*/) {
	double X;
	double portion = 0; // relative position from start_node (in meters)

	double total_nW = number_of_wagons + 1; // train.number_of_wagons does not include loco/first wagon

	// find occupied block section
	for (int i = 0; i < train_route[indexOfRoute].N_Block_Sections; i++) {
		if (((instant_spatial_position[t] - wagon * (train_length / total_nW)) >= train_route[indexOfRoute].sequence_of_block_sections[i].start_node.X * 1000) && (instant_spatial_position[t] - wagon * (train_length / total_nW)) < (train_route[indexOfRoute].sequence_of_block_sections[i].end_node.X * 1000)) {
			portion = (instant_spatial_position[t] - wagon * (train_length / total_nW)) - train_route[indexOfRoute].sequence_of_block_sections[i].start_node.X * 1000;

			// calculate X position
			if (train_route[indexOfRoute].reversed_direction) {
				X = (train_route[indexOfRoute].sequence_of_block_sections[i].GeoXBegNode - portion) / 1000;
			} else {
				X = (train_route[indexOfRoute].sequence_of_block_sections[i].GeoXBegNode + portion) / 1000;
			}
			return X;
		}
	}

	// did not find position correctly
	return -1;
}

// function to occupy entire single track
void Train::occupySingleTrack(Section* BS, int Blocks, int hTail, int hHead, int t) {
	// check all single tracks
	for (int l = 0; l < singleTrackLimits.size(); l++) {
		int firstBS = -1, lastBS = -1;
		bool outOfSingleTrack = true;
		std::string firstLimit = std::get<0>(singleTrackLimits[l]), secondLimit = std::get<1>(singleTrackLimits[l]);

		// reorder limits for reversed routes (limits always oriented with X increasing)
		if (train_route[indexOfRoute].reversed_direction) {
			firstLimit = std::get<1>(singleTrackLimits[l]);
			secondLimit = std::get<0>(singleTrackLimits[l]);
		}

		for (int i = 0; i < Blocks; i++) {
			// begin of single track
			if (BS[i].ID == firstLimit) {
				// train after begin single track
				if (hHead >= (i - 1)) {
					// block also previous signalling_block_sections
					firstBS = i - 1;

					// route bound
					if (firstBS < 0) {
						firstBS = 0;
					}
				}
				// train before single track
				else {
					break;
				}
			}

			// end of single track
			if (BS[i].ID == secondLimit) {
				// train before end single track
				if (hTail <= (i + 1)) {
					// occupy also next signalling_block_sections
					lastBS = i + 1;

					// route bound
					if (lastBS > (Blocks - 1)) {
						lastBS = Blocks - 1;
					}
				}
				// train after single track
				else {
					break;
				}
			}

			// position found when train route crosses the entire single track
			if (firstBS != -1 && lastBS != -1) {
				outOfSingleTrack = false;
				break;
			}
			// position found when train route crosses only one limit of single track
			else if ((i == (Blocks - 1)) && ((firstBS != -1) || (lastBS != -1))) {
				outOfSingleTrack = false;
			}
		}

		// single track occupied
		if (!outOfSingleTrack) {
			// set train occupying the single track
			std::get<2>(singleTrackLimits[l]) = type + std::to_string(ID);

			// find entrance/exit signalling_block_sections
			int prevBlocksST[2] = {-1, -1};
			int blocksST[2] = {-1, -1};
			for (int i = 0; i < ::Blocks; i++) {
				// prev signalling_block_sections entrance
				if (::signalling_block_sections[i].ID == std::get<0>(singleTrackLimits[l])) {
					prevBlocksST[0] = i;
				}
				// signalling_block_sections entrance
				if (::signalling_block_sections[i].ID == std::get<3>(singleTrackLimits[l])) {
					blocksST[0] = i;
				}
				// prev signalling_block_sections exit
				if (::signalling_block_sections[i].ID == std::get<1>(singleTrackLimits[l])) {
					prevBlocksST[1] = i;
				}
				// signalling_block_sections exit
				if (::signalling_block_sections[i].ID == std::get<4>(singleTrackLimits[l])) {
					blocksST[1] = i;
				}
			}

			// occupy entry and exit of single track
			for (int i = 0; i < 2; i++) {
				int h = blocksST[i];
				int Prev_Block = prevBlocksST[i];
				occupyBlockAndConnected(::signalling_block_sections[h], ::signalling_block_sections[Prev_Block], -1, -1);
				// if the Block Section has a switch in diverging position
				if (::signalling_block_sections[h].withSwitchDiv == true) {
					if ((::signalling_block_sections[h].SignallingLevel == 3) || (::signalling_block_sections[h].SignallingLevel == 4)) {
						activateBlocksWithSwitchesDiv(::signalling_block_sections[h], ::signalling_block_sections[Prev_Block].trackLineId, ::signalling_block_sections[h].XStartSwitch); // XStartSwitch to occupy all signalling_block_sections
					} else {
						activateBlocksWithSwitchesDivFixedBlock(::signalling_block_sections[h], ::signalling_block_sections[Prev_Block].trackLineId, -1);
					}
				}
			}
		}

		// release single track if train left it
		if (std::get<2>(singleTrackLimits[l]) == (type + std::to_string(ID))) {
			int hRelease = -1;

			// non-rev route (left end of single track - X-oriented)
			if (!train_route[indexOfRoute].reversed_direction && hTail > 1 && std::get<1>(singleTrackLimits[l]) == BS[hTail - 2].ID) {
				// train left single track 1 timestep ago
				if (((instant_spatial_position[(t - 1) - (int)(S_delay / timestep)] - train_length) < BS[hTail].end_node.X * 1000) && ((instant_spatial_position[(t - 1) - (int)(S_delay / timestep)] - train_length) >= BS[hTail].start_node.X * 1000)) {
					// release begin of single track
					// find signalling_block_sections to release
					for (int h = 0; h < ::Blocks; h++) {
						if (::signalling_block_sections[h].ID == std::get<3>(singleTrackLimits[l])) {
							hRelease = h;
							if (hRelease < 0) {
								hRelease = 0;
							}
							break;
						}
					}
				}
			}
			// rev route (left begin of single track - X-oriented)
			else if (train_route[indexOfRoute].reversed_direction && hTail > 1 && std::get<0>(singleTrackLimits[l]) == BS[hTail - 2].ID) {
				// train left single track 1 timestep ago
				if (((instant_spatial_position[(t - 1) - (int)(S_delay / timestep)] - train_length) < BS[hTail].end_node.X * 1000) && ((instant_spatial_position[(t - 1) - (int)(S_delay / timestep)] - train_length) >= BS[hTail].start_node.X * 1000)) {
					// release begin of single track
					// find signalling_block_sections to release
					for (int h = 0; h < ::Blocks; h++) {
						if (::signalling_block_sections[h].ID == std::get<4>(singleTrackLimits[l])) {
							hRelease = h;
							if (hRelease < 0) {
								hRelease = 0;
							}
							break;
						}
					}
				}
			}

			// release entrance of single track
			if (hRelease != -1) {
				releaseLastBlockAndConnected(::signalling_block_sections[hRelease]);

				// set single track as not occupied
				std::get<2>(singleTrackLimits[l]).clear();
			}
		}
	}
}

// unlock single track (unlock signalling_block_sections for a train passing a single track)
void Train::unlockSingleTrack(Section* BS, int Blocks, int t) {
	for (int l = 0; l < singleTrackLimits.size(); l++) {
		// single track occupied by this train
		if (std::get<2>(singleTrackLimits[l]) == (type + std::to_string(ID))) {
			// signalling_block_sections occupied by the train
			// collect all signalling_block_sections from head to tail
			int hTail = 0, hHead = 0;
			for (int h = 0; h < Blocks; h++) {
				if (((instant_spatial_position[t - (int)(S_delay / timestep)] - train_length) < BS[h].end_node.X * 1000) && ((instant_spatial_position[t - (int)(S_delay / timestep)] - train_length) >= BS[h].start_node.X * 1000)) {
					hTail = h;
				}
				if ((instant_spatial_position[t - (int)(S_delay / timestep)] < BS[h].end_node.X * 1000) && (instant_spatial_position[t - (int)(S_delay / timestep)] >= BS[h].start_node.X * 1000)) {
					hHead = h;
					break;
				}
			}

			// release end of single track (2 signalling_block_sections to avoid yellow and red signals)
			if (((hHead < (Blocks - 1)) && (!train_route[indexOfRoute].reversed_direction && BS[hHead + 1].ID == std::get<1>(singleTrackLimits[l])) || (train_route[indexOfRoute].reversed_direction && BS[hHead + 1].ID == std::get<0>(singleTrackLimits[l]))) || ((!train_route[indexOfRoute].reversed_direction && BS[hHead].ID == std::get<1>(singleTrackLimits[l])) || (train_route[indexOfRoute].reversed_direction && BS[hHead].ID == std::get<0>(singleTrackLimits[l])))) {
				int hRelease = hHead + 1;

				// changes on previous signalling_block_sections
				if (hRelease > 0) {
					snprintf(BS[hRelease - 1].state, sizeof(BS[hRelease - 1].state), "%s", "green");
					for (int k = 0; k < BS[hRelease - 1].total_arcs; k++) {
						BS[hRelease - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}

					// revert changes to speeds
					double MinSpeedLim = BS[hRelease].arcs_in_signalling_block_section[0].speedLimit;
					if (BS[hRelease].arcs_in_signalling_block_section[0].signalSpeedLimit < MinSpeedLim) {
						MinSpeedLim = BS[hRelease].arcs_in_signalling_block_section[0].signalSpeedLimit;
					}

					// the speed limit at the end of block section BLS[h] is MinSpeedLim
					BS[hRelease - 1].arcs_in_signalling_block_section[BS[hRelease - 1].total_arcs - 1].speedInBraking = MinSpeedLim;
				}

				// changes on signalling_block_sections itself
				BS[hRelease].code = 270;
				BS[hRelease].exit_speed = 0;
			}
		}
	}
}

// function to protect all station areas
void protectStationAreas(int i) {
	for (int k = 0; k < numRegions; k++) {
		// cout << ">>>>>>>>>" << regional_train[k].trainDescription << "is OutOfSimulation" << regional_train[k].OutOfSimulation << endl;
		if (!regional_train[k].OutOfSimulation) {
			if ((i >= regional_train[k].departure_time) && regional_train[k].CanEnter) {
				// find signalling_block_sections occupied by head of train
				int hHead = 0; // hTail = 0;
				for (int h = 0; h < train_route[regional_train[k].indexOfRoute].N_Block_Sections; h++) {
					if ((regional_train[k].instant_spatial_position[i - (int)(S_delay / timestep)] < train_route[regional_train[k].indexOfRoute].sequence_of_block_sections[h].end_node.X * 1000) && (regional_train[k].instant_spatial_position[i - (int)(S_delay / timestep)] >= train_route[regional_train[k].indexOfRoute].sequence_of_block_sections[h].start_node.X * 1000)) {
						hHead = h;
						break;
					}
				}

				// occupy station interlocking area
				for (int s = 0; s < stationBoundarySections.size(); s++) {
					// check if section ahead of train is entrance of station
					if (stationBoundarySections[s].entrance->ID == train_route[regional_train[k].indexOfRoute].sequence_of_block_sections[hHead + 1].ID) {
						// check if plaform is booked or train leaving
						bool platformBooked = false;
						std::vector<std::string> stationsToProtect = {"Alm", "Asd", "Asdz"};
						if (std::find(stationsToProtect.begin(), stationsToProtect.end(), regional_train[k].Stations[regional_train[k].numStations - 1].stationName) != stationsToProtect.end()) {
							if (!stationBoundarySections[s].exit) {
								for (int tr = 0; tr < numRegions; tr++) {
									if (regional_train[tr].ID == regional_train[k].ID && regional_train[tr].type == regional_train[k].type) {
										continue;
									}
									if (regional_train[tr].Stations[regional_train[tr].numStations - 1].stationName == regional_train[k].Stations[regional_train[k].numStations - 1].stationName && regional_train[tr].reservedPlatform == regional_train[k].arrivalPlatform) {
										platformBooked = true;
										break;
									}
									// wait for trains leaving the station to prevent deadlocks
									if (regional_train[tr].Stations[0].stationName == regional_train[k].Stations[regional_train[k].numStations - 1].stationName) {
										if (train_route[regional_train[tr].indexOfRoute].reversed_direction != train_route[regional_train[k].indexOfRoute].reversed_direction) {
											// if the other train is not booking a platform, it is not at the platform - enough to check position
											if (train_route[regional_train[k].indexOfRoute].reversed_direction == false && regional_train[tr].trainXPosition(i) > regional_train[k].trainXPosition(i) && regional_train[tr].numStations > 1) {
												platformBooked = true;
												break;
											} else if (train_route[regional_train[k].indexOfRoute].reversed_direction == true && regional_train[tr].trainXPosition(i) < regional_train[k].trainXPosition(i) && regional_train[tr].numStations > 1) {
												platformBooked = true;
												break;
											}
										}
									}
								}
							}
						}

						stationBoundarySections[s].protectEntrance(hHead + 1, regional_train[k].indexOfRoute, platformBooked);
						break; // train occupies at most one station area
					}
					// check if section ahead of train is section before entrance of station
					else if (stationBoundarySections[s].entrance->ID == train_route[regional_train[k].indexOfRoute].sequence_of_block_sections[hHead + 2].ID) {
						// do not protect if section before entrance is occupied by another train, otherwise it will be blocked
						bool needToBlock = true;
						for (auto occ = BlocksOccupied.begin(); occ != BlocksOccupied.end(); ++occ) {
							if (train_route[regional_train[k].indexOfRoute].sequence_of_block_sections[hHead + 1].ID == *occ) {
								needToBlock = false;
								break;
							}
						}
						if (needToBlock) {
							// check if plaform is booked
							bool platformBooked = false;
							std::vector<std::string> stationsToProtect = {"Alm", "Asd", "Asdz"};
							if (std::find(stationsToProtect.begin(), stationsToProtect.end(), regional_train[k].Stations[regional_train[k].numStations - 1].stationName) != stationsToProtect.end()) {
								if (!stationBoundarySections[s].exit) {
									for (int tr = 0; tr < numRegions; tr++) {
										if (regional_train[tr].ID == regional_train[k].ID && regional_train[tr].type == regional_train[k].type) {
											continue;
										}
										if (regional_train[tr].Stations[regional_train[tr].numStations - 1].stationName == regional_train[k].Stations[regional_train[k].numStations - 1].stationName && regional_train[tr].reservedPlatform == regional_train[k].arrivalPlatform) {
											platformBooked = true;
											break;
										}
										// wait for trains leaving the station to prevent deadlocks
										if (regional_train[tr].Stations[0].stationName == regional_train[k].Stations[regional_train[k].numStations - 1].stationName) {
											if (train_route[regional_train[tr].indexOfRoute].reversed_direction != train_route[regional_train[k].indexOfRoute].reversed_direction) {
												// if the other train is not booking a platform, it is not at the platform - enough to check position
												if (train_route[regional_train[k].indexOfRoute].reversed_direction == false && regional_train[tr].trainXPosition(i) > regional_train[k].trainXPosition(i) && regional_train[tr].numStations > 1) {
													platformBooked = true;
													break;
												} else if (train_route[regional_train[k].indexOfRoute].reversed_direction == true && regional_train[tr].trainXPosition(i) < regional_train[k].trainXPosition(i) && regional_train[tr].numStations > 1) {
													platformBooked = true;
													break;
												}
											}
										}
									}
								}
							}

							stationBoundarySections[s].protectEntrance(hHead + 2, regional_train[k].indexOfRoute, platformBooked);
						}
						break; // train occupies at most one station area
					}
					// check if train entered station area
					else if (stationBoundarySections[s].entrance->ID == train_route[regional_train[k].indexOfRoute].sequence_of_block_sections[hHead].ID) {
						// FOR NOW, IGNORE AREAS WITH EXIT SECTION
						if (stationBoundarySections[s].exit) {
							continue;
						}

						regional_train[k].reservedPlatform = regional_train[k].arrivalPlatform;
						break;
					}
				}
			}
		}
	}
}

// set vector sizes with length of simulation from user input
void Train::setTrainVectorSizesFromInput(int vec_size) {
	// define vector sizes with length of simulation from user input
	instant_train_speed = std::vector<double>(vec_size, 0);
	instant_spatial_position = std::vector<double>(vec_size, 0);
	instant_train_power_consumption = std::vector<double>(vec_size, 0);
	instant_block_section_occupied = std::vector<std::string>(vec_size);
	instant_train_energy_consumption = std::vector<double>(vec_size, 0);
	BX = std::vector<double>(vec_size, 0);
	Xob = std::vector<double>(vec_size, 0);
	Vob = std::vector<double>(vec_size, 0);
	Eq = std::vector<int>(vec_size, 0);

	// previously on train class constructor (there won't work because size of vector is zero until user sets length of simulation and constructor is called before that)
	instant_train_energy_consumption[0] = 0;
}
