#include "Rescheduling.h"

#include <thread>
#include <chrono>
#include <cstdio>

Rescheduling* dispatchingTool = new Rescheduling();

// class constructor
Rescheduling::Rescheduling() {
	// set entry time (used for first train)
	entryTime = 1; // cannot be zero because the simulation uses previous timestep to check if the train can enter the simulation

	timeUpdateInterval = 2 * 60; // time update every 2 min by default

	// load dictionary
	// loadDict();

	// print dict
	// printDict();

	// initialize output file (erase)
	initalizeOutfileFile(initial_variables.InputMainFolder + "/Rescheduling");
}

// sets the rescheduling
void Rescheduling::loadDict() {
	// file with dictionary
	std::ifstream myfile_dict(initial_variables.InputMainFolder + "/Rescheduling/LineRouteTimetableDict.txt");

	// open file
	if (myfile_dict.is_open()) {
		std::string line;

		// read file line by line
		while (getline(myfile_dict, line)) {
			std::string lineName, stationDep, stationArr, routeName, timetableFile, depPlatformName, arrPlatformName;
			int lineID, routeID;
			int depPlatform = -1, arrPlatform = -1;
			std::tuple<int, std::string, int, int> dictKey;
			std::tuple<std::string, int, std::string> dictValue;

			// ignore empty lines (in the end of the file)
			if (line.empty())
				continue;

			// parse line
			std::istringstream lineStream(line);
			getline(lineStream, lineName, '\t');
			getline(lineStream, stationDep, '\t');
			getline(lineStream, depPlatformName, '\t');
			getline(lineStream, stationArr, '\t');
			getline(lineStream, arrPlatformName, '\t');
			getline(lineStream, routeName, '\t');
			getline(lineStream, timetableFile, '\t');

			// conversion to int
			lineID = std::stoi(lineName);
			routeID = std::stoi(routeName);
			if (!depPlatformName.empty()) {
				depPlatform = std::stoi(depPlatformName);
			}
			if (!arrPlatformName.empty()) {
				arrPlatform = std::stoi(arrPlatformName);
			}

			// add line to dictionary
			dictKey = std::make_tuple(lineID, stationDep, depPlatform, arrPlatform);
			dictValue = std::make_tuple(stationArr, routeID, timetableFile);
			reschedulingDict.insert({dictKey, dictValue});
		}
		// close file
		myfile_dict.close();
	} else {
		// error opening the file
		std::cout << "Unable to open file with rescheduling dictionary.\n";
	}
}

// print dictionary
void Rescheduling::printDict() {
	std::cout << "DICTIONARY\n";
	std::cout << "line|stationDep|platformDep|stationArr|platformArr|route|timetableFile\n";
	for (auto it = reschedulingDict.begin(); it != reschedulingDict.end(); ++it) {
		std::cout << std::get<0>(it->first) << "|" << std::get<1>(it->first) << "|";
		std::cout << std::get<2>(it->first) << "|" << std::get<0>(it->second) << "|";
		std::cout << std::get<3>(it->first) << "|" << std::get<1>(it->second) << "|" << std::get<2>(it->second) << "\n";
	}
}

