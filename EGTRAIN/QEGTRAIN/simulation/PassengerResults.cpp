#include "simulation/PassengerResults.h"
#include "simulation/RunOutputFile.h"
#include "simulation/SimulationRun.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <map>
#include <utility>

using namespace std;

namespace {
// A time or duration: whole seconds as an integer, anything else with three decimals.
string formatSeconds(double value) {
	if (value == floor(value) && fabs(value) < 1e15)
		return to_string(static_cast<long long>(value));
	return formatRatio(value);
}

// A mean that may be absent, with three decimals.
string formatMeanOrDash(bool present, double value) {
	return present ? formatRatio(value) : "-";
}

// The trains of the legs that the passenger rode, joined by "+", or "-" when it rode none.
string ridden(const Journey& journey, double& inVehicle) {
	string trains;
	inVehicle = 0;
	for (const Trip& trip : journey.Trips) {
		if (!trip.IsTripCompleted || trip.Actual_Boarding_Time == -9999 || trip.Actual_Arrival_Time == -9999)
			continue;
		trains += (trains.empty() ? "" : "+") + trip.TrainServiceDescription;
		inVehicle += trip.Actual_Arrival_Time - trip.Actual_Boarding_Time;
	}
	return trains.empty() ? "-" : trains;
}

const char* modeName(JourneyMode mode) {
	return mode == JourneyMode::Live ? "live" : "assigned";
}

string journeyRow(const Passenger& passenger, const Journey& journey) {
	double inVehicle = 0;
	const string trains = ridden(journey, inVehicle);
	const bool completed = journey.IsJourneyCompleted;
	string row = passenger.ID + "\t" + journey.ID + "\t" + modeName(journey.Mode) + "\t" + journey.Dep_Station_ID + "\t" + journey.Arr_Station_ID;
	row += "\t" + (journey.IsJourneyStarted ? formatSeconds(journey.Actual_Departure_Time) : string("-1"));
	row += "\t" + trains + "\t" + formatSeconds(journey.Waitingtime) + "\t" + formatSeconds(journey.Walkingtime) + "\t" + formatSeconds(inVehicle);
	row += "\t" + (completed ? formatSeconds(journey.Actual_Arrival_Time) : string("-"));
	row += "\t" + (completed ? formatSeconds(journey.totalJourneyArrivalDelay) : string("-"));
	row += string("\t") + (journey.IsReplanned ? "1" : "0") + "\t" + journeyOutcomeName(journey.Outcome) + "\n";
	return row;
}

// The statistics of one pair from the journeys collected for it.
struct PairJourneys {
	int journeys = 0, noRoute = 0;
	vector<double> waits, travels, delays; // of the completed journeys
};

double mean(const vector<double>& values) {
	double sum = 0;
	for (const double value : values)
		sum += value;
	return sum / static_cast<double>(values.size());
}

string stopRow(const SimulationRun& run, int trainIndex, int stop) {
	const Train& train = run.trains[static_cast<size_t>(trainIndex)];
	const StopCallCounts* found = run.stopCalls.find(trainIndex, stop);
	const StopCallCounts counts = found != nullptr ? *found : StopCallCounts();
	const double start = run.settings.startingSimulationTime;
	const double departure = train.StationDepartures[stop];
	const Node& node = train.Stations[static_cast<size_t>(stop)];
	string row = train.trainDescription + "\t" + node.stationName + "\t" + node.stationPlatformId;
	row += "\t" + (train.StationStandStarts[stop] >= 0 ? formatSeconds(train.StationStandStarts[stop] + start) : string("-1")) + "\t" + (departure >= 0 ? formatSeconds(departure + start) : string("-1"));
	row += "\t" + to_string(counts.boarded) + "\t" + to_string(counts.alighted) + "\t" + to_string(counts.leftBehind) + "\t" + to_string(counts.loadAfter);
	row += "\t" + to_string(train.MAX_OnBoard_Passengers) + "\t";
	row += train.MAX_OnBoard_Passengers > 0 ? formatRatio(static_cast<double>(counts.loadAfter) / train.MAX_OnBoard_Passengers) : string("-");
	return row + "\n";
}

// The call of a train at a stop, with the counts so far.
GuiStationCall callOf(const SimulationRun& run, int trainIndex, int stop) {
	const Train& train = run.trains[static_cast<size_t>(trainIndex)];
	const StopCallCounts* found = run.stopCalls.find(trainIndex, stop);
	const StopCallCounts counts = found != nullptr ? *found : StopCallCounts();
	GuiStationCall call;
	call.station = train.Stations[static_cast<size_t>(stop)].stationName;
	call.train = train.operatingCode.empty() ? train.trainDescription : train.operatingCode;
	call.arrival = static_cast<int>(train.StationStandStarts[stop]);
	call.departure = train.StationDepartures[stop] >= 0 ? static_cast<int>(train.StationDepartures[stop]) : -1;
	call.clock = call.arrival + static_cast<int>(run.settings.startingSimulationTime);
	call.boarded = counts.boarded;
	call.alighted = counts.alighted;
	call.leftBehind = counts.leftBehind;
	call.load = counts.loadAfter;
	return call;
}

// The place of a call that began: train index, stop index and the step it began.
struct CallRef {
	int train, stop, arrival;
};

vector<CallRef> beganCalls(const SimulationRun& run, int step) {
	vector<CallRef> refs;
	const int trains = min(run.numTrains, static_cast<int>(run.trains.size()));
	for (int n = 0; n < trains; ++n) {
		const Train& train = run.trains[static_cast<size_t>(n)];
		for (int stop = 0; stop < train.numStations && stop < Train::kMaxTimetableStations; ++stop) {
			const double began = train.StationStandStarts[stop];
			if (train.Stations[static_cast<size_t>(stop)].StepStopped > 0 && began >= 0 && began <= step
				&& !train.Stations[static_cast<size_t>(stop)].stationName.empty())
				refs.push_back({n, stop, static_cast<int>(began)});
		}
	}
	stable_sort(refs.begin(), refs.end(), [](const CallRef& a, const CallRef& b) { return a.arrival < b.arrival; });
	return refs;
}

void writeText(const string& path, const string& text) {
	ofstream file = openRunOutputFile(path, ios::binary);
	file << text;
	closeRunOutputFile(file, path);
}
} // namespace

