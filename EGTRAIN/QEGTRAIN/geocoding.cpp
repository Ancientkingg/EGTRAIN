#include "geocoding.h"

// reads station coordinates, region and corridor(s)
void readStationInfo() {
	// strings to read stations from file
	string line, station;

	// float vector to store coordinates of stations
	vector<array<float, 2>> station_coord;

	// (latitude, longitude) of a single station (read/write files)
	array<float, 2> coord{};

	// file with coordinates of stations
	ifstream myfile_stations(InputMainFolder + "/GUI/StationsCoord.txt");

	// read coordinates of stations
	if (myfile_stations.is_open()) {
		int i = 0;

		// read stations line by line
		while (getline(myfile_stations, line)) {
			// ignore empty lines (in the end of the files)
			if (line.empty())
				continue;

			// processing line to get station name
			station = parse_station(line);

			// parse line
			string name, slat, slon, regions, regionX, corridors;

			istringstream lineStream(line);

			getline(lineStream, name, '\t');
			getline(lineStream, slat, '\t');
			getline(lineStream, slon, '\t');
			getline(lineStream, regions, '\t');
			getline(lineStream, regionX, '\t');
			getline(lineStream, corridors, '\t');

			coord[0] = stof(slat);
			coord[1] = stof(slon);

			// add coordinates to list of stations
			station_coord.push_back(coord);
			StationArray[i].latitude = stod(slat);
			StationArray[i].longitude = stod(slon);

			// add regions to station
			std::string token;
			char delimiter = ',';
			std::istringstream tokenStreamRegions(regions);
			while (std::getline(tokenStreamRegions, token, delimiter)) {
				StationArray[i].regions.push_back(std::stoi(token));
			}

			// add regionX to station
			std::istringstream tokenStreamRegionX(regionX);
			int j = 0; // index over regions
			while (std::getline(tokenStreamRegionX, token, delimiter)) {
				StationArray[i].regionX[StationArray[i].regions[j]] = std::stod(token);
				j++;
			}

			// add corridor(s) to station
			std::istringstream tokenStreamCorridors(corridors);
			while (std::getline(tokenStreamCorridors, token, delimiter)) {
				StationArray[i].corridors.push_back(token);
			}

			// update index
			i++;
		}

		// close files
		myfile_stations.close();
	}

	// error opening the files
	else {
		cout << "Unable to open file with coordinates of stations.\n";
	}
}

// parses a line from Stations file
string parse_station(string line) {
	// parse line
	string x, name;

	istringstream lineStream(line);

	getline(lineStream, x, '\t');
	getline(lineStream, name, '\t');

	// add space(s) when having uppercase(s)
	int length = name.length();
	for (int i = 1; i < length; i++) // ignore possible initial uppercase (start from 1)
	{
		int c = name[i];
		if (isupper(c)) {
			name.insert(i, "+");
			i++; // go to the next (added space)
		}
	}

	return name;
}
