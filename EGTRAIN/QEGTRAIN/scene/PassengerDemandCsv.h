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
/// Windows line ends, empty lines and empty trailing cells are allowed. Counts are whole numbers of 0 or more; a count on
/// the diagonal must be 0.
PassengerDemandCsv readPassengerDemandCsv(const std::string& text, const std::vector<SceneStation>& stations);

/// Writes slice in the form that readPassengerDemandCsv reads: the length of its period, then a row and a column for
/// every station in the order of stations, by id.
std::string writePassengerDemandCsv(const ScenePassengerDemandSlice& slice, const std::vector<SceneStation>& stations);

#endif
