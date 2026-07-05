#ifndef DISPATCHDECISION_H
#define DISPATCHDECISION_H

#include <string>
#include <vector>
#include <iostream>

class DispatchDecision {
public:
	// class constructor
	DispatchDecision(std::string messageType, int lineID, std::string stationArr, int arrivalPlatform, int routeID, std::string timetableFile, int intendedDepTime, std::vector<int> dwellTimes);
	DispatchDecision(std::string messageType, int lineID, std::string stationDep, int arrivalPlatform, int routeID, int startTime);

	std::string messageType;
	std::string lineName, stationDep, stationArr, trainName, timeString, timetableFile;
	int routeID, lineID, arrivalPlatform;
	int intendedDepTime = 0;
	int startTime = 0;
	std::vector<int> dwellTimes;
};

#endif // DISPATCHDECISION_H
