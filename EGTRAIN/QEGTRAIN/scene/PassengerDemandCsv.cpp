#include "scene/PassengerDemandCsv.h"

#include "util/TimeFormat.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace {

constexpr int kDaySeconds = 86400;

std::string trimmed(const std::string& text) {
	const std::size_t first = text.find_first_not_of(" \t");
	if (first == std::string::npos)
		return {};
	return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

std::string lowerAscii(const std::string& text) {
	std::string out;
	out.reserve(text.size());
	for (unsigned char ch : text)
		out.push_back(static_cast<char>(std::tolower(ch)));
	return out;
}

// Splits one CSV line into trimmed fields, supporting RFC 4180 double-quoted fields, and drops empty trailing fields.
// Returns false on a malformed quoted field (unclosed quote or characters after the closing quote).
bool splitCsvLine(const std::string& line, std::vector<std::string>& cells) {
	cells.clear();
	std::size_t pos = 0;
	const std::size_t size = line.size();
	while (pos < size) {
		while (pos < size && (line[pos] == ' ' || line[pos] == '\t'))
			++pos;
		if (pos < size && line[pos] == '"') {
			++pos;
			std::string field;
			bool closed = false;
			while (pos < size) {
				if (line[pos] == '"') {
					if (pos + 1 < size && line[pos + 1] == '"') {
						field.push_back('"');
						pos += 2;
					} else {
						closed = true;
						++pos;
						break;
					}
				} else {
					field.push_back(line[pos]);
					++pos;
				}
			}
			if (!closed)
				return false;
			while (pos < size && (line[pos] == ' ' || line[pos] == '\t'))
				++pos;
			if (pos < size && line[pos] != ',')
				return false;
			cells.push_back(trimmed(field));
			if (pos < size && line[pos] == ',')
				++pos;
		} else {
			const std::size_t comma = line.find(',', pos);
			if (comma == std::string::npos) {
				cells.push_back(trimmed(line.substr(pos)));
				break;
			}
			cells.push_back(trimmed(line.substr(pos, comma - pos)));
			pos = comma + 1;
		}
	}
	while (!cells.empty() && cells.back().empty())
		cells.pop_back();
	return true;
}

// Escapes a field for CSV output if it contains a comma, quote or line break.
std::string escapeCsvField(const std::string& field) {
	if (field.find_first_of(",\"\r\n") == std::string::npos)
		return field;
	std::string quoted;
	quoted.reserve(field.size() + 2);
	quoted.push_back('"');
	for (char ch : field) {
		if (ch == '"')
			quoted.push_back('"');
		quoted.push_back(ch);
	}
	quoted.push_back('"');
	return quoted;
}

// The station id for a header or row label: an id first, then a name; empty when none matches.
std::string stationIdFor(const std::string& label, const std::vector<SceneStation>& stations) {
	for (const SceneStation& station : stations)
		if (station.id == label)
			return station.id;
	for (const SceneStation& station : stations)
		if (!station.name.empty() && station.name == label)
			return station.id;
	return {};
}

// A whole number of 0 or more, where empty text is 0; false for anything else.
bool parseCount(const std::string& text, int& count) {
	if (text.empty()) {
		count = 0;
		return true;
	}
	if (text.find_first_not_of("0123456789") != std::string::npos)
		return false;
	errno = 0;
	const long value = std::strtol(text.c_str(), nullptr, 10);
	if (errno != 0 || value > 1000000000L)
		return false;
	count = static_cast<int>(value);
	return true;
}

// Parses a non-negative integer of digits into value.
bool parseDigits(const std::string& text, int& value, int maxValue) {
	if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
		return false;
	errno = 0;
	const long parsed = std::strtol(text.c_str(), nullptr, 10);
	if (errno != 0 || parsed < 0 || parsed > maxValue)
		return false;
	value = static_cast<int>(parsed);
	return true;
}

// Parses HH:MM:SS, HH:MM or whole seconds since midnight, from 0 to 86400.
bool parseTimeSeconds(const std::string& text, int& seconds) {
	if (text.empty())
		return false;
	const std::size_t firstColon = text.find(':');
	if (firstColon == std::string::npos)
		return parseDigits(text, seconds, kDaySeconds);
	const std::size_t secondColon = text.find(':', firstColon + 1);
	if (secondColon != std::string::npos && text.find(':', secondColon + 1) != std::string::npos)
		return false;
	const std::string hourPart = text.substr(0, firstColon);
	const std::string minutePart = secondColon == std::string::npos
		? text.substr(firstColon + 1)
		: text.substr(firstColon + 1, secondColon - firstColon - 1);
	const std::string secondPart = secondColon == std::string::npos ? std::string("00") : text.substr(secondColon + 1);
	if (hourPart.empty() || hourPart.size() > 2 || minutePart.size() != 2 || secondPart.size() != 2)
		return false;
	int hours = 0;
	int minutes = 0;
	int secs = 0;
	if (!parseDigits(hourPart, hours, 24) || !parseDigits(minutePart, minutes, 59) || !parseDigits(secondPart, secs, 59))
		return false;
	const int total = hours * 3600 + minutes * 60 + secs;
	if (total > kDaySeconds)
		return false;
	seconds = total;
	return true;
}

// Non-empty CSV lines with their 1-based line numbers; malformed quoted lines are recorded through onError.
std::vector<std::pair<int, std::vector<std::string>>> readCsvLines(
	const std::string& text, const std::function<void(int, const std::string&)>& onError) {
	std::string body = text;
	if (body.rfind("\xEF\xBB\xBF", 0) == 0)
		body.erase(0, 3);
	std::vector<std::pair<int, std::vector<std::string>>> lines;
	std::istringstream stream(body);
	std::string line;
	for (int number = 1; std::getline(stream, line); ++number) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		std::vector<std::string> cells;
		if (!splitCsvLine(line, cells)) {
			onError(number, "Unclosed or malformed quoted field");
			continue;
		}
		if (!cells.empty())
			lines.emplace_back(number, std::move(cells));
	}
	return lines;
}

} // namespace

