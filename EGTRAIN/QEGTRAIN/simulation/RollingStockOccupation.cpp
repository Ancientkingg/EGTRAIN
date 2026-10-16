#include "simulation/RollingStock.h"
#include "scene/SignallingLevel.h"
#include "util/Log.h"

#include <algorithm>
#include <list>
#include <string>

using namespace std;

bool Train::isAspectRestrictive(const Section& BS) const {
	// red-red is the restrictive aspect of level 5 (BACC), red that of the other levels
	return ((BS.SignallingLevel == levelValue(SignallingLevel::Bacc)) && (BS.state == SignalState::RedRed))
		|| ((BS.SignallingLevel != levelValue(SignallingLevel::Bacc)) && (BS.state == SignalState::Red));
}

void Train::Det_Section_Occupied_By_Train(int i, Section* BS, int Blocks) {
	if (OutOfSimulation == 0) {
		if ((i >= departure_time) && (CanEnter == 1)) {
			if (BS == nullptr || Blocks <= 0 || timestep <= 0)
				return;
			const int delayedIndex = i - static_cast<int>(S_delay / timestep);
			if (delayedIndex < 1 || delayedIndex >= static_cast<int>(instant_spatial_position.size()))
				return;

			// collect all signalling_block_sections from head to tail
			int hTail = 0, hHead = 0;
			for (int h = 0; h < Blocks; h++) {
				if (((instant_spatial_position[delayedIndex] - train_length) < BS[h].end_node.X * 1000) && ((instant_spatial_position[delayedIndex] - train_length) >= BS[h].start_node.X * 1000)) {
					hTail = h;
				}
				if ((instant_spatial_position[delayedIndex] < BS[h].end_node.X * 1000) && (instant_spatial_position[delayedIndex] >= BS[h].start_node.X * 1000)) {
					hHead = h;
					break;
				}
			}

			// occupy single tracks (this function limits teh entrance of two trains cnsecutively on the same single track even if they have the same direction which should not be the case
			// the entrance should only be limited to trains on the dingle track if they have opposite direction
			// occupySingleTrack(BS, Blocks, hTail, hHead, i);

			// release double switch when train leaves it (its first half is the section before the one that starts with the virtual signal)
			if (hTail > 1 && BS[hTail - 1].start_node.virtualSignal) {
				releaseDoubleSwitch(BS[hTail - 1], BS[hTail - 2]);
			}
			// The lock of a double switch does not hold the section after it, which this train enters next: when the lock alone puts it in
			// BlocksOccupied, it is taken out again.
			auto lockDoubleSwitch = [](const Section& current, const Section& previous, const Section* ahead) {
				const bool wasOccupied = ahead != nullptr && std::find(BlocksOccupied.begin(), BlocksOccupied.end(), ahead->ID) != BlocksOccupied.end();
				occupyDoubleSwitch(current, previous);
				if (ahead != nullptr && !wasOccupied)
					BlocksOccupied.remove(ahead->ID);
			};
			// check if train is crossing a double switch
			for (int h = hTail; h <= hHead; h++) {
				// 2nd half of double switch occupied
				if (h > 0 && BS[h].start_node.virtualSignal) {
					lockDoubleSwitch(BS[h], BS[h - 1], h + 1 < Blocks ? &BS[h + 1] : nullptr);
				}
				// only 1st half of double switch occupied
				if (BS[h].end_node.virtualSignal && h < (Blocks - 1)) {
					lockDoubleSwitch(BS[h + 1], BS[h], h + 2 < Blocks ? &BS[h + 2] : nullptr);
				}
			}

			// occupy all signalling_block_sections from head to tail
			for (int h = hTail; h <= hHead; h++) {
				int Prev_Block = h - 1;
				if (h == 0)
					Prev_Block = 0;
				occupyBlockAndConnected(BS[h], BS[Prev_Block], (instant_spatial_position[delayedIndex] - train_length), (instant_spatial_position[delayedIndex - 1] - train_length));
				// if the Block Section has a switch in diverging position

				if (BS[h].withSwitchDiv == true) {
					if (isMovingBlockLevel(BS[h].SignallingLevel)) {
						activateBlocksWithSwitchesDiv(BS[h], BS[Prev_Block].trackLineId, (instant_spatial_position[i - (int)(S_delay / timestep)] - train_length));
					} else {
						activateBlocksWithSwitchesDivFixedBlock(BS[h], BS[Prev_Block].trackLineId, (instant_spatial_position[i - (int)(S_delay / timestep)] - train_length));
					}
				}
			}
		}
		// Release signalling system when train exits simulation(this operation must be done without signalling delay because the train dies as soon as it overcomes the last Node of its route)
		if ((i > 0) && (instant_spatial_position[i - 1] >= train_route[indexOfRoute].x_of_end_node * 1000)) {
			releaseLastBlockAndConnected(Bs);
			OutOfSimulation = true;
		}
	}
}

