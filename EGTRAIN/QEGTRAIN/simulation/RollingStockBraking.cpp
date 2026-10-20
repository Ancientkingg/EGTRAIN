#include "simulation/RollingStock.h"
#include "scene/SignallingLevel.h"
#include "simulation/SimulationRun.h"
#include "util/Log.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <list>
#include <string>
#include <vector>

using namespace std;

double Train::BrakingDistanceFastComputation(double V1, double V2, double X1, double X2, Section* BS, int N_BS) {
	double AverageGradient = 0;
	double AverageCurvature = 0;
	int N_arcs = 0;

	for (int i = 0; i < N_BS; i++) {
		for (int j = 0; j < BS[i].total_arcs; j++) {
			if ((BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000 > X1) && (BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000 <= X2)) {
				AverageGradient = AverageGradient + BS[i].arcs_in_signalling_block_section[j].gradient;
				AverageCurvature = AverageCurvature + BS[i].arcs_in_signalling_block_section[j].curvature;
				N_arcs++;
			}
		}
	}
	// Computing Average Gradient and Average Curvature Radii
	AverageGradient = AverageGradient / N_arcs;
	AverageCurvature = AverageCurvature / N_arcs;

	// Computing the braking curve with closed formula
	double BrakingDistance;
	// BrakingDistance = V1 * timestep + (max_train_decelaration + total_train_resistances(((V1 + V2) / (2)), AverageGradient, AverageCurvature) / (total_train_mass*massFactor))*(V1 + V2) / (2 * Jerk) + (pow(V1, 2) - pow(V2, 2)) / (2 * (max_train_decelaration + total_train_resistances(((V1 + V2) / (2 * Jerk)), AverageGradient, AverageCurvature) / (total_train_mass*massFactor)));   //this formula is the full closed braking curve formula

	BrakingDistance = (pow(V1, 2) - pow(V2, 2)) / (2 * (max_train_decelaration + total_train_resistances(((V1 + V2) / 2), AverageGradient, AverageCurvature) / (total_train_mass * massFactor))); // this one resulted to be the best in terms of closeness to simulation results

	return BrakingDistance;
}

double Train::AccelerationTimeFollowingMode(double V1, double V2, double X1, double X2, Section* BS, int N_BS) {
	double AverageGradient = 0;
	double AverageCurvature = 0;
	int N_arcs = 0;

	for (int i = 0; i < N_BS; i++) {

		for (int j = 0; j < BS[i].total_arcs; j++) {

			if ((BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000 > X1) && (BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000 <= X2)) {

				AverageGradient = AverageGradient + BS[i].arcs_in_signalling_block_section[j].gradient;

				AverageCurvature = AverageCurvature + BS[i].arcs_in_signalling_block_section[j].curvature;

				N_arcs++;
			}
		}
	}
	// Computing Average Gradient and Average Curvature Radii
	AverageGradient = AverageGradient / N_arcs;
	AverageCurvature = AverageCurvature / N_arcs;

	// Computing the acceleration curve of the train

	double AccelerationDistance = 0;
	double AccelerationTime = 0;

	// Splitting the speed difference into several intervals
	double V_Step = -1;
	int N_V_Steps = 200;
	if (V2 > V1) {
		V_Step = (V2 - V1) / N_V_Steps;
	} else {
		EG_LOG(SimEvents, Error) << "ERROR: Train " << this->trainDescription << " is trying to compute an average acceleration distance with a target speed V2 lower than the starting speed V1. A braking curve should be computed in this case. Please check the reason why this is happening\n";
	}

	if (V_Step > 0) {
		for (int i = 0; i <= N_V_Steps; i++) {
			double V_Current_Interval = V1 + i * V_Step;
			double V_Previous_Interval = 0;
			if (i == 0) {
				V_Previous_Interval = V1;
			} else {
				V_Previous_Interval = V1 + (i - 1) * V_Step;
			}

			// Computation of piecewise acceleration distance
			double StepAcceleration = 0;
			StepAcceleration = (this->tractiveEffort(V_Previous_Interval) - this->total_train_resistances(V_Previous_Interval, AverageGradient, AverageCurvature)) / (total_train_mass * massFactor);
			AccelerationDistance = AccelerationDistance + (pow(V_Current_Interval, 2) - pow(V_Previous_Interval, 2)) / (2 * StepAcceleration);
			AccelerationTime = AccelerationTime + (V_Current_Interval - V_Previous_Interval) / StepAcceleration;
		}
	}

	if (AccelerationTime <= 0) {
		// if the Acceleration Distance was negative that probably meant that the average Gradient was too high for the train to accelerate and the locomotive was not powerful enough
		// In this case we create a rescue acceleration distance that considers the gradient flat, i.e. =0
		double RescueAccelerationDistance = 0;
		double RescueAccelerationTime = 0;

		if (V_Step > 0) {
			for (int i = 0; i <= N_V_Steps; i++) {
				double V_Current_Interval = V1 + i * V_Step;
				double V_Previous_Interval = 0;
				if (i == 0) {
					V_Previous_Interval = V1;
				} else {
					V_Previous_Interval = V1 + (i - 1) * V_Step;
				}

				// Computation of piecewise acceleration distance
				double StepAccelerationOnFlatGround = 0;
				StepAccelerationOnFlatGround = (this->tractiveEffort(V_Previous_Interval) - this->total_train_resistances(V_Previous_Interval, 0, AverageCurvature)) / (total_train_mass * massFactor);
				RescueAccelerationDistance = RescueAccelerationDistance + (pow(V_Current_Interval, 2) - pow(V_Previous_Interval, 2)) / (2 * StepAccelerationOnFlatGround);
				RescueAccelerationTime = RescueAccelerationTime + (V_Current_Interval - V_Previous_Interval) / StepAccelerationOnFlatGround;
			}
		}

		if (RescueAccelerationTime > 0) {
			AccelerationTime = RescueAccelerationTime;
		} else { // In case also the rescue acceleration distance is negative that means that the train cannot accelerate on this section because resistances are larger than tractive effort
				 // At this point use the cruising mode for the train and the acceleration distance is 0 therefore

			EG_LOG(SimEvents, Warning) << "Train " << this->trainDescription << " has air resistances larger than tractive effort on this section with V1: " << V1 << " V2: " << V2 << " X1: " << X1 << " X2: " << X2 << ". The Cruising mode will be applied since trains cannot accelerate\n";
			AccelerationTime = -99999;
		}
	}

	return AccelerationTime;
}