string formatRatio(double value) {
	char buffer[64];
	snprintf(buffer, sizeof buffer, "%.3f", value);
	return buffer;
}

StopCallCounts& StopCallLog::at(int train, int stop) {
	if (static_cast<size_t>(train) >= calls_.size())
		calls_.resize(static_cast<size_t>(train) + 1);
	vector<Entry>& stops = calls_[static_cast<size_t>(train)];
	if (static_cast<size_t>(stop) >= stops.size())
		stops.resize(static_cast<size_t>(stop) + 1);
	Entry& entry = stops[static_cast<size_t>(stop)];
	entry.met = true;
	return entry.counts;
}

const StopCallCounts* StopCallLog::find(int train, int stop) const {
	if (static_cast<size_t>(train) >= calls_.size() || static_cast<size_t>(stop) >= calls_[static_cast<size_t>(train)].size())
		return nullptr;
	const Entry& entry = calls_[static_cast<size_t>(train)][static_cast<size_t>(stop)];
	return entry.met ? &entry.counts : nullptr;
}

vector<GuiStationCall> stationCalls(const SimulationRun& run) {
	vector<GuiStationCall> calls;
	for (const CallRef& ref : beganCalls(run, numeric_limits<int>::max()))
		calls.push_back(callOf(run, ref.train, ref.stop));
	return calls;
}

vector<GuiStationCall> latestStationCalls(const SimulationRun& run, int step) {
	const vector<CallRef> refs = beganCalls(run, step);
	vector<GuiStationCall> latest;
	forLatestStationCalls(
		refs.rbegin(), refs.rend(),
		[&run](const CallRef& ref) -> const string& {
			return run.trains[static_cast<size_t>(ref.train)].Stations[static_cast<size_t>(ref.stop)].stationName;
		},
		[&](const CallRef& ref) { latest.push_back(callOf(run, ref.train, ref.stop)); });
	return latest;
}

double nearestRankPercentile(vector<double> values, int p) {
	sort(values.begin(), values.end());
	const size_t n = values.size();
	const size_t rank = (static_cast<size_t>(p) * n + 99) / 100; // ceil(p * n / 100) in integers
	return values[max<size_t>(rank, 1) - 1];
}

