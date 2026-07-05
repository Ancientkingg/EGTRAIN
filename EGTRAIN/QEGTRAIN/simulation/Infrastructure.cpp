#include "simulation/Infrastructure.h"
#include <cstdio>
#include <cfloat>

extern Logger owl;
int t[40000];
double timestep = 1;
// double times = 0;
double signalCode1 = 11.111, signalCode2 = 0, signalCode3 = 0; // Signalling System Speed Codes (for Track Circuit Sisgnalling System: blockSets.A.connections.connections.)
int numTrackLines = 0;				   // This is the total number of TrackLines
int Signalling_Level = 0;			   // This variable set the type of signalling system implemented: if it is set to 0-> ETCS-Level 0 (Track Circuit), if it is set to 1-> ETCS-Level 1, and so on.
int Headway = 0;
int HeadwayD = 0; // This Headway is calculated on the basis of Block Section Length (indipendently of the signalling system this headway consents the train to start his run (from BS[0]) only when green light is on) Headway is for the Even Track while HeadwayD for the Odd Track

list<StationPlatform> AllStationPlatforms; // This is a global list containing all Station platforms existing in the modelled network
int numAllStationPlatforms = 0;			   // This variable provides the total number of station platforms in the modelled network (it is the size of the list AllStationPlatforms)

InitialParameters initial_variables(0);
string InputMainFolder = initial_variables.InputMainFolder; // "Input_EGTRAIN";       //This is the Folder of all the input of EGTRAIN
// string OutputMainFolder = initial_variables.OutputMainFolder;// "Output_EGTRAIN";       //This is the Folder of all the input of EGTRAIN

// extern InitialParameters initial_variables;


// Class constructor function
InfraElement::InfraElement() {
	ID = SectionID = SwitchName = stationName = ConnectedPoint = "None";
	isStation = IsSwitch = IsSignal = IsTrackDetSecBorder = withSwitchDiv = false;
	XCoordinate = YCoordinate = GeoXCoord = GeoYCoord = -1;
	isEndOfDivSwitchStartOfADivSwitch = false;
	XConnectedPoint = -1;
}


// Node Default Values
Node::Node() {
	X = Y = dwellTime = 0;
	ID = 0;
	isSignalled = virtualCouplingNode = false;
	station = respectOrder = false;
	StopTime = 0;
	StepStopped = 0;
	numConnections = 0;
	indexOrderList = -1;
	tdsbGeoCoordX = tdsbGeoCoordY = -1;
	arcSpeedLimit = 0;
	latitude = longitude = graphX = graphY = 0;
	virtualSignal = false;
	stationPlatformId = "None";
	for (int i = 0; i < 6; i++) {
		connectIdBlockSet[i] = 0;
		connectXNode[i] = 0;
	}
}

void Node::initialiseIdConnectedBlocks(string* IDConnectedBSofBlock, int N_IDConnectedBSofBlock) {
	if (this->numConnections > 0) {
		for (int i = 0; i < this->numConnections; i++) {
			string NameToSearch;
			char blockSetId[100], XNode[100];
			snprintf(blockSetId, sizeof(blockSetId), "%d", this->connectIdBlockSet[i]);
			snprintf(XNode, sizeof(XNode), "%f", this->connectXNode[i]);

			NameToSearch = NameToSearch + "-B" + blockSetId + "@-" + XNode;
			if (N_IDConnectedBSofBlock > 0) {
				for (int h = 0; h < N_IDConnectedBSofBlock; h++) {
					string Prova = IDConnectedBSofBlock[h];
					IDConnectedBSofBlock[h].find(NameToSearch, 0);
					if (IDConnectedBSofBlock[h].find(NameToSearch, 0) != -1) {
						this->IDConnectedBlocks.push_back(IDConnectedBSofBlock[h]);
						break; // break the for loop on the strings of the connected blocks of the block section
					}
				}
			}
		}
	}
}