const Arc* Train::BrakingArcCursor::find(double head, Section* sections, int count, double* safeUpper) {
	if (arc && head >= lookupHead && head < earlierFuture) {
		if (head < arc->endNode.X * 1000) {
			lookupHead = head;
			if (safeUpper) *safeUpper = std::min(earlierFuture, arc->endNode.X * 1000);
			return arc;
		}
		++arcIndex;
	} else {
		section = arcIndex = 0;
		earlierFuture = std::numeric_limits<double>::infinity();
	}
	lookupHead = head;
	arc = nullptr;
	for (; section < count; ++section, arcIndex = 0)
		for (; arcIndex < sections[section].total_arcs; ++arcIndex) {
			const Arc& candidate = sections[section].arcs_in_signalling_block_section[arcIndex];
			if (head >= candidate.startNode.X * 1000 && head < candidate.endNode.X * 1000) {
				arc = &candidate;
				if (safeUpper) *safeUpper = std::min(earlierFuture, candidate.endNode.X * 1000);
				return arc;
			}
			if (candidate.startNode.X * 1000 > head)
				earlierFuture = std::min(earlierFuture, candidate.startNode.X * 1000);
		}
	if (safeUpper) *safeUpper = earlierFuture;
	return nullptr;
}

const Arc* Train::brakingArc(double head, Section* sections, int count, double* safeUpper) const {
	BrakingArcCursor cursor;
	return cursor.find(head, sections, count, safeUpper);
}

bool Train::inverseBrakingStep(double nextHead, double nextSpeed, Section* sections, int count, double& head, double& speed) {
	head = nextHead - nextSpeed * timestep;
	const Arc* arc = brakingArc(head, sections, count);
	if (!arc || total_train_mass <= 0 || massFactor <= 0 || timestep <= 0 || nextSpeed < 0)
		return false;
	const double mass = total_train_mass * massFactor;
	const double r0 = total_train_resistances(0, arc->gradient, arc->curvature);
	const double r1 = total_train_resistances(1, arc->gradient, arc->curvature);
	const double r2 = total_train_resistances(2, arc->gradient, arc->curvature);
	const double quadratic = (r2 - 2 * r1 + r0) / (2 * mass) * timestep;
	const double linear = 1 - ((r1 - r0) / mass * timestep - quadratic);
	const double constant = nextSpeed + (max_train_decelaration + r0 / mass) * timestep;
	if (!std::isfinite(quadratic) || !std::isfinite(linear) || !std::isfinite(constant) || quadratic < 0)
		return false;
	if (quadratic == 0) {
		if (linear <= 0)
			return false;
		speed = constant / linear;
	} else {
		const double discriminant = linear * linear - 4 * quadratic * constant;
		if (linear <= 0 || discriminant <= 0 || !std::isfinite(discriminant))
			return false; // No finite, unique root on the increasing branch, including the fold.
		speed = 2 * constant / (linear + std::sqrt(discriminant));
		if (linear - 2 * quadratic * speed <= 0)
			return false;
	}
	if (!std::isfinite(head) || !std::isfinite(speed) || speed < 0)
		return false;
	// Refine against the actual resistance expression, avoiding cancellation in
	// the sampled coefficients. Choose the adjacent non-overshooting inverse.
	for (int iteration = 0; iteration < 4; ++iteration) {
		const double residual = speed - (max_train_decelaration + total_train_resistances(speed, arc->gradient, arc->curvature) / mass) * timestep - nextSpeed;
		const double refined = speed - residual / (linear - 2 * quadratic * speed);
		if (!std::isfinite(refined) || refined < 0 || linear - 2 * quadratic * refined <= 0)
			return false;
		if (refined == speed)
			break;
		speed = refined;
	}
	if (brakingCurveSpeed(speed, *arc) > nextSpeed)
		speed = std::nextafter(speed, 0.0);
	return brakingCurveSpeed(speed, *arc) <= nextSpeed;
}

bool Train::predictBraking(double head, double speed, double targetSpeed, Section* sections, int count,
	double& end, std::vector<double>* positions, std::vector<double>* speeds) {
	BrakingArcCursor cursor;
	for (int step = 0; step < 1999; ++step) {
		end = head; // An above-target route exit can reject a shooting bracket, never validate a plan.
		if (!std::isfinite(head) || !std::isfinite(speed) || speed < 0)
			return false;
		if (positions) {
			positions->push_back(head);
			speeds->push_back(speed);
		}
		if (speed <= targetSpeed) {
			end = head;
			return true;
		}
		const Arc* arc = cursor.find(head, sections, count);
		if (!arc)
			return false;
		speed = brakingCurveSpeed(speed, *arc);
		head += speed * timestep;
	}
	return false;
}

bool Train::predictBrakingPrefix(double head, double speed, int steps, Section* sections, int count,
	double& end, double& endSpeed, std::vector<double>* positions, std::vector<double>* speeds) {
	if (steps < 0 || steps >= 1999)
		return false;
	BrakingArcCursor cursor;
	for (int step = 0; step <= steps; ++step) {
		end = head;
		endSpeed = speed;
		if (!std::isfinite(head) || !std::isfinite(speed) || speed < 0)
			return false;
		if (positions) {
			positions->push_back(head);
			speeds->push_back(speed);
		}
		if (step == steps)
			return true;
		const Arc* arc = cursor.find(head, sections, count);
		if (!arc)
			return false;
		speed = brakingCurveSpeed(speed, *arc);
		head += speed * timestep;
	}
	return false;
}

