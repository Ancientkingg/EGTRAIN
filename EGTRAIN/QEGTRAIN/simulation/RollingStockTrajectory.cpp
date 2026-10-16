#include "simulation/RollingStock.h"
#include "util/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <list>
#include <string>
#include <vector>

double Train::train_energy_consumption(int t) {
	double Pot1 = 0;
	double Pot2 = 0;
	if (instant_train_power_consumption[t - 1] >= 0)
		Pot1 = instant_train_power_consumption[t - 1];
	else
		Pot1 = 0;
	if (instant_train_power_consumption[t] >= 0)
		Pot2 = instant_train_power_consumption[t];
	else
		Pot2 = 0;
	instant_train_energy_consumption[t] = instant_train_energy_consumption[t - 1] + ((Pot1 + Pot2) / 2000000) * timestep;
	return instant_train_energy_consumption[t];
}

void Train::checkEntrance(int i, Section* BS) {
	// Where a signalling level applies, one train enters a section per step: a train that entered earlier in
	// this step has put the section in BlocksOccupied, which the aspect only shows from the next step. The list
	// is cleared at the end of every step, so here it holds only entries of this step. Waiting trains are
	// visited in the order in which they are due, so the one due first enters first.
	const bool waiting = !CanEnter;
	const bool separated = BS[0].SignallingLevel >= 0;
	if (waiting && separated && std::find(BlocksOccupied.begin(), BlocksOccupied.end(), BS[0].ID) != BlocksOccupied.end())
		return;
	if (waiting && isFirstSectionFailed(BS[0])) // enters when the failure has ended, at any signalling level
		return;
	if (waiting && separated && i >= departure_time && entryConflictsWithCrossover(i, BS[0]))
		return;
	string PreviousTrain = "None"; // the previous Train according to the list
	int IndexOL = BS[0].arcs_in_signalling_block_section[0].startNode.indexOrderList;

	if ((IndexOL >= 0) && (IndexOL < N_OrderLists) && (IndexOL < 20) && (OL[IndexOL].numTeList > 0)) {
		EG_LOG(SimStep, Info) << "this is the indexOL TRUE \n"
							  << IndexOL;
		for (int j = 1; j < OL[IndexOL].numTeList; j++) { // Determining which is the previous train in the list
			if (trainDescription == OL[IndexOL].TE[j].trainDescription)
				PreviousTrain = OL[IndexOL].TE[j - 1].trainDescription;
		}
		// if the Previous Train has already departed or if this train is the first of the list to depart
		if ((PreviousTrain == OL[IndexOL].LastEnteredTrain) || (PreviousTrain == "None") || (trainDescription == OL[IndexOL].TE[0].trainDescription)) {
			// if the time is equal or higher than the departure time and the signals are permissive set CanEnter true
			if ((BS[0].SignallingLevel == 3) || (BS[0].SignallingLevel == 4)) {
				bool IsRouteStartingPointOccupied = false; // this bool is true if start_node of signalling_block_sections[0] is present in the list of Movement Authorities ETCS_MA, i.e. if the beginning of the train route is occupied by another train
				if (ETCS_MA.size() > 0) {
					for (list<MovementAuthority>::iterator it = ETCS_MA.begin(); it != ETCS_MA.end(); it++) {
						// if the list of Movement Autority includes a Movement Autority related to the beginning Node of the Route which belongs to a train different from this, then break the for loop over the list of ETCS_MA and set the boolean IsRouteStartingPointOccupied to true
						if ((it->BSID == BS[0].ID) && (abs(it->AbsPosEoA - BS[0].GeoXBegNode) < 0.001) && (it->TrainInfo.trainDescription != this->trainDescription) && obeysAuthority(*it)) {
							IsRouteStartingPointOccupied = true;
							break;
						}
					}
				}
				if ((i >= departure_time) && (IsRouteStartingPointOccupied == 0)) {
					counter2++;
					if (counter2 == 1)
						CanEnter = true;
				}
			} else {
				if ((i >= departure_time) && ((BS[0].code == 270) || (BS[0].code == 180))) {
					counter2++;
					if (counter2 == 1)
						CanEnter = true;
				}
			}
		}
	}

	else { // In case there is no ordered sequence of trains to be respected by trains at the beginning of the route

		if ((BS[0].SignallingLevel == 3) || (BS[0].SignallingLevel == 4)) { // if the signalling system of the firt block section is ETCS Level 3 or Level 4 (Virtual Coupling)
			bool IsRouteStartingPointOccupied = false;						// this bool is true if start_node of signalling_block_sections[0] is present in the list of Movement Authorities ETCS_MA, i.e. if the beginning of the train route is occupied by another train
			if (ETCS_MA.size() > 0) {
				for (list<MovementAuthority>::iterator it = ETCS_MA.begin(); it != ETCS_MA.end(); it++) {
					// if the list of Movement Autority includes a Movement Autority related to the beginning Node of the Route which belongs to a train different from this, then break the for loop over the list of ETCS_MA and set the boolean IsRouteStartingPointOccupied to true
					if ((it->BSID == BS[0].ID) && (abs(it->AbsPosEoA - BS[0].GeoXBegNode) < 0.001) && (it->TrainInfo.trainDescription != this->trainDescription) && obeysAuthority(*it)) {
						IsRouteStartingPointOccupied = true;
						break;
					}
				}
			}
			if ((i >= departure_time) && (IsRouteStartingPointOccupied == 0)) {
				counter2++;
				if (counter2 == 1)
					CanEnter = true;
			}

		} else { // if instead the signalling system of the first block section is a fixed-block signalling system
			if ((i >= departure_time) && ((BS[0].code == 270) || (BS[0].code == 180))) {
				counter2++;
				if (counter2 == 1)
					CanEnter = true;
			}
		}
	}
	if (waiting && separated && CanEnter) // the entering train occupies the first section at once
		occupyBlockAndConnected(BS[0], BS[0], Start_Node_X * 1000, Start_Node_X * 1000);
}

