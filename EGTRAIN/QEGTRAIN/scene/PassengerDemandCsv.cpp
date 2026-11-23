#include "scene/PassengerDemandCsv.h"

#include <cerrno>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace {

std::string trimmed(const std::string& text) {
	const std::size_t first = text.find_first_not_of(" \t");
	if (first == std::string::npos)
		return {};
	return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

// The cells of one line, trimmed, without the empty cells at its end.
std::vector<std::string> cellsOf(const std::string& line) {
	std::vector<std::string> cells;
	std::string cell;
	std::istringstream stream(line);
	while (std::getline(stream, cell, ','))
		cells.push_back(trimmed(cell));
	while (!cells.empty() && cells.back().empty())
		cells.pop_back();
	return cells;
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

// A whole number of 0 or more; false for anything else.
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

} // namespace

PassengerDemandCsv readPassengerDemandCsv(const std::string& text, const std::vector<SceneStation>& stations) {
	PassengerDemandCsv result;
	std::string body = text;
	if (body.rfind("\xEF\xBB\xBF", 0) == 0)
		body.erase(0, 3);

	// The non-empty lines with their line numbers.
	std::vector<std::pair<int, std::vector<std::string>>> lines;
	std::istringstream stream(body);
	std::string line;
	for (int number = 1; std::getline(stream, line); ++number) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		std::vector<std::string> cells = cellsOf(line);
		if (!cells.empty())
			lines.emplace_back(number, std::move(cells));
	}
	const auto error = [&result](int number, const std::string& message) {
		result.errors.push_back("Line " + std::to_string(number) + ": " + message);
	};

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
		out << ',' << station.id;
	out << '\n';
	for (const SceneStation& origin : stations) {
		out << origin.id;
		for (const SceneStation& destination : stations) {
			const auto found = counts.find({origin.id, destination.id});
			out << ',' << (found == counts.end() ? 0 : found->second);
		}
		out << '\n';
	}
	return out.str();
}
