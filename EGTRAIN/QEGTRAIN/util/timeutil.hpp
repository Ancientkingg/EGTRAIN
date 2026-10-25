#pragma once

#include <sstream>
#include <time.h>
#include "util/portability.h" // localtime_r shim on MSVC

/// \file
/// Text forms of simulation times and of a local date and time.

/// The local date and time of step \p t as "YYYY-MM-DD HH:MM:SS", without a time zone: midnight of
/// the current day of the computer plus \p startingSimulationTime plus \p t. \p t is seconds from
/// the start of the run and \p startingSimulationTime the start of the run in seconds since
/// midnight.
inline std::string simulationTime(int t, int startingSimulationTime) {
	time_t time0 = time(nullptr);

	struct tm ltm, stime;
	time_t now;
	localtime_r(&time0, &ltm);
	now = time0 - ((ltm.tm_hour * 3600) + (ltm.tm_min * 60) + ltm.tm_sec) + startingSimulationTime;
	time_t simulation_time = now + t;
	localtime_r(&simulation_time, &stime);
	char simTime[100]; // simulation time
	strftime(simTime, 50, "%Y-%m-%d %H:%M:%S", &stime);

	return simTime;
}

/// \p tm as "Mon 01.01.1900 12:00:00": weekday, day, month, year, and the time. The hour has no
/// leading zero.
inline std::string formatDateTime(const tm& tm) {
	std::stringstream ss;

	// weekday
	switch (tm.tm_wday) {
		case 0:
			ss << "Sun ";
			break;
		case 1:
			ss << "Mon ";
			break;
		case 2:
			ss << "Tue ";
			break;
		case 3:
			ss << "Wed ";
			break;
		case 4:
			ss << "Thu ";
			break;
		case 5:
			ss << "Fri ";
			break;
		case 6:
			ss << "Sat ";
			break;
		default:
			break;
	}

	// date
	if (tm.tm_mday < 10)
		ss << "0" << tm.tm_mday << ".";
	else
		ss << tm.tm_mday << ".";
	if (tm.tm_mon + 1 < 10)
		ss << "0" << tm.tm_mon + 1 << ".";
	else
		ss << tm.tm_mon + 1 << ".";
	ss << tm.tm_year + 1900 << " ";

	// time
	ss << tm.tm_hour << ":";
	if (tm.tm_min < 10)
		ss << "0" << tm.tm_min << ":";
	else
		ss << tm.tm_min << ":";
	if (tm.tm_sec < 10)
		ss << "0" << tm.tm_sec;
	else
		ss << tm.tm_sec;

	return ss.str();
}

/// \p tm in a form safe for file names, "1900_01_01_12_00_00". The hour has no leading zero.
inline std::string formatDateTimeFilename(const tm& tm) {
	std::stringstream ss;

	// date
	ss << tm.tm_year + 1900 << "_";
	if (tm.tm_mon + 1 < 10)
		ss << "0" << tm.tm_mon + 1 << "_";
	else
		ss << tm.tm_mon + 1 << "_";
	if (tm.tm_mday < 10)
		ss << "0" << tm.tm_mday << "_";
	else
		ss << tm.tm_mday << "_";

	// time
	ss << tm.tm_hour << "_";
	if (tm.tm_min < 10)
		ss << "0" << tm.tm_min << "_";
	else
		ss << tm.tm_min << "_";
	if (tm.tm_sec < 10)
		ss << "0" << tm.tm_sec;
	else
		ss << tm.tm_sec;

	return ss.str();
}