bool Train::brakingTerminalUpper(double head, double speed, double ceiling, Section* sections, int count, double& upper) {
	const Arc* arc = brakingArc(head, sections, count);
	if (!arc) {
		upper = head;
		return false;
	}
	const double maximum = std::max(0.0, speed + (tractiveEffort(speed) - total_train_resistances(speed, arc->gradient, arc->curvature)) / (total_train_mass * massFactor) * timestep);
	upper = head + std::min(ceiling, maximum) * timestep;
	return std::isfinite(upper);
}

bool Train::constructBrakingTerminal(double head, double speed, double anchor, double ceiling, Section* sections, int count, double& terminal) {
	const Arc* arc = brakingArc(head, sections, count);
	if (!arc || !std::isfinite(head) || !std::isfinite(speed) || speed < 0 || !std::isfinite(anchor)
		|| anchor < head || !std::isfinite(ceiling) || ceiling < 0 || timestep <= 0 || total_train_mass <= 0 || massFactor <= 0)
		return false;
	const double mass = total_train_mass * massFactor;
	const double resistance = total_train_resistances(speed, arc->gradient, arc->curvature), effort = tractiveEffort(speed);
	if (!std::isfinite(mass) || !std::isfinite(timestep) || !std::isfinite(resistance) || !std::isfinite(effort) || !std::isfinite(max_train_decelaration))
		return false;
	const double minimum = brakingCurveSpeed(speed, *arc);
	const double maximum = std::min(ceiling, std::max(0.0, speed + (effort - resistance) / mass * timestep));
	if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum)
		return false;
	terminal = std::clamp((anchor - head) / timestep, minimum, maximum);
	const double end = head + terminal * timestep;
	const double roundoff = 8 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::fabs(anchor));
	return std::isfinite(end) && std::fabs(end - anchor) <= roundoff;
}

bool Train::planPositiveBraking(double head, double speed, const Arc& arc, Section* sections, int count, int suggested) {
	const double firstMinimum = brakingCurveSpeed(speed, arc);
	const double firstMaximum = std::max(0.0, speed + (tractiveEffort(speed) - total_train_resistances(speed, arc.gradient, arc.curvature)) / (total_train_mass * massFactor) * timestep);
	const double anchor = Xobmin - 0.002;
	const double roundoff = 8 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::fabs(anchor));
	double minimumNative = Vobmin;
	for (int k = 0; k <= BrakStep; ++k)
		if (Vbrak[k] > 0) minimumNative = std::min(minimumNative, Vbrak[k]);
	const double phaseSteps = std::ceil(std::max(0.0, Sbrak[0] - head) / (minimumNative * timestep));
	if (!std::isfinite(phaseSteps))
		return false;
	const int extra = phaseSteps >= 1998 ? 1999 : std::max(1, static_cast<int>(phaseSteps) + 1);
	for (int length = std::max(1, suggested - 1); length <= std::min(1999, suggested + extra); ++length) {
		double low = firstMinimum, high = firstMaximum, end, endSpeed, upper;
		if (length == 1) {
			double terminal;
			if (!constructBrakingTerminal(head, speed, anchor, Vobmin, sections, count, terminal)) continue;
			Sbrak[0] = head;
			Vbrak[0] = speed;
			Sbrak[1] = head + terminal * timestep;
			Vbrak[1] = terminal;
			BrakStep = 1;
			return true;
		}
		if (!std::isfinite(low) || !std::isfinite(high) || low > high || !predictBrakingPrefix(head + low * timestep, low, length - 2, sections, count, end, endSpeed)
			|| !brakingTerminalUpper(end, endSpeed, Vobmin, sections, count, upper) || upper > anchor + roundoff) continue;
		const bool completeUpper = predictBrakingPrefix(head + high * timestep, high, length - 2, sections, count, end, endSpeed);
		if (!completeUpper) {
			if (!(end > anchor)) continue;
			upper = end;
		} else if (!brakingTerminalUpper(end, endSpeed, Vobmin, sections, count, upper)) {
			if (!(end > anchor)) continue;
			upper = end;
		}
		if (upper < anchor) continue;
		for (int iteration = 0; iteration < 64; ++iteration) {
			const double middle = low + (high - low) / 2;
			if (middle == low || middle == high) break;
			const bool complete = predictBrakingPrefix(head + middle * timestep, middle, length - 2, sections, count, end, endSpeed);
			if (!complete || !brakingTerminalUpper(end, endSpeed, Vobmin, sections, count, upper)) {
				if (!(end > anchor)) break;
				upper = end;
			}
			if (upper < anchor) low = middle;
			else high = middle;
		}
		std::vector<double> positions{head}, speeds{speed};
		if (!predictBrakingPrefix(head + high * timestep, high, length - 2, sections, count, end, endSpeed, &positions, &speeds)) continue;
		double terminal;
		if (!constructBrakingTerminal(end, endSpeed, anchor, Vobmin, sections, count, terminal)) continue;
		positions.push_back(end + terminal * timestep);
		speeds.push_back(terminal);
		if (std::fabs(positions.back() - anchor) > roundoff || positions.size() > Sbrak.size()) continue;
		std::copy(positions.begin(), positions.end(), Sbrak.begin());
		std::copy(speeds.begin(), speeds.end(), Vbrak.begin());
		BrakStep = static_cast<int>(positions.size()) - 1;
		return true;
	}
	return false;
}

