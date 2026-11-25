#ifndef PassengerDemandCsv_h
#define PassengerDemandCsv_h

#include "scene/SceneModel.h"

#include <string>
#include <vector>

/// An origin-destination matrix read from CSV text.
struct PassengerDemandCsv {
	std::vector<ScenePassengerDemandCell> cells; ///< One cell per pair with passengers, in row then column order.
	double periodSeconds = 0.0;					 ///< The Time Reference row: the length of the period; 0 when absent.
	std::vector<std::string> errors;			 ///< One message per problem, with its line; the cells are kept only when empty.
	/// True when no problem was found.
	bool ok() const { return errors.empty(); }
};

/// Reads an origin-destination matrix in this form, where the first row is optional and a count may be empty for 0:
///
///     Time Reference,3600
///     O/D,Guingamp,Paimpol
///     Guingamp,0,75
///     Paimpol,52,0
///
/// Each row is an origin, each column a destination. A station is matched on its id, then on its name. A byte order mark,
/// Windows line ends, empty lines and empty trailing cells are allowed. A field may be quoted as in RFC 4180, so that a
/// name can hold a comma, with a quote inside it written twice; a line with an unclosed quote, or text after a closing
/// quote, is an error. Counts are whole numbers of 0 or more; a count on the diagonal must be 0.
PassengerDemandCsv readPassengerDemandCsv(const std::string& text, const std::vector<SceneStation>& stations);

/// Writes slice in the form that readPassengerDemandCsv reads: the length of its period, then a row and a column for
/// every station in the order of stations, by id.
std::string writePassengerDemandCsv(const ScenePassengerDemandSlice& slice, const std::vector<SceneStation>& stations);

/// One unknown station name skipped while reading a demand table.
struct PassengerDemandUnknownStation {
	std::string name;		  ///< Station name as written in the table.
	int rows = 0;			  ///< Number of rows skipped because of this name.
	long long passengers = 0; ///< Total passengers skipped across those rows.
};

/// A day of origin-destination demand read from a long-format CSV table.
struct PassengerDemandTable {
	std::vector<ScenePassengerDemandSlice> slices;				///< Non-overlapping slices in time order, with cells in station order.
	std::vector<PassengerDemandUnknownStation> unknownStations; ///< Unknown station names skipped, in first-seen order.
	long long totalPassengers = 0;								///< Total passengers across all kept slices.
	std::vector<std::string> errors;							///< One message per problem, with its line; slices are kept only when empty.
	/// True when no problem was found.
	bool ok() const { return errors.empty(); }
};

/// Reads a whole day of origin-destination demand from a long-format CSV table with a header row naming the columns
/// in any order (case-insensitive):
///
///     origin,destination,start,end,count
///     Guingamp,Paimpol,07:00:00,08:00:00,75
///     Paimpol,Guingamp,07:00:00,08:00:00,52
///     Guingamp,Paimpol,08:00:00,09:00:00,40
///
/// start and end accept HH:MM:SS, HH:MM or whole seconds since midnight (0 to 86400). A period column (whole seconds,
/// HH:MM:SS or HH:MM) may replace end, with end = start + period; exactly one of end and period must be present.
/// Stations are matched by id, then by name; rows naming unknown stations are skipped and listed in unknownStations.
/// Rows with the same start and end form one slice, duplicate origin-destination pairs within a slice add up, and rows
/// with count 0 are skipped. Overlapping distinct periods and a total above kMaxScenePassengerDemand are errors, and so is
/// a table that gives no slice at all: one without rows, or one where every row names an unknown station or has count 0.
PassengerDemandTable readPassengerDemandTable(const std::string& text, const std::vector<SceneStation>& stations);

/// A demand table written as CSV text, with what the table could not hold.
struct PassengerDemandTableText {
	std::string text;	  ///< The CSV text.
	int emptySlices = 0;  ///< Slices left out because they have no passengers to write.
	int droppedCells = 0; ///< Cells with passengers left out because a station is not in the case.
	int roundedTimes = 0; ///< Start and end times of written slices that were rounded to whole seconds.
};

/// Writes slices in the table format that readPassengerDemandTable reads: header origin,destination,start,end,count,
/// station ids, times as HH:MM:SS, slices in time order and cells in the order of stations. Reading the text back gives
/// the same slices, apart from their ids and what the counts of the result report.
PassengerDemandTableText writePassengerDemandTable(
	const std::vector<ScenePassengerDemandSlice>& slices, const std::vector<SceneStation>& stations);

#endif