vector<OriginDestinationSummary> summariseOriginDestinations(const list<Passenger>& passengers) {
	map<pair<string, string>, PairJourneys> pairs;
	for (const Passenger& passenger : passengers) {
		for (const Journey& journey : passenger.Journeys) {
			PairJourneys& pair = pairs[{journey.Dep_Station_ID, journey.Arr_Station_ID}];
			++pair.journeys;
			if (journey.Outcome == JourneyOutcome::NoRoute)
				++pair.noRoute;
			if (!journey.IsJourneyCompleted)
				continue;
			pair.waits.push_back(journey.Waitingtime);
			pair.travels.push_back(journey.Actual_Arrival_Time - journey.Actual_Departure_Time);
			pair.delays.push_back(journey.totalJourneyArrivalDelay);
		}
	}
	vector<OriginDestinationSummary> summaries;
	for (const auto& entry : pairs) {
		const PairJourneys& pair = entry.second;
		OriginDestinationSummary summary;
		summary.origin = entry.first.first;
		summary.destination = entry.first.second;
		summary.journeys = pair.journeys;
		summary.noRoute = pair.noRoute;
		summary.completed = static_cast<int>(pair.waits.size());
		if (summary.completed > 0) {
			summary.meanWait = mean(pair.waits);
			summary.p95Wait = nearestRankPercentile(pair.waits, 95);
			summary.meanTravel = mean(pair.travels);
			summary.meanDelay = mean(pair.delays);
		}
		summaries.push_back(summary);
	}
	return summaries;
}

namespace {
const char* const kJourneysHeader = "PaxID\tJourneyID\tMode\tOrigin\tDestination\tAppeared\tTrains\tWait\tWalk\tInVehicle\tArrival\tDelay\tReplanned\tOutcome\n";

// Writes the rows to the file as they are made, so the whole text is never in memory.
void writePassengerJourneys(const string& path, const list<Passenger>& passengers) {
	ofstream file = openRunOutputFile(path, ios::binary);
	file << kJourneysHeader;
	for (const Passenger& passenger : passengers)
		for (const Journey& journey : passenger.Journeys)
			file << journeyRow(passenger, journey);
	closeRunOutputFile(file, path);
}
} // namespace

string passengerJourneysText(const list<Passenger>& passengers) {
	string text = kJourneysHeader;
	for (const Passenger& passenger : passengers)
		for (const Journey& journey : passenger.Journeys)
			text += journeyRow(passenger, journey);
	return text;
}

string stationStopsText(const SimulationRun& run) {
	string text = "Train\tStation\tPlatform\tArrival\tDeparture\tBoarded\tAlighted\tLeftBehind\tLoadAfterDeparture\tCapacity\tLoadFactor\n";
	for (int n = 0; n < run.numTrains; ++n) {
		const Train& train = run.trains[static_cast<size_t>(n)];
		for (int stop = 0; stop < train.numStations && stop < Train::kMaxTimetableStations; ++stop)
			if (train.Stations[static_cast<size_t>(stop)].StepStopped > 0)
				text += stopRow(run, n, stop);
	}
	return text;
}

string originDestinationSummaryText(const list<Passenger>& passengers) {
	string text = "Origin\tDestination\tJourneys\tCompleted\tNoRoute\tMeanWait\tP95Wait\tMeanTravel\tMeanDelay\n";
	for (const OriginDestinationSummary& s : summariseOriginDestinations(passengers)) {
		const bool any = s.completed > 0;
		text += s.origin + "\t" + s.destination + "\t" + to_string(s.journeys) + "\t" + to_string(s.completed) + "\t" + to_string(s.noRoute);
		text += "\t" + formatMeanOrDash(any, s.meanWait) + "\t" + (any ? formatSeconds(s.p95Wait) : string("-"));
		text += "\t" + formatMeanOrDash(any, s.meanTravel) + "\t" + formatMeanOrDash(any, s.meanDelay) + "\n";
	}
	return text;
}

void printPassengerResultFiles(const SimulationRun& run, const string& mainFolder) {
	writePassengerJourneys(mainFolder + "/PassengerJourneys.txt", run.passengers);
	writeText(mainFolder + "/StationStops.txt", stationStopsText(run));
	writeText(mainFolder + "/ODSummary.txt", originDestinationSummaryText(run.passengers));
}