double Train::BrakDist_Block(double V1, double V2, double X0, Section* BS, int Blocks) {
	if (V1 <= V2)
		return X0;
	double nextHead = X0 - 0.002, nextSpeed = V2;
	for (int step = 1; step < 2000; ++step) {
		double head, speed;
		if (!inverseBrakingStep(nextHead, nextSpeed, BS, Blocks, head, speed)) {
			if (head < BS[0].start_node.X * 1000)
				return head;
			return -1;
		}
		if (speed >= V1) {
			if (V2 > 0) {
				const double anchor = X0 - 0.002;
				double low = head, high = nextHead, end, endSpeed, upper;
				if (!predictBrakingPrefix(low, V1, step - 1, BS, Blocks, end, endSpeed) || !brakingTerminalUpper(end, endSpeed, V2, BS, Blocks, upper) || upper > anchor) return -1;
				const bool completeUpper = predictBrakingPrefix(high, V1, step - 1, BS, Blocks, end, endSpeed);
				if (!completeUpper || !brakingTerminalUpper(end, endSpeed, V2, BS, Blocks, upper)) {
					if (!(end > anchor)) return -1;
					upper = end;
				}
				if (upper < anchor) return -1;
				for (int iteration = 0; iteration < 64; ++iteration) {
					const double middle = low + (high - low) / 2;
					if (middle == low || middle == high) break;
					const bool complete = predictBrakingPrefix(middle, V1, step - 1, BS, Blocks, end, endSpeed);
					if (!complete || !brakingTerminalUpper(end, endSpeed, V2, BS, Blocks, upper)) {
						if (!(end > anchor)) return -1;
						upper = end;
					}
					if (upper < anchor) low = middle;
					else high = middle;
				}
				if (!predictBrakingPrefix(high, V1, step - 1, BS, Blocks, end, endSpeed)) return -1;
				double terminal;
				if (!constructBrakingTerminal(end, endSpeed, anchor, V2, BS, Blocks, terminal)) return -1;
				return high;
			}
			double low = head, high = nextHead, end;
			const double roundoff = 8 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::fabs(X0 - 0.002));
			if (!predictBraking(low, V1, V2, BS, Blocks, end) || end > X0 - 0.002 + roundoff)
				return -1;
			const bool completeUpper = predictBraking(high, V1, V2, BS, Blocks, end);
			if ((!completeUpper && !(end > X0 - 0.002)) || end < X0 - 0.002 - roundoff)
				return -1;
			for (int iteration = 0; iteration < 64; ++iteration) {
				const double middle = low + (high - low) / 2;
				if (middle == low || middle == high)
					break;
				if (!predictBraking(middle, V1, V2, BS, Blocks, end) && !(end > X0 - 0.002))
					return -1;
				if (end <= X0 - 0.002)
					low = middle;
				else
					high = middle;
			}
			return low; // Conservative side of a boundary discontinuity, not a target snap.
		}
		nextHead = head;
		nextSpeed = speed;
	}
	return -1;
}

bool Train::needsSingleTrackReservation(double head, double speed, double entry, Section* sections, int count) {
	if (head >= entry)
		return true;
	if (!std::isfinite(speed) || speed < 0 || total_train_mass <= 0 || massFactor <= 0 || !std::isfinite(max_train_speed))
		return true;
	double nextSpeed = speed;
	bool foundArc = false;
	for (int b = 0; b < count && !foundArc; ++b)
		for (int a = 0; a < sections[b].total_arcs; ++a) {
			const Arc& arc = sections[b].arcs_in_signalling_block_section[a];
			if (head < arc.startNode.X * 1000 || head >= arc.endNode.X * 1000)
				continue;
			const double acceleration = (std::max(tractiveEffort(speed), tractiveEffort(0))
											- total_train_resistances(speed, arc.gradient, arc.curvature))
				/ (total_train_mass * massFactor);
			if (!std::isfinite(acceleration))
				return true; // no safe movement bound
			nextSpeed = std::min(max_train_speed, speed + std::max(0.0, acceleration) * timestep);
			foundArc = true;
			break;
		}
	if (!foundArc || !std::isfinite(nextSpeed))
		return true;
	const double nextHead = head + std::max(speed, nextSpeed) * timestep;
	if (nextHead >= entry)
		return true;
	auto needsBraking = [&](double target, double targetSpeed) {
		if (nextSpeed <= targetSpeed)
			return false;
		if (nextHead >= target)
			return true;
		const double brakingStart = BrakDist_Block(nextSpeed, targetSpeed, target, sections, count);
		if (!std::isfinite(brakingStart) || brakingStart == -1)
			return true; // a failed lookup cannot establish a later safe request
		if (brakingStart < sections[0].start_node.X * 1000)
			return true; // the last braking opportunity precedes the route origin
		return nextHead >= brakingStart;
	};
	// Fixed-block locks also constrain the approach blocks. Reserve before those speed targets become too late,
	// rather than introducing a speed cap on a train already in the block carrying it.
	for (int b = 0; b < count; ++b)
		if (sections[b].start_node.X * 1000 == entry) {
			if (b > 0 && sections[b - 1].SignallingLevel == levelValue(SignallingLevel::Atb)
				&& needsBraking(sections[b - 1].start_node.X * 1000, signalCode1))
				return true;
			if (b > 0 && sections[b - 1].SignallingLevel == levelValue(SignallingLevel::Bacc)
				&& needsBraking(sections[b - 1].start_node.X * 1000, 0))
				return true;
			if (b > 1 && sections[b - 2].SignallingLevel == levelValue(SignallingLevel::Bacc)
				&& needsBraking(sections[b - 2].start_node.X * 1000, signalCode1))
				return true;
			break;
		}
	return needsBraking(entry, 0);
}