// handle message sent from rescheduling tool
void Rescheduling::handleDispMessage(std::string message) {
	std::string messageType;
	std::string lineName, stationDep, trainName, timeString;
	int lineID, trainIdx, intendedDepTime, platform = 0;
	std::vector<int> dwellTimes;

	// remove any null characters
	message.erase(std::remove(message.begin(), message.end(), '\0'), message.end());

	// parse message
	std::istringstream lineStream(message);
	getline(lineStream, messageType, ',');

	// remaining message
	std::string token;
	std::vector<std::string> tokens;
	char delimiter = ',';
	while (std::getline(lineStream, token, delimiter)) {
		tokens.push_back(token);
	}

	// initial train position (init,station,trainType,trainIndex,'platform')
	if (messageType == "init") {
		stationDep = tokens[0];
		std::string trainType = tokens[1];

		// conversion to int
		try {
			trainIdx = std::stoi(tokens[2]);

			// check if platform was assigned (not assigned if 0 or empty)
			if (tokens.size() == 4 && !tokens[3].empty()) {
				platform = std::stoi(tokens[3]);
			}
		} catch (const std::invalid_argument& ia) {
			std::cout << "\nWarning: Invalid message from rescheduling tool.\n\n";
			return;
		}

		// invalid train index
		if (trainIdx < 0 || trainIdx > numRegions) {
			std::cout << "\nWarning: Invalid message from rescheduling tool.\n\n";
			return;
		}

		// invalid train type
		if (trainType != "SPR" && trainType != "IC") {
			std::cout << "\nWarning: Invalid message from rescheduling tool.\n\n";
			return;
		}

		// find any route starting at that station
		int routeID = -1;
		for (auto it = reschedulingDict.begin(); it != reschedulingDict.end(); ++it) {
			if (std::get<1>(it->first) == stationDep) {
				// platform not assigned
				if (platform == 0) {
					routeID = std::get<1>(it->second);
					platform = std::get<2>(it->first);
				} else if (platform == std::get<2>(it->first)) {
					routeID = std::get<1>(it->second);
				}

				// found route
				if (routeID != -1) {
					// send train to station platform
					// create disp decision object (lineID, stationDep, route, startTime)
					dispDecision decision = dispDecision("init", std::get<0>(it->first), stationDep, platform, std::get<1>(it->second), entryTime);

					// set time for the next train to enter the simulation
					entryTime++;

					// train object doesn't exist
					if (trainIdx == numRegions) {
						// load new train
						std::string train_folder = initial_variables.InputMainFolder + "/Trains/NWO_trains/";
						std::string filename;

						// sprinter
						if (trainType == "SPR") {
							filename = train_folder + "SprinterSLT.txt";
						}
						// intercity
						else {
							filename = train_folder + "IntercityVIRM.txt";
						}
						loadTrainType((char*)filename.c_str(), numRegions);

						// set vector sizes according to the length of the simulation
						regional_train[trainIdx].setTrainVectorSizesFromInput(initial_variables.times);

						// assign #train of its type
						int typeID = 0;
						for (int i = 0; i < numRegions; i++) {
							if (regional_train[i].type == regional_train[trainIdx].type) {
								typeID++;
							}
						}
						regional_train[trainIdx].ID = typeID;

						// Setting trainDescription
						char IDChar[20];
						snprintf(IDChar, sizeof(IDChar), "%d", (int)regional_train[trainIdx].ID);
						regional_train[trainIdx].trainDescription = regional_train[trainIdx].type + "-" + IDChar;
					}

					// execute decision
					regional_train[trainIdx].implementInitMsg(decision);

					break;
				}
			}
		}
	}
	// dispatching decision (disp,station,line,train,intendedDepTime,'arrivalPlatform','1stDwellIntermediate','lastDwellIntermediate')
	else if (messageType == "disp") {
		stationDep = tokens[0];

		// conversion to int
		try {
			lineID = std::stoi(tokens[1]);
			trainIdx = std::stoi(tokens[2]);
			intendedDepTime = std::stoi(tokens[3]);

			// check if platform was assigned (not assigned if 0 or empty)
			if (tokens.size() >= 5 && !tokens[4].empty()) {
				platform = std::stoi(tokens[4]);
			}

			// collect dwell times
			if (tokens.size() >= 6) {
				for (int i = 5; i < tokens.size(); i++) {
					dwellTimes.push_back(std::stoi(tokens[i]));
				}
			}

		} catch (const std::invalid_argument& ia) {
			std::cout << "\nWarning: Invalid message from rescheduling tool.\n\n";
			return;
		}

		// invalid train index
		if (trainIdx < 0 || trainIdx >= numRegions) {
			std::cout << "\nWarning: Invalid message from rescheduling tool.\n\n";
			return;
		}

		// TEMPORARY SOLUTION TO ORDERED MAP
		std::vector<int> alternativeRouteIDs = {66, 67, 73, 72, 62, 63};

		// platform assigned
		bool routeFound = true;
		std::string destination;
		if (platform != 0) {
			// get info from dict
			auto findRes = reschedulingDict.find({lineID, stationDep, regional_train[trainIdx].arrivalPlatform, platform});

			// did not find suitable route
			if (findRes == reschedulingDict.end()) {
				// find destination
				for (auto it = reschedulingDict.begin(); it != reschedulingDict.end(); ++it) {
					if (std::get<0>(it->first) == lineID && std::get<1>(it->first) == stationDep) {
						destination = std::get<0>(it->second);
						routeFound = false;
						break;
					}
				}
			}
			// found route
			else {
				// create disp decision object (lineID, stationArr, route, timetableFile, intendedDepTime, dwellTimes)
				dispDecision decision = dispDecision("disp", lineID, std::get<0>(findRes->second), platform, std::get<1>(findRes->second), std::get<2>(findRes->second), intendedDepTime, dwellTimes);

				// add disp decision to train object
				regional_train[trainIdx].dispDecisions.push_back(decision);
			}
		}
		// platform not assigned
		else {
			// find suitable route
			int routeID = -1;
			for (auto it = reschedulingDict.begin(); it != reschedulingDict.end(); ++it) {
				// find destination
				if (std::get<0>(it->first) == lineID && std::get<1>(it->first) == stationDep) {
					destination = std::get<0>(it->second);
				}

				if (std::get<0>(it->first) == lineID && std::get<1>(it->first) == stationDep && std::get<2>(it->first) == regional_train[trainIdx].arrivalPlatform) {
					// route must be default route
					if (std::find(alternativeRouteIDs.begin(), alternativeRouteIDs.end(), std::get<1>(it->second)) != alternativeRouteIDs.end()) {
						continue;
					}

					platform = std::get<3>(it->first);
					routeID = std::get<1>(it->second);

					// create disp decision object (lineID, stationArr, arrivalPlatform, route, timetableFile, intendedDepTime, dwellTimes)
					dispDecision decision = dispDecision("disp", lineID, std::get<0>(it->second), platform, std::get<1>(it->second), std::get<2>(it->second), intendedDepTime, dwellTimes);

					// add disp decision to train object
					regional_train[trainIdx].dispDecisions.push_back(decision);

					break;
				}
			}

			// did not find suitable route
			if (routeID == -1) {
				routeFound = false;
			}
		}

		// did not find a suitable route
		if (!routeFound) {
			// find timetable file
			std::string timetableFile;
			for (auto it = reschedulingDict.begin(); it != reschedulingDict.end(); ++it) {
				if (std::get<0>(it->first) == lineID && std::get<1>(it->first) == stationDep) {
					timetableFile = std::get<2>(it->second);
					break;
				}
			}

			// find route linking origin and destination (check all options - needed for big stations with two 'corridors')
			int longRouteID = -1, longRouteDepPlatform = -1, longRouteArrPlatform = -1;
			bool foundLongRoute = false, builtNewRoute = false;
			for (auto itt = reschedulingDict.begin(); itt != reschedulingDict.end(); ++itt) {
				if (std::get<0>(itt->first) == lineID && std::get<1>(itt->first) == stationDep && std::get<0>(itt->second) == destination) {
					// long route must be default route
					if (std::find(alternativeRouteIDs.begin(), alternativeRouteIDs.end(), std::get<1>(itt->second)) != alternativeRouteIDs.end()) {
						continue;
					}
					longRouteID = std::get<1>(itt->second);
					longRouteDepPlatform = std::get<2>(itt->first);
					longRouteArrPlatform = std::get<3>(itt->first);
					foundLongRoute = true;

					// find a route starting/ending at disered platform
					int routeID = -1, newRouteID = -1;
					builtNewRoute = false;
					for (auto it = reschedulingDict.begin(); it != reschedulingDict.end(); ++it) {
						// departing from different platform
						if (std::get<1>(it->first) == stationDep && std::get<2>(it->first) == regional_train[trainIdx].arrivalPlatform && std::get<2>(it->first) != longRouteDepPlatform) {
							routeID = std::get<1>(it->second);
							platform = longRouteArrPlatform;

							// found route
							if (routeID != -1 && routeID != longRouteID && train_route[routeID].reversed_direction == train_route[longRouteID].reversed_direction) {
								// try to create a route from existing routes
								newRouteID = createNewJointRoute(routeID, longRouteID, "initial");
							}
						}
						// arriving at different platform
						else if (std::get<0>(it->second) == destination && std::get<3>(it->first) == platform && std::get<3>(it->first) != longRouteArrPlatform) {
							routeID = std::get<1>(it->second);

							// found route
							if (routeID != -1 && train_route[routeID].reversed_direction == train_route[longRouteID].reversed_direction) {
								// try to create a route from existing routes
								newRouteID = createNewJointRoute(routeID, longRouteID, "final");
							}
						}

						// created a suitable route
						if (newRouteID != -1) {
							// create disp decision object (lineID, stationArr, arrivalPlatform, route, timetableFile, intendedDepTime, dwellTimes)
							dispDecision decision = dispDecision("disp", lineID, destination, platform, newRouteID, timetableFile, intendedDepTime, dwellTimes);
							builtNewRoute = true;

							// add disp decision to train object
							regional_train[trainIdx].dispDecisions.push_back(decision);

							break;
						}
					}

					// created a suitable route
					if (builtNewRoute) {
						break;
					}
				}
			}
			if (!foundLongRoute) {
				std::cout << "\nWarning: There is no route connecting the stations.\n\n";
				return;
			}
			// could not create a suitable route
			else if (!builtNewRoute) {
				std::cout << "\nWarning: Could not find a suitable route.\n\n";
				return;
			}
		}
	}
	// update on dwell times
	else if (messageType == "dwell") {
		// conversion to int
		try {
			trainIdx = std::stoi(tokens[0]);

			// collect dwell times
			for (int i = 1; i < tokens.size(); i++) {
				dwellTimes.push_back(std::stoi(tokens[i]));
			}

		} catch (const std::invalid_argument& ia) {
			std::cout << "\nWarning: Invalid dwell message from rescheduling tool.\n\n";
			return;
		}

		// create disp decision object (lineID, stationArr, arrivalPlatform, route, timetableFile, intendedDepTime, dwellTimes)
		dispDecision decision = dispDecision("dwell", -1, "", -1, -1, "", -1, dwellTimes);

		// add disp decision to train object
		regional_train[trainIdx].dispDecisions.push_back(decision);
	}
	// definition of time update interval
	else if (messageType == "time") {
		// conversion to int
		try {
			timeUpdateInterval = std::stoi(tokens[0]);
		} catch (const std::invalid_argument& ia) {
			std::cout << "\nWarning: Invalid time update interval message from rescheduling tool.\n\n";
			return;
		}
	}
	// invalid message
	else {
		std::cout << "\nWarning: Invalid message from rescheduling tool.\n\n";
		std::cout << "message type is: --" << messageType << "--\n";
	}
}

// initialize train service path diagram (print header)
void Rescheduling::initalizeTrainServicePathDiagram(string FolderName) {
	std::string FileName = FolderName + "/TrainServicePathDiagram.txt";
	std::ofstream FileOutput;

	FileOutput.open((char*)FileName.c_str(), std::ios::binary | std::ios::trunc); // overwrite

	FileOutput << "Train\tLine\tRevRoute\tCorridor";
	for (int t = 0; t < initial_variables.times; t++) {
		FileOutput << "\t" << (t * timestep);
	}
	FileOutput << "\n";

	FileOutput.close();
}