// Function to load Nodes parameter values
void loadNode(Node* N, int& numNodes, char* name1) {

	double** Matr;
	int NODS = 1500; /*char name1[100];*/
	int i = 0, j = 0;

	Matr = new double*[NODS]; // Defining a temporary matrix
	for (int h = 0; h < NODS; h++) {
		Matr[h] = new double[3];
	}

	/*cout<<"Insert the path of Node data file [ID, X(Km), Y(Km)]:  ";  cin.get(name1,99);*/

	ifstream nodinput;
	nodinput.open(name1, ios::binary);
	if (!nodinput) {
		cout << "Error 56 :impossible to open the file\n";
	}

	while (nodinput) {
		nodinput >> Matr[i][j];
		j++;
		numNodes++;
		if (j > 2) {
			i++;
			j = 0;
		}
	} // Reading Node data file

	numNodes = (numNodes - 1) / 3;

	nodinput.close();
	// cin.ignore();

	for (int i = 0; i < numNodes; i++) {
		N[i].ID = Matr[i][0];
		N[i].X = Matr[i][1];
		N[i].Y = Matr[i][2];
	}

	for (int i = 0; i < NODS; i++)
		delete Matr[i]; // Deleting Matrix
	delete Matr;
}


// Arc Default Values
Arc::Arc() {
	length = curvature = gradient = speedLimit = 0;
	ID = 0;
	signalSpeedLimit = 999;
	speedInBraking = 0;
	fs = 0;
	brakingDistance = 0;
}

void Arc::arcLength() {
	length = sqrt(pow((endNode.X - startNode.X), 2) + pow((endNode.Y - startNode.Y), 2)) * 1000;
}

// Function for loading Arc data(friend function of Arc class): it can access to private members

void loadArc(Arc* A, int& arcs, Node* N, int numNodes, char* name2) {
	double** Matr;
	int ARCS = 1500; /*char name2[100];*/
	int i = 0, j = 0;

	Matr = new double*[ARCS];
	for (int h = 0; h < ARCS; h++) {
		Matr[h] = new double[6];
	}

	/*cout<<"Insert the path of Arc data file [ID, Ns_ID, Ne_ID, Rcurv(m), gradient(%), Vlimit(m/s)]: ";  cin.get(name2,99);*/

	ifstream arcinput;
	arcinput.open(name2, ios::binary);
	if (!arcinput) {
		cout << "Error 54 :impossible to open the file : " << name2 << "\n";
	}

	while (arcinput) {
		arcinput >> Matr[i][j];
		j++;
		arcs++;
		if (j > 5) {
			i++;
			j = 0;
		}
	}

	arcs = (arcs - 1) / 6;

	arcinput.close(); // Closing Arc Data File
					  // cin.ignore();

	// Fixing Arc parameter values
	for (int i = 0; i < arcs; i++) {
		A[i].ID = Matr[i][0];
		A[i].startNode.ID = Matr[i][1];
		A[i].endNode.ID = Matr[i][2];
		A[i].curvature = Matr[i][3];
		A[i].gradient = Matr[i][4];
		A[i].speedLimit = Matr[i][5];
	}

	for (int i = 0; i < arcs; i++) {
		for (int j = 0; j < numNodes; j++) {
			if (A[i].startNode.ID == N[j].ID) {
				A[i].startNode = N[j];
			}
			if (A[i].endNode.ID == N[j].ID) {
				A[i].endNode = N[j];
			}
		}
		A[i].arcLength();
	}

	for (int i = 0; i < ARCS; i++)
		delete Matr[i]; // Deleting Temporary Matrix
	delete Matr;
}

// const int maxsize = 1500;

BlockSet::BlockSet() {
	ID = len = arcs = numNodes = 0;
	region = 0;
	graphID = -1;
	firstSwitchX = -1;
	lastSwitchX = DBL_MAX;
}

int BlockSet::findArc(Arc A) {
	for (int i = 0; i < len; i++)
		if (A == member[i])
			return i;
	return -1;
}

bool BlockSet::isMember(Arc A) {
	if (findArc(A) != -1)
		return true;
	else
		return false;
}

void BlockSet::showSet() {
	for (int i = 0; i < len; i++) {
		cout << member[i].ID << " ";
	}
}

// Set the final Abscissa and Braking distance of Track arcs
void BlockSet::setFs() {

	for (int i = 0; i < len; i++) {
		member[i].fs = member[i].length + member[i - 1].fs;
	}
	for (int i = len - 1; i >= 0; i--) {
		member[i].speedInBraking = member[i + 1].speedLimit;
	}
}

