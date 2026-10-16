#include "simulation/RollingStock.h"

#include <cmath>
#include <cstddef>

double Train::tractiveEffort(double V) {
	double Ftr = 0.0;
	for (int i = 0; i < velocityIntervals; i++) {
		if ((V >= Vlb[i]) && (V < Vub[i])) {
			Ftr = C0[i] + C1[i] * V + C2[i] * pow(V, 2);
		}
	}
	if (servicePerformancePercent == 100.0)
		return Ftr;
	return Ftr * servicePerformancePercent / 100.0;
}

double Train::brakingEffort(double T) {
	double FBr;
	if (Jerk * T < max_train_decelaration) {
		FBr = total_train_mass * massFactor * Jerk * T;
	} else {
		FBr = total_train_mass * massFactor * max_train_decelaration;
	}
	return FBr;
}

double Train::traction_unit_resistences(double V) {
	double FRt;
	FRt = 4.2 * (mass_of_traction_unit * g / 1000) + 0.72 * pow(((V + 4.17) * 3.6), 2);
	return FRt;
}

double Train::wagons_resistances(double V) {
	double Frwp;
	Frwp = (1.9 + resistanceCoefficient * (V * 3.6)) * (massPerWagonAxle * g / 1000) + 4.8 * (number_of_wagons + 2.7) * frontal_wagon_area * pow(((V + 4.17) * 3.6 / 10), 2); // Sauthoff Formulae for Passenger Trains
	return Frwp;
}

double Train::gradient_resistances(double G) {
	double Frlg;
	Frlg = g * (total_train_mass)*G;
	return Frlg;
}

double Train::curvature_resistances(double R) {
	if (R == 0.0)
		return 0.0;
	double Frlc;
	Frlc = g * (total_train_mass) * 700 / (R * 1000);
	return Frlc;
}

double Train::total_train_resistances(double V, double G, double R) {
	double FR;
	FR = traction_unit_resistences(V) + wagons_resistances(V) + gradient_resistances(G) + curvature_resistances(R);
	return FR;
}

void Train::Initialise_Gibson_Dwell_Time_Parameters(float beta0, float beta1, float beta2, float beta3, float beta4, float beta5, float beta6, float beta7) {
	this->GibsonDwellTimeParameters[0] = beta0;
	this->GibsonDwellTimeParameters[1] = beta1;
	this->GibsonDwellTimeParameters[2] = beta2;
	this->GibsonDwellTimeParameters[3] = beta3;
	this->GibsonDwellTimeParameters[4] = beta4;
	this->GibsonDwellTimeParameters[5] = beta5;
	this->GibsonDwellTimeParameters[6] = beta6;
	this->GibsonDwellTimeParameters[7] = beta7;
}

void Train::sanitizeTerminalPowerSample() {
	if (End_Time >= 0 && End_Time < static_cast<int>(instant_train_power_consumption.size())
		&& !std::isfinite(instant_train_power_consumption[static_cast<std::size_t>(End_Time)])) {
		instant_train_power_consumption[static_cast<std::size_t>(End_Time)] = 0.0;
		if (End_Time < static_cast<int>(instant_train_tractive_effort.size()))
			instant_train_tractive_effort[static_cast<std::size_t>(End_Time)] = 0.0;
	}
}

void Train::TotalEnergyConsumptionWithAndWithoutRegBraking(double EtaSubst, double EtaRegBrak) {
	sanitizeTerminalPowerSample();
	// This is the total Energy consumed by the train to move
	this->TotalEnergyConsumed = instant_train_energy_consumption[End_Time];
	this->EnergyForAuxiliaries = 0.10 * TotalEnergyConsumed;

	// Adding the EnergyForAuxiliaries to the total Energy consumed
	TotalEnergyConsumed = TotalEnergyConsumed + EnergyForAuxiliaries;

	// The total Energy requested to the Substation is TotalEnergyConsumed/EtaSubst
	this->TotalEnergySubstationRequest = TotalEnergyConsumed / EtaSubst;

	// Computing the EnergyConsumedwith the regenerative braking

	int TrainStartTime = (int)departure_time;
	TotalEnergyConsWithRegBrak = instant_train_energy_consumption[TrainStartTime]; // Initialize the first value of the totalEnergyConsumption

	for (int i = TrainStartTime + 1; i <= End_Time; i++) {

		double Pot1 = 0, Pot2 = 0;
		// Setting Pot1
		if (instant_train_power_consumption[i - 1] >= 0)
			Pot1 = instant_train_power_consumption[i - 1];
		else
			Pot1 = EtaRegBrak * instant_train_power_consumption[i - 1];
		// Setting Pot 2
		if (instant_train_power_consumption[i] >= 0)
			Pot2 = instant_train_power_consumption[i];
		else
			Pot2 = EtaRegBrak * instant_train_power_consumption[i];

		this->TotalEnergyConsWithRegBrak = TotalEnergyConsWithRegBrak + ((Pot1 + Pot2) / 2000000) * timestep;
	}
	// Adding the energy for auxiliaries to the TotalEnrgy Consuption with regenerative braking
	TotalEnergyConsWithRegBrak = TotalEnergyConsWithRegBrak + EnergyForAuxiliaries;

	this->TotalEnergySubstRequestWithRegBrak = TotalEnergyConsWithRegBrak / EtaSubst; // this is the energy requested rom the substation by the train when it uses regenerative braking
}

Regional& Regional::operator=(const Regional& ob2) {
	number_of_wagons = ob2.number_of_wagons;
	MAX_OnBoard_Passengers = trainPassengerCapacity(number_of_wagons);
	massFactor = ob2.massFactor;
	mass_of_traction_unit = ob2.mass_of_traction_unit;
	mass_of_a_wagon = ob2.mass_of_a_wagon;
	total_train_mass = ob2.total_train_mass;
	max_train_decelaration = ob2.max_train_decelaration;
	max_train_speed = ob2.max_train_speed;
	g = ob2.g;
	frontal_wagon_area = ob2.frontal_wagon_area;
	massPerWagonAxle = ob2.massPerWagonAxle;
	Jerk = ob2.Jerk;
	train_length = ob2.train_length;
	operatingCode = ob2.operatingCode;
	serviceId = ob2.serviceId;
	serviceOccurrence = ob2.serviceOccurrence;
	servicePerformancePercent = ob2.servicePerformancePercent;
	hasConfiguredMaximumSpeed = ob2.hasConfiguredMaximumSpeed;
	configuredMaximumSpeedKmh = ob2.configuredMaximumSpeedKmh;
	compositionMaximumSpeedMs = ob2.compositionMaximumSpeedMs;
	appliedMaximumSpeedMs = ob2.appliedMaximumSpeedMs;
	appliedMaximumSpeedKmh = ob2.appliedMaximumSpeedKmh;
	directIncidentIds = ob2.directIncidentIds;
	firstDirectIncidentTime = ob2.firstDirectIncidentTime;
	firstDirectIncidentLocation = ob2.firstDirectIncidentLocation;
	destinationTerminationRequested = ob2.destinationTerminationRequested;
	destinationTerminated = ob2.destinationTerminated;
	return *this;
}
