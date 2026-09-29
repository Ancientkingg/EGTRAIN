#include "widgets/NetworkLegendWidget.h"

#include <QApplication>
#include <QImage>
#include <QLabel>
#include <QRegularExpression>
#include <QScrollArea>
#include <QToolButton>

#include <algorithm>
#include <cstdlib>
#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static bool containsColor(const QImage& image, const QColor& color) {
	for (int y = 0; y < image.height(); ++y)
		for (int x = 0; x < image.width(); ++x)
			if (image.pixelColor(x, y).rgb() == color.rgb())
				return true;
	return false;
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
		classifyStation(),
		classifyStation(),
		classifyStation()};
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
	ok &= expect(entries.size() == 10, "case content produces stable deduplicated entries");
	ok &= expect(entries.at(0).color == classifyTrackSpeed(200.0 / 3.6).color
		&& entries.at(1).color == classifyTrackSpeed(120.0 / 3.6).color
		&& entries.at(2).color == freeTrackVisual().color
		&& entries.at(2).lineWidth == freeTrackVisual().width
		&& entries.at(2).penStyle == Qt::SolidLine,
		"speed-class entries use the renderer base styles");
	legend.setFixedWidth(180);
	QApplication::processEvents();
	ok &= expect(std::none_of(entries.cbegin(), entries.cend(), [](const NetworkLegendEntry& entry) {
		return entry.kind == NetworkLegendEntryKind::Track && entry.trackState != TrackOperationalState::Free;
	}), "nonvisual operational states have no map-key swatches");
	auto* trainSwatch = legend.findChild<QWidget*>("mapKeySwatch3");
	const QImage trainImage = trainSwatch ? trainSwatch->grab().toImage() : QImage();
	ok &= expect(trainSwatch && containsColor(trainImage, classifyTrainType("IC", "IC 2201").fill),
		"train swatch mirrors historical locomotive fill");
	auto* stationSwatch = legend.findChild<QWidget*>("mapKeySwatch5");
	ok &= expect(stationSwatch && stationSwatch->size() == QSize(46, 18),
		"station swatch keeps its compact map-key footprint");
	auto* stopSignalSwatch = legend.findChild<QWidget*>("mapKeySwatch6");
	const QImage stopSignalImage = stopSignalSwatch ? stopSignalSwatch->grab().toImage() : QImage();
	ok &= expect(stopSignalSwatch && containsColor(stopSignalImage, QColor(Qt::red)),
		"signal swatch renders the historical red plate");

	int intercityCount = 0;
	int stationCount = 0;
	bool stopSignalFound = false;
	bool passengerFound = false;
	for (const NetworkLegendEntry& entry : entries) {
		if (entry.trainKind == TrainVisualKind::Intercity) {
			++intercityCount;
			ok &= expect(entry.color == classifyTrainType("IC", "IC 2201").fill,
				"train entry uses renderer classification");
		}
		if (entry.kind == NetworkLegendEntryKind::Station) {
			++stationCount;
			ok &= expect(entry.iconResource == ":/icons/station-dark.svg"
				&& classifyStation().iconResource == ":/icons/station.svg",
				"station entry uses the light-surface variant of the shared pictogram");
		}
		if (entry.signalCue == SignalCueKind::Stop) {
			stopSignalFound = true;
			ok &= expect(entry.iconResource == classifySignalAspect(0).iconResource,
				"signal entry uses renderer classification");
		}
		if (entry.kind == NetworkLegendEntryKind::Passenger) {
			passengerFound = true;
			ok &= expect(entry.iconResource == ":/icons/pax_icon.png",
				"passenger entry uses the renderer icon");
		}
	}
	ok &= expect(intercityCount == 1, "duplicate train categories are removed");
	ok &= expect(stationCount == 1, "duplicate station markers are removed");
	ok &= expect(stopSignalFound, "signal cues are included");
	ok &= expect(passengerFound, "passenger-load cue is included");

	const QStringList labelsBeforeCollapse = legend.entryLabels();
	legend.setExpanded(false);
	QApplication::processEvents();
	ok &= expect(!legend.isExpanded() && body && !body->isVisible(), "map key collapses");
	ok &= expect(header && header->isVisible(), "map key header remains visible while collapsed");
	legend.setExpanded(true);
	const QStringList labelsAfterExpand = legend.entryLabels();
	ok &= expect(labelsAfterExpand.size() == labelsBeforeCollapse.size()
		&& std::equal(labelsAfterExpand.cbegin(), labelsAfterExpand.cend(), labelsBeforeCollapse.cbegin()),
		"collapsing does not change map key entries");

	for (QLabel* row : legend.findChildren<QLabel*>(QRegularExpression("^mapKeyEntry")))
		ok &= expect(row->maximumWidth() <= 160, "map key row stays within 160 logical pixels");

	NetworkLegendContent previewContent;
	previewContent.hasTracks = true;
	previewContent.showOperationalTrackStates = false;
	previewContent.hasSelectedTrack = true;
	previewContent.stationVisuals = {classifyStation()};
	previewContent.hasSignals = true;
	legend.setCaseContent(previewContent);
	const QVector<NetworkLegendEntry> previewEntries = legend.entries();
	ok &= expect(previewEntries.size() == 8
			&& previewEntries.at(0).color == classifyTrackSpeed(200.0 / 3.6).color
			&& previewEntries.at(1).color == classifyTrackSpeed(120.0 / 3.6).color
			&& previewEntries.at(2).color == classifyTrackSpeed(0.0).color
			&& previewEntries.at(3).label == "Selected track"
			&& previewEntries.at(4).label == "Station"
			&& previewEntries.at(5).label == "Stop signal"
			&& previewEntries.at(6).label == "Caution signal"
			&& previewEntries.at(7).label == "Proceed signal",
		"preview key explains every operational signal aspect");
	ok &= expect(previewEntries.at(3).color == QColor(Qt::blue)
			&& previewEntries.at(3).lineWidth == 4,
		"preview selected-track key matches the highlighted path");

	// Resizing the rail or changing the font must not clip wrapped speed ranges.
	QScrollArea rail;
	rail.setWidgetResizable(true);
	auto* constrainedLegend = new NetworkLegendWidget;
	constrainedLegend->setCaseContent(content);
	rail.setWidget(constrainedLegend);
	rail.show();
	for (int fontSize : {11, 18}) {
		rail.setStyleSheet(QString("QWidget#mapKeyBody QLabel { font-size: %1px; }").arg(fontSize));
		for (int width : {180, 230}) {
			rail.setFixedSize(width, 200);
			QApplication::processEvents();
			QApplication::processEvents();
			for (QLabel* label : constrainedLegend->findChildren<QLabel*>(QRegularExpression("^mapKeyEntry"))) {
				ok &= expect(label->height() >= label->heightForWidth(label->width()),
					"wrapped map key text fits after width and font changes");
				ok &= expect(label->parentWidget()->rect().contains(label->geometry()),
					"wrapped map key text stays within its own row");
			}
		}
	}

	return ok ? 0 : 1;
}
