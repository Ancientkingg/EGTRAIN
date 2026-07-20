#ifndef NETWORKLEGENDWIDGET_H
#define NETWORKLEGENDWIDGET_H

#include "graphics/VisualPolish.h"

#include <QVector>
#include <QWidget>

enum class NetworkLegendEntryKind { Track, Train, Station, Signal, Passenger };

struct NetworkLegendEntry {
	NetworkLegendEntryKind kind = NetworkLegendEntryKind::Track;
	QString label;
	QColor color;
	int lineWidth = 0;
	Qt::PenStyle penStyle = Qt::NoPen;
	TrackOperationalState trackState = TrackOperationalState::Free;
	TrainVisualKind trainKind = TrainVisualKind::Passenger;
	TrainBadgeShape trainShape = TrainBadgeShape::Rounded;
	StationVisualKind stationKind = StationVisualKind::StopMarker;
	SignalCueKind signalCue = SignalCueKind::Neutral;
	QString iconResource;
};

struct NetworkLegendContent {
	bool hasTracks = false;
	QVector<TrainVisual> trainVisuals;
	QVector<StationVisual> stationVisuals;
	bool hasSignals = false;
	bool hasPassengers = false;
};

class QToolButton;

class NetworkLegendWidget : public QWidget {
public:
	explicit NetworkLegendWidget(QWidget* parent = nullptr);

	void setCaseContent(const NetworkLegendContent& content);
	QVector<NetworkLegendEntry> entries() const { return m_entries; }
	QStringList entryLabels() const;

	bool isExpanded() const { return m_expanded; }
	void setExpanded(bool expanded);

private:
	void rebuildRows();

	QToolButton* m_toggle = nullptr;
	QWidget* m_body = nullptr;
	QVector<NetworkLegendEntry> m_entries;
	bool m_expanded = true;
};

#endif // NETWORKLEGENDWIDGET_H