int Train::cachedBrakingSuccessor(int previousInstant, const Arc& arc, double target, double targetSpeed) {
	if (counter <= 0 || BrakStep <= 0 || BrakStep >= static_cast<int>(Sbrak.size())
		|| (previousInstant < static_cast<int>(Xob.size()) && (Xob[previousInstant] != target || Vob[previousInstant] != targetSpeed)))
		return -1;
	const double head = instant_spatial_position[previousInstant], speed = instant_train_speed[previousInstant];
	for (int k = 0; k < BrakStep; ++k) {
		if (head != Sbrak[k] || speed != Vbrak[k])
			continue;
		const double nextSpeed = k == 0 || (targetSpeed > 0 && k + 1 == BrakStep) ? Vbrak[k + 1] : brakingCurveSpeed(speed, arc);
		const double nextHead = head + nextSpeed * timestep;
		const double minimum = brakingCurveSpeed(speed, arc);
		const double maximum = std::max(0.0, speed + (tractiveEffort(speed) - total_train_resistances(speed, arc.gradient, arc.curvature)) / (total_train_mass * massFactor) * timestep);
		if (nextSpeed < minimum || nextSpeed > maximum || nextHead != Sbrak[k + 1] || nextSpeed != Vbrak[k + 1])
			return -1;
		return k + 1;
	}
	return -1;
}