void Train::checkIfMovementAuthorityStillValid(double speed, double head, const Section& section) {
	if (!IsTrainStoppedForEoA)
		return;
	const MovementAuthority& saved = Last_MA_StoppedAt;
	const double distance = saved.RelativePosEoA - head;
	const bool applicable = obeysAuthority(saved) && (saved.type != "TrainEnd" || (saved.typePart == "Tale" && saved.ReversedDirection == train_route[indexOfRoute].reversed_direction) || (saved.typePart == "Front" && saved.ReversedDirection != train_route[indexOfRoute].reversed_direction));
	bool valid = false;
	if (speed == 0 && distance >= 0 && distance < 4 && section.ID == saved.BSID && applicable)
		for (const MovementAuthority& authority : ETCS_MA)
			if (authority.BSID == saved.BSID && authority.type == saved.type && authority.typePart == saved.typePart
				&& authority.ReversedDirection == saved.ReversedDirection && obeysAuthority(authority)
				&& authority.TrainInfo.trainDescription == saved.TrainInfo.trainDescription
				&& abs(authority.AbsPosEoA - saved.AbsPosEoA) < 0.001) {
				valid = true;
				break;
			}
	IsTrainStoppedForEoA = valid;
	if (!valid)
		BrakingForEoA = false;
}

void Train::leaveFollowingModeWhenLeaderIsGone(int time_seconds) {
	if (IsTrainInFollowingMode == 0)
		return;
	for (const MovementAuthority& authority : ETCS_MA)
		if (authority.TrainInfo.trainDescription == LeadingTrainInFollowingMode)
			return;
	EG_LOG(SimEvents, Info) << "At time instant " << time_seconds << " train " << trainDescription << " is decoupled from train " << LeadingTrainInFollowingMode << ", which reports no position any more\n";
	VCmsgTimestep.push_back(time_seconds);
	VCmsgTrain.push_back(trainDescription);
	VCmsgText.push_back("Train is now decoupled");
	MovementAuthority ResetPredictedMAToDecoupleAt;
	IsTrainInFollowingMode = false;
	LeadingTrainInFollowingMode = "None";
	IsTrainDecoupling = false;
	Predicted_MA_To_DecoupleAt = ResetPredictedMAToDecoupleAt;
	IsInUnintentionalDecoupling = false;
	CounterFollowingMode = 0;
}

void Train::PrintTrajectory() {
	string path;
	path = path + initial_variables.OutputMainFolder + "/TEMP/Traj_Train_" + trainDescription + ".txt";
	ofstream trainout;
	trainout.open((char*)path.c_str(), ios::binary);
	trainout << "Time[s]\tSpeed[m/s]\tPosition[m]\tTail_Position[m]\tPower_Cons[kW]\tBX[m]\tinstant_train_energy_consumption[KWh]\tBlock" << "\n";
	const auto segments = validTrajectorySegments(instant_spatial_position, earliestActiveTrajectoryIndex, End_Time);
	bool firstSegment = true;
	if (train_route[indexOfRoute].reversed_direction == 0) {
		for (const auto& segment : segments) {
			if (!firstSegment)
				trainout << "\n";
			firstSegment = false;
			for (int i = segment.first; i <= segment.last; i++) {
				trainout << i * timestep << "\t" << instant_train_speed[i] << "\t" << instant_spatial_position[i] << "\t" << instant_spatial_position[i] - train_length << "\t" << instant_train_power_consumption[i] / 1000 << "\t" <<

					BX[i] << "\t" << instant_train_energy_consumption[i] * 0.27778 << "\t" << instant_block_section_occupied[i] <<

					// Bs.
					"\n";
			}
		}
	} else { // If instead the train runs on a reversed route then subtract the instant_spatial_position[i] from the Total Length of the route
		// Written positions fall on a reversed route, so the tail is the position plus the train length.
		for (const auto& segment : segments) {
			if (!firstSegment)
				trainout << "\n";
			firstSegment = false;
			for (int i = segment.first; i <= segment.last; i++) {
				trainout << i * timestep << "\t" << instant_train_speed[i] << "\t" << train_route[indexOfRoute].OriginalRefReversedRoute - instant_spatial_position[i] << "\t" << train_route[indexOfRoute].OriginalRefReversedRoute - instant_spatial_position[i] + train_length << "\t" << instant_train_power_consumption[i] / 1000 << "\t" << BX[i] << "\t" << instant_train_energy_consumption[i] * 0.27778 << "\t" << instant_block_section_occupied[i] << "\n";
			}
		}
	}
	trainout.close();
}