// initialize output file (erase)
void Rescheduling::initalizeOutfileFile(string FolderName) {
	std::string FileName = FolderName + "/EGTRAINOutput.txt";
	std::ofstream FileOutput;

	FileOutput.open((char*)FileName.c_str(), std::ios::binary | std::ios::trunc); // overwrite

	FileOutput.close();
}

// read input file from dispatching tool
void Rescheduling::loadDispDecisions(std::string FolderName, int t) {
	// initialize train service path diagram file
	if (t == 0) {
		initalizeTrainServicePathDiagram(initial_variables.OutputMainFolder + "/TrainTrajectories");
	}

	// file with dispatching decisions
	std::string FileName = FolderName + "/ReschedulingInput.txt";
	std::ifstream myfile_dict(FileName);

	// open file
	if (myfile_dict.is_open()) {
		std::string line;

		// read decisions line by line
		while (getline(myfile_dict, line)) {
			// ignore empty lines (in the end of the file)
			if (line.empty())
				continue;

			// handle message
			handleDispMessage(line);
		}

		// close file
		myfile_dict.close();

		// erase file after reading
		std::ofstream FileOutput;
		FileOutput.open((char*)FileName.c_str(), std::ios::binary | std::ios::trunc); // overwrite
		FileOutput.close();
	} else {
		// error opening the file
		std::cout << "Unable to open input file from rescheduling tool.\n";
	}
}

// create new train object (based on existing)
// for now for a single train type
void Rescheduling::createTrainObject(int trainIdx) {
	// copy train object data from existing train

	// Setting Physical parameters
	regional_train[trainIdx].ID = regional_train[trainIdx - 1].ID + 1;
	regional_train[trainIdx].type = regional_train[trainIdx - 1].type;
	regional_train[trainIdx].indexOfRoute = regional_train[trainIdx - 1].indexOfRoute;
	regional_train[trainIdx].mass_of_traction_unit = regional_train[trainIdx - 1].mass_of_traction_unit;
	regional_train[trainIdx].mass_of_a_wagon = regional_train[trainIdx - 1].mass_of_a_wagon;
	regional_train[trainIdx].number_of_wagons = regional_train[trainIdx - 1].number_of_wagons;
	regional_train[trainIdx].max_train_speed = regional_train[trainIdx - 1].max_train_speed;
	regional_train[trainIdx].max_train_decelaration = regional_train[trainIdx - 1].max_train_decelaration;
	regional_train[trainIdx].frontal_wagon_area = regional_train[trainIdx - 1].frontal_wagon_area;
	regional_train[trainIdx].resistanceCoefficient = regional_train[trainIdx - 1].resistanceCoefficient;
	regional_train[trainIdx].Jerk = regional_train[trainIdx - 1].Jerk;
	regional_train[trainIdx].train_length = regional_train[trainIdx - 1].train_length;
	regional_train[trainIdx].massPerWagonAxle = regional_train[trainIdx].mass_of_a_wagon * regional_train[trainIdx].number_of_wagons;
	regional_train[trainIdx].massFactor = (1.09 * regional_train[trainIdx].mass_of_traction_unit + 1.06 * regional_train[trainIdx].massPerWagonAxle) / (regional_train[trainIdx].mass_of_traction_unit + regional_train[trainIdx].massPerWagonAxle);
	regional_train[trainIdx].total_train_mass = regional_train[trainIdx].mass_of_traction_unit + regional_train[trainIdx].massPerWagonAxle;

	// Setting trainDescription
	char IDChar[20];
	snprintf(IDChar, sizeof(IDChar), "%d", (int)regional_train[trainIdx].ID);
	regional_train[trainIdx].trainDescription = regional_train[trainIdx].type + "-" + IDChar;

	// Setting Train Start_Node_X
	regional_train[trainIdx].TrainRouteID = train_route[regional_train[trainIdx].indexOfRoute].ID;
	regional_train[trainIdx].Start_Node_X = train_route[regional_train[trainIdx].indexOfRoute].x_of_start_node;

	// Setting Traction Unit Characteristics
	regional_train[trainIdx].velocityIntervals = regional_train[trainIdx - 1].velocityIntervals;

	for (int h = 0; h < regional_train[trainIdx].velocityIntervals; h++) {
		regional_train[trainIdx].Vlb[h] = regional_train[trainIdx - 1].Vlb[h];
		regional_train[trainIdx].Vub[h] = regional_train[trainIdx - 1].Vub[h];
		regional_train[trainIdx].C0[h] = regional_train[trainIdx - 1].C0[h];
		regional_train[trainIdx].C1[h] = regional_train[trainIdx - 1].C1[h];
		regional_train[trainIdx].C2[h] = regional_train[trainIdx - 1].C2[h];
	}

	// Setting Stations and Dwell Times
	regional_train[trainIdx].numStations = regional_train[trainIdx - 1].numStations;
	regional_train[trainIdx].Stations = new Node[regional_train[trainIdx].numStations];

	for (int s = 0; s < regional_train[trainIdx].numStations; s++) {
		regional_train[trainIdx].Stations[s].stationName = regional_train[trainIdx - 1].Stations[s].stationName;
		regional_train[trainIdx].Stations[s].dwellTime = regional_train[trainIdx - 1].Stations[s].dwellTime;
		regional_train[trainIdx].Stations[s].StopTime = regional_train[trainIdx].Stations[s].dwellTime / timestep;
		regional_train[trainIdx].ScheduledArrivals[s] = regional_train[trainIdx - 1].ScheduledArrivals[s];
		regional_train[trainIdx].ScheduledDepartures[s] = regional_train[trainIdx - 1].ScheduledDepartures[s];
	}
	// Setting Headways and Departure Times
	regional_train[trainIdx].departure_time = entryTime;

	// Setting the departure time in the original timetable
	regional_train[trainIdx].scheduled_departure_time = regional_train[trainIdx].departure_time;

	// update counter
	numRegions++;

	// clear any dispatching decisions
	if (!regional_train[trainIdx].dispDecisions.empty()) {
		regional_train[trainIdx].dispDecisions.clear();
	}

	// set vector sizes according to the length of the simulation
	regional_train[trainIdx].setTrainVectorSizesFromInput(initial_variables.times);
}

// print time update (append to file)
void Rescheduling::printTimeUpdateMsg(int t, std::string FolderName) {
	// time to print
	if (t % timeUpdateInterval == 0) {
		std::string FileName = FolderName + "/EGTRAINOutput.txt";
		std::ofstream FileOutput;

		FileOutput.open((char*)FileName.c_str(), std::ios::binary | std::ios::app); // append

		if (FileOutput.is_open()) {
			FileOutput << "egtrain,time," << t << "\n";

			FileOutput.close();

			// temporary solution to avoid file conflicts
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		} else // error opening file
		{
			std::cout << "Error opening file to write arr/dep message\n";
		}
	}
}

