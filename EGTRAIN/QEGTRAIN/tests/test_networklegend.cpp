#include "widgets/NetworkLegendWidget.h"

#include <QApplication>
#include <QLabel>
#include <QRegularExpression>
#include <QToolButton>

#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	bool ok = true;

	NetworkLegendContent content;
	content.hasTracks = true;
	content.trainVisuals = {
		classifyTrainType("IC", "IC 2201"),
		classifyTrainType("", "sprinter 301"),
		classifyTrainType("IC", "IC 2202")};
	content.stationVisuals = {
		classifyStation(true, 4),
		classifyStation(true, 1),
		classifyStation(true, 4)};
	content.hasSignals = true;
	content.hasPassengers = true;

	NetworkLegendWidget legend;
	legend.setCaseContent(content);
	legend.show();
	QApplication::processEvents();

	ok &= expect(legend.isExpanded(), "map key starts expanded");
	auto* header = legend.findChild<QToolButton*>("mapKeyToggle");
	auto* body = legend.findChild<QWidget*>("mapKeyBody");
	ok &= expect(header && header->isVisible(), "map key header is visible");
	ok &= expect(body && body->isVisible(), "map key body is visible while expanded");

	const QVector<NetworkLegendEntry> entries = legend.entries();
	ok &= expect(entries.size() == 11, "case content produces stable deduplicated entries");
	ok &= expect(entries.at(0).label == "Prepared route"
		&& entries.at(0).trackState == TrackOperationalState::Prepared
		&& entries.at(0).color == classifyTrackState(TrackOperationalState::Prepared).color
		&& entries.at(0).penStyle == classifyTrackState(TrackOperationalState::Prepared).style,
		"prepared track entry uses renderer classification");
	ok &= expect(entries.at(1).label == "Occupied section"
		&& entries.at(1).color == classifyTrackState(TrackOperationalState::Occupied).color,
		"occupied track entry uses renderer classification");
	ok &= expect(entries.at(2).label == "Blocked section"
		&& entries.at(2).penStyle == classifyTrackState(TrackOperationalState::Blocked).style,
		"blocked track entry keeps its non-color cue");

	int intercityCount = 0;
	int interchangeCount = 0;
	bool stopSignalFound = false;
	bool passengerFound = false;
	for (const NetworkLegendEntry& entry : entries) {
		if (entry.trainKind == TrainVisualKind::Intercity) {
			++intercityCount;
			ok &= expect(entry.color == classifyTrainType("IC", "IC 2201").fill,
				"train entry uses renderer classification");
		}
		if (entry.stationKind == StationVisualKind::Interchange) {
			++interchangeCount;
			ok &= expect(entry.iconResource == classifyStation(true, 4).iconResource,
				"station entry uses renderer classification");
		}
		if (entry.signalCue == SignalCueKind::Stop) {
			stopSignalFound = true;
			ok &= expect(entry.iconResource == classifySignalAspect(0).iconResource,
				"signal entry uses renderer classification");
		}
		if (entry.kind == NetworkLegendEntryKind::Passenger) {
			passengerFound = true;
			ok &= expect(entry.iconResource == ":/icons/passenger.svg",
				"passenger entry uses the renderer icon");
		}
	}
	ok &= expect(intercityCount == 1, "duplicate train categories are removed");
	ok &= expect(interchangeCount == 1, "duplicate station roles are removed");
	ok &= expect(stopSignalFound, "signal cues are included");
	ok &= expect(passengerFound, "passenger-load cue is included");

	const QStringList labelsBeforeCollapse = legend.entryLabels();
	legend.setExpanded(false);
	QApplication::processEvents();
	ok &= expect(!legend.isExpanded() && body && !body->isVisible(), "map key collapses");
	ok &= expect(header && header->isVisible(), "map key header remains visible while collapsed");
	legend.setExpanded(true);
	ok &= expect(legend.entryLabels() == labelsBeforeCollapse,
		"collapsing does not change map key entries");

	for (QLabel* row : legend.findChildren<QLabel*>(QRegularExpression("^mapKeyEntry")))
		ok &= expect(row->maximumWidth() <= 160, "map key row stays within 160 logical pixels");

	return ok ? 0 : 1;
}
