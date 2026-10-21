#ifndef NETWORKLEGENDWIDGET_H
#define NETWORKLEGENDWIDGET_H

#include "graphics/VisualPolish.h"

#include <QVector>
#include <QWidget>

/// What a row of the map key stands for, which decides how its swatch is drawn.
enum class NetworkLegendEntryKind { Track, ///< A line in the colour and style of a track.
	Train,								   ///< A train body in its fill and outline.
	Station,							   ///< The mark of a station class.
	Signal,								   ///< A signal head.
	Passenger };						   ///< The icon of iconResource, else a filled square.

/// How a signal row draws its head: a lit lamp, an empty ring or a crossed lamp.
enum class NetworkLegendSignalLook { Lamp, ///< A lamp filled with color, with the mark of signalCue.
	Unavailable,						   ///< An empty ring with a dash.
	Failed };							   ///< A lamp filled with color and crossed in white.

/// One row of the map key: a swatch and a label. Each kind reads only some of the fields.
struct NetworkLegendEntry {
	NetworkLegendEntryKind kind = NetworkLegendEntryKind::Track; ///< What the row stands for.
	QString label;												 ///< The text of the row.
	QString toolTip;											 ///< The tooltip of the row; empty shows the label.
	QColor color;												 ///< The line of a track, the fill of a train or the lamp of a signal.
	QColor outlineColor;										 ///< The outline of a train.
	int lineWidth = 0;											 ///< The width of a track line in pixels; the swatch draws at least 2.
	Qt::PenStyle penStyle = Qt::NoPen;							 ///< The line style of a track.
	/// The operational state of a track. A state other than Free also draws the line of a free track.
	TrackOperationalState trackState = TrackOperationalState::Free;
	SignalCueKind signalCue = SignalCueKind::Neutral;					///< The mark in a lit signal lamp: a bar for Stop, a dot for Caution.
	NetworkLegendSignalLook signalLook = NetworkLegendSignalLook::Lamp; ///< How a signal head is drawn.
	StationClass stationClass = StationClass::Station;					///< The mark of a station.
	QString iconResource;												///< The Qt resource path of the icon of a passenger row; empty for none.
};

/// One train on the canvas. serviceId is empty for a train without a service.
struct NetworkLegendTrain {
	QColor fill;	   ///< The body colour; trains with the same fill share a row.
	QColor outline;	   ///< The outline colour.
	QString serviceId; ///< The id of the service the train runs.
};

/// What the canvas of the case shows, which decides the rows of the map key.
struct NetworkLegendContent {
	bool hasTracks = false; ///< Adds the rows of the high speed, mainline and local tracks.
	/// Adds the row of the occupied sections, when hasTracks is set. False for the track preview.
	bool showOperationalTrackStates = true;
	bool hasSelectedTrack = false;		   ///< Adds a row for the selected track, when hasTracks is set.
	QVector<NetworkLegendTrain> trains;	   ///< One row for the default colour and one per other colour.
	QVector<StationVisual> stationVisuals; ///< One row per station class that occurs.
	bool hasSignals = false;			   ///< Adds the rows of the stop, caution, proceed, unavailable and failed signal.
	bool hasPassengers = false;			   ///< Adds the passenger count row.
};

class QToolButton;

/// The map key of the canvas: a "Map key" button that folds a list of rows, each a swatch and a label.
class NetworkLegendWidget : public QWidget {
public:
	/// An expanded key without rows.
	explicit NetworkLegendWidget(QWidget* parent = nullptr);

	/// Replaces the rows with the ones that content calls for.
	void setCaseContent(const NetworkLegendContent& content);
	/// The rows, from top to bottom.
	QVector<NetworkLegendEntry> entries() const { return m_entries; }
	/// The labels of the rows, from top to bottom.
	QStringList entryLabels() const;

	/// True while the rows are shown.
	bool isExpanded() const { return m_expanded; }
	/// Shows or hides the rows and sets the arrow and tooltip of the button to match.
	void setExpanded(bool expanded);

private:
	void rebuildRows();

	QToolButton* m_toggle = nullptr;
	QWidget* m_body = nullptr;
	QVector<NetworkLegendEntry> m_entries;
	bool m_expanded = true;
};

#endif // NETWORKLEGENDWIDGET_H