// create joint route from existing routes
int Rescheduling::createNewJointRoute(int shortRouteID, int longRouteID, std::string differentPart) {
	// find first common block
	bool foundCommon = false;
	std::string commonID;
	int commonIdxShort = -1, commonIdxLong = -1;
	if (differentPart == "initial") {
		for (int rs = 0; rs < train_route[shortRouteID].N_Block_Sections; rs++) {
			for (int rl = 0; rl < train_route[longRouteID].N_Block_Sections; rl++) {
				if (train_route[shortRouteID].sequence_of_block_sections[rs].ID == train_route[longRouteID].sequence_of_block_sections[rl].ID) {
					commonID = train_route[shortRouteID].sequence_of_block_sections[rs].ID;
					commonIdxLong = rl;
					commonIdxShort = rs;
					foundCommon = true;
					break;
				}
			}
			if (foundCommon) {
				break;
			}
		}
		// prevent that short route becomes the long route
		if ((commonIdxShort - 1 - 0) > (train_route[longRouteID].N_Block_Sections - 1 - commonIdxLong)) {
			foundCommon = false;
		}
	} else if (differentPart == "final") {
		for (int rs = train_route[shortRouteID].N_Block_Sections - 1; rs >= 0; rs--) {
			for (int rl = train_route[longRouteID].N_Block_Sections - 1; rl >= 0; rl--) {
				if (train_route[shortRouteID].sequence_of_block_sections[rs].ID == train_route[longRouteID].sequence_of_block_sections[rl].ID) {
					commonID = train_route[shortRouteID].sequence_of_block_sections[rs].ID;
					commonIdxLong = rl;
					commonIdxShort = rs;
					foundCommon = true;
					break;
				}
			}
			if (foundCommon) {
				break;
			}
		}
		// prevent that short route becomes the long route
		if ((train_route[shortRouteID].N_Block_Sections - commonIdxShort - 1) > (commonIdxLong - 0)) {
			foundCommon = false;
		}
	}

	// no common block
	if (!foundCommon) {
		return -1;
	}

	// clean joint route file
	std::string FileName = initial_variables.InputMainFolder + "/Rescheduling/JointRoute.txt";
	std::ofstream FileOutput;

	FileOutput.open((char*)FileName.c_str(), std::ios::binary | std::ios::trunc); // overwrite

	if (differentPart == "initial") {
		// add blocks from short route
		for (int rs = 0; rs < commonIdxShort; rs++) {
			FileOutput << train_route[shortRouteID].sequence_of_block_sections[rs].ID << endl;
		}
		// add remaining blocks from long route
		for (int rl = commonIdxLong; rl < train_route[longRouteID].N_Block_Sections; rl++) {
			FileOutput << train_route[longRouteID].sequence_of_block_sections[rl].ID << endl;
		}
	} else if (differentPart == "final") {
		// add blocks from long route
		for (int rl = 0; rl <= commonIdxLong; rl++) {
			FileOutput << train_route[longRouteID].sequence_of_block_sections[rl].ID << endl;
		}
		// add remaining blocks from short route
		for (int rs = commonIdxShort + 1; rs < train_route[shortRouteID].N_Block_Sections; rs++) {
			FileOutput << train_route[shortRouteID].sequence_of_block_sections[rs].ID << endl;
		}
	}

	FileOutput.close();

	// create joint route
	Route jointRoute;

	jointRoute.createRoute((char*)FileName.c_str());

	// adjust route if it crosses regions
	jointRoute.adjustRouteAcrossDiffRegions();

	// set corridor
	jointRoute.corridor = train_route[longRouteID].corridor;

	// add new route to Route array
	train_route.push_back(jointRoute);
	N_Routes++;

	int jointRouteIdx = N_Routes - 1;

	return jointRouteIdx;
}

// function that implements the dynamic platform allocation
void Rescheduling::checkArrivalPlatform(int trainIdx, int i) {
	bool platformOccupied = false;

	if (!regional_train[trainIdx].OutOfSimulation) {
		if ((i >= regional_train[trainIdx].departure_time) && regional_train[trainIdx].CanEnter) {
			// find signalling_block_sections occupied by head of train
			int hHead = 0;
			for (int h = 0; h < train_route[regional_train[trainIdx].indexOfRoute].N_Block_Sections; h++) {
				if ((regional_train[trainIdx].instant_spatial_position[i - (int)(S_delay / timestep)] < train_route[regional_train[trainIdx].indexOfRoute].sequence_of_block_sections[h].end_node.X * 1000) && (regional_train[trainIdx].instant_spatial_position[i - (int)(S_delay / timestep)] >= train_route[regional_train[trainIdx].indexOfRoute].sequence_of_block_sections[h].start_node.X * 1000)) {
					hHead = h;
					break;
				}
			}

			for (int s = 0; s < stationBoundarySections.size(); s++) {
				// entering station area
				if (stationBoundarySections[s].entrance->ID == train_route[regional_train[trainIdx].indexOfRoute].sequence_of_block_sections[hHead + 1].ID && stationBoundarySections[s].direction == train_route[regional_train[trainIdx].indexOfRoute].reversed_direction) {
					// check if plaform is booked/occupied
					std::vector<std::string> stationsToProtect = {"Alm", "Asd", "Asdz"};
					if (std::find(stationsToProtect.begin(), stationsToProtect.end(), regional_train[trainIdx].Stations[regional_train[trainIdx].numStations - 1].stationName) != stationsToProtect.end()) {
						for (int tr = 0; tr < numRegions; tr++) {
							if (regional_train[tr].ID == regional_train[trainIdx].ID && regional_train[tr].type == regional_train[trainIdx].type) {
								continue;
							}
							if (regional_train[tr].Stations[regional_train[tr].numStations - 1].stationName == regional_train[trainIdx].Stations[regional_train[trainIdx].numStations - 1].stationName && regional_train[tr].reservedPlatform == regional_train[trainIdx].arrivalPlatform) {
								platformOccupied = true;
								break;
							}
						}
					} else {
						// find platform signalling_block_sections
						int platformBSIndex = -1;
						for (int h = hHead + 1; h < train_route[regional_train[trainIdx].indexOfRoute].N_Block_Sections; h++) {
							for (int k = 0; k < train_route[regional_train[trainIdx].indexOfRoute].sequence_of_block_sections[h].total_arcs; k++) {
								if (train_route[regional_train[trainIdx].indexOfRoute].sequence_of_block_sections[h].arcs_in_signalling_block_section[k].endNode.stationName == regional_train[trainIdx].Stations[regional_train[trainIdx].numStations - 1].stationName) {
									platformBSIndex = h;
									break;
								}
							}
							if (platformBSIndex != -1) {
								break;
							}
						}

						if (platformBSIndex != -1) {
							for (auto occ = BlocksOccupied.begin(); occ != BlocksOccupied.end(); ++occ) {
								// platform occupied
								if (train_route[regional_train[trainIdx].indexOfRoute].sequence_of_block_sections[platformBSIndex].ID == *occ) {
									platformOccupied = true;
									break; // train occupies at most one station area
								}
							}
						}
					}

					break; // train occupies at most one station area
				}
			}

			// try another platform
			if (platformOccupied) {
				changeTrainRoute(trainIdx, i);
			}
		}
	}
}

