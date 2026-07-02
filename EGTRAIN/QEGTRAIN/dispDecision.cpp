#include "dispDecision.h"

// class constructor
dispDecision::dispDecision(std::string messageType, int lineID, std::string stationArr, int arrivalPlatform, int routeID, std::string timetableFile, int intendedDepTime, std::vector<int> dwellTimes)
	: messageType(messageType), lineID(lineID), stationArr(stationArr), arrivalPlatform(arrivalPlatform), routeID(routeID), timetableFile(timetableFile), intendedDepTime(intendedDepTime), dwellTimes(dwellTimes) {}

dispDecision::dispDecision(std::string messageType, int lineID, std::string stationDep, int arrivalPlatform, int routeID, int startTime)
	: messageType(messageType), lineID(lineID), stationDep(stationDep), arrivalPlatform(arrivalPlatform), routeID(routeID), startTime(startTime) {}