void Train::ReportPositionToRBC(int i, Section* BS, int N_BS, double ETCS3SafetyMargin) {
	if (BS == nullptr || N_BS <= 0 || timestep <= 0)
		return;
	if (OutOfSimulation == 0) {
		if ((i >= departure_time) && (CanEnter == 1)) {
			const int delayedIndex = i - static_cast<int>(S_delay / timestep);
			if (delayedIndex < 0
				|| delayedIndex >= static_cast<int>(instant_spatial_position.size())
				|| delayedIndex >= static_cast<int>(instant_train_speed.size()))
				return;

			double LastTrainAcceleration = 0;
			if (delayedIndex >= 1) {
				LastTrainAcceleration = (instant_train_speed[delayedIndex] - instant_train_speed[delayedIndex - 1]) / timestep;
			}

			for (int h = 0; h < N_BS; h++) {
				if (isMovingBlockLevel(BS[h].SignallingLevel)) {

					// Report the position of the max safe rear end

					// Case if the train-based detection of the rear is more permissive (i.e the position of the rear & the (rear - ETCS3SafetyMargin) are in the same block)
					if (((instant_spatial_position[delayedIndex] - train_length - ETCS3SafetyMargin) < BS[h].end_node.X * 1000) && ((instant_spatial_position[delayedIndex] - train_length - ETCS3SafetyMargin) >= BS[h].start_node.X * 1000) && ((instant_spatial_position[delayedIndex] - train_length) < BS[h].end_node.X * 1000)) {
						// Defining veriables for the ID of the previous, Current and Next Section of the edge of the train
						string CurrentSectionID, NextSectionID;
						CurrentSectionID = BS[h].ID;
						int Prev_Block = h - 1;
						if (h == 0)
							Prev_Block = 0;
						int Next_Block = -1;

						if (h == N_BS - 1) { // this is the pointer to the next block section of the MA
							Next_Block = h;
							NextSectionID = "None";
						} else {
							Next_Block = h + 1;
							NextSectionID = BS[h + 1].ID;
						}

						if (BS[h].withSwitchDiv == 0) {
							elaborateRbcMas((instant_spatial_position[delayedIndex] - train_length - ETCS3SafetyMargin), instant_train_speed[delayedIndex], LastTrainAcceleration, BS[h], CurrentSectionID, NextSectionID, trainDescription, train_route[this->indexOfRoute], train_route[this->indexOfRoute].reversed_direction, "TrainEnd", "Tale");
							lockSwitchesOnAllConnectedSections((instant_spatial_position[delayedIndex] + ETCS3SafetyMargin), (instant_spatial_position[delayedIndex] - train_length - ETCS3SafetyMargin), instant_train_speed[delayedIndex], LastTrainAcceleration, BS[h], CurrentSectionID, NextSectionID, trainDescription, train_route[this->indexOfRoute].reversed_direction, "None");
						} else { // if instead the block section has a diverging switch
							elaborateMaOnBlockSectionsWithSwitchDiv((instant_spatial_position[delayedIndex] - train_length - ETCS3SafetyMargin), instant_train_speed[delayedIndex], LastTrainAcceleration, BS[h], trainDescription, train_route[indexOfRoute], "Tale");
							lockSwitchesWhileTrainTraverses((instant_spatial_position[delayedIndex] + ETCS3SafetyMargin), (instant_spatial_position[delayedIndex] - train_length - ETCS3SafetyMargin), instant_train_speed[delayedIndex], LastTrainAcceleration, BS[h], trainDescription, train_route[indexOfRoute], "Tale");
						}
					}

					// Case if the track-based vacancy detection is more permissive than the train-based method
					else if (((instant_spatial_position[delayedIndex] - train_length) < BS[h].end_node.X * 1000) && ((instant_spatial_position[delayedIndex] - train_length) >= BS[h].start_node.X * 1000) && (instant_spatial_position[delayedIndex] - train_length - ETCS3SafetyMargin) < BS[h].start_node.X * 1000) {
						// Defining veriables for the ID of the previous, Current and Next Section of the edge of the train
						string CurrentSectionID, NextSectionID;
						CurrentSectionID = BS[h].ID;
						int Prev_Block = h - 1;
						if (h == 0)
							Prev_Block = 0;
						int Next_Block = -1;

						if (h == N_BS - 1) { // this is the pointer to the next block section of the MA
							Next_Block = h;
							NextSectionID = "None";
						} else {
							Next_Block = h + 1;
							NextSectionID = BS[h + 1].ID;
						}

						if (BS[h].withSwitchDiv == 0) {
							elaborateRbcMas((BS[h].start_node.X * 1000), instant_train_speed[delayedIndex], LastTrainAcceleration, BS[h], CurrentSectionID, NextSectionID, trainDescription, train_route[this->indexOfRoute], train_route[this->indexOfRoute].reversed_direction, "TrainEnd", "Tale");
							lockSwitchesOnAllConnectedSections((instant_spatial_position[delayedIndex] + ETCS3SafetyMargin), (BS[h].start_node.X * 1000), instant_train_speed[delayedIndex], LastTrainAcceleration, BS[h], CurrentSectionID, NextSectionID, trainDescription, train_route[this->indexOfRoute].reversed_direction, "None");
						} else { // if instead the block section has a diverging switch
							elaborateMaOnBlockSectionsWithSwitchDiv((BS[h].start_node.X * 1000), instant_train_speed[delayedIndex], LastTrainAcceleration, BS[h], trainDescription, train_route[indexOfRoute], "Tale");
							lockSwitchesWhileTrainTraverses((instant_spatial_position[delayedIndex] + ETCS3SafetyMargin), (BS[h].start_node.X * 1000), instant_train_speed[delayedIndex], LastTrainAcceleration, BS[h], trainDescription, train_route[indexOfRoute], "Tale");
						}
					}

					// if the train is in the simulation but its tale is still outside of the network (so the position of the tale is lower than the beginning of the first block section) then the MA should refer to the beginning Node of the first block section of the route
					if ((instant_spatial_position[delayedIndex] - train_length - ETCS3SafetyMargin) < BS[0].start_node.X * 1000) {

						elaborateRbcMas((instant_spatial_position[delayedIndex] - train_length - ETCS3SafetyMargin), instant_train_speed[delayedIndex], LastTrainAcceleration, BS[0], BS[0].ID, N_BS > 1 ? BS[1].ID : "None", trainDescription, train_route[this->indexOfRoute], train_route[this->indexOfRoute].reversed_direction, "TrainEnd", "Tale");
					}

					// Detecting the MA related to the front of the train
					if (((instant_spatial_position[delayedIndex] + ETCS3SafetyMargin) < BS[h].end_node.X * 1000) && ((instant_spatial_position[delayedIndex] + ETCS3SafetyMargin) >= BS[h].start_node.X * 1000)) {
						// Defining veriables for the ID of the previous, Current and Next Section of the edge of the train
						string CurrentSectionID, NextSectionID;
						CurrentSectionID = BS[h].ID;
						int Prev_Block = h - 1;
						if (h == 0)
							Prev_Block = 0;
						int Next_Block = -1;

						if (h == N_BS - 1) { // this is the pointer to the next block section of the MA
							Next_Block = h;
							NextSectionID = "None";
						} else {
							Next_Block = h + 1;
							NextSectionID = BS[h + 1].ID;
						}

						if (BS[h].withSwitchDiv == 0) {
							elaborateRbcMas((instant_spatial_position[delayedIndex] + ETCS3SafetyMargin), instant_train_speed[delayedIndex], LastTrainAcceleration, BS[h], CurrentSectionID, NextSectionID, trainDescription, train_route[this->indexOfRoute], train_route[this->indexOfRoute].reversed_direction, "TrainEnd", "Front");
							lockSwitchesOnAllConnectedSections((instant_spatial_position[delayedIndex] + ETCS3SafetyMargin), (instant_spatial_position[delayedIndex] - train_length - ETCS3SafetyMargin), instant_train_speed[delayedIndex], LastTrainAcceleration, BS[h], CurrentSectionID, NextSectionID, trainDescription, train_route[this->indexOfRoute].reversed_direction, "None");
						} else { // if instead the block section has a diverging switch
							elaborateMaOnBlockSectionsWithSwitchDiv((instant_spatial_position[delayedIndex] + ETCS3SafetyMargin), instant_train_speed[delayedIndex], LastTrainAcceleration, BS[h], trainDescription, train_route[indexOfRoute], "Front");
							lockSwitchesWhileTrainTraverses((instant_spatial_position[delayedIndex] + ETCS3SafetyMargin), (instant_spatial_position[delayedIndex] - train_length - ETCS3SafetyMargin), instant_train_speed[delayedIndex], LastTrainAcceleration, BS[h], trainDescription, train_route[indexOfRoute], "Front");
						}
						break;
					}
				}
			}
		}
	}
}