// change platform at destination
void Rescheduling::changeTrainRoute(int trainIdx, int i) {
	std::string destination;
	std::string stationDep = regional_train[trainIdx].Stations[0].stationName;

	// find possible suitable routes
	int routeID = -1, platform = -1;
	for (auto it = reschedulingDict.begin(); it != reschedulingDict.end(); ++it) {
		// find destination
		if (std::get<0>(it->first) == regional_train[trainIdx].dispLineID && std::get<1>(it->first) == stationDep) {
			destination = std::get<0>(it->second);
		}

		if (std::get<0>(it->first) == regional_train[trainIdx].dispLineID && std::get<1>(it->first) == stationDep && std::get<2>(it->first) != regional_train[trainIdx].arrivalPlatform) {
			// do not choose platform if booked by another train
			bool platUsedByOtherTrain = false;
			for (int tr = 0; tr < numRegions; tr++) {
				if (tr != trainIdx && regional_train[tr].Stations[regional_train[tr].numStations - 1].stationName == regional_train[trainIdx].Stations[regional_train[trainIdx].numStations - 1].stationName) {
					if (regional_train[tr].reservedPlatform == std::get<2>(it->first)) {
						platUsedByOtherTrain = true;
						break;
					}
				}
			}
			if (platUsedByOtherTrain) {
				continue;
			}

			platform = std::get<3>(it->first);
			routeID = std::get<1>(it->second);

			// find platform signalling_block_sections
			bool platformOccupied = false;
			int platformBSIndex = -1;
			for (int h = train_route[routeID].N_Block_Sections - 1; h >= 0; h--) {
				for (int k = 0; k < train_route[routeID].sequence_of_block_sections[h].total_arcs; k++) {
					if (train_route[routeID].sequence_of_block_sections[h].arcs_in_signalling_block_section[k].endNode.stationName == regional_train[trainIdx].Stations[regional_train[trainIdx].numStations - 1].stationName) {
						platformBSIndex = h;
						break;
					}
				}
				if (platformBSIndex != -1) {
					break;
				}
			}

			if (platformBSIndex != -1) {
				for (auto occ = BlocksOccupied.begin(); occ != BlocksOccupied.end(); ++occ) {
					// platform occupied
					if (train_route[routeID].sequence_of_block_sections[platformBSIndex].ID == *occ) {
						platformOccupied = true;
						break;
					}
				}
			}

			if (!platformOccupied) {
				// change route
				regional_train[trainIdx].indexOfRoute = routeID;
				regional_train[trainIdx].TrainRouteID = train_route[routeID].ID;
				regional_train[trainIdx].Start_Node_X = train_route[routeID].x_of_start_node;

				// update arrival platform
				regional_train[trainIdx].arrivalPlatform = platform;

				return;
			}
		}
	}

	// find suitable route
	for (auto itt = reschedulingDict.begin(); itt != reschedulingDict.end(); ++itt) {
		if (std::get<0>(itt->first) == regional_train[trainIdx].dispLineID && std::get<1>(itt->first) == stationDep && std::get<0>(itt->second) == destination) {
			// find a route starting/ending at disered platform
			int routeID = -1, newRouteID = -1;
			for (auto it = reschedulingDict.begin(); it != reschedulingDict.end(); ++it) {
				// arriving at different platform
				if (std::get<0>(it->second) == destination && std::get<3>(it->first) != regional_train[trainIdx].arrivalPlatform) {
					// do not choose platform if booked by another train
					bool platUsedByOtherTrain = false;
					for (int tr = 0; tr < numRegions; tr++) {
						if (tr != trainIdx && regional_train[tr].Stations[regional_train[tr].numStations - 1].stationName == regional_train[trainIdx].Stations[regional_train[trainIdx].numStations - 1].stationName) {
							if (regional_train[tr].reservedPlatform == std::get<3>(it->first)) {
								platUsedByOtherTrain = true;
								break;
							}
						}
					}
					if (platUsedByOtherTrain) {
						continue;
					}

					routeID = std::get<1>(it->second);
					platform = std::get<3>(it->first);

					// find platform signalling_block_sections
					bool platformOccupied = false;
					int platformBSIndex = -1;
					for (int h = train_route[routeID].N_Block_Sections - 1; h >= 0; h--) {
						for (int k = 0; k < train_route[routeID].sequence_of_block_sections[h].total_arcs; k++) {
							if (train_route[routeID].sequence_of_block_sections[h].arcs_in_signalling_block_section[k].endNode.stationName == regional_train[trainIdx].Stations[regional_train[trainIdx].numStations - 1].stationName) {
								platformBSIndex = h;
								break;
							}
						}
						if (platformBSIndex != -1) {
							break;
						}
					}

					if (platformBSIndex != -1) {
						for (auto occ = BlocksOccupied.begin(); occ != BlocksOccupied.end(); ++occ) {
							// platform occupied
							if (train_route[routeID].sequence_of_block_sections[platformBSIndex].ID == *occ) {
								platformOccupied = true;
								break;
							}
						}
					}

					if (!platformOccupied) {
						// found route
						if (routeID != -1 && train_route[routeID].reversed_direction == train_route[regional_train[trainIdx].indexOfRoute].reversed_direction) {
							// try to create a route from existing routes
							newRouteID = createNewJointRoute(routeID, regional_train[trainIdx].indexOfRoute, "final");
						}
					}
				}

				// created a suitable route
				if (newRouteID != -1) {
					// change route
					regional_train[trainIdx].indexOfRoute = newRouteID;
					regional_train[trainIdx].TrainRouteID = train_route[newRouteID].ID;
					regional_train[trainIdx].Start_Node_X = train_route[newRouteID].x_of_start_node;

					// update arrival platform
					regional_train[trainIdx].arrivalPlatform = platform;

					return;
				}
			}
		}
	}
}

/*************************************************************************************************************************************************/

// Definition of Objects to convert EGTRAIN infrastructure, timetable and rolling stock to real-time traffic rescheduling algorithms

/*************************************************************************************************************************************************/

// Definition of objects for converting EGTRAIN inputs into real-time traffic rescheduling tool RECIFE

std::list<TopologySequence> All_Topology_Sequences;

std::list<Arc> Additional_Arcs_To_Create_TDS;

// This function fills in the Unused TopoParts In TopoSequence list for the TDS which are on switches, hence having unused TopoParts when crossed in a direction rather than another
void fillinUnusedTopoPartsOfTopoSequencesForTdsOnSwitches(list<TopologySequence>& All_TopoSequences) {
	if (All_TopoSequences.size() != 0) {
		for (auto i = All_TopoSequences.begin(); i != All_TopoSequences.end(); i++) {
			for (auto j = All_TopoSequences.begin(); j != All_TopoSequences.end(); j++) {
				// if the Topology Sequences i and j are two different sequences of the same TDS
				if ((j->TDS_ID == i->TDS_ID) && (j->ID != i->ID)) {
					// then add the list of TopoParts which are not used in a sequence to the list of unused TopoParts of the other sequence
					for (auto h = j->TopologyPart_List.begin(); h != j->TopologyPart_List.end(); h++) {
						bool IsTopoPartAlreadyThere = false;
						list<TopologyPart>::iterator k = i->TopologyPart_List.begin();

						while (k != i->TopologyPart_List.end()) {
							// if the TopoPart of Topology Sequence j is already in Topology Sequence i
							if ((k->ID == h->ID) && (k->StartX == h->StartX) && (k->EndX == h->EndX)) {
								// Then set boolean value to true
								IsTopoPartAlreadyThere = true;
								// and break the for loop cycle over k
								break;
							}
							k++;
						}

						// if the Topology Part of j is not in Topology Sequence i, then add it to the list of unused TopoParts in i
						if (IsTopoPartAlreadyThere == 0) {
							i->Unused_TopoParts_In_Sequence.push_back(*h);
						}
					}
				}
			}
		}
	}
}

