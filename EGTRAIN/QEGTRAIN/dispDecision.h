#ifndef dispDecision_H
#define dispDecision_H

#include <string>
#include <vector>
#include <iostream>

class dispDecision {
public:
	// class constructor
	dispDecision(std::string messageType, int lineID, std::string stationArr, int arrivalPlatform, int routeID, std::string timetableFile, int intendedDepTime, std::vector<int> dwellTimes);
	dispDecision(std::string messageType, int lineID, std::string stationDep, int arrivalPlatform, int routeID, int startTime);

	std::string messageType;
	std::string lineName, stationDep, stationArr, trainName, timeString, timetableFile;
	int routeID, lineID, arrivalPlatform;
	int intendedDepTime = 0;
	int startTime = 0;
	std::vector<int> dwellTimes;
};

#endif // dispDecision_H