// Function to initialize the TrackLine: FolderName is the name of the Folder in which nodes and arcs are saved
void BlockSet::defineTrainPath(char* FolderName) {
	// FileNode is the name of the File containing the Nodes and FileArc contains the arcs instead
	string FileNode = FolderName, FileArc = FolderName;
	FileNode = FileNode + "/NodiCumPari.txt";
	loadNode(N, numNodes, (char*)FileNode.c_str()); // Load up Nodes

	FileArc = FileArc + "/ArchiCumPari.txt";
	loadArc(A, arcs, N, numNodes, (char*)FileArc.c_str()); // Load up Arcs
	for (int i = 0; i < arcs; i++) {
		*this = *this + A[i];
	} // Set members of the TrackLine
}

BlockSet blockSets[268];

// Function to Load all the TrackLines
void loadTrackLine(char* TrackLineFolder) {
	for (int i = 0; i < numTrackLines; i++) {
		string FolderName = TrackLineFolder;
		char BIndex[10];
		snprintf(BIndex, sizeof(BIndex), "/B%d", i);
		string Bnumber(BIndex);
		FolderName = FolderName + Bnumber;
		blockSets[i].ID = i; // Set the ID number
		blockSets[i].defineTrainPath((char*)FolderName.c_str());
		cout << "\rLoading TrackLine B" << i;

		// Print Text File that can be imprted in AutoCad for Viewing the Path
		ofstream TrackLineView;
		if (BIndex[0] == '/') {
			memmove(BIndex, BIndex + 1, strlen(BIndex));
		}
		string TrackLineName = InputMainFolder + "/TrackLines/";
		TrackLineName = TrackLineName + BIndex + ".txt";
		TrackLineView.open((char*)TrackLineName.c_str());
		for (int j = 0; j < blockSets[i].numNodes; j++) {
			TrackLineView << blockSets[i].N[j].X << "," << i << "\n";
		}
		TrackLineView.close();
	}
}

// This class define all connections amongst TrackLines, therefore represents switches, joints and all the other track connections
int numConnections = 0;

Connections::Connections() {
	idFirstTrackLine = idSecondTrackLine = 0;
	xFirstNode = xSecondNode = 0;
	speedlimit = 16.667; // by default the speedlimit on a switch is set to 16.667 m /s; i.e. 60 km/h
	graphXFirstNode = graphYFirstNode = graphXSecondNode = graphYSecondNode = 0;
}

// This function sets the connections between two nodes of two different TrackLines
void Connections::setConnections(BlockSet* blockSets) {
	for (int h = 0; h < numTrackLines; h++) {
		if (blockSets[h].ID == this->idFirstTrackLine) {
			// Modifying Node on the First BlockSet
			// This instance is to consider also the initial Node of the first member of the BlockSet
			if (blockSets[h].member[0].startNode.X == this->xFirstNode) {
				blockSets[h].member[0].startNode.numConnections++;																	// increasing number of connections
				blockSets[h].member[0].startNode.connectIdBlockSet[blockSets[h].member[0].startNode.numConnections - 1] = this->idSecondTrackLine; // setting ID of BlockSet for the first Node
				blockSets[h].member[0].startNode.connectXNode[blockSets[h].member[0].startNode.numConnections - 1] = this->xSecondNode;
			} // Setting X of the connected Node for the first Node
			  // This instance is instead to complete connections for all the other members
			for (int i = 0; i < blockSets[h].len; i++) {
				if (blockSets[h].member[i].endNode.X == this->xFirstNode) {
					if (blockSets[h].member[i].endNode.X == 53.783) {
						std::cout << "track " << h << " km " << blockSets[h].member[i].endNode.X;
					}
					blockSets[h].member[i].endNode.numConnections++;																	// increasing number of connections
					blockSets[h].member[i].endNode.connectIdBlockSet[blockSets[h].member[i].endNode.numConnections - 1] = this->idSecondTrackLine; // setting ID of BlockSet for the first Node
					blockSets[h].member[i].endNode.connectXNode[blockSets[h].member[i].endNode.numConnections - 1] = this->xSecondNode;
				} // Setting X of the connected Node for the first Node
			}
		}

		// Modifying Node on the Second BlockSet
		if (blockSets[h].ID == this->idSecondTrackLine) {
			// Modifying Node on the Second BlockSet
			// This instance is to consider also the initial Node of the first member of the BlockSet
			if (blockSets[h].member[0].startNode.X == this->xSecondNode) {
				blockSets[h].member[0].startNode.numConnections++;																   // increasing number of connections
				blockSets[h].member[0].startNode.connectIdBlockSet[blockSets[h].member[0].startNode.numConnections - 1] = this->idFirstTrackLine; // setting ID of BlockSet for the first Node
				blockSets[h].member[0].startNode.connectXNode[blockSets[h].member[0].startNode.numConnections - 1] = this->xFirstNode;
			} // Setting X of the connected Node for the first Node
			  // This instance is instead to complete connections for all the other members
			for (int i = 0; i < blockSets[h].len; i++) {
				if (blockSets[h].member[i].endNode.X == this->xSecondNode) {

					blockSets[h].member[i].endNode.numConnections++;																   // increasing number of connections
					blockSets[h].member[i].endNode.connectIdBlockSet[blockSets[h].member[i].endNode.numConnections - 1] = this->idFirstTrackLine; // setting ID of BlockSet for the first Node
					blockSets[h].member[i].endNode.connectXNode[blockSets[h].member[i].endNode.numConnections - 1] = this->xFirstNode;
					if (blockSets[h].member[i].endNode.X == 52.219) {
						std::cout << "track " << h << " km " << blockSets[h].member[i].endNode.X;
					}
				} // Setting X of the connected Node for the first Node
			}
		}
	}
}

