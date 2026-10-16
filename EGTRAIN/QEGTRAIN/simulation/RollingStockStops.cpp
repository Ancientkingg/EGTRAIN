#include "simulation/RollingStock.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace std;

void Train::cacheStationPositions() {
	for (int s = 0; s < kMaxTimetableStations; s++) {
		stationBlockSection[s] = -1;
		stationArc[s] = -1;
	}
	for (int s = 0; s < numStations; s++) {
		for (int h = 0; h < train_route[indexOfRoute].N_Block_Sections; h++) {
			for (int k = 0; k < train_route[indexOfRoute].sequence_of_block_sections[h].total_arcs; k++) {
				if (train_route[indexOfRoute].sequence_of_block_sections[h].arcs_in_signalling_block_section[k].endNode.stationName == Stations[s].stationName) {
					stationBlockSection[s] = h;
					stationArc[s] = k;
				}
			}
		}
	}
}

void Train::computeArrivalAndDepartureAtLocation(double LocationPosition, TrainEvent& PassingPoint) {
	const int TrainEntryTime = std::max(1, static_cast<int>(departure_time));
	double ArrivalTime = -1, DepartureTime = -1;
	bool IsArrivalFound = false;
	bool IsDepartureFound = false;
	for (int t = TrainEntryTime; t < initial_variables.times; t++) {
		if (IsArrivalFound == 0) {
			if (isShortOfBrakingPoint(instant_spatial_position[t - 1], LocationPosition) && !isShortOfBrakingPoint(instant_spatial_position[t], LocationPosition)) {
				IsArrivalFound = true;
				ArrivalTime = (t - 1) * timestep;
			}
		}
		if (IsDepartureFound == 0) {
			if (!isPastStopHoldback(instant_spatial_position[t - 1], LocationPosition) && isPastStopHoldback(instant_spatial_position[t], LocationPosition)) {
				IsDepartureFound = true;
				DepartureTime = (t - 1) * timestep;
			}
		}
		if ((IsArrivalFound == 1) && (IsDepartureFound == 1))
			break; // if both arrival and depature of the train have been found we can break the for loop over the time t
	}
	// A train that reached the location is reported. It has no departure when it stays there
	// until the end of the run, as at its last stop.
	if (IsArrivalFound == 1) {
		PassingPoint.Time = ArrivalTime;
		if (IsDepartureFound == 1)
			PassingPoint.Time2 = DepartureTime;
		// The position of the PassingPoint must be the absolute geographical position, so we must use a conversion if we are using a route that is reversed
		if (train_route[indexOfRoute].reversed_direction == 1) {
			PassingPoint.Position = train_route[indexOfRoute].OriginalRefReversedRoute - LocationPosition;
		} else {
			PassingPoint.Position = LocationPosition;
		}
	}
}

void Train::ComputeTimetablingPoints() {
	if (TimetablePoints.empty() == 1) {
		for (int i = 0; i < train_route[indexOfRoute].N_Block_Sections; i++) {
			for (int j = 0; j < train_route[indexOfRoute].sequence_of_block_sections[i].total_arcs; j++) {
				if (train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.stationName.empty() != 1) { // if the Node on the route is a station or junction area
					TrainEvent TTPoint;
					TTPoint.SuccessorID = train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.stationName; // this is the name of the location of the timetable point
					TTPoint.Position = train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.X;
					TTPoint.trainDescription = this->trainDescription; // Assigning the trainDescription to the Train Event
																	   // In this case we compute the arrival and departure of the train from the location and we assign these to the TrainEvent
					computeArrivalAndDepartureAtLocation(train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.X * 1000, TTPoint);
					// Compute the arrival and departure times of the train from the location and push it back into the TimeTablePoints List
					this->TimetablePoints.push_back(TTPoint);
				}
			}
		}
	}
}

double Train::Det_Run_Start_Time() {
	for (int t = 0; t < End_Time; t++) {
		if (instant_train_speed[t] > 0) {
			RunStartTime = t - 1;
			break;
		}
	}
	return RunStartTime * timestep;
}

string Train::stationNameForArrivalStats(int stationIndex) const {
	if (stationIndex < 0 || stationIndex >= numStations)
		return "None";
	if (StationArrivalNames[stationIndex] != "None")
		return StationArrivalNames[stationIndex];
	return Stations[stationIndex].stationName;
}

const TrainEvent* Train::timetablePointOfStop(int stopIndex) const {
	const string stationName = stationNameForArrivalStats(stopIndex);
	int call = 1;
	for (int previous = 0; previous < stopIndex; previous++) {
		if (stationNameForArrivalStats(previous) == stationName)
			call++;
	}
	int seen = 0;
	for (const TrainEvent& event : TimetablePoints) {
		if (event.SuccessorID == stationName && ++seen == call)
			return &event;
	}
	return nullptr;
}

void Train::Determine_Actual_Station_Arrivals() {
	for (int s = 0; s < numStations; s++) {
		if (const TrainEvent* point = timetablePointOfStop(s)) {
			StationArrivals[s] = point->Time >= 0 ? point->Time : -1;
			if (point->Time >= 0)
				StationArrivalNames[s] = point->SuccessorID;
		}
	}

	const double toleranceMeters = 5.0;
	const int sampleCount = trajectorySize();
	for (int s = 0; s < numStations; s++) {
		if (StationArrivals[s] >= 0 || timetablePointOfStop(s))
			continue;

		const double stationMeters = stationRoutePositionMeters(s);
		if (stationMeters < 0)
			continue;
		for (int t = 1; t < sampleCount; t++) {
			if (instant_spatial_position[t - 1] == -9999 || instant_spatial_position[t] == -9999)
				continue;

			const double previous = trainXPosition(t - 1) * 1000.0;
			const double current = trainXPosition(t) * 1000.0;

			const double low = std::min(previous, current) - toleranceMeters;
			const double high = std::max(previous, current) + toleranceMeters;
			if (stationMeters >= low && stationMeters <= high) {
				StationArrivals[s] = t * timestep;
				if (StationArrivalNames[s] == "None")
					StationArrivalNames[s] = Stations[s].stationName;
				break;
			}
		}
	}
}