void initialiseTopologySequencesForRecife(Section* Blocks, int N_Blocks, list<TDS> All_TDS, list<TopologySequence>& AllTopoSequences) {
	for (auto it = All_TDS.begin(); it != All_TDS.end(); it++) {

		for (int i = 0; i < N_Blocks; i++) {
			if (Blocks[i].TDS_in_block.empty() != 1) {
				for (auto td = Blocks[i].TDS_in_block.begin(); td != Blocks[i].TDS_in_block.end(); td++) {
					string BlockTDS_ID = (*td)->ID;
					if (it->ID == BlockTDS_ID) {
						if (Blocks[i].withSwitchDiv == 0) {
							TopologySequence TS;
							TS.TDS_ID = it->ID;
							TS.ID = TS.TDS_ID + "_straight";
							TS.blocksection_ID = Blocks[i].ID;

							int counter = 0; // this counts the number of Topologyparts in the TDS
							// defining topologypart
							for (int j = 0; j < Blocks[i].total_arcs; j++) {

								if (((Blocks[i].arcs_in_signalling_block_section[j].startNode.X >= (*td)->start_node.X) && (Blocks[i].arcs_in_signalling_block_section[j].startNode.X < (*td)->end_node.X)) && ((Blocks[i].arcs_in_signalling_block_section[j].endNode.X > (*td)->start_node.X) && (Blocks[i].arcs_in_signalling_block_section[j].endNode.X <= (*td)->end_node.X))) {
									// When both the start and end Node of the arcs are within the TDS boundaries
									// we should take the whole Arc

									counter++;
									TopologyPart TDSPart;
									TDSPart.ID = (*td)->ID + "_a_" + to_string(counter);
									TDSPart.TDS_arc = &Blocks[i].arcs_in_signalling_block_section[j];

									// specifying the X and Y coordinates of the TDSPart
									TDSPart.trackLineId = Blocks[i].trackLineId;
									TDSPart.StartX = TDSPart.TDS_arc->startNode.X;
									TDSPart.StartY = TDSPart.trackLineId;
									TDSPart.EndX = TDSPart.TDS_arc->endNode.X;
									TDSPart.EndY = TDSPart.trackLineId;

									TS.TopologyPart_List.push_back(TDSPart);

								}

								// if the end start Node of the Arc is lower than the start of the TDS then take only the part after the start Node of the TDS
								else if ((Blocks[i].arcs_in_signalling_block_section[j].startNode.X < (*td)->start_node.X) && ((Blocks[i].arcs_in_signalling_block_section[j].endNode.X <= (*td)->end_node.X) && (Blocks[i].arcs_in_signalling_block_section[j].endNode.X > (*td)->start_node.X))) {

									counter++;
									TopologyPart TDSPart;
									TDSPart.ID = (*td)->ID + "_a_" + to_string(counter);

									// create a copy of the Arc
									Arc ArcCopy = Blocks[i].arcs_in_signalling_block_section[j];

									Additional_Arcs_To_Create_TDS.push_back(ArcCopy);

									list<Arc>::iterator EndElement = Additional_Arcs_To_Create_TDS.end();
									EndElement--;

									EndElement->startNode = (*td)->start_node;

									// the length should be changed accordingly as the Arc is cut at the end
									EndElement->length = EndElement->endNode.X - EndElement->startNode.X;
									TDSPart.TDS_arc = &(*EndElement);

									// specifying the X and Y coordinates of the TDSPart
									TDSPart.trackLineId = Blocks[i].trackLineId;
									TDSPart.StartX = TDSPart.TDS_arc->startNode.X;
									TDSPart.StartY = TDSPart.trackLineId;
									TDSPart.EndX = TDSPart.TDS_arc->endNode.X;
									TDSPart.EndY = TDSPart.trackLineId;

									TS.TopologyPart_List.push_back(TDSPart);

								}

								else if (((Blocks[i].arcs_in_signalling_block_section[j].startNode.X >= (*td)->start_node.X) && (Blocks[i].arcs_in_signalling_block_section[j].startNode.X < (*td)->end_node.X)) && (Blocks[i].arcs_in_signalling_block_section[j].endNode.X > (*td)->end_node.X)) {

									counter++;
									TopologyPart TDSPart;
									TDSPart.ID = (*td)->ID + "_a_" + to_string(counter);

									// create a copy of the Arc
									Arc ArcCopy = Blocks[i].arcs_in_signalling_block_section[j];

									Additional_Arcs_To_Create_TDS.push_back(ArcCopy);

									list<Arc>::iterator EndElement = Additional_Arcs_To_Create_TDS.end();
									EndElement--;

									EndElement->endNode = (*td)->end_node;

									// the length should be changed accordingly as the Arc is cut at the end
									EndElement->length = EndElement->endNode.X - EndElement->startNode.X;
									TDSPart.TDS_arc = &(*EndElement);

									// specifying the X and Y coordinates of the TDSPart
									TDSPart.trackLineId = Blocks[i].trackLineId;
									TDSPart.StartX = TDSPart.TDS_arc->startNode.X;
									TDSPart.StartY = TDSPart.trackLineId;
									TDSPart.EndX = TDSPart.TDS_arc->endNode.X;
									TDSPart.EndY = TDSPart.trackLineId;

									TS.TopologyPart_List.push_back(TDSPart);

								}

								else if ((Blocks[i].arcs_in_signalling_block_section[j].startNode.X < (*td)->start_node.X) && (Blocks[i].arcs_in_signalling_block_section[j].endNode.X > (*td)->end_node.X)) { // if instead the TDS is shorter than the Arc, i.e. if both the start and end of the Arc are outside of the start and end Node of the TDS

									counter++;
									TopologyPart TDSPart;
									TDSPart.ID = (*td)->ID + "_a_" + to_string(counter);

									// create a copy of the Arc
									Arc ArcCopy = Blocks[i].arcs_in_signalling_block_section[j];

									Additional_Arcs_To_Create_TDS.push_back(ArcCopy);

									list<Arc>::iterator EndElement = Additional_Arcs_To_Create_TDS.end();
									EndElement--;

									EndElement->startNode = (*td)->start_node; // cut the start of the Arc with the start Node of the TDS
									EndElement->endNode = (*td)->end_node;	// cut the end of the Arc with end Node of the TDS

									// the length should be changed accordingly as the Arc is cut at the end
									EndElement->length = EndElement->endNode.X - EndElement->startNode.X;
									TDSPart.TDS_arc = &(*EndElement);

									// specifying the X and Y coordinates of the TDSPart
									TDSPart.trackLineId = Blocks[i].trackLineId;
									TDSPart.StartX = TDSPart.TDS_arc->startNode.X;
									TDSPart.StartY = TDSPart.trackLineId;
									TDSPart.EndX = TDSPart.TDS_arc->endNode.X;
									TDSPart.EndY = TDSPart.trackLineId;

									TS.TopologyPart_List.push_back(TDSPart);
								}
							}

							AllTopoSequences.push_back(TS);
						}
						// From here we need to write the conditions to create TopoSequence for block sections with diverging switch
						// the conditions will be exactly the same as for those without switch apart from the fact that
						// if the trackline of the TDS is equal to the FirstTrackLine of the block section then the Arc with the switch is the last one
						// and the TopologySequence ID can be TDS_ID_Diverging
						// else if it is equal to the Secondtrackline of the Block section, then the topologySequence ID is TDS_ID Converging
						// Things to be done is to add variable x and y to the topo
						else { // if the block section has a diverging switch

							TopologySequence TS;
							TS.TDS_ID = it->ID;
							TS.blocksection_ID = Blocks[i].ID;
							double TDS_StartNodeX = -1;
							double TDS_EndNodeX = -1;
							bool TDSOnFirstTrackLine = 0;

							// check whether the TDS trackline ID is the same of the first or the second connected trackline of the block section
							if (it->TracklineID == Blocks[i].FirstConnectedTrackLineID) {
								TS.ID = TS.TDS_ID + "_diverging";
								// if the TDS ID has the ID of the FirstConnectBlock section then the start Node of the TDS is the actual start Node
								// and the end Node is the Node on the switch
								TDS_StartNodeX = it->start_node.X;
								TDS_EndNodeX = it->node_on_switch.X;
								TDSOnFirstTrackLine = 1;

							} else if (it->TracklineID == Blocks[i].SecondConnectedTrackLineID) {
								TS.ID = TS.TDS_ID + "_converging";
								// if the TDS has the TrackLine ID of the second connected TrackLine then the beginning switch is the Node of the switch and the end Node is the actual end Node of the TDS
								TDS_StartNodeX = it->node_on_switch.X;
								TDS_EndNodeX = it->end_node.X;
							}

							int counter = 0; // this counts the number of Topologyparts in the TDS
							// defining topologypart
							for (int j = 0; j < Blocks[i].total_arcs; j++) {

								bool TDSPartCreated = false; // This variable turns to 1 only if a an Arc of the block section falls in the TDS section considered,
								// i.e. only i a TDSPart is created for the TDS to be part of a Topology Sequence

								if (((Blocks[i].arcs_in_signalling_block_section[j].startNode.X >= TDS_StartNodeX) && (Blocks[i].arcs_in_signalling_block_section[j].startNode.X < TDS_EndNodeX)) && ((Blocks[i].arcs_in_signalling_block_section[j].endNode.X > TDS_StartNodeX) && (Blocks[i].arcs_in_signalling_block_section[j].endNode.X <= TDS_EndNodeX))) {
									// When both the start and end Node of the arcs are within the TDS boundaries
									// we should take the whole Arc

									counter++;

									TopologyPart TDSPart;

									TDSPartCreated = true;
									TDSPart.ID = (*td)->ID + "_a_" + to_string(counter);
									TDSPart.TDS_arc = &Blocks[i].arcs_in_signalling_block_section[j];

									// specifying the X and Y coordinates of the TDSPart
									TDSPart.trackLineId = it->TracklineID;
									TDSPart.StartX = TDSPart.TDS_arc->startNode.X;
									TDSPart.StartY = TDSPart.trackLineId;
									TDSPart.EndX = TDSPart.TDS_arc->endNode.X;
									TDSPart.EndY = TDSPart.trackLineId;

									TS.TopologyPart_List.push_back(TDSPart);

								}

								// if the end start Node of the Arc is lower than the start of the TDS then take only the part after the start Node of the TDS
								else if ((Blocks[i].arcs_in_signalling_block_section[j].startNode.X < (*td)->start_node.X) && ((Blocks[i].arcs_in_signalling_block_section[j].endNode.X <= (*td)->end_node.X) && (Blocks[i].arcs_in_signalling_block_section[j].endNode.X > (*td)->start_node.X))) {

									counter++;
									TopologyPart TDSPart;

									TDSPartCreated = true;
									TDSPart.ID = (*td)->ID + "_a_" + to_string(counter);

									// create a copy of the Arc
									Arc ArcCopy = Blocks[i].arcs_in_signalling_block_section[j];

									Additional_Arcs_To_Create_TDS.push_back(ArcCopy);

									list<Arc>::iterator EndElement = Additional_Arcs_To_Create_TDS.end();
									EndElement--;

									if (TDSOnFirstTrackLine == 1) {
										EndElement->startNode = (*td)->start_node;
									}

									else {
										EndElement->startNode = (*td)->node_on_switch;
									}

									// the length should be changed accordingly as the Arc is cut at the beginning
									EndElement->length = EndElement->endNode.X - EndElement->startNode.X;
									TDSPart.TDS_arc = &(*EndElement);

									// specifying the X and Y coordinates of the TDSPart
									TDSPart.trackLineId = it->TracklineID;
									TDSPart.StartX = TDSPart.TDS_arc->startNode.X;
									TDSPart.StartY = TDSPart.trackLineId;
									TDSPart.EndX = TDSPart.TDS_arc->endNode.X;
									TDSPart.EndY = TDSPart.trackLineId;

									TS.TopologyPart_List.push_back(TDSPart);

								}

								else if (((Blocks[i].arcs_in_signalling_block_section[j].startNode.X >= (*td)->start_node.X) && (Blocks[i].arcs_in_signalling_block_section[j].startNode.X < (*td)->end_node.X)) && (Blocks[i].arcs_in_signalling_block_section[j].endNode.X > (*td)->end_node.X)) {

									counter++;
									TopologyPart TDSPart;

									TDSPartCreated = true;
									TDSPart.ID = (*td)->ID + "_a_" + to_string(counter);

									// create a copy of the Arc
									Arc ArcCopy = Blocks[i].arcs_in_signalling_block_section[j];

									Additional_Arcs_To_Create_TDS.push_back(ArcCopy);

									list<Arc>::iterator EndElement = Additional_Arcs_To_Create_TDS.end();
									EndElement--;

									if (TDSOnFirstTrackLine == 1) {
										EndElement->endNode = (*td)->node_on_switch;
									} else {

										EndElement->endNode = (*td)->end_node;
									}

									// the length should be changed accordingly as the Arc is cut at the end
									EndElement->length = EndElement->endNode.X - EndElement->startNode.X;
									TDSPart.TDS_arc = &(*EndElement);

									// specifying the X and Y coordinates of the TDSPart
									TDSPart.trackLineId = it->TracklineID;
									TDSPart.StartX = TDSPart.TDS_arc->startNode.X;
									TDSPart.StartY = TDSPart.trackLineId;
									TDSPart.EndX = TDSPart.TDS_arc->endNode.X;
									TDSPart.EndY = TDSPart.trackLineId;

									TS.TopologyPart_List.push_back(TDSPart);
								}

								else if ((Blocks[i].arcs_in_signalling_block_section[j].startNode.X < (*td)->start_node.X) && (Blocks[i].arcs_in_signalling_block_section[j].endNode.X > (*td)->end_node.X)) {
									// if instead both the start and end edges of the Arc are outside the start and end of the TDS ( i.e. the TDS is shorter than the Arc)
									counter++;
									TopologyPart TDSPart;

									TDSPartCreated = true;
									TDSPart.ID = (*td)->ID + "_a_" + to_string(counter);

									// create a copy of the Arc
									Arc ArcCopy = Blocks[i].arcs_in_signalling_block_section[j];

									Additional_Arcs_To_Create_TDS.push_back(ArcCopy);

									list<Arc>::iterator EndElement = Additional_Arcs_To_Create_TDS.end();
									EndElement--;

									if (TDSOnFirstTrackLine == 1) {
										EndElement->startNode = (*td)->start_node;
										EndElement->endNode = (*td)->node_on_switch;
									} else {
										EndElement->startNode = (*td)->node_on_switch;
										EndElement->endNode = (*td)->end_node;
									}

									// the length should be changed accordingly as the Arc is cut at the end
									EndElement->length = EndElement->endNode.X - EndElement->startNode.X;
									TDSPart.TDS_arc = &(*EndElement);

									// specifying the X and Y coordinates of the TDSPart
									TDSPart.trackLineId = it->TracklineID;
									TDSPart.StartX = TDSPart.TDS_arc->startNode.X;
									TDSPart.StartY = TDSPart.trackLineId;
									TDSPart.EndX = TDSPart.TDS_arc->endNode.X;
									TDSPart.EndY = TDSPart.trackLineId;

									TS.TopologyPart_List.push_back(TDSPart);
								}

								// If a TDS Part has been created for the current TDS then the following condition should be checked.
								if (TDSPartCreated == 1) {
									// if the beginning Node of the TDSPart is equal to the beginning of the switch and the trackline ID of the TDS is the FirstConnected TracklineID
									// then rename the TDSPart
									list<TopologyPart>::iterator LastInsertedElement = TS.TopologyPart_List.end();
									LastInsertedElement--; // pointer to the last inserted element
									// if the beginning Node of the TDSPart is equal to the beginning of the switch and the trackline ID of the TDS is the FirstConnected TracklineID
									// then rename the TDSPart
									if (((*(*LastInsertedElement).TDS_arc).startNode.X == Blocks[i].XStartSwitch) && (it->TracklineID == Blocks[i].FirstConnectedTrackLineID)) {
										LastInsertedElement->ID = (*td)->ID + "_a_div_switch";

										// Adjusting the y coordinate of the Arc edges if the Arc is one the diverging switch in the block section
										LastInsertedElement->StartY = Blocks[i].FirstConnectedTrackLineID;
										LastInsertedElement->EndY = abs((Blocks[i].SecondConnectedTrackLineID - Blocks[i].FirstConnectedTrackLineID)) / 2;

									} else if (((*(*LastInsertedElement).TDS_arc).endNode.X == Blocks[i].XEndSwitch) && (it->TracklineID == Blocks[i].SecondConnectedTrackLineID)) {
										LastInsertedElement->ID = (*td)->ID + "_a_conv_switch";

										// Adjusting the y coordinate of the Arc edges if the Arc is one the diverging switch in the block section
										LastInsertedElement->StartY = abs((Blocks[i].SecondConnectedTrackLineID - Blocks[i].FirstConnectedTrackLineID)) / 2;
										LastInsertedElement->EndY = Blocks[i].SecondConnectedTrackLineID;
									}
								}
							}

							AllTopoSequences.push_back(TS);
						}
					}
				}
			}
		}
	}
	// After that all topology sequences have been set up for all the TDS then fill in the list of Unused_TopologyParts
	// for all the TDS containing switches as they have more than 1 Topology Sequence depending on whether the switch is set in the diverging or straight direction

	fillinUnusedTopoPartsOfTopoSequencesForTdsOnSwitches(AllTopoSequences);
}