Connections connections[708];

// Function for loading Connections
void loadConnectionsOldVersion(int& numConnections, char* name2) {
	double** Matr;
	int N_CONNECTIONS = 1000; /*char name2[100];*/
	int i = 0, j = 0;

	Matr = new double*[N_CONNECTIONS];
	for (int h = 0; h < N_CONNECTIONS; h++) {
		Matr[h] = new double[4];
	}

	ifstream conninput;
	conninput.open(name2, ios::binary);
	if (!conninput) {
		cout << "Error 55 :impossible to open the file\n";
	} else
		cout << "Opening Connections file.\n";

	while (conninput) {

		conninput >> Matr[i][j];
		j++;
		numConnections++;

		if (j > 3) {
			i++;
			j = 0;
		}
	}

	numConnections = (numConnections - 1) / 4;

	conninput.close(); // Closing Connection Data File
					   // cin.ignore();

	// Fixing Arc parameter values
	for (int i = 0; i < numConnections; i++) {
		connections[i].idFirstTrackLine = (int)Matr[i][0];
		connections[i].idSecondTrackLine = (int)Matr[i][2];
		connections[i].xFirstNode = Matr[i][1];
		connections[i].xSecondNode = Matr[i][3];
		char A[20], blockSets[20], H[20], W[20];
		snprintf(A, sizeof(A), "%d", connections[i].idFirstTrackLine);
		snprintf(blockSets, sizeof(blockSets), "%d", connections[i].idSecondTrackLine);
		snprintf(H, sizeof(H), "%f", connections[i].xFirstNode);
		snprintf(W, sizeof(W), "%f", connections[i].xSecondNode);
		connections[i].ID = connections[i].ID + A + "-" + blockSets + "-" + H + "-" + W;
		std::cout << "\rConnection: " << i << " : " << connections[i].ID;
	}

	for (int i = 0; i < N_CONNECTIONS; i++)
		delete Matr[i]; // Deleting Temporary Matrix
	delete Matr;
	cout << "\n\n";

	// Print on a text File to Visualize connections on Autocad
	ofstream VisualizeConnections;
	string VisualFile;
	VisualFile = InputMainFolder + "/TrackLines/VisualizeConnections.txt";
	VisualizeConnections.open((char*)VisualFile.c_str());
	for (int i = 0; i < numConnections; i++) {
		VisualizeConnections << connections[i].xFirstNode << "," << connections[i].idFirstTrackLine << "\n"
							 << connections[i].xSecondNode << "," << connections[i].idSecondTrackLine << "\n";
	}

	// Establish connections between TrackLines Nodes
	for (int i = 0; i < numConnections; i++) {
		connections[i].setConnections(blockSets);
	}
}

// Function for loading Connections. This is a new improved version which also allows to load speed limits on switches if there is an extra column in the Connection File.
// In the connection file it is now possible to write a row ( to describe a switch connection ) with 5 elements, specifically as: " TracklineID1 X1 TrackLineID2 X2 speed(m/s)". In that case the indicated speed limit in m/s will be set as limit on the switch
// if the row describing a switch connection has instead the original 4 elements (i.e. : "TracklineID1 X1 TrackLineID2 X2" speed(m/s)") a default speed of 16.67 m/s (i.e. 60 km/h) will be set to the switch

void loadConnections(int& numConnections, string ConnectionFilePath) {
	ifstream InputFile;

	InputFile.open((char*)ConnectionFilePath.c_str(), ios::binary);

	if (!InputFile) {
		cout << "\n\nERROR:Cannot Read file with Switch connections\n\n";
	}

	else {
		cout << "\nLoading Switch Connection file\n\n";
		string FileLine;
		int ConnectionCounter = 0;

		// Iterating through the lines of the file
		while (getline(InputFile, FileLine)) {
			ConnectionCounter++;	   // This counts the number of rows, hence switch connections in the File
			int N_elements_In_row = 0; // this counts the number of elements which are specified in the row for a switch connection

			// Streaming file line into string LineStream
			istringstream LineStream(FileLine);

			// Tokenize the LineStream to initialise the attributes of a switch connection
			string tok, TrackLineID1, TrackLineID2, X1, X2;
			// Splitting LineStream in tokens separated by the space charachter " "

			while (getline(LineStream, tok, '\t')) {
				N_elements_In_row++; // Increasing number of elements in the row

				if (N_elements_In_row == 1) {
					TrackLineID1 = tok;
					connections[ConnectionCounter - 1].idFirstTrackLine = atoi(tok.c_str());

				} else if (N_elements_In_row == 2) {
					X1 = tok;
					connections[ConnectionCounter - 1].xFirstNode = stod(tok);

				} else if (N_elements_In_row == 3) {
					TrackLineID2 = tok;
					connections[ConnectionCounter - 1].idSecondTrackLine = atoi(tok.c_str());

				} else if (N_elements_In_row == 4) {
					X2 = tok;
					connections[ConnectionCounter - 1].xSecondNode = stod(tok);

				}
				// if a row has also the 5th element specifying the speed then set the corresponding speed to the switch connection
				else if (N_elements_In_row == 5) {

					connections[ConnectionCounter - 1].speedlimit = stod(tok);
				}
			}
			// Updating total number of connections
			numConnections = ConnectionCounter;

			// Initialising Connection ID and printing it out on screen
			connections[ConnectionCounter - 1].ID = connections[ConnectionCounter - 1].ID + TrackLineID1 + "-" + X1 + "-" + TrackLineID2 + "-" + X2;

			std::cout << "\rConnection: " << ConnectionCounter - 1 << " : " << connections[ConnectionCounter - 1].ID;
			cout << "\n\n";
		}
	}
	// Closing connection file
	InputFile.close();

	// Print on a text File to Visualize connections on Autocad
	ofstream VisualizeConnections;
	string VisualFile;
	VisualFile = InputMainFolder + "/TrackLines/VisualizeConnections.txt";
	VisualizeConnections.open((char*)VisualFile.c_str());
	for (int i = 0; i < numConnections; i++) {
		VisualizeConnections << connections[i].xFirstNode << "," << connections[i].idFirstTrackLine << "\n"
							 << connections[i].xSecondNode << "," << connections[i].idSecondTrackLine << "\n";
	}

	// Establish connections between TrackLines Nodes
	for (int i = 0; i < numConnections; i++) {
		connections[i].setConnections(blockSets);
	}
}

// Function to order pairs of elements
bool orderPassengerListOnPlatform(pair<string, double>& A, pair<string, double>& blockSets) {
	if (A.second < blockSets.second)
		return true;
	else
		return false;
}


Stations::Stations() {
	Av_Arrival_Delay = Std_Arrival_Delay = Tot_Consec_Delay = Perc_Delayed_T_5min = Perc_Delayed_T_3min = Perc_Delayed_T = Max_TotalDelay = Max_Cons_Delay = -1;
	totalArrivalDelay = 0;
	N_Stopped_Trains = N_Delayed_Arr = N_Delayed_Arr_3min = N_Delayed_Arr_5min = -1;
	latitude = longitude = graphX = graphY = 0;
	N_StationPlatforms = 0;
}

Stations StationArray[95]; // Array of all the station in the network
Stations TotalInputDelays; // Element which collects the amount of entrance delays and disturbances set as input
Stations EntranceInputDelays;
Stations DisturbanceInput;
Stations Final_Station; // Fittitious Station to measure train delays at their own Final Station
int numStations = 0;