PassengerDemandCsv readPassengerDemandCsv(const std::string& text, const std::vector<SceneStation>& stations) {
	PassengerDemandCsv result;
	const auto error = [&result](int number, const std::string& message) {
		result.errors.push_back("Line " + std::to_string(number) + ": " + message);
	};
	const std::vector<std::pair<int, std::vector<std::string>>> lines = readCsvLines(text, error);

	std::size_t next = 0;
	if (next < lines.size() && lines[next].second[0] == "Time Reference") {
		const auto& [number, cells] = lines[next];
		char* end = nullptr;
		const double period = cells.size() == 2 ? std::strtod(cells[1].c_str(), &end) : 0.0;
		if (cells.size() != 2 || end == cells[1].c_str() || *end != '\0' || !(period > 0.0))
			error(number, "Time Reference must be followed by a length in seconds above 0");
		else
			result.periodSeconds = period;
		++next;
	}
	if (next >= lines.size()) {
		result.errors.push_back("The file has no header row with the destination stations");
		return result;
	}

	const int headerLine = lines[next].first;
	const std::vector<std::string>& header = lines[next].second;
	std::vector<std::string> destinations;
	std::set<std::string> seenDestinations;
	for (std::size_t column = 1; column < header.size(); ++column) {
		const std::string id = stationIdFor(header[column], stations);
		if (id.empty())
			error(headerLine, "Unknown destination station " + header[column]);
		else if (!seenDestinations.insert(id).second)
			error(headerLine, "Destination station " + header[column] + " appears twice");
		destinations.push_back(id);
	}
	if (destinations.empty())
		error(headerLine, "The header row names no destination station");

	std::set<std::string> seenOrigins;
	for (std::size_t row = next + 1; row < lines.size(); ++row) {
		const auto& [number, cells] = lines[row];
		const std::string origin = stationIdFor(cells[0], stations);
		if (origin.empty()) {
			error(number, "Unknown origin station " + cells[0]);
			continue;
		}
		if (!seenOrigins.insert(origin).second)
			error(number, "Origin station " + cells[0] + " appears twice");
		if (cells.size() - 1 > destinations.size())
			error(number, "The row has more counts than the header has stations");
		for (std::size_t column = 1; column < cells.size() && column - 1 < destinations.size(); ++column) {
			int count = 0;
			if (!parseCount(cells[column], count)) {
				error(number, "Count " + cells[column] + " for " + header[column] + " is not a whole number of 0 or more");
				continue;
			}
			const std::string& destination = destinations[column - 1];
			if (count == 0 || destination.empty())
				continue;
			if (destination == origin) {
				error(number, "Count for " + cells[0] + " to itself must be 0");
				continue;
			}
			result.cells.push_back({origin, destination, count});
		}
	}
	if (!result.errors.empty())
		result.cells.clear();
	return result;
}

std::string writePassengerDemandCsv(const ScenePassengerDemandSlice& slice, const std::vector<SceneStation>& stations) {
	std::map<std::pair<std::string, std::string>, int> counts;
	for (const ScenePassengerDemandCell& cell : slice.cells)
		counts[{cell.originStationId, cell.destinationStationId}] += cell.count;
	std::ostringstream out;
	out.precision(15);
	out << "Time Reference," << (slice.endSeconds - slice.startSeconds) << "\n";
	out << "O/D";
	for (const SceneStation& station : stations)
		out << ',' << escapeCsvField(station.id);
	out << '\n';
	for (const SceneStation& origin : stations) {
		out << escapeCsvField(origin.id);
		for (const SceneStation& destination : stations) {
			const auto found = counts.find({origin.id, destination.id});
			out << ',' << (found == counts.end() ? 0 : found->second);
		}
		out << '\n';
	}
	return out.str();
}

PassengerDemandTable readPassengerDemandTable(const std::string& text, const std::vector<SceneStation>& stations) {
	PassengerDemandTable result;
	const auto error = [&result](int number, const std::string& message) {
		result.errors.push_back("Line " + std::to_string(number) + ": " + message);
	};
	const std::vector<std::pair<int, std::vector<std::string>>> lines = readCsvLines(text, error);
	if (lines.empty()) {
		if (result.errors.empty())
			result.errors.push_back("The file has no header row");
		return result;
	}

	const int headerLine = lines[0].first;
	const std::vector<std::string>& header = lines[0].second;
	constexpr std::size_t kMissing = static_cast<std::size_t>(-1);
	std::size_t originCol = kMissing;
	std::size_t destinationCol = kMissing;
	std::size_t startCol = kMissing;
	std::size_t endCol = kMissing;
	std::size_t periodCol = kMissing;
	std::size_t countCol = kMissing;
	const auto assignColumn = [&](std::size_t& target, std::size_t column, const std::string& name) {
		if (target != kMissing)
			error(headerLine, "Column " + name + " appears twice");
		else
			target = column;
	};
	for (std::size_t column = 0; column < header.size(); ++column) {
		const std::string key = lowerAscii(header[column]);
		if (key == "origin")
			assignColumn(originCol, column, "origin");
		else if (key == "destination")
			assignColumn(destinationCol, column, "destination");
		else if (key == "start")
			assignColumn(startCol, column, "start");
		else if (key == "end")
			assignColumn(endCol, column, "end");
		else if (key == "period")
			assignColumn(periodCol, column, "period");
		else if (key == "count")
			assignColumn(countCol, column, "count");
	}
	if (originCol == kMissing)
		error(headerLine, "The header row is missing the origin column");
	if (destinationCol == kMissing)
		error(headerLine, "The header row is missing the destination column");
	if (startCol == kMissing)
		error(headerLine, "The header row is missing the start column");
	if (countCol == kMissing)
		error(headerLine, "The header row is missing the count column");
	if (endCol != kMissing && periodCol != kMissing)
		error(headerLine, "The header row cannot have both end and period columns");
	if (endCol == kMissing && periodCol == kMissing)
		error(headerLine, "The header row must have either an end or a period column");
	if (!result.errors.empty())
		return result;

	// Station order for deterministic output matching the scene.
	std::unordered_map<std::string_view, std::size_t> stationOrder;
	stationOrder.reserve(stations.size());
	for (std::size_t idx = 0; idx < stations.size(); ++idx)
		stationOrder.emplace(stations[idx].id, idx);

	std::unordered_map<std::string, std::size_t> unknownIndex;
	const auto recordUnknown = [&result, &unknownIndex](const std::string& name, int count) {
		const auto found = unknownIndex.find(name);
		if (found == unknownIndex.end()) {
			unknownIndex.emplace(name, result.unknownStations.size());
			result.unknownStations.push_back({name, 1, count});
		} else {
			result.unknownStations[found->second].rows += 1;
			result.unknownStations[found->second].passengers += count;
		}
	};

	struct SliceAccumulator {
		int firstLine = 0;
		std::map<std::pair<std::size_t, std::size_t>, long long> cells;
	};
	std::map<std::pair<int, int>, SliceAccumulator> slicesByPeriod;
	bool zeroRows = false;
	const auto fieldAt = [](const std::vector<std::string>& cells, std::size_t column) -> std::string {
		return column < cells.size() ? cells[column] : std::string();
	};

	for (std::size_t row = 1; row < lines.size(); ++row) {
		const int number = lines[row].first;
		const std::vector<std::string>& cells = lines[row].second;
		if (cells.size() > header.size()) {
			error(number, "The row has more columns than the header");
			continue;
		}
		const std::string originText = fieldAt(cells, originCol);
		const std::string destinationText = fieldAt(cells, destinationCol);
		const std::string startText = fieldAt(cells, startCol);
		const std::string countText = fieldAt(cells, countCol);

		int startSec = 0;
		int endSec = 0;
		bool timeOk = true;
		if (!parseTimeSeconds(startText, startSec)) {
			error(number, "Start time '" + startText + "' is not HH:MM:SS, HH:MM or seconds since midnight");
			timeOk = false;
		}
		if (endCol != kMissing) {
			const std::string endText = fieldAt(cells, endCol);
			if (!parseTimeSeconds(endText, endSec)) {
				error(number, "End time '" + endText + "' is not HH:MM:SS, HH:MM or seconds since midnight");
				timeOk = false;
			}
		} else {
			const std::string periodText = fieldAt(cells, periodCol);
			int periodSec = 0;
			if (!parseTimeSeconds(periodText, periodSec) || periodSec <= 0) {
				error(number, "Period '" + periodText + "' must be a positive length in seconds or HH:MM:SS");
				timeOk = false;
			} else if (timeOk) {
				endSec = startSec + periodSec;
				if (endSec > kDaySeconds) {
					error(number, "Start plus period exceeds 24:00:00");
					timeOk = false;
				}
			}
		}
		if (timeOk && endSec <= startSec) {
			error(number, "End time must be after start time");
			timeOk = false;
		}

		int count = 0;
		bool rowOk = timeOk;
		if (countText.empty() || !parseCount(countText, count)) {
			error(number, "Count '" + countText + "' is not a whole number of 0 or more");
			rowOk = false;
		}
		if (originText.empty()) {
			error(number, "Origin station is empty");
			rowOk = false;
		}
		if (destinationText.empty()) {
			error(number, "Destination station is empty");
			rowOk = false;
		}
		if (!rowOk)
			continue;

		const std::string originId = stationIdFor(originText, stations);
		const std::string destinationId = stationIdFor(destinationText, stations);
		if (originId.empty() || destinationId.empty()) {
			if (originId.empty())
				recordUnknown(originText, count);
			if (destinationId.empty() && !(originId.empty() && destinationText == originText))
				recordUnknown(destinationText, count);
			continue;
		}
		if (originId == destinationId) {
			error(number, "Origin and destination station " + originText + " must differ");
			continue;
		}
		if (count == 0) {
			zeroRows = true;
			continue;
		}

		auto& accumulator = slicesByPeriod[{startSec, endSec}];
		if (accumulator.firstLine == 0)
			accumulator.firstLine = number;
		accumulator.cells[{stationOrder[originId], stationOrder[destinationId]}] += count;
	}

	// Check for overlapping distinct periods (slicesByPeriod is ordered by start, then end).
	int maxEndSec = -1;
	int maxEndStartSec = 0;
	for (const auto& entry : slicesByPeriod) {
		const int startSec = entry.first.first;
		const int endSec = entry.first.second;
		if (maxEndSec > startSec) {
			error(entry.second.firstLine,
				"Period " + formatClockSeconds(startSec) + "-" + formatClockSeconds(endSec) + " overlaps "
					+ formatClockSeconds(maxEndStartSec) + "-" + formatClockSeconds(maxEndSec));
		}
		if (endSec > maxEndSec) {
			maxEndSec = endSec;
			maxEndStartSec = startSec;
		}
	}

	long long grandTotal = 0;
	int sliceNumber = 1;
	for (const auto& entry : slicesByPeriod) {
		ScenePassengerDemandSlice slice;
		slice.id = "slice-" + std::to_string(sliceNumber++);
		slice.startSeconds = static_cast<double>(entry.first.first);
		slice.endSeconds = static_cast<double>(entry.first.second);
		for (const auto& cellEntry : entry.second.cells) {
			grandTotal += cellEntry.second;
			slice.cells.push_back({stations[cellEntry.first.first].id, stations[cellEntry.first.second].id,
				static_cast<int>(cellEntry.second)});
		}
		result.slices.push_back(std::move(slice));
	}
	if (grandTotal > kMaxScenePassengerDemand) {
		result.errors.push_back("Total demand is " + std::to_string(grandTotal)
			+ " passengers, more than the limit of " + std::to_string(kMaxScenePassengerDemand));
	}
	// An import replaces all slices, so a table that gives none would delete all demand.
	if (result.errors.empty() && result.slices.empty()) {
		if (lines.size() == 1) {
			result.errors.push_back("The table has no rows below the header");
		} else if (result.unknownStations.empty()) {
			result.errors.push_back("Every row has a count of 0");
		} else {
			constexpr std::size_t kMaxNames = 10;
			const std::size_t shown = std::min(result.unknownStations.size(), kMaxNames);
			std::string names;
			for (std::size_t idx = 0; idx < shown; ++idx)
				names += (idx == 0 ? "" : ", ") + result.unknownStations[idx].name;
			if (result.unknownStations.size() > shown)
				names += " and " + std::to_string(result.unknownStations.size() - shown) + " more";
			result.errors.push_back(
				"Every row names an unknown station (" + names + ")" + (zeroRows ? " or has a count of 0" : ""));
		}
	}
	if (!result.errors.empty()) {
		result.slices.clear();
		result.totalPassengers = 0;
	} else {
		result.totalPassengers = grandTotal;
	}
	return result;
}

