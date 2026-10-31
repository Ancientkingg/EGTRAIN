#ifndef NETWORKPRESENTERTESTACCESS_H
#define NETWORKPRESENTERTESTACCESS_H

#include "graphics/NetworkPresenter.h"

// The friend of NetworkPresenter for the tests of the canvas and the scripted GUI runs. It holds what no product code
// reads: the registries that only the presenter uses, and the writes with which a test puts items in them. The
// functions take a const or a non-const presenter and return the field with the same constness.
class NetworkPresenterTestAccess {
public:
	template <typename Presenter>
	static auto& allTrains(Presenter& presenter) { return presenter.allTrains; }
	template <typename Presenter>
	static auto& trainBadges(Presenter& presenter) { return presenter.m_trainBadges; }
	template <typename Presenter>
	static auto& allSignals(Presenter& presenter) { return presenter.allSignals; }
	template <typename Presenter>
	static auto& signalsByAheadId(Presenter& presenter) { return presenter.m_signalsByAheadId; }
	template <typename Presenter>
	static auto& signalDecorations(Presenter& presenter) { return presenter.m_signalDecorations; }
	template <typename Presenter>
	static auto& allArcs(Presenter& presenter) { return presenter.allArcs; }
	template <typename Presenter>
	static auto& tracksBySectionId(Presenter& presenter) { return presenter.m_tracksBySectionId; }
	template <typename Presenter>
	static auto& tracksByOccupiedArc(Presenter& presenter) { return presenter.m_tracksByOccupiedArc; }
	template <typename Presenter>
	static auto& activeTrackItems(Presenter& presenter) { return presenter.m_activeTrackItems; }
	template <typename Presenter>
	static auto& allPlatforms(Presenter& presenter) { return presenter.allPlatforms; }
	template <typename Presenter>
	static auto& stationOverlays(Presenter& presenter) { return presenter.m_stationOverlays; }
	template <typename Presenter>
	static auto& stationDecorations(Presenter& presenter) { return presenter.m_stationDecorations; }
	template <typename Presenter>
	static auto& stationLabels(Presenter& presenter) { return presenter.m_stationLabels; }
	template <typename Presenter>
	static auto& stationPictures(Presenter& presenter) { return presenter.m_stationPictures; }
	template <typename Presenter>
	static auto& stationTrackSpacing(Presenter& presenter) { return presenter.m_stationTrackSpacing; }
	template <typename Presenter>
	static auto& vcMessageItems(Presenter& presenter) { return presenter.m_vcMessageItems; }
	template <typename Presenter>
	static auto& trainPaxItem(Presenter& presenter) { return presenter.trainPaxItem; }
	template <typename Presenter>
	static auto& trainPaxInfoItem(Presenter& presenter) { return presenter.trainPaxInfoItem; }
	template <typename Presenter>
	static auto& paxIconItem(Presenter& presenter) { return presenter.paxIconItem; }
	template <typename Presenter>
	static auto& paxIconInfoItem(Presenter& presenter) { return presenter.paxIconInfoItem; }
	template <typename Presenter>
	static auto& effect(Presenter& presenter) { return presenter.effect; }
	template <typename Presenter>
	static auto& inspectedSignal(Presenter& presenter) { return presenter.m_inspectedSignal; }
	template <typename Presenter>
	static auto& trackPreview(Presenter& presenter) { return presenter.m_cachedTrackPreview; }
	template <typename Presenter>
	static auto& passengerPixmap(Presenter& presenter) { return presenter.pax_pixmap_scaled; }
	template <typename Presenter>
	static auto& stationLayerVisible(Presenter& presenter) { return presenter.m_stationLayerVisible; }
	template <typename Presenter>
	static auto& stationNamesVisible(Presenter& presenter) { return presenter.m_stationNamesVisible; }
	template <typename Presenter>
	static auto& passengerLayerVisible(Presenter& presenter) { return presenter.m_passengerLayerVisible; }

	static void setStationNearLook(NetworkPresenter& presenter, bool nearLook) { presenter.setStationNearLook(nearLook); }
	static void applyPlatformVisibility(NetworkPresenter& presenter) { presenter.applyPlatformVisibility(); }
};

using PresenterAccess = NetworkPresenterTestAccess;

#endif // NETWORKPRESENTERTESTACCESS_H