// Function to Load and Set the Stations in the whole Network, while automatically assigning the platform ID number
void loadStations(char* FileStations) {
	string** Matr;
	int N_STATIONS = 1000; /*char name2[100];*/
	int i = 0, j = 0;

	Matr = new string*[N_STATIONS];
	for (int h = 0; h < N_STATIONS; h++) {
		Matr[h] = new string[2];
	}

	ifstream stationinput;
	stationinput.open(FileStations, ios::binary);
	if (!stationinput) {
		cout << "Error 57 :impossible to open Stations file\n";
	} else
		cout << "Stations are loaded\n";

	while (stationinput) {
		stationinput >> Matr[i][j];
		j++;
		numStations++;
		if (j > 1) {
			i++;
			j = 0;
		}
	}

	numStations = (numStations - 1) / 2;

	stationinput.close(); // Closing Station Data File
						  // cin.ignore();

	for (int h = 0; h < numStations; h++) {
		int StationPlatformCounter = 0; // This counter counts the number of Station platforms of a Station
		for (int i = 0; i < numTrackLines; i++) {
			if (blockSets[i].member[0].startNode.X == atof(Matr[h][0].c_str())) {

				blockSets[i].member[0].startNode.stationName = Matr[h][1];
				StationPlatformCounter++;
				blockSets[i].member[j].startNode.stationPlatformId = "Platform_" + to_string(StationPlatformCounter);
			}

			for (int j = 0; j < blockSets[i].len; j++) {
				if (blockSets[i].member[j].endNode.X == atof(Matr[h][0].c_str())) {
					blockSets[i].member[j].endNode.stationName = Matr[h][1];
					StationPlatformCounter++;
					blockSets[i].member[j].endNode.stationPlatformId = "Platform_" + to_string(StationPlatformCounter);
				}
			}
		}
	}

	// Loading StationArray
	for (int i = 0; i < numStations; i++) {
		StationArray[i].X = atof(Matr[i][0].c_str());
		StationArray[i].stationName = Matr[i][1];
	}
	// Initializing the object Final_Station
	Final_Station.stationName = "Final_Station";
	// Deleting Temporary Matrix
	delete[] Matr;
}

// Print Station Names for All the TrackLines
void printStations() {
	ofstream output;
	string FileName;
	FileName = InputMainFolder + "/TrackLines/TrackandStations.txt";
	output.open((char*)FileName.c_str(), ios::binary);
	for (int i = 0; i < numTrackLines; i++) {
		output << "\n"
			   << blockSets[i].ID << "\n";
		for (int j = 0; j < blockSets[i].len; j++) {
			if (blockSets[i].member[j].endNode.stationName.empty() != 1)
				output << blockSets[i].member[j].endNode.stationName << " ";
		}
	}
	output.close();
}