// Function to print all infrastructure elements for RECIFE conflict detection and resolution algorithm
void printInfrastructureFileForRecife(Section* Blocks, int N_Blocks, list<TopologySequence>& AllTopoSequences, string OutputFolder) {
	ofstream RecifeInfraStream;
	string RECIFEInfraOutputFile;
	RECIFEInfraOutputFile = RECIFEInfraOutputFile + OutputFolder + "/Source_Infrastructure_RECIFE.txt";
	RecifeInfraStream.open((char*)RECIFEInfraOutputFile.c_str(), ios::binary);
	RecifeInfraStream << "Trackline \t BlockID  \t EntrySignal \t ExitSignal \t BlockStartX[Km] \t BlockEndX[Km] \t TopologySequences \t TDS_Id \t topologypartID \t topologyStartY \t topologyEndY \t TopoPartStartX[Km] \tTopoPartEndX[Km] \t length \t gradient \t curve \t speedlimit\n";

	for (int i = 0; i < N_Blocks; i++) {
		string blockinfoline;

		blockinfoline = blockinfoline + tostring(Blocks[i].trackLineId) + " \t " + Blocks[i].ID + "\t sign_" + Blocks[i].start_node.tdsbId + "\t sign_" + Blocks[i].end_node.tdsbId + "\t " + tostring(Blocks[i].start_node.X) + "\t " + tostring(Blocks[i].end_node.X) + "\t ";

		for (auto td = Blocks[i].TDS_in_block.begin(); td != Blocks[i].TDS_in_block.end(); td++) {

			list<string> InfoTDSPartList;		   // This is the list of information regarding each Topology Part in the TDS (including both those used and unused in the block section)
			string TDSTopoSequences;			   // This is ithe string collecting all of the Topology Sequences belonging to the TDS
			int TopoSequenceCounter = 0;		   // This is a counter on the identified Topology Sequence
			bool TopoPartInfoAlreadyTaken = false; // boolean which turn to true if all the arcs (TopologyParts) of the TDS have been already collected in text

			list<TopologySequence>::iterator LastTopoSequenceElement = AllTopoSequences.end(); // This is the last element of the Topology Sequence list
			LastTopoSequenceElement--;														   // this is the last Topology Sequence of the list

			for (auto s = AllTopoSequences.begin(); s != AllTopoSequences.end(); s++) {

				if (s->TDS_ID == (*td)->ID) {
					int TDPartCounter = 0;
					TopoSequenceCounter++; // increase teh counter everytime there is a Topology Sequence found for the TDS
					if (TopoSequenceCounter == 1) {
						TDSTopoSequences = TDSTopoSequences + "[";

					} else { // if there is more than a topology sequence then they need to be split by a comma
						TDSTopoSequences = TDSTopoSequences + ",";
					}

					TDSTopoSequences = TDSTopoSequences + "['" + s->ID + "',[";

					for (auto p = s->TopologyPart_List.begin(); p != s->TopologyPart_List.end(); p++) {
						TDSTopoSequences = TDSTopoSequences + "[" + tostring(TDPartCounter) + ",'" + p->ID + "']";
						TDPartCounter++;
						// if the TopoPart of the TDS is not the last of the TDS
						if (TDPartCounter < (s->TopologyPart_List.size())) {

							TDSTopoSequences = TDSTopoSequences + ",";
							// if the Info of the Topology Parts of the TDS have not yet been taken
							if (TopoPartInfoAlreadyTaken == 0) {
								// then fill in the list InfoTDSPart
								string ArcInfo;
								ArcInfo = p->ID + "\t" + tostring(p->StartY) + "\t" + tostring(p->EndY) + "\t" + tostring(p->StartX) + "\t" + tostring(p->EndX) + "\t" + tostring((*p).TDS_arc->length) + "\t" + tostring((*p).TDS_arc->gradient) + "\t" + tostring((*p).TDS_arc->curvature) + "\t" + tostring((*p).TDS_arc->speedLimit);
								InfoTDSPartList.push_back(ArcInfo);
							}
						} else {
							TDSTopoSequences = TDSTopoSequences + "]]";

							// if the Info of the Topology Parts of the TDS have not yet been taken
							if (TopoPartInfoAlreadyTaken == 0) {
								// then fill in the list InfoTDSPart
								string ArcInfo;
								ArcInfo = p->ID + "\t" + tostring(p->StartY) + "\t" + tostring(p->EndY) + "\t" + tostring(p->StartX) + "\t" + tostring(p->EndX) + "\t" + tostring((*p).TDS_arc->length) + "\t" + tostring((*p).TDS_arc->gradient) + "\t" + tostring((*p).TDS_arc->curvature) + "\t" + tostring((*p).TDS_arc->speedLimit);
								InfoTDSPartList.push_back(ArcInfo);
								// Also add the Arc (i.e. the Topology Parts of the TDS which are not used in the block section) of the TDS
								for (auto u = s->Unused_TopoParts_In_Sequence.begin(); u != s->Unused_TopoParts_In_Sequence.end(); u++) {
									string UnusedArcInfo;
									UnusedArcInfo = u->ID + "\t" + tostring(u->StartY) + "\t" + tostring(u->EndY) + "\t" + tostring(u->StartX) + "\t" + tostring(u->EndX) + "\t" + tostring((*u).TDS_arc->length) + "\t" + tostring((*u).TDS_arc->gradient) + "\t" + tostring((*u).TDS_arc->curvature) + "\t" + tostring((*u).TDS_arc->speedLimit);
									InfoTDSPartList.push_back(UnusedArcInfo);
								}
								TopoPartInfoAlreadyTaken = true; // set that the information of all the arcs (Topology Parts) of the TDS have now been taken in the form of a text string
							}
						}
					}
				}
				if (s == LastTopoSequenceElement) {
					TDSTopoSequences = TDSTopoSequences + "]\t" + (*td)->ID + "\t";
				}
			}
			// Write in the text file the information about all of the arcs ( Topology Parts) for the TDS in the block section
			for (auto v = InfoTDSPartList.begin(); v != InfoTDSPartList.end(); v++) {
				RecifeInfraStream << blockinfoline << TDSTopoSequences << *v << "\n";
			}
		}
	}
	RecifeInfraStream.close();
	// After that the RECIFE infrastructure file has been printed, delete all the elements in the Topology Sequences an in the additional arcs to create TDS to free memory
	AllTopoSequences.clear();
	Additional_Arcs_To_Create_TDS.clear();
}