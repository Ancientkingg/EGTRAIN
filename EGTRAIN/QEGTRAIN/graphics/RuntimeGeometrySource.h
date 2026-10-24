#ifndef RUNTIMEGEOMETRYSOURCE_H
#define RUNTIMEGEOMETRYSOURCE_H

#include <string>

class BlockSet;
class Connections;
class Route;
class Section;
class StationPlatform;
class Stations;

/// Where NetworkPresenter takes the infrastructure of a run from: the track lines with their arcs
/// and nodes, the stations, the connections between track lines, the signalling block sections, the
/// routes of the trains and the platforms of the stations. The presenter works out scene
/// coordinates and display levels and writes them into the same objects, so the objects stay where
/// they are until the presenter has cleared the scene, because the items on the canvas point to
/// their arcs and nodes. The objects belong to the run, not to the presenter.
///
/// An index is at least 0 and below the matching count. The source does not check it.
class RuntimeGeometrySource {
public:
	virtual ~RuntimeGeometrySource() = default;

	/// The number of track lines.
	virtual int trackCount() const = 0;
	/// The track line with this index.
	virtual BlockSet& track(int index) const = 0;
	/// The region of the track line with this index.
	virtual int regionOfTrack(int index) const = 0;

	/// The number of stations.
	virtual int stationCount() const = 0;
	/// The station with this index.
	virtual Stations& station(int index) const = 0;

	/// The number of connections between track lines.
	virtual int connectionCount() const = 0;
	/// The connection with this index.
	virtual Connections& connection(int index) const = 0;

	/// The number of signalling block sections.
	virtual int sectionCount() const = 0;
	/// The signalling block section with this index.
	virtual const Section& section(int index) const = 0;

	/// The route with this index, or null when there is none.
	virtual const Route* route(int index) const = 0;

	/// The first platform with this station id and platform id, or null.
	virtual const StationPlatform* platform(const std::string& stationId, const std::string& platformId) const = 0;

	/// The run shows the passengers on the platforms and the passenger load of a train.
	virtual bool passengerGui() const = 0;
};

#endif // RUNTIMEGEOMETRYSOURCE_H