void Print_Station_Delay_Stats(string Name_StationDelay, string kindofdelay) {
	ofstream FileOutput;
	string FileOutName;
	if (kindofdelay == "pos")
		FileOutName = FileOutName + Name_StationDelay + "/Stats_Stations.txt";
	else if (kindofdelay == "pos&neg")
		FileOutName = FileOutName + Name_StationDelay + "/Pos&Neg_Stats_Stations.txt";
	FileOutput.open((char*)FileOutName.c_str());
	FileOutput << "StName" << " " << "Av_Delay" << " " << "Std_Delay" << " " << "Total_Delay" << " " << "Max_Tot_Delay" << " " << "Cum_Cons_Delay" << " " << "Max_Cons_Delay" << " " << "Perc_Delayed" << " " << "Perc_Delayed_3min" << " " << "Perc_Delayed_5min" << " " << "N_StopTrains" << " " << "N_DelTrains" << "\n";
	// Print first the amount of Entrance delay and disturbances to dwell times in input
	FileOutput << "Ent_Del+DwT_Dist" << " " << TotalInputDelays.Av_Arrival_Delay << " " << TotalInputDelays.Std_Arrival_Delay << " " << TotalInputDelays.totalArrivalDelay << " " << TotalInputDelays.Max_TotalDelay << " " << "N/D" << " " << "N/D" << " " << TotalInputDelays.Perc_Delayed_T << " " << TotalInputDelays.Perc_Delayed_T_3min << " " << TotalInputDelays.Perc_Delayed_T_5min << " " << TotalInputDelays.N_Stopped_Trains << " " << TotalInputDelays.N_Delayed_Arr << "\n";
	FileOutput << "Ent_Delays" << " " << EntranceInputDelays.Av_Arrival_Delay << " " << EntranceInputDelays.Std_Arrival_Delay << " " << EntranceInputDelays.totalArrivalDelay << " " << EntranceInputDelays.Max_TotalDelay << " " << "N/D" << " " << "N/D" << " " << EntranceInputDelays.Perc_Delayed_T << " " << EntranceInputDelays.Perc_Delayed_T_3min << " " << EntranceInputDelays.Perc_Delayed_T_5min << " " << EntranceInputDelays.N_Stopped_Trains << " " << EntranceInputDelays.N_Delayed_Arr << "\n";
	FileOutput << "DwT_Dist" << " " << DisturbanceInput.Av_Arrival_Delay << " " << DisturbanceInput.Std_Arrival_Delay << " " << DisturbanceInput.totalArrivalDelay << " " << DisturbanceInput.Max_TotalDelay << " " << "N/D" << " " << "N/D" << " " << DisturbanceInput.Perc_Delayed_T << " " << DisturbanceInput.Perc_Delayed_T_3min << " " << DisturbanceInput.Perc_Delayed_T_5min << " " << DisturbanceInput.N_Stopped_Trains << " " << DisturbanceInput.N_Delayed_Arr << "\n";

	// Print the calculated delays for all the stations
	for (int s = 1; s < numStations; s++) { // Print it for every station but the first, if you want it also for the first just start the for loop with s=0
		FileOutput << StationArray[s].stationName << " " << StationArray[s].Av_Arrival_Delay << " " << StationArray[s].Std_Arrival_Delay << " " << StationArray[s].totalArrivalDelay << " " << StationArray[s].Max_TotalDelay << " " << StationArray[s].Tot_Consec_Delay << " " << StationArray[s].Max_Cons_Delay << " " << StationArray[s].Perc_Delayed_T << " " << StationArray[s].Perc_Delayed_T_3min << " " << StationArray[s].Perc_Delayed_T_5min << " " << StationArray[s].N_Stopped_Trains << " " << StationArray[s].N_Delayed_Arr << "\n";
	}

	// Computing Totals over the stations
	double TotalAV = 0, TotalStd = 0, TOTDelay = 0, MAX_TOTDelay = 0, TOTCONSDelay = 0, MAX_CONSDelay = 0, AV_Punct = 0, AV_Punct_3min = 0, AV_Punct_5min = 0;
	for (int s = 1; s < numStations; s++) {								// Calculate it for every station but the first, if you want it also for the first station just start the loop with s=0
		TotalAV = TotalAV + StationArray[s].Av_Arrival_Delay;			// Calculating the Average Delay over the stations
		TotalStd = TotalStd + StationArray[s].Std_Arrival_Delay;		// Calculating the Average Std over the stations
		TOTDelay = TOTDelay + StationArray[s].totalArrivalDelay;		// Calculating the Total Delay over all stations
		TOTCONSDelay = TOTCONSDelay + StationArray[s].Tot_Consec_Delay; // Calculating the Total consecutive delay over all stations
		if (StationArray[s].Max_TotalDelay > MAX_TOTDelay)
			MAX_TOTDelay = StationArray[s].Max_TotalDelay; // Calculating the MAX TOTAL DELAY
		if (StationArray[s].Max_Cons_Delay > MAX_CONSDelay)
			MAX_CONSDelay = StationArray[s].Max_Cons_Delay;					 // Calculating the MAX CONSECUTIVE DELAY
		AV_Punct = AV_Punct + StationArray[s].Perc_Delayed_T;				 // Calculating the  Avergae Punctuality at stations
		AV_Punct_3min = AV_Punct_3min + StationArray[s].Perc_Delayed_T_3min; // Calculating the  Avergae Punctuality at 3 min at stations
		AV_Punct_5min = AV_Punct_5min + StationArray[s].Perc_Delayed_T_5min; // Calculating the  Avergae Punctuality at 5 min at stations
	}

	/*//Calculating the averages over all stations
	TotalAV=TotalAV/numStations;
	TotalStd=TotalStd/numStations;
	AV_Punct=AV_Punct/numStations;                        //Calculating the  Average Punctuality at stations
	AV_Punct_3min=AV_Punct_3min/numStations;          //Calculating the  Average Punctuality at 3 min at stations
	AV_Punct_5min=AV_Punct_5min/numStations;          //Calculating the  Average Punctuality at 5 min at stations*/

	// Calculating the averages over all stations but the first
	TotalAV = TotalAV / (numStations - 1);
	TotalStd = TotalStd / (numStations - 1);
	AV_Punct = AV_Punct / (numStations - 1);			  // Calculating the  Avergae Punctuality at stations
	AV_Punct_3min = AV_Punct_3min / (numStations - 1); // Calculating the  Avergae Punctuality at 3 min at stations
	AV_Punct_5min = AV_Punct_5min / (numStations - 1); // Calculating the  Avergae Punctuality at 5 min at stations

	FileOutput << "TOTALS" << " " << TotalAV << " " << TotalStd << " " << TOTDelay << " " << MAX_TOTDelay << " " << TOTCONSDelay << " " << MAX_CONSDelay << " " << AV_Punct << " " << AV_Punct_3min << " " << AV_Punct_5min << "\n";
	// Printing the Final_Delay fittitious station
	FileOutput << Final_Station.stationName << " " << Final_Station.Av_Arrival_Delay << " " << Final_Station.Std_Arrival_Delay << " " << Final_Station.totalArrivalDelay << " " << Final_Station.Max_TotalDelay << " " << Final_Station.Tot_Consec_Delay << " " << Final_Station.Max_Cons_Delay << " " << Final_Station.Perc_Delayed_T << " " << Final_Station.Perc_Delayed_T_3min << " " << Final_Station.Perc_Delayed_T_5min << " " << Final_Station.N_Stopped_Trains << " " << Final_Station.N_Delayed_Arr << "\n";

	FileOutput.close();
}


