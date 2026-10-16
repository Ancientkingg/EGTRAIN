#include "graphics/NetworkPresenter.h"

#include <QGraphicsScene>

NetworkPresenter::NetworkPresenter(QGraphicsScene* scene) : m_scene(scene) {}

void NetworkPresenter::clear() {
	m_inspectedSignal = nullptr;
	if (m_scene)
		m_scene->clear();

	// The scene has deleted the items, so every pointer to one is dropped.
	m_trainBadges.clear();
	m_prevTrainPositions.clear();
	allTrains.clear();
	allSignals.clear();
	m_signalDecorations.clear();
	m_stationOverlays.clear();
	m_vcMessageItems.clear();
	m_stationDecorations.clear();
	m_stationLabels.clear();
	m_stationPictures.clear();
	m_signalsByAheadId.clear();
	allArcs.clear();
	allPlatforms.clear();
	m_tracksBySectionId.clear();
	m_tracksByOccupiedArc.clear();
	m_activeTrackItems.clear();
	trainPaxInfoItem = nullptr;
	trainPaxItem = nullptr;
	paxIconInfoItem = nullptr;
	paxIconItem = nullptr;
	effect = nullptr;
}
