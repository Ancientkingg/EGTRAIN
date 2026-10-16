#ifndef NETWORKPRESENTER_H
#define NETWORKPRESENTER_H

#include <QList>
#include <QMap>
#include <QPointF>
#include <QPointer>

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "graphics/items/HighlightEffect.h"

class PassengerItem;
class PlatformItem;
class QGraphicsItem;
class QGraphicsItemGroup;
class QGraphicsPixmapItem;
class QGraphicsScene;
class SignalItem;
class StationOverlayItem;
class TrackLineItem;
class TrainBadgeItem;
class TrainItemGroup;

// Keeps track of the items on the network canvas. The scene owns every item; the lists, maps and
// pointers below only observe them and are never used to delete one. They are public until all
// the code that fills them is in this class.
//
// clear() deletes every item of the scene and then forgets all the pointers to them. A pointer
// that stays after the scene is cleared would dangle, so the registries belong with the code that
// clears the scene.
class NetworkPresenter {
public:
	explicit NetworkPresenter(QGraphicsScene* scene);

	// Deletes every item of the scene and empties every registry. The signal under inspection is
	// forgotten first, so nothing reads it while the scene deletes its items.
	void clear();

	// The train groups, the badge of each train by train index, and the centre of each badge in the
	// previous update by train index.
	QList<TrainItemGroup*> allTrains;
	QMap<int, TrainBadgeItem*> m_trainBadges;
	QMap<int, QPointF> m_prevTrainPositions;

	// The signals, the signals of each section by the id of the section ahead of them, and the
	// decorations of the signals.
	QList<SignalItem*> allSignals;
	std::unordered_map<std::string, QList<SignalItem*>> m_signalsByAheadId;
	QList<QGraphicsItem*> m_signalDecorations;

	// The track lines, the track lines of each signalling section by section id, the track line of
	// each arc by track and start position, and the track lines that show a state other than free.
	QList<TrackLineItem*> allArcs;
	std::map<std::string, std::vector<TrackLineItem*>> m_tracksBySectionId;
	std::map<std::pair<int, double>, TrackLineItem*> m_tracksByOccupiedArc;
	std::set<TrackLineItem*> m_activeTrackItems;

	// The platforms, the station overlays, everything that the station layer shows, the station
	// names, and the picture of each overlay.
	QList<PlatformItem*> allPlatforms;
	QList<StationOverlayItem*> m_stationOverlays;
	QList<QGraphicsItem*> m_stationDecorations;
	QList<QGraphicsItem*> m_stationLabels;
	QMap<StationOverlayItem*, QGraphicsPixmapItem*> m_stationPictures;

	// The vehicle-coupling message of each train by train index.
	QMap<int, QGraphicsItemGroup*> m_vcMessageItems;

	// The signal whose details are shown, the selected train and the group that shows its
	// passenger information, and the selected passenger icon and the group that shows its information.
	SignalItem* m_inspectedSignal = nullptr;
	TrainItemGroup* trainPaxItem = nullptr;
	QGraphicsItemGroup* trainPaxInfoItem = nullptr;
	PassengerItem* paxIconItem = nullptr;
	QGraphicsItemGroup* paxIconInfoItem = nullptr;

	// The effect on the item selected last. The item deletes it with itself.
	QPointer<HighlightEffect> effect;

private:
	QPointer<QGraphicsScene> m_scene;
};

#endif // NETWORKPRESENTER_H