// The constructor class of Location class
Location::Location() {
	Name = "None";
	MaxHW = -1;
	CriticalTrainCouple = "None";
	MinHW = -1;
	MinimumTrainCouple = "None";
	Position = 0;
}

// Function to see if two locations are the same or not
bool Location::areLocationsEqual(Location blockSets) {
	bool AreTheSameLocation = false; // This variable turns to true only if the Locations are the same
	if (this->Name.empty() != 1) {	 // if the name is not null
		string NameA, StationA, firstBlockA, secondBlockA;
		NameA = this->Name;
		istringstream Line(NameA);
		string tok;
		list<string> TokensA;

		while (getline(Line, tok, '/')) {
			if (tok.size() > 0)
				TokensA.push_back(tok);
		}

		// Now checking the blockSets
		string NameB, StationB, firstBlockB, secondBlockB;
		NameB = blockSets.Name;
		istringstream LineB(NameB);
		string tok2;
		list<string> TokensB;

		while (getline(LineB, tok2, '/')) {
			if (tok2.size() > 0)
				TokensB.push_back(tok2);
		}

		// The two Locations can be compared only if they have the same number of tokens in the name otherwise they are different by definition
		if (TokensA.size() == TokensB.size()) {

			if (TokensA.size() == 1) {
				list<string>::iterator p = TokensA.begin();
				firstBlockA = *p;
				// Do the same for Location blockSets
				list<string>::iterator k = TokensB.begin();
				firstBlockB = *k;

				if (firstBlockA == firstBlockB)
					AreTheSameLocation = true;

			} else if (TokensA.size() == 2) {
				list<string>::iterator p = TokensA.begin();
				firstBlockA = *p;
				p++; // advance p of one position
				secondBlockA = *p;

				// Do the same for Location blockSets
				list<string>::iterator k = TokensB.begin();
				firstBlockB = *k;
				k++;
				secondBlockB = *k;

				if (((firstBlockA == firstBlockB) && (secondBlockA == secondBlockB)) || ((firstBlockA == secondBlockB) && (secondBlockA == firstBlockB)))
					AreTheSameLocation = true;
			} else if (TokensA.size() == 3) {
				list<string>::iterator p = TokensA.begin();
				StationA = *p;
				p++;
				firstBlockA = *p;
				p++; // advance p of one position
				secondBlockA = *p;

				// Do the same for Location blockSets
				list<string>::iterator k = TokensB.begin();
				StationB = *k;
				k++;
				firstBlockB = *k;
				k++;
				secondBlockB = *k;
				if (((StationA == StationB) && (firstBlockA == firstBlockB) && (secondBlockA == secondBlockB)) || ((StationA == StationB) && (firstBlockA == secondBlockB) && (secondBlockA == firstBlockB)))
					AreTheSameLocation = true;
			}

			else {
				cout << "\n\nERROR: The Location " << this->Name << "has an anomaly in the number of elements in its name\n\n";
			}
		}
	}

	if (AreTheSameLocation == 1)
		return true;
	else
		return false;
}

list<Location> AllLocations; // This is the list containing all the Locations of the networks (i.e. all possible block sections and/or all possible sections for ETCS level 3);