double Train::europeanVitalComputerWithListsImproved(double S, double V, int PreviousInstant, Section* BS, int Blocks, bool& continueCached) {
	continueCached = false;
	IsTrainCoupling = false;
	int N_BrakP;
	int ArcPos = 0;
	int BlockPos = 0;
	double MinBX = 99999999999; // Number of Braking points in the Block Section occupied by the train, Index of the Arc of the signalling_block_sections occupied by the train, Index of the signalling_block_sections occupied by the train, Matrix of all Brak Distance of the Braking point of the Block Section occupied by the train, Minimum Braking Abscissa, Braking Abscissa of the block section arcs which follows the signalling_block_sections occupied by the train
	Node ClosestETCSBrakingPoint;
	ClosestETCSBrakingPoint.X = 9999999999;			 // This is the closest ETCS braking point. In this case the closestpoint is not the MA having teh closest abscissa to the current position of the train but it is the MA which requirest the train to brake the earliest to be met
	double TargetSpeedForMA = -1;					 // This is the Speed the train needs to target to meet the most restrictive MA
	double ClosestPointToBrakeForETCS = 9999999999;	 // This is the value of the closese Point to Brake For ETCS
	int SignallingLevelClosestETCSBrakingPoint = -1; // This is the signalling level of the closest ETCS Braking Point

	Node ETCSBrakingPoint; // Node to collect all the information of the ETCS BrakingPoints

	list<Node> BrakingPoints; // This is the list of the nodes constituting braking points

	for (int h = 0; h < Blocks; h++) {
		if ((S < BS[h].end_node.X * 1000) && (S >= BS[h].start_node.X * 1000)) {
			BlockPos = h;
			break;
		}
	}
	for (int j = 0; j < BS[BlockPos].total_arcs; j++) {
		if ((S < BS[BlockPos].arcs_in_signalling_block_section[j].endNode.X * 1000) && (S >= BS[BlockPos].arcs_in_signalling_block_section[j].startNode.X * 1000)) {
			ArcPos = j;
			break;
		}
	}
	N_BrakP = BS[BlockPos].total_arcs - ArcPos; // Initializing dimensions of Braking points vectors

	// Putting in the list of braking points the nodes of the Arc where the train currently is
	for (int p = ArcPos; p < BS[BlockPos].total_arcs; p++) {
		// Setting the current Arc speedInBraking to the final Node of Arc
		BS[BlockPos].arcs_in_signalling_block_section[p].endNode.arcSpeedLimit = BS[BlockPos].arcs_in_signalling_block_section[p].speedInBraking;
		BrakingPoints.push_back(BS[BlockPos].arcs_in_signalling_block_section[p].endNode);
	}

	// Calculating the total number of breaking points to find the minimum breaking distance (all the arcs of the block section which follows the occupied one are also considered)
	int Cons_Block = 8;
	if (BlockPos + Cons_Block > Blocks - 1) {
		Cons_Block = Blocks - BlockPos - 1;
	}

	// Putting in list of Braking Points all the nodes belonging to the other block sections
	for (int z = 0; z < Cons_Block; z++) {
		for (int p = 0; p < BS[z + BlockPos + 1].total_arcs; p++) {
			BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].endNode.arcSpeedLimit = BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].speedInBraking;
			BrakingPoints.push_back(BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].endNode);
		}
	}

	// Identification of the closest EoA where the train needs to brake at (i.e. the EoA for which the train needs to brake the earliest to meet it)
	for (int z = 0; z <= Cons_Block; z++) {
		// A SignalFailure EoA applies at every signalling level, so scan the MA list on any section carrying one
		bool hasSignalFailureMa = false;
		for (list<MovementAuthority>::iterator it = run_->etcsMa.begin(); it != run_->etcsMa.end(); it++) {
			if ((it->type == "SignalFailure") && (it->BSID == BS[z + BlockPos].ID) && obeysAuthority(*it)) {
				hasSignalFailureMa = true;
				break;
			}
		}
		if (isMovingBlockLevel(BS[z + BlockPos].SignallingLevel) || hasSignalFailureMa) {
			// Identifying Braking Points computed by the EVC if the train is virtually coupled to a train ahead
			if (IsTrainInFollowingMode == 1) { // if the train is in following mode
				if ((Predicted_MA_To_DecoupleAt.TrainInfo.trainDescription != "None") && (Predicted_MA_To_DecoupleAt.TrainInfo.trainDescription == LeadingTrainInFollowingMode)) {
					if (BS[z + BlockPos].ID == Predicted_MA_To_DecoupleAt.BSID) {
						Node DecouplingBrakingPoint;
						DecouplingBrakingPoint.X = Predicted_MA_To_DecoupleAt.RelativePosEoA;
						DecouplingBrakingPoint.isSignalled = true;
						DecouplingBrakingPoint.stationName = "DecoupleTrain"; // For a Decoupling Point as a convention we say that it has a station name DecoupleTrain. We did so not to increase the number of variables of the class Node
						BrakingPoints.push_back(DecouplingBrakingPoint);
					}
				}
			}
			// Now identifying braking points due to ETCS MA
			if (run_->etcsMa.size() > 0) {
				for (list<MovementAuthority>::iterator it = run_->etcsMa.begin(); it != run_->etcsMa.end(); it++) {
					if ((it->BSID == BS[z + BlockPos].ID) && (it->TrainInfo.trainDescription != trainDescription) && obeysAuthority(*it)) {
						double AbscissaToBrakeAtForEoA = -1;	// This is the progressive the train needs to start braking at to meet the EoA
						double SpeedForEoA = -1;				// This is the speed to be met to respect the MA
						bool BrakingForEoAModified = false;		// This variable is true when we modify the BrakingPoint because of a prediction distance to allow the train to couple with the one ahead
						bool CanTrainLinkForCoupling = false;	// This variable is a local variable to state whether the train is linking to another train ahead for virtual coupling. This boolean is true only when the closest breaking point is the Train End of a train in the same direction having the next Route section in common on a Bs with Signalling Level 4
						double PredictedDistanceToCoupling = 0; // This is the Distance need by the train to Couple with the train ahead

						ETCSBrakingPoint.X = authorityRoutePosition(*it, BS[z + BlockPos]);
						// A behind or front end cannot become a forward leader through virtual projection.
						if (BS[z + BlockPos].SignallingLevel == levelValue(SignallingLevel::VirtualCoupling) && it->type == "TrainEnd"
							&& it->ReversedDirection == run_->infra.routes[indexOfRoute].reversed_direction
							&& (it->typePart != "Tale" || !(ETCSBrakingPoint.X > S)))
							continue;

						if (it->type == "SignalFailure") {
							// a failed signal or a held single-track section is a stop target at every signalling level
							SpeedForEoA = 0;
						} else if (BS[z + BlockPos].SignallingLevel == levelValue(SignallingLevel::EtcsLevel3)) {
							SpeedForEoA = 0;

						} else if (BS[z + BlockPos].SignallingLevel == levelValue(SignallingLevel::VirtualCoupling)) {
							string NextBlockID;
							if (z + BlockPos == run_->infra.routes[this->indexOfRoute].N_Block_Sections - 1) { // if the block is the last of the train route then the ID of the Next Block is None
								NextBlockID = "None";
							} else {
								NextBlockID = BS[z + BlockPos + 1].ID;
							}
							if (it->type == "DivergingSwitchOccupied") {
								if ((it->TrainInfo.NextSectionID != NextBlockID) || ((it->TrainInfo.CurrentSectionID != BS[z + BlockPos].ID) && (it->TrainInfo.NextSectionID == NextBlockID))) {
									SpeedForEoA = 0;
								}
							} else if (it->type == "TrainEnd") {
								// In case the MA is a Train End the Speed shall be 0 if the train ahead is going in the opposite direction
								if (it->ReversedDirection != run_->infra.routes[this->indexOfRoute].reversed_direction) {
									SpeedForEoA = 0;

								}
								// Otherwise
								else {
									if ((it->TrainInfo.NextSectionID == NextBlockID) || (it->TrainInfo.NextSectionID == "None")) { // when the trains have the same next block section or the leading train is approahing the end of its route
										SpeedForEoA = it->TrainInfo.TrainSpeed;													   // the speed is the one of the train ahead if the trains are following the same direction and ahve at least one more block section in common

										if ((this->IsTrainInFollowingMode == 0) && (it->TrainInfo.Acceleration < 0)) {
											// A leader that brakes stops where this train would stop from the speed of the leader. A train that is not coupled yet
											// stops behind that point instead of matching the speed of the leader. Without a braking distance (-1) the end of
											// authority stays where the leader reported it.
											const double LeaderBrakingStart = BrakDist_Block(it->TrainInfo.TrainSpeed, 0, ETCSBrakingPoint.X, BS, Blocks);
											// The inverse ends at X - 0.002; its holdback is not forward travel.
											const double LeaderStandstill = LeaderBrakingStart < 0 || it->TrainInfo.TrainSpeed == 0
												? ETCSBrakingPoint.X
												: std::min(ETCSBrakingPoint.X + ((ETCSBrakingPoint.X - 0.002) - LeaderBrakingStart), run_->infra.routes[this->indexOfRoute].x_of_end_node * 1000);
											PredictedDistanceToCoupling = LeaderStandstill - ETCSBrakingPoint.X;
											ETCSBrakingPoint.X = LeaderStandstill;
											BrakingForEoAModified = true;
											SpeedForEoA = 0;
										} else if ((this->IsTrainInFollowingMode == 0) || ((this->IsInUnintentionalDecoupling == 1) && (it->TrainInfo.Acceleration >= 0))) { // if the train is not yet in following mode then
											if (V > SpeedForEoA) {
												CanTrainLinkForCoupling = true;									   // This is the only case when trains can start linking to be virtually coupled, if they are not coupled already
												double TimeToBrake = ((V - SpeedForEoA) / max_train_decelaration); // +(1 * timestep);  //This is the time needed by the train to reach the speed of the train ahead, plus 1 is needed to cover up for the integration delay of 1 step
												PredictedDistanceToCoupling = it->TrainInfo.TrainSpeed * TimeToBrake;
												// With the statement below trains will always try to couple with a train ahead if that is possible
												if ((ETCSBrakingPoint.X + PredictedDistanceToCoupling) <= run_->infra.routes[this->indexOfRoute].x_of_end_node * 1000) { // if with the predicted distance the Braking Point is still within the limits of train route then
													ETCSBrakingPoint.X = ETCSBrakingPoint.X + PredictedDistanceToCoupling;												 // Change the distance of the BrakingPoint. We chang it here so that the function can also detect other EoA for other train that might pop up between the real BrakingPoint.X and the prediction distance
													BrakingForEoAModified = true;
												}
											}
										}
									} else { // or if the trains do not have the next block section in common the trains need to split apart and this train will separate from the previous by means of an absolute braking distance
										SpeedForEoA = 0;
									}
								}
							}
						}

						if (SpeedForEoA > -1) { // if we have a valid MA and the SpeedForEoa is therefore different from -1
							// let us compute the approximate abscissa to Brake at for EoA
							AbscissaToBrakeAtForEoA = ETCSBrakingPoint.X - (pow(V, 2) - pow(SpeedForEoA, 2) / (2 * this->max_train_decelaration));

							if ((ETCSBrakingPoint.X > S) && (AbscissaToBrakeAtForEoA < ClosestPointToBrakeForETCS)) {
								ClosestPointToBrakeForETCS = AbscissaToBrakeAtForEoA; // if the AbscissaToBrakeAtForEoA is lower then ClosestPointToBrakeForETCS then this one becomes the ClosestPointToBrakeAt
								TargetSpeedForMA = SpeedForEoA;						  // The TargetSpeed for MA becomes the Speed ForEoA
								ClosestETCSBrakingPoint = ETCSBrakingPoint;
								ClosestETCSBrakingPoint.isSignalled = true; // putting isSignalled to true, gives a way to the Braking point to be recognised as an End of Authority
								SignallingLevelClosestETCSBrakingPoint = BS[z + BlockPos].SignallingLevel;

								// if the train can link with a train ahead
								ClosestETCSBrakingPoint.virtualCouplingNode = CanTrainLinkForCoupling;
								// Assigning the last received MA
								this->Last_Received_MA = *it;
								Last_Received_MA.RelativePosEoA = ETCSBrakingPoint.X; // Setting the Relative position of the EoA on the route of the train
								// In case the distance of ETCSBrakingPoint was modified then we need to subtract the Predicted Distance from it
								if (BrakingForEoAModified == 1) {
									Last_Received_MA.RelativePosEoA = ETCSBrakingPoint.X - PredictedDistanceToCoupling;
								}
								if (ClosestETCSBrakingPoint.virtualCouplingNode == 1) { // In case the train is not in following mode and can couple at a train ahead
									// Assigning the Predicted MA to link at
									this->Predicted_MA_To_CoupleAt = *it;
									Predicted_MA_To_CoupleAt.RelativePosEoA = ETCSBrakingPoint.X;
								}
							}
						}
					}
				}
			}
		}
	}
	// if there is a Closest ETCS Braking point that has been assigned, then add it to the list of braking points
	if (ClosestETCSBrakingPoint.X != 9999999999) {
		BrakingPoints.push_back(ClosestETCSBrakingPoint);
	}

	// Resolve each collected target once, before deciding whether the incumbent is still live.
	struct Target {
		Node node;
		double position, speed;
	};
	std::vector<Target> targets;
	int incumbent = -1;
	for (const Node& node : BrakingPoints) {
		const double position = node.isSignalled ? node.X : node.X * 1000;
		const double speed = node.isSignalled && node.stationName == "DecoupleTrain" ? 0
			: node.isSignalled														 ? TargetSpeedForMA
																					 : node.arcSpeedLimit;
		targets.push_back({node, position, speed});
		if (position > S && position == Xob[PreviousInstant] && speed == Vob[PreviousInstant])
			incumbent = static_cast<int>(targets.size()) - 1;
	}
	const Arc& arc = BS[BlockPos].arcs_in_signalling_block_section[ArcPos];
	const int successor = incumbent < 0 ? -1 : cachedBrakingSuccessor(PreviousInstant, arc, targets[incumbent].position, targets[incumbent].speed);
	const double proposedSpeed = successor < 0 ? V : Vbrak[successor];
	const double proposedHead = successor < 0 ? S : Sbrak[successor];
	int selected = -1;
	for (int index = 0; index < static_cast<int>(targets.size()); ++index) {
		const Target& target = targets[index];
		if (target.position <= S || (target.speed > 0 && proposedSpeed <= target.speed))
			continue;
		// A retained stop cannot cross a target beyond its zero-speed endpoint.
		if (successor >= 0 && targets[incumbent].speed == 0 && Vbrak[BrakStep] == 0
			&& Sbrak[BrakStep] < target.position)
			continue;
		double point = BrakDist_Block(proposedSpeed, target.speed, target.position, BS, Blocks);
		if (successor >= 0) {
			if (index == incumbent)
				continue;
			if (target.speed == 0) {
				double end;
				const double anchor = target.position - 0.002;
				const double roundoff = 8 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::fabs(anchor));
				if (predictBraking(proposedHead, proposedSpeed, 0, BS, Blocks, end) && end <= anchor + roundoff)
					continue;
			} else if (std::isfinite(point) && point != -1 && proposedHead < point)
				continue;
			// A missing prediction cannot establish that the proposed successor is safe.
			if (!std::isfinite(point) || point == -1)
				point = S;
		} else if (target.node.isSignalled && target.speed == 0 && S + V * timestep >= target.position
			&& (point == -1 || point > S))
			point = S;
		if (point != -1 && point < MinBX) {
			MinBX = point;
			selected = index;
		}
	}
	// An incompatible successor must be interrupted now, before any nominal acceleration.
	if (successor >= 0 && selected >= 0)
		MinBX = std::min(MinBX, S);
	if (successor >= 0 && selected < 0) {
		selected = incumbent;
		continueCached = true;
		MinBX = BrakDist_Block(V, targets[incumbent].speed, targets[incumbent].position, BS, Blocks);
		if (MinBX == -1)
			MinBX = S;
	}
	if (selected >= 0) {
		const Target& target = targets[selected];
		Xobmin = target.position;
		Vobmin = target.speed;
		BrakingForEoA = target.node.isSignalled;
		IsTrainCoupling = target.node.virtualCouplingNode;
		if (IsTrainInFollowingMode == 1)
			IsTrainDecoupling = target.node.stationName == "DecoupleTrain";
	}
	return MinBX;
}