string Train::DetermineOccupiedTDS(int timeinstant, bool IsRouteReversed) {
	string Occupied_TDS_ID = "";
	double BeginningNode = -1, FinalNode = -1; // This are the Beginning and End Node of the TDS
	// if the route of the train is not reversed
	if (IsRouteReversed == 0) {
		if (Bs.TDS_in_block.size() > 0) {
			for (auto i = Bs.TDS_in_block.begin(); i != Bs.TDS_in_block.end(); i++) {
				// if the TDS is straight and not on a diverging switch
				if ((*i)->node_on_switch.X == -1) {
					BeginningNode = (*i)->start_node.X * 1000;
					FinalNode = (*i)->end_node.X * 1000;
				}
				// if instead the TDS is on a diverging switch
				else {
					// Determine Beginning and final nodes
					if ((*i)->node_on_switch.X >= (*i)->connection_node.X) {
						BeginningNode = (*i)->start_node.X * 1000;
						FinalNode = (*i)->node_on_switch.X * 1000;
					}
					// Otherwise it is valid the following condition
					else {
						BeginningNode = (*i)->node_on_switch.X * 1000;
						FinalNode = (*i)->end_node.X * 1000;
					}
				}

				if ((instant_spatial_position[timeinstant] >= BeginningNode) && (instant_spatial_position[timeinstant] < FinalNode)) {
					Occupied_TDS_ID = (*i)->ID;
					break; // break the for loop over the TDS in the block section
				}
			}
		}
	}
	// if instead the route of the train is reversed
	else {
		// the the TDS nodes should be reversed as well
		if (Bs.TDS_in_block.size() > 0) {
			for (auto i = Bs.TDS_in_block.begin(); i != Bs.TDS_in_block.end(); i++) {
				// if the TDS is straight and not on a diverging switch
				if ((*i)->node_on_switch.X == -1) {
					// Beginning Node is the absolute distance between the end Node of the TDS ( as it is reversed) and the absolute position of the Beginning Node of the reversed block + the relative start Node position of the reversed block
					BeginningNode = (Bs.GeoXBegNode - (*i)->end_node.tdsbGeoCoordX) + Bs.start_node.X * 1000;
					FinalNode = (Bs.GeoXBegNode - (*i)->start_node.tdsbGeoCoordX) + Bs.start_node.X * 1000;
				}

				// if instead the TDS is on a diverging switch
				else {
					// Determine Beginning and final nodes
					if ((*i)->node_on_switch.X >= (*i)->connection_node.X) {
						BeginningNode = (Bs.GeoXBegNode - (*i)->node_on_switch.tdsbGeoCoordX) + Bs.start_node.X * 1000;
						FinalNode = (Bs.GeoXBegNode - (*i)->start_node.tdsbGeoCoordX) + Bs.start_node.X * 1000;
					}
					// Otherwise it is valid the following condition
					else {
						BeginningNode = (Bs.GeoXBegNode - (*i)->end_node.tdsbGeoCoordX) + Bs.start_node.X * 1000;
						FinalNode = (Bs.GeoXBegNode - (*i)->node_on_switch.tdsbGeoCoordX) + Bs.start_node.X * 1000;
					}
				}

				if ((instant_spatial_position[timeinstant] >= BeginningNode) && (instant_spatial_position[timeinstant] < FinalNode)) {
					Occupied_TDS_ID = (*i)->ID;
					break; // break the for loop over the TDS in the block section
				}
			}
		}
	}
	// finally return the ID of teh occupied TDS
	return Occupied_TDS_ID;
}

void Train::recordDirectIncidentId(const std::string& incidentId, double timeSeconds,
	double locationMeters) {
	if (incidentId.empty())
		return;
	if (std::find(directIncidentIds.begin(), directIncidentIds.end(), incidentId)
		== directIncidentIds.end())
		directIncidentIds.push_back(incidentId);
	if (firstDirectIncidentTime < 0.0) {
		firstDirectIncidentTime = timeSeconds;
		firstDirectIncidentLocation = locationMeters;
	}
}

void Train::recordDirectIncident(const SimulationIncident& incident, double timeSeconds,
	double locationMeters) {
	recordDirectIncidentId(incident.id.empty() ? incident.target : incident.id,
		timeSeconds, locationMeters);
}

void Train::recordDirectSignalFailure(const MovementAuthority& authority, double timeSeconds) {
	if (authority.type != "SignalFailure")
		return;
	const std::string prefix = "signal_failure:";
	const std::string& encoded = authority.TrainInfo.trainDescription;
	if (encoded.rfind(prefix, 0) != 0)
		return;
	const std::string incidentId = encoded.substr(prefix.size());
	recordDirectIncidentId(incidentId, timeSeconds, authority.AbsPosEoA);
}

double Train::effectiveIncidentSpeedLimit(double candidate, int timestepIndex) {
	const SimulationIncident* incident = Active_Train_Breakdown(trainDescription, timestepIndex);
	const double cap = incident ? incident->reducedSpeedKmh / 3.6 : 0.0;
	if (incident && incident->hasReducedSpeed && cap > 0.0 && std::isfinite(cap)
		&& cap < candidate) {
		recordDirectIncident(*incident, timestepIndex * timestep,
			instant_spatial_position.empty() ? 0.0 : instant_spatial_position[std::max(0, timestepIndex - 1)]);
		return cap;
	}
	return candidate;
}

