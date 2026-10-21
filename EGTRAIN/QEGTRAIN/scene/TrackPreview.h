#ifndef TRACKPREVIEW_H
#define TRACKPREVIEW_H

#include <string>
#include <vector>

/// A point of a preview line. x and y start as the node coordinates in kilometres; a projection from the
/// station views and normalizeTrackPreview replace them with scene coordinates.
struct TrackPreviewPoint {
	double x = 0.0;		///< Horizontal preview coordinate.
	double y = 0.0;		///< Vertical preview coordinate.
	std::string nodeId; ///< The node; empty for an interpolated point.
	double rawX = 0.0;	///< Chainage of the node in kilometres; source x for position-only station lookup.
};

/// The preview of one visible track.
struct TrackPreviewLine {
	std::string id;						   ///< The track.
	std::vector<TrackPreviewPoint> points; ///< The nodes of the track in chain order.
	double displayOffset = 0.0;			   ///< Vertical offset added to every point, in the units of the points.
	/// True when the points come from the station views. Set only after the entire line maps successfully.
	bool authoredStationProjection = false;
	/// The distance between two levels when displayOffset holds the level of the track, in the units of the
	/// points; 0 when it does not.
	double levelSpacing = 0.0;
};

/// A connection between two visible tracks.
struct TrackPreviewConnection {
	std::string id;			   ///< Authored connection, including duplicate endpoint pairs.
	std::string firstTrackId;  ///< Track of the from node.
	std::string firstNodeId;   ///< The from node.
	std::string secondTrackId; ///< Track of the to node.
	std::string secondNodeId;  ///< The to node.
};

/// A station on the preview.
struct TrackPreviewStation {
	std::string name;		  ///< Display name; the id when the station has no name.
	std::string nodeId;		  ///< The first platform node on a visible track; empty for a station placed by position.
	double x = 0.0;			  ///< Chainage in kilometres of that node, or the station position.
	bool hasPlatform = false; ///< True when the station is placed at a platform node.
	std::string id;			  ///< The station.
};

/// A signal on the preview, at the start of a block section.
struct TrackPreviewSignal {
	std::string trackId;   ///< The track.
	std::string sectionId; ///< Runtime id of the section that starts there.
	std::string nodeId;	   ///< The node at the start of the section; empty when none lies there.
	double rawX = 0.0;	   ///< Chainage in kilometres of the start of the section.
};

/// What the network preview draws of a scene.
struct TrackPreviewResult {
	bool normalized = false;		 ///< Set by normalizeTrackPreview. Normalization is idempotent; reload after scene edits.
	double normalizationScale = 1.0; ///< Projected coordinates to normalized scene coordinates.
	/// Historical presentation distances to scene coordinates. Degree-derived conversion requires successful
	/// authored projection on every visible nonempty line; raw/mixed/invalid or degenerate scenes retain one.
	double presentationScale = 1.0;
	std::vector<TrackPreviewLine> lines;			 ///< The visible tracks with at least two nodes.
	std::vector<TrackPreviewConnection> connections; ///< Connections between visible tracks.
	std::vector<TrackPreviewStation> stations;		 ///< Stations on a visible track or with a position.
	/// Block section starts of the visible tracks, without the first section of each track.
	std::vector<TrackPreviewSignal> previewSignals;
	std::vector<std::string> warnings; ///< What could not be drawn as authored, as text.
};

struct SceneModel;

/// The preview of the visible tracks of the scene, before normalization.
TrackPreviewResult loadTrackPreview(const SceneModel& scene);

// These helpers operate on projected scene coordinates only; rawX remains the
// canonical chainage used to interpolate runtime positions.

/// The point of the line at the node. Returns false when the line has none.
bool trackPreviewPointAtNode(const TrackPreviewLine& line, const std::string& nodeId,
	TrackPreviewPoint& point);
/// The point of the line at chainage rawX in kilometres, interpolated between the points around it, or the
/// closest point when rawX lies outside the line. Returns false for an empty line or a non-finite result.
bool trackPreviewPointAtX(const TrackPreviewLine& line, double rawX,
	TrackPreviewPoint& point);
/// The preview scaled so that its larger extent equals the longest chainage span of a line in metres. A
/// normalized preview is returned unchanged.
TrackPreviewResult normalizeTrackPreview(const TrackPreviewResult& preview);

#endif