bool Train::DrawBrakingCurve(double V1, double V2, double X0, Section* BS, int Blocks) {
	std::vector<double> positions{X0 - 0.002}, speeds{V2};
	BrakStep = -1;
	if (V1 <= V2)
		return false;
	for (int step = 1; step < 2000; ++step) {
		double head, speed;
		if (!inverseBrakingStep(positions.back(), speeds.back(), BS, Blocks, head, speed))
			return false;
		positions.push_back(head);
		speeds.push_back(speed);
		if (speed >= V1) {
			BrakStep = step;
			std::reverse_copy(positions.begin(), positions.end(), Sbrak.begin());
			std::reverse_copy(speeds.begin(), speeds.end(), Vbrak.begin());
			return true;
		}
	}
	return false;
}

bool Train::planBraking(double head, double speed, const Arc& arc, Section* sections, int count) {
	const double mass = total_train_mass * massFactor;
	const double resistance = total_train_resistances(speed, arc.gradient, arc.curvature);
	double low = brakingCurveSpeed(speed, arc);
	double high = std::max(0.0, speed + (tractiveEffort(speed) - resistance) / mass * timestep);
	const double anchor = Xobmin - 0.002;
	const double roundoff = 8 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::fabs(anchor));
	double end;
	if (!std::isfinite(low) || !std::isfinite(high) || high < low
		|| !predictBraking(head + low * timestep, low, Vobmin, sections, count, end) || end > anchor + roundoff)
		return false;
	if (std::fabs(end - anchor) > roundoff) {
		const bool completeUpper = predictBraking(head + high * timestep, high, Vobmin, sections, count, end);
		if ((!completeUpper && !(end > anchor)) || end < anchor)
			return false;
		for (int iteration = 0; iteration < 64; ++iteration) {
			const double middle = low + (high - low) / 2;
			if (middle == low || middle == high)
				break;
			if (!predictBraking(head + middle * timestep, middle, Vobmin, sections, count, end) && !(end > anchor))
				return false;
			if (end <= anchor)
				low = middle;
			else
				high = middle;
		}
	}
	std::vector<double> positions{head}, speeds{speed};
	if (!predictBraking(head + low * timestep, low, Vobmin, sections, count, end, &positions, &speeds))
		return false;
	// Roundoff only. A gap caused by an arc discontinuity is not a feasible stop.
	if (Vobmin == 0 && std::fabs(end - anchor) > roundoff)
		return false;
	if (positions.size() > Sbrak.size())
		return false;
	std::copy(positions.begin(), positions.end(), Sbrak.begin());
	std::copy(speeds.begin(), speeds.end(), Vbrak.begin());
	BrakStep = static_cast<int>(positions.size()) - 1;
	return true;
}