void Train::trajectoryComputationIncludingMovingBlock(int time_seconds, double signalCode1, double signalCode2, double signalCode3) {
	if (OutOfSimulation == 0) {
		if (time_seconds == 0) {
			CanEnter = false;
			instant_train_speed[0] = 0.0;
			instant_spatial_position[0] = Start_Node_X * 1000;
			instant_block_section_occupied[0] = "-";
			instant_train_power_consumption[0] = 0.0;
			instant_train_tractive_effort[0] = 0.0;
			instant_train_energy_consumption[0] = 0.0;
			return;
		}
		// A broken-down train holds its last state for the whole incident
		// window; entrance processing is also suspended, so a train whose
		// window covers its entry time simply enters after the window.
		if (time_seconds > 0 && Incident_Holds_Train(trainDescription, time_seconds)) {
			if (const SimulationIncident* incident = Active_Train_Breakdown(trainDescription, time_seconds))
				recordDirectIncident(*incident, time_seconds * timestep, instant_spatial_position[time_seconds - 1]);
			instant_train_speed[time_seconds] = 0;
			instant_spatial_position[time_seconds] = instant_spatial_position[time_seconds - 1];
			instant_train_power_consumption[time_seconds] = 0;
			instant_train_tractive_effort[time_seconds] = 0;
			train_energy_consumption(time_seconds);
			Eq[time_seconds] = 5;
			return;
		}

		// Determine if the Train can enter the route
		checkEntrance(time_seconds, train_route[indexOfRoute].sequence_of_block_sections.data());
		if ((time_seconds >= departure_time) && (CanEnter == 1)) {
			double V_lim;
			int U;
			U = time_seconds;
			int Previous_Block_Index = 0;
			Arc As; // use local variable instead of Train class member (faster simulation)
			string Occupied_TDS;

			// Determination of the right Block Section
			for (int h = 0; h < train_route[indexOfRoute].N_Block_Sections; h++) {
				if ((instant_spatial_position[time_seconds - 1] < train_route[indexOfRoute].sequence_of_block_sections[h].end_node.X * 1000)
					&& (instant_spatial_position[time_seconds - 1] >= train_route[indexOfRoute].sequence_of_block_sections[h].start_node.X * 1000)) {
					Bs = train_route[indexOfRoute].sequence_of_block_sections[h];
					instant_block_section_occupied[time_seconds] = Bs.ID;

					if (h == 0)
						Previous_Block_Index = h;
					else {
						Previous_Block_Index = h - 1;
						break;
					}
				}
			}

			// Determine the TDS which is currently occupied in the Block section Bs by the nose of the train
			Occupied_TDS = DetermineOccupiedTDS((time_seconds - 1), train_route[indexOfRoute].reversed_direction);

			// Determination of the right running Arc
			for (int j = 0; j < Bs.total_arcs; j++) {
				if ((instant_spatial_position[time_seconds - 1] < Bs.arcs_in_signalling_block_section[j].endNode.X * 1000)
					&& (instant_spatial_position[time_seconds - 1] >= Bs.arcs_in_signalling_block_section[j].startNode.X * 1000))
					As = Bs.arcs_in_signalling_block_section[j];
				// logger.Log("\nkkkk");
				////	logger.Log(to_string(instant_spatial_position[time_seconds - 1]));
			}
			// Determination of the right speed limit to observe
			double Speeds[3];
			Speeds[0] = max_train_speed;
			Speeds[1] = As.speedLimit;
			Speeds[2] = As.signalSpeedLimit;
			V_lim = Speeds[0];
			for (int k = 0; k < 3; k++) {
				if (Speeds[k] <= V_lim)
					V_lim = Speeds[k];
			}
			V_lim = effectiveIncidentSpeedLimit(V_lim, time_seconds);

			// Imposing Station Nodes via cached indices
			for (int s = 0; s < numStations; s++) {
				int h = stationBlockSection[s];
				int k = stationArc[s];
				if (h >= 0 && k >= 0) {
					auto& Arc = train_route[indexOfRoute].sequence_of_block_sections[h].arcs_in_signalling_block_section[k];
					if (Arc.endNode.X * 1000 >= instant_spatial_position[time_seconds - 1]) {
						Arc.endNode.station = 1;
						Arc.speedInBraking = 0;
					}
				}
			}

			// Determination of the Movement Authority that has been previouly respected by the train (especially if it stopped at a given EoA)
			// determining Last_MA_StoppedAt for the train
			if ((instant_train_speed[time_seconds - 1] == 0)
				&& (this->BrakingForEoA == 1)
				&& (IsTrainStoppedForEoA == 0)
				&& (this->Last_Received_MA.RelativePosEoA - instant_spatial_position[time_seconds - 1] >= 0)
				&& (this->Last_Received_MA.RelativePosEoA - instant_spatial_position[time_seconds - 1] < 4)) {

				Last_MA_StoppedAt = Last_Received_MA;
				IsTrainStoppedForEoA = true;
				recordDirectSignalFailure(Last_MA_StoppedAt, time_seconds * timestep);
			}

			checkIfMovementAuthorityStillValid(instant_train_speed[time_seconds - 1], instant_spatial_position[time_seconds - 1], Bs);

			leaveFollowingModeWhenLeaderIsGone(time_seconds);

			const double head = instant_spatial_position[time_seconds - 1];
			const double speed = instant_train_speed[time_seconds - 1];
			// Keep a reached speed target until its boundary, but only while it is still present.
			if (Vob[time_seconds - 1] > 0 && speed <= Vob[time_seconds - 1] && head < Xob[time_seconds - 1])
				for (const Section& section : train_route[indexOfRoute].sequence_of_block_sections)
					for (int arc = 0; arc < section.total_arcs; ++arc)
						if (section.arcs_in_signalling_block_section[arc].endNode.X * 1000 == Xob[time_seconds - 1]
							&& section.arcs_in_signalling_block_section[arc].speedInBraking == Vob[time_seconds - 1])
							V_lim = std::min(V_lim, Vob[time_seconds - 1]);
			const bool accelerating = speed < V_lim;
			double candidateSpeed = speed, candidateHead = head + speed * timestep;
			if (accelerating) {
				// This part of the code below instead has been updated to consider the train crusing (and not accelerating like before) is the Traction Surplus with respect to the motion resistances is null. In other words the train is now considered crusing when traction and resistance forces are equal.
				double Traction_Surplus = 0;
				Traction_Surplus = tractiveEffort(instant_train_speed[time_seconds - 1]) - total_train_resistances(instant_train_speed[time_seconds - 1], As.gradient, As.curvature);

				// Compute instant speed
				candidateSpeed = instant_train_speed[time_seconds - 1] + (Traction_Surplus) / (total_train_mass * massFactor) * timestep;

				if (candidateSpeed < 0)
					candidateSpeed = 0;

				if (candidateSpeed > V_lim)
					candidateSpeed = V_lim;

				double Scruis = 0;
				Scruis = instant_spatial_position[time_seconds - 1] + candidateSpeed * timestep;

				// Compute spatial position of the train at current time based on Traction Surplus sign (i.e. whether it is 0, negative or positive)
				if (Traction_Surplus == 0) {
					candidateHead = Scruis;
				} else {
					candidateHead = instant_spatial_position[time_seconds - 1]
						+ ((total_train_mass * massFactor) / (Traction_Surplus)) * instant_train_speed[time_seconds - 1]
							* (candidateSpeed - instant_train_speed[time_seconds - 1]);

					if (candidateHead < Scruis) {
						candidateHead = instant_spatial_position[time_seconds - 1] + instant_train_speed[time_seconds - 1] * timestep + 0.5 * ((candidateSpeed - instant_train_speed[time_seconds - 1]) / timestep) * pow(timestep, 2);
					}
				}
			}
			bool continueCached;
			const double Braking_Distance = europeanVitalComputerWithListsImproved(head, std::max(speed, candidateSpeed),
				time_seconds - 1, train_route[indexOfRoute].sequence_of_block_sections.data(), train_route[indexOfRoute].N_Block_Sections, continueCached);
			const bool mustBrake = continueCached || speed > V_lim || !isShortOfBrakingPoint(head, Braking_Distance)
				|| (candidateSpeed > Vobmin && candidateHead >= Braking_Distance);
			// Check only non-braking candidates; an active plan keeps its own force-bounded step.
			const bool infeasibleCandidate = !mustBrake && speed > 0 && candidateSpeed < brakingCurveSpeed(speed, As);
			// Acceleration phase
			if (!mustBrake && !infeasibleCandidate && accelerating) {
				instant_train_speed[time_seconds] = candidateSpeed;
				instant_spatial_position[time_seconds] = candidateHead;

				Eq[time_seconds] = 1;
				instant_train_tractive_effort[time_seconds] = tractiveEffort(instant_train_speed[time_seconds - 1]);
				instant_train_power_consumption[time_seconds] = instant_train_tractive_effort[time_seconds] * instant_train_speed[time_seconds - 1];
				train_energy_consumption(time_seconds);
				temp = time_seconds;
				stop = time_seconds;
				BX[time_seconds] = Braking_Distance;
				Xob[time_seconds] = Xobmin;
				Vob[time_seconds] = Vobmin;
				counter = 0;
				GradientExceptionInBraking = false;
			}

			/*//Cruising Phase
			else if((instant_spatial_position[i-1]<Braking_Distance)&&(fabs((V_lim-instant_train_speed[i-1]))<2.78*timestep)){
			instant_train_speed[i]=V_lim;    if (instant_train_speed[i]<0)instant_train_speed[i]=0;
			instant_spatial_position[i]=instant_spatial_position[i-1]+instant_train_speed[i-1]*timestep;
			instant_train_power_consumption[i]=total_train_resistances(instant_train_speed[i-1],As.gradient,As.curvature)*instant_train_speed[i-1];    Eq[i]=2;
			temp=i; stop=i; BX[i]=Braking_Distance;Xob[i]=Xobmin; Vob[i]=Vobmin; counter=0;}*/

			// Braking Phase
			else if ((mustBrake || infeasibleCandidate)
				&& (instant_spatial_position[time_seconds - 1] < train_route[indexOfRoute].x_of_end_node * 1000)
				&& (speed > 0 || (speed == 0 && mustBrake))) {

				if (speed == 0 || infeasibleCandidate || (speed > V_lim && isShortOfBrakingPoint(head, Braking_Distance))) {
					fullBrakingStep(time_seconds, As);
					counter = 0;
				} else
					brakingStep(time_seconds, As, train_route[indexOfRoute].sequence_of_block_sections.data(), train_route[indexOfRoute].N_Block_Sections);

				// if two braking curves are both restrictive
				/*if ((Xobj!=Xobmin)||(Vobj!=Vobmin)||((Xobj!=Xobmin)&&(Vobj!=Vobmin))){counter=1;
				Xobj=Xobmin;   Vobj=Vobmin;   Eq[i]=53;
				DrawBrakingCurve(instant_train_speed[i-1],Vobj,Xobj,signalling_block_sections,Blocks);
				for (int t=0;t<BrakStep;t++){
				if((Vbrak[t]<instant_train_speed[i-1])&&(Sbrak[t]>instant_spatial_position[i-1])){ brakingPoint=t; break;}}
				instant_train_speed[i]=Vbrak[brakingPoint+counter-1];    if (instant_train_speed[i]<0)instant_train_speed[i]=0;
				instant_spatial_position[i]=Sbrak[brakingPoint+counter-1];}*/

				if (Eq[time_seconds] == 54) {
					instant_train_tractive_effort[time_seconds] =
						-(brakingEffort((time_seconds - temp - 1) * timestep)
							+ gradient_resistances(As.gradient)
							+ curvature_resistances(As.curvature)
							- total_train_resistances(instant_train_speed[time_seconds - 1], As.gradient, As.curvature));
				} else {
					instant_train_tractive_effort[time_seconds] = total_train_mass * massFactor * (instant_train_speed[time_seconds] - speed) / timestep
						+ total_train_resistances(speed, As.gradient, As.curvature);
				}
				instant_train_power_consumption[time_seconds] =
					instant_train_tractive_effort[time_seconds] * instant_train_speed[time_seconds - 1]; /*Eq[i]=3;*/
				train_energy_consumption(time_seconds);
				// A fallback from rest must not restart an active station's dwell clock.
				if (speed > 0)
					stop = time_seconds;
				BX[time_seconds] = Braking_Distance;
				Xob[time_seconds] = Xobmin;
				Vob[time_seconds] = Vobmin;
			}

			/*//Brake if the train enters next Arc with a speed higher than the Arc speed limit
			else if ((instant_train_speed[i-1]>V_lim)){
			instant_train_speed[i]=instant_train_speed[i-1]+(-brakingEffort((i-temp-1)*timestep)-(gradient_resistances(As.gradient)+curvature_resistances(As.curvature)))/(total_train_mass*massFactor)*timestep;     if (instant_train_speed[i]<0)instant_train_speed[i]=0;
			instant_spatial_position[i]=instant_spatial_position[i-1]+((total_train_mass*massFactor)/(-brakingEffort((i-temp-1)*timestep)-(gradient_resistances(As.gradient)+curvature_resistances(As.curvature))))*instant_train_speed[i-1]*(instant_train_speed[i]-instant_train_speed[i-1]);
			instant_train_power_consumption[i]=-(brakingEffort((i-temp-1)*timestep)+gradient_resistances(As.gradient)+curvature_resistances(As.curvature)-total_train_resistances(instant_train_speed[i-1],As.gradient,As.curvature))*instant_train_speed[i-1];   Eq[i]=4;
			BX[i]=Braking_Distance;Xob[i]=Xobmin; Vob[i]=Vobmin; stop=i; counter=0;}*/

			// Cruising Phase
			else if (!mustBrake && !accelerating && speed > 0) {
				instant_train_speed[time_seconds] = candidateSpeed;
				instant_spatial_position[time_seconds] = candidateHead;
				const double cruisingEffort = total_train_resistances(speed, As.gradient, As.curvature);
				instant_train_tractive_effort[time_seconds] = cruisingEffort;
				instant_train_power_consumption[time_seconds] = cruisingEffort * speed;
				Eq[time_seconds] = 12;
				train_energy_consumption(time_seconds);
				temp = time_seconds;
				stop = time_seconds;
				BX[time_seconds] = Braking_Distance;
				Xob[time_seconds] = Xobmin;
				Vob[time_seconds] = Vobmin;
				counter = 0;
				GradientExceptionInBraking = false;
			}

			double leaderHead = 0;
			const MovementAuthority* liveLeader = IsTrainCoupling || IsTrainInFollowingMode
				? currentFollowingAuthority(head, leaderHead)
				: nullptr;
			// Determination of Train Coupling phase for Virtual Coupling
			if ((IsTrainCoupling == 1) && liveLeader
				&& liveLeader->TrainInfo.trainDescription == Predicted_MA_To_CoupleAt.TrainInfo.trainDescription
				&& ((IsTrainInFollowingMode == 0) || (IsInUnintentionalDecoupling == 1))
				&& (abs(instant_train_speed[time_seconds] - liveLeader->TrainInfo.TrainSpeed) < 0.278)
				&& (abs(this->Predicted_MA_To_CoupleAt.RelativePosEoA - instant_spatial_position[time_seconds - 1]) <= 30)) {
				// When the condition above occurs then the train is transitioning towards the following mode
				IsInUnintentionalDecoupling = false;
				IsTrainCoupling = false;														   // So we set the ending of the Coupling transition period
				IsTrainInFollowingMode = true;													   // And the activation of the following mode
				LeadingTrainInFollowingMode = Predicted_MA_To_CoupleAt.TrainInfo.trainDescription; // Setting the trainDescription of the train which is leading during Virtual Coupling
																								   // Sending a message that train is in following mode
				EG_LOG(SimEvents, Info) << "At time instant " << time_seconds << " train " << trainDescription << " is virtually coupled to train " << LeadingTrainInFollowingMode << "\n";

				//-------------------------------
				// GUI notification
				VCmsgTimestep.push_back(time_seconds);
				VCmsgTrain.push_back(trainDescription);
				VCmsgText.push_back("Train is now virtually\ncoupled to the leader");
				//-------------------------------
			}

			// Determination of Train Uncoupling from the leader when it is in following mode and the route diverges from the leader's
			if ((IsTrainInFollowingMode == 1) && (IsTrainDecoupling == 1)) {
				// When the train is decoupling it can be considered totally uncoupled and not anymore in following mode when the tale of the leading train has fully crossed the splitting infrastructure element and the corresponding MA is communicated with type="TrainUncoupled"
				if ((Predicted_MA_To_DecoupleAt.TrainInfo.trainDescription != "None")
					&& (Predicted_MA_To_DecoupleAt.TrainInfo.trainDescription == LeadingTrainInFollowingMode)
					&& (Predicted_MA_To_DecoupleAt.type == "TrainUncoupled")) {

					EG_LOG(SimEvents, Info) << "At time instant " << time_seconds << " train " << trainDescription << " intentionally decoupled from train " << LeadingTrainInFollowingMode << " because of diverging routes\n";

					//-------------------------------
					// GUI notification
					VCmsgTimestep.push_back(time_seconds);
					VCmsgTrain.push_back(trainDescription);
					VCmsgText.push_back("Train is now decoupled");
					//-------------------------------

					MovementAuthority ResetPredictedMAToDecoupleAt;
					IsTrainInFollowingMode = false;
					LeadingTrainInFollowingMode = "None";
					IsTrainDecoupling = false;
					Predicted_MA_To_DecoupleAt = ResetPredictedMAToDecoupleAt;
					IsInUnintentionalDecoupling = false;
				}
			}

			/*******************  Virtual Coupling Following mode equations  ***************************************************************************************************************************/

			if ((IsTrainInFollowingMode == 1) && (IsTrainDecoupling == 0) && (IsInUnintentionalDecoupling == 0)) {
				CounterFollowingMode++; // This sets the number of time steps that the train is in following mode
				// if the train has outdistanced for more than 30 meters from the EoA of the train ahead, then interrupt the following mode and let the train to go free
				if (liveLeader && liveLeader->TrainInfo.trainDescription == LeadingTrainInFollowingMode
					&& leaderHead - head > 30) {
					IsInUnintentionalDecoupling = true; // Originally I had IsTrainInFollowingMode = false;
					CounterFollowingMode = 0;			// set also the CounterFollowingMode to 0 again

					EG_LOG(SimEvents, Info) << "At time instant " << time_seconds << " train " << trainDescription << " is unintentionally decoupling from train " << LeadingTrainInFollowingMode << " because it could not reach the same speeds\n";

					//-------------------------------
					// GUI notification
					VCmsgTimestep.push_back(time_seconds);
					VCmsgTrain.push_back(trainDescription);
					VCmsgText.push_back("Train is now unintentionally\ndecoupling from the leader");
					//-------------------------------
				}

				// The first coupling step keeps the normal result. Later steps are proposals, not overrides.
				const bool stationaryHold = speed == 0 && (V_lim == 0 || (As.endNode.X * 1000 - head < 4 && (As.endNode.station || As.endNode.respectOrder || !strcmp(Bs.state, "red"))));
				if (CounterFollowingMode > 1 && liveLeader && liveLeader->TrainInfo.trainDescription == LeadingTrainInFollowingMode
					&& !mustBrake && !infeasibleCandidate && !IsTrainStoppedForEoA && !StoppedForServiceStop && !stationaryHold
					&& std::isfinite(liveLeader->TrainInfo.TrainSpeed) && std::isfinite(liveLeader->TrainInfo.Acceleration)) {
					const double mass = total_train_mass * massFactor;
					const double resistance = total_train_resistances(speed, As.gradient, As.curvature);
					const double minimum = brakingCurveSpeed(speed, As);
					const double maximum = std::max(0.0, speed + (tractiveEffort(speed) - resistance) / mass * timestep);
					const double acceleration = std::max((minimum - speed) / timestep,
						std::min((maximum - speed) / timestep, liveLeader->TrainInfo.Acceleration));
					double proposedSpeed = (acceleration < 0 ? liveLeader->TrainInfo.TrainSpeed : speed) + acceleration * timestep;
					if (acceleration == 0 && liveLeader->TrainInfo.TrainSpeed == 0)
						proposedSpeed = 0;
					// Bound the final leader-relative intent by this train's own actual-start force interval.
					proposedSpeed = std::min(V_lim, std::max(minimum, std::min(maximum, std::max(0.0, proposedSpeed))));
					const double proposedHead = head + speed * timestep;
					if (std::isfinite(proposedHead) && std::isfinite(proposedSpeed) && proposedHead >= head && proposedSpeed >= 0
						&& proposedSpeed >= minimum && proposedSpeed <= maximum && proposedSpeed <= V_lim
						&& proposedHead <= instant_spatial_position[time_seconds] && proposedSpeed <= instant_train_speed[time_seconds]
						&& isShortOfBrakingPoint(proposedHead, Braking_Distance)) {
						instant_spatial_position[time_seconds] = proposedHead;
						instant_train_speed[time_seconds] = proposedSpeed;
						instant_train_tractive_effort[time_seconds] = mass * (proposedSpeed - speed) / timestep + resistance;
						instant_train_power_consumption[time_seconds] = instant_train_tractive_effort[time_seconds] * speed;
						train_energy_consumption(time_seconds);
						temp = time_seconds;
						stop = time_seconds;
						counter = 0;
						Eq[time_seconds] = 1002;
					}
				}
			}

			/**************************************************************************************************************************/

			// Release of signalling system when the train exits simulation
			if (instant_spatial_position[time_seconds - 1] >= train_route[indexOfRoute].x_of_end_node * 1000) {
				End_Time = time_seconds - 1;
				if (destinationTerminationRequested)
					destinationTerminated = true;
				instant_train_speed[time_seconds] = 0;
				instant_spatial_position[time_seconds] = -9999;
				instant_train_power_consumption[time_seconds] = -9999;
				instant_train_tractive_effort[time_seconds] = -9999;
				Xobmin = train_route[indexOfRoute].x_of_end_node * 1000;
				Vobmin = train_route[indexOfRoute].sequence_of_block_sections[train_route[indexOfRoute].N_Block_Sections - 1].arcs_in_signalling_block_section[train_route[indexOfRoute].sequence_of_block_sections[train_route[indexOfRoute].N_Block_Sections - 1].total_arcs - 1].speedInBraking; /*This is the speedInBraking of the last Arc of the Route*/
				relLastSectionMixedSignalling(train_route[indexOfRoute].sequence_of_block_sections[train_route[indexOfRoute].N_Block_Sections - 1].ID);
				OutOfSimulation = true;
			}
			/*else if (instant_spatial_position[i-1]<0){instant_train_speed[i]=0; instant_spatial_position[i]=-9999; instant_train_power_consumption[i]=-9999; }*/

			/**************************************************************************************************************************/

			// Standing Train Conditions
			if (instant_train_speed[time_seconds - 1] == 0) {

				// A replacement authority's approved stationary step also constrains node handling.
				const bool authorityHold = IsTrainStoppedForEoA || (mustBrake && BrakingForEoA && instant_train_speed[time_seconds] == 0 && instant_spatial_position[time_seconds] == head);

				// Stopping at a station for an estabilished dwell time
				if (((As.endNode.X * 1000 - instant_spatial_position[time_seconds - 1]) < 4) && (As.endNode.station == 1)) {
					this->StoppedForServiceStop = true; // Stating that the train is stopping at a station to perform a service stop
					Eq[time_seconds] = 5;
					double stoptime = 0;
					double dep_time = 0;
					counter = 0;
					for (int s = 0; s < numStations; s++) {
						if (Stations[s].stationName == As.endNode.stationName) {
							this->CurrentServiceStop = Stations[s].stationName; // Setting the name of the station the train is currently stopping at
							this->CurrentServiceStopPlatform = Stations[s].stationPlatformId;
							recordCurrentServiceStopArrival(time_seconds);

							EG_LOG(SimStep, Info) << "At time " << time_seconds << " Train" << this->trainDescription << " is stopping at " << this->CurrentServiceStop << " at " << this->CurrentServiceStopPlatform << "\n";

							stoptime = Stations[s].StopTime;
							dep_time = ScheduledDepartures[s];
							recordCurrentServiceStopArrival(time_seconds);
							if (Stations[s].StepStopped == 0)
								stop = time_seconds;
							Stations[s].StepStopped++;
							if (Stations[s].StepStopped > Stations[s].StopTime)
								stoptime = 0;
						}
					}


					// Check if in this Station the train has also to respect a given OL Order
					if (As.endNode.respectOrder == 1) {
						bool Condition_To_Stop_Train = Conditions_To_Stop_Train_For_OL_Order(As.endNode.respectOrder, As.endNode.indexOrderList, Bs);
						// The train can leave the station only if the previous train has passed, it has satisfied the min dwell time and the signal aspect is non restrictive and there is not condition to stop the train becasue of an ETCS 3 EoA
						if ((Condition_To_Stop_Train == 0) && ((time_seconds - stop) > stoptime) && (time_seconds > dep_time) && (authorityHold == 0)) {
							instant_train_speed[time_seconds] = 0.0001;
							instant_spatial_position[time_seconds] = As.endNode.X * 1000 + 0.0001;
							this->StoppedForServiceStop = false; // Stating that the train is no longer stoppoed at a service station
							this->CurrentServiceStop = "None";	 // This states that the train is no longer stopping at the service station
						}
						// Otherwise the train has to remain stopped at the station
						else {
							instant_train_speed[time_seconds] = 0;
							instant_spatial_position[time_seconds] = authorityHold ? head : As.endNode.X * 1000 - 0.0001;
						}
					}
					// If the station Node does not have to respect a specific train order, the train has to be stopped at the station for the dwell time and can depart only after that ensuring that it does not have to respect any ETCS 3 EoA at that station
					else {
						// A platform at the end of a block section is also the stopping point of that block's signal: the train waits there while the signal is red.
						const bool signalAtPlatformIsRed = Bs.total_arcs > 0
							&& std::fabs(Bs.arcs_in_signalling_block_section[Bs.total_arcs - 1].endNode.X - As.endNode.X) < 1e-9
							&& strcmp(Bs.state, "red") == 0;
						if (((time_seconds - stop) <= stoptime) || (time_seconds <= dep_time) || (authorityHold == 1) || signalAtPlatformIsRed) {
							instant_train_speed[time_seconds] = 0;
							instant_spatial_position[time_seconds] = authorityHold ? head : As.endNode.X * 1000 - 0.0001;
						} else {
							instant_train_speed[time_seconds] = 0.0001;
							instant_spatial_position[time_seconds] = As.endNode.X * 1000 + 0.0001;
							this->StoppedForServiceStop = false; // Stating that the train is no longer stoppoed at a service station
							this->CurrentServiceStop = "None";	 // This states that the train is no longer stopping at the service station
							this->CurrentServiceStopPlatform = "None";
							BrakingForEoA = false; // setting the variable Braking for EoA to false since the train is starting to move again
						}
					}
					instant_train_power_consumption[time_seconds] = 0;
					instant_train_tractive_effort[time_seconds] = 0;
					train_energy_consumption(time_seconds);
				}

				// Stopping at a checkpoint Node where a certain train order must be respected
				else if ((As.endNode.X * 1000 - instant_spatial_position[time_seconds - 1] < 4) && (As.endNode.respectOrder == 1)) {
					counter = 0;
					bool Cond_To_Stop_Train = Conditions_To_Stop_Train_For_OL_Order(As.endNode.respectOrder, As.endNode.indexOrderList, Bs);


					if ((Cond_To_Stop_Train == 0) && (authorityHold == 0)) { // the checkpoint Node can coincide with an ETCS3 EoA, that is why the train can depart from this Node only after having verified that the checkpoint does not coincide with an ETCS3 EoA or that the EoA has been deleted
						Eq[time_seconds] = 532;
						instant_train_speed[time_seconds] = 0.0001;
						instant_spatial_position[time_seconds] = As.endNode.X * 1000 + 0.0001;
						BrakingForEoA = false; // setting back to false BrakingForEoa since the train starts to move again
					}
					// otherwise it has to be stopped
					else {
						Eq[time_seconds] = 534;
						instant_train_speed[time_seconds] = 0;
						instant_spatial_position[time_seconds] = authorityHold ? head : As.endNode.X * 1000 - 0.0001;
					}

					instant_train_power_consumption[time_seconds] = 0;
					instant_train_tractive_effort[time_seconds] = 0;
					train_energy_consumption(time_seconds);
				}

				// Stopping at a Red Signal
				else if (As.endNode.X * 1000 - instant_spatial_position[time_seconds - 1] < 4) {
					Eq[time_seconds] = 6;

					// also a signal can coincide with an ETCS3 EoA, that is why a train must stop at the signal also if this not red but and ETCS 3 EoA exists
					if ((!strcmp(Bs.state, "red")) || (authorityHold == 1)) {
						instant_spatial_position[time_seconds] = authorityHold ? head : As.endNode.X * 1000 - 0.0001;
						instant_train_speed[time_seconds] = 0;
					} else {
						instant_spatial_position[time_seconds] = As.endNode.X * 1000 + 0.0001;
						instant_train_speed[time_seconds] = 0.0001;
						BrakingForEoA = false; // setting BrakingForEoA to false since the train starts moving again
					}
					instant_train_power_consumption[time_seconds] = 0;
					instant_train_tractive_effort[time_seconds] = 0;
					train_energy_consumption(time_seconds);
					counter = 0;
				}
				// if noone of the previous conditions is verified it means that the train is stopped at an ETCS3 EoA
				else if (this->IsTrainStoppedForEoA == 1) {
					instant_train_speed[time_seconds] = 0;
					instant_spatial_position[time_seconds] = head;
					Eq[time_seconds] = 323;
					instant_train_power_consumption[time_seconds] = 0;
					instant_train_tractive_effort[time_seconds] = 0;
					train_energy_consumption(time_seconds);
					counter = 0;
				}

				// if the speed limit on the Arc is 0: this case happens only when the BACC is implemented
				if (V_lim == 0) {
					counter = 0;
					Eq[time_seconds] = 958;
					instant_train_speed[time_seconds] = 0;
					instant_spatial_position[time_seconds] = instant_spatial_position[time_seconds - 1];
					instant_train_power_consumption[time_seconds] = 0;
					instant_train_tractive_effort[time_seconds] = 0;
					train_energy_consumption(time_seconds);
				}
				GradientExceptionInBraking = false;
			}

			// Update the status of Infrastructure elements on the route of the train
			if (time_seconds > 0) {
				UpdateInfrastructureElementStatus(
					train_route[indexOfRoute].InfrastructureElements, time_seconds,
					instant_spatial_position[time_seconds - 1],
					instant_spatial_position[time_seconds],
					instant_train_speed[time_seconds - 1],
					trainDescription);
			}

			// if the train has just entered the track therefore its position instant_spatial_position[i] is higher than 1 meter and lower than 3 meters VirtualQ[ID-1] must be updated to ID
			Update_LastEnteredTrain_For_All_OL_Improved(time_seconds, train_route[indexOfRoute].sequence_of_block_sections[Previous_Block_Index]);
		}
		/***********************************************************************************************************************/
		else {
			instant_train_speed[time_seconds] = 0.0;
			instant_spatial_position[time_seconds] = Start_Node_X * 1000;
			instant_block_section_occupied[time_seconds] = "-";
			instant_train_power_consumption[time_seconds] = 0.0;
			instant_train_tractive_effort[time_seconds] = 0.0;
		}
		// cout << instant_spatial_position[i] << " " << As.gradient << " " << As.curvature << "\n";
	} else {
		instant_block_section_occupied[time_seconds] = "-";
	}
}

void Train::recordCurrentServiceStopArrival(int time_seconds) {
	if (time_seconds < 0 || CurrentServiceStop == "None")
		return;

	for (int s = 0; s < numStations; s++) {
		if (Stations[s].stationName == CurrentServiceStop && StationArrivals[s] < 0) {
			StationArrivals[s] = time_seconds * timestep;
			StationArrivalNames[s] = CurrentServiceStop;
			break;
		}
	}
}