void Train::computeArrivalDelaysAtStations() {
	TotalInputDelays = EntranceDelay;
	for (int s = 0; s < numStations; s++) {
		// Calculate Total Delay in input
		TotalInputDelays = TotalInputDelays + StationDisturbance[s];
		// a train that did not arrive at station s has no arrival delay; a stop without a planned arrival has none either
		if (StationArrivals[s] >= 0 && ScheduledArrivals[s] >= 0) {
			// Compute StationDelay
			StationDelay[s] = StationArrivals[s] - ScheduledArrivals[s];
			// Compute ConsecutiveDelay
			double Cum_Disturbance = 0; // Cumulated disturbances to dwell times of the previous stations
			for (int k = 0; k < s; k++) {
				Cum_Disturbance = Cum_Disturbance + StationDisturbance[k];
			}
			StationConsecDelay[s] = StationDelay[s] - EntranceDelay - Cum_Disturbance;
			// The threshold is 30 sec, that is why if the train has a delay or a consecutive delay less or equal than 30 s is not considered delayed
			if (StationDelay[s] <= 0)
				StationDelay[s] = 0;
			if (StationConsecDelay[s] <= 0)
				StationConsecDelay[s] = 0;
		}
	}
}

void Train::Compute_Pos_And_Neg_Arrival_Delays_At_Stations() {
	for (int s = 0; s < numStations; s++) {
		// if the train is arrived at station s and has a planned arrival there
		if (StationArrivals[s] >= 0 && ScheduledArrivals[s] >= 0) {
			// Compute StationDelay
			StationDelay[s] = StationArrivals[s] - ScheduledArrivals[s];
			// Compute ConsecutiveDelay
			double Cum_Disturbance = 0; // Cumulated disturbances to dwell times of the previous stations
			for (int k = 0; k < s; k++) {
				Cum_Disturbance = Cum_Disturbance + StationDisturbance[k];
			}
			StationConsecDelay[s] = StationDelay[s] - EntranceDelay - Cum_Disturbance;
		}
	}
}

bool Train::stationIsOnRoute(int stationIndex, const vector<string>& allowedBlockIds) const {
	if (Stations.empty() || stationIndex < 0 || stationIndex >= numStations || indexOfRoute < 0 || indexOfRoute >= static_cast<int>(train_route.size()))
		return false;
	const string& stationName = Stations[stationIndex].stationName;
	if (stationName.empty() || stationName == "None")
		return false;
	const auto matches = [&stationName](const Node& node) {
		return node.station && node.stationName == stationName;
	};
	const Route& route = train_route[indexOfRoute];
	for (int h = 0; h < route.N_Block_Sections; ++h) {
		const Section& section = route.sequence_of_block_sections[h];
		if (!allowedBlockIds.empty() && std::find(allowedBlockIds.begin(), allowedBlockIds.end(), section.ID) == allowedBlockIds.end())
			continue;
		if (matches(section.start_node) || matches(section.end_node))
			return true;
		for (int arcIndex = 0; arcIndex < section.total_arcs; ++arcIndex) {
			if (matches(section.arcs_in_signalling_block_section[arcIndex].endNode))
				return true;
		}
	}
	return false;
}

double Train::stationRoutePositionMeters(int stationIndex) const {
	if (!stationIsOnRoute(stationIndex))
		return -1;
	const string stationName = stationNameForArrivalStats(stationIndex);
	double stationX = Stations[stationIndex].X;
	if (stationX == 0) {
		for (int h = 0; h < ::numStations; h++) {
			if (StationArray[h].stationName == stationName) {
				stationX = StationArray[h].X;
				break;
			}
		}
	}
	const double stationMeters = stationX * 1000.0;
	for (int h = 0; h < train_route[indexOfRoute].N_Block_Sections; h++) {
		const Section& section = train_route[indexOfRoute].sequence_of_block_sections[h];
		const double low = std::min(section.GeoXBegNode, section.GeoXEndNode) - 5.0;
		const double high = std::max(section.GeoXBegNode, section.GeoXEndNode) + 5.0;
		if (stationMeters >= low && stationMeters <= high) {
			const double portion = train_route[indexOfRoute].reversed_direction
				? section.GeoXBegNode - stationMeters
				: stationMeters - section.GeoXBegNode;
			return section.start_node.X * 1000.0 + portion;
		}
	}

	return stationMeters;
}

void Train::recordStationPassagesAtTime(int time_seconds) {
	if (time_seconds <= 0 || time_seconds >= trajectorySize())
		return;

	const double previous = instant_spatial_position[time_seconds - 1];
	const double current = instant_spatial_position[time_seconds];
	if (previous == -9999 || current == -9999)
		return;

	const double toleranceMeters = 5.0;
	const double low = std::min(previous, current) - toleranceMeters;
	const double high = std::max(previous, current) + toleranceMeters;
	for (int s = 0; s < numStations; s++) {
		if (StationArrivals[s] >= 0)
			continue;

		const double stationMeters = stationRoutePositionMeters(s);
		if (stationMeters < 0)
			continue;
		if (stationMeters >= low && stationMeters <= high) {
			StationArrivals[s] = time_seconds * timestep;
			if (StationArrivalNames[s] == "None")
				StationArrivalNames[s] = Stations[s].stationName;
		}
	}
}