void Train::brakingStep(int time_seconds, const Arc& As, Section* BS, int Blocks) {
	const double head = instant_spatial_position[time_seconds - 1];
	const double speed = instant_train_speed[time_seconds - 1];
	const bool changedTarget = time_seconds - 1 < static_cast<int>(Xob.size())
		&& (Xob[time_seconds - 1] != Xobmin || Vob[time_seconds - 1] != Vobmin);
	// Retain a native or forward plan only at a real stored state, never by interpolation.
	const int successor = cachedBrakingSuccessor(time_seconds - 1, As, Xobmin, Vobmin);
	if (successor >= 0) {
		instant_train_speed[time_seconds] = Vbrak[successor];
		instant_spatial_position[time_seconds] = Sbrak[successor];
		brakingPoint = successor;
		Eq[time_seconds] = 52;
		GradientExceptionInBraking = Vbrak[successor] < Vobmin && successor < BrakStep;
		return;
	}
	const bool newCurve = counter == 0 || changedTarget;
	const double coverage = std::max(speed, std::max(0.0, speed + (tractiveEffort(speed) - total_train_resistances(speed, As.gradient, As.curvature)) / (total_train_mass * massFactor) * timestep));
	const bool established = !newCurve || DrawBrakingCurve(coverage, Vobmin, Xobmin, BS, Blocks);
	int remaining = BrakStep;
	for (int k = 0; k < BrakStep; ++k)
		if (head >= Sbrak[k] && head < Sbrak[k + 1]) {
			remaining = BrakStep - k;
			break;
		}
	const bool planned = established && (Vobmin > 0 ? planPositiveBraking(head, speed, As, BS, Blocks, remaining) : planBraking(head, speed, As, BS, Blocks));
	if (planned) {
		instant_train_speed[time_seconds] = Vbrak[1];
		instant_spatial_position[time_seconds] = Sbrak[1];
		brakingPoint = 1;
		Eq[time_seconds] = newCurve ? 55 : 53;
		counter = 1;
		GradientExceptionInBraking = Vbrak[1] < Vobmin && BrakStep > 1;
		return;
	}
	fullBrakingStep(time_seconds, As);
	counter = 0;
	BrakStep = -1;
}

void Train::fullBrakingStep(int time_seconds, const Arc& As) {
	const double speed = instant_train_speed[time_seconds - 1];
	const double mass = total_train_mass * massFactor;
	const double deceleration = (mass * max_train_decelaration + total_train_resistances(speed, As.gradient, As.curvature)) / mass;
	instant_train_speed[time_seconds] = std::max(0.0, speed - deceleration * timestep);
	instant_spatial_position[time_seconds] = instant_spatial_position[time_seconds - 1] + (speed + instant_train_speed[time_seconds]) / 2 * timestep;
	GradientExceptionInBraking = false;
	Eq[time_seconds] = 54;
}