PassengerDemandTableText writePassengerDemandTable(
	const std::vector<ScenePassengerDemandSlice>& slices, const std::vector<SceneStation>& stations) {
	std::vector<const ScenePassengerDemandSlice*> ordered;
	ordered.reserve(slices.size());
	for (const ScenePassengerDemandSlice& slice : slices)
		ordered.push_back(&slice);
	std::sort(ordered.begin(), ordered.end(), [](const ScenePassengerDemandSlice* a, const ScenePassengerDemandSlice* b) {
		if (a->startSeconds != b->startSeconds)
			return a->startSeconds < b->startSeconds;
		if (a->endSeconds != b->endSeconds)
			return a->endSeconds < b->endSeconds;
		return a->id < b->id;
	});

	std::unordered_map<std::string_view, std::size_t> stationOrder;
	stationOrder.reserve(stations.size());
	for (std::size_t idx = 0; idx < stations.size(); ++idx)
		stationOrder.emplace(stations[idx].id, idx);

	PassengerDemandTableText result;
	std::ostringstream out;
	out << "origin,destination,start,end,count\n";
	for (const ScenePassengerDemandSlice* slice : ordered) {
		std::map<std::pair<std::size_t, std::size_t>, long long> counts;
		for (const ScenePassengerDemandCell& cell : slice->cells) {
			if (cell.count <= 0)
				continue;
			const auto originIt = stationOrder.find(cell.originStationId);
			const auto destinationIt = stationOrder.find(cell.destinationStationId);
			if (originIt == stationOrder.end() || destinationIt == stationOrder.end()) {
				++result.droppedCells;
				continue;
			}
			counts[{originIt->second, destinationIt->second}] += cell.count;
		}
		if (counts.empty()) {
			++result.emptySlices;
			continue;
		}
		for (const double seconds : {slice->startSeconds, slice->endSeconds})
			if (std::round(seconds) != seconds)
				++result.roundedTimes;
		const std::string startText = formatClockSeconds(std::llround(slice->startSeconds));
		const std::string endText = formatClockSeconds(std::llround(slice->endSeconds));
		for (const auto& cellEntry : counts) {
			out << escapeCsvField(stations[cellEntry.first.first].id) << ','
				<< escapeCsvField(stations[cellEntry.first.second].id) << ','
				<< startText << ',' << endText << ',' << cellEntry.second << '\n';
		}
	}
	result.text = out.str();
	return result;
}
