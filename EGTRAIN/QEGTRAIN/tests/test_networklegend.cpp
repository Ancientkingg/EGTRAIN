#include "widgets/NetworkLegendWidget.h"

#include <QApplication>
#include <QImage>
#include <QLabel>
#include <QRegularExpression>
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

static bool containsColorNear(const QImage& image, const QColor& color, int tolerance) {
	for (int y = 0; y < image.height(); ++y) {
		for (int x = 0; x < image.width(); ++x) {
			const QColor pixel = image.pixelColor(x, y);
			if (std::abs(pixel.red() - color.red()) <= tolerance
				&& std::abs(pixel.green() - color.green()) <= tolerance
				&& std::abs(pixel.blue() - color.blue()) <= tolerance)
				return true;
		}
	}
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
	ok &= expect(entries.size() == 13, "case content produces stable deduplicated entries");
	ok &= expect(entries.at(0).color == classifyTrackSpeed(200.0 / 3.6).color
		&& entries.at(1).color == classifyTrackSpeed(120.0 / 3.6).color
		&& entries.at(2).color == freeTrackVisual().color
		&& entries.at(2).lineWidth == freeTrackVisual().width
		&& entries.at(2).penStyle == Qt::SolidLine,
		"speed-class entries use the renderer base styles");
	ok &= expect(entries.at(3).label == "Permissive signalling"
		&& entries.at(3).trackState == TrackOperationalState::Prepared
		&& entries.at(3).color == classifyTrackState(TrackOperationalState::Prepared).color
		&& entries.at(3).penStyle == classifyTrackState(TrackOperationalState::Prepared).style,
		"permissive signalling entry uses renderer classification");
	legend.setFixedWidth(180);
	QApplication::processEvents();
	auto* permissiveLabel = legend.findChild<QLabel*>("mapKeyEntry3");
	ok &= expect(permissiveLabel && permissiveLabel->wordWrap()
		&& permissiveLabel->width() >= permissiveLabel->fontMetrics().horizontalAdvance("signalling")
		&& permissiveLabel->minimumHeight() >= permissiveLabel->heightForWidth(permissiveLabel->width())
		&& permissiveLabel->height() >= permissiveLabel->fontMetrics().lineSpacing() * 2,
		"permissive signalling label wraps at the narrow case dock width");
	ok &= expect(entries.at(4).label == "Occupied section"
		&& entries.at(4).color == classifyTrackState(TrackOperationalState::Occupied).color,
		"occupied track entry uses renderer classification");
	ok &= expect(entries.at(5).label == "Blocked section"
		&& entries.at(5).penStyle == classifyTrackState(TrackOperationalState::Blocked).style,
		"blocked track entry keeps its non-color cue");
	auto* preparedSwatch = legend.findChild<QWidget*>("mapKeySwatch3");
	const QImage preparedImage = preparedSwatch ? preparedSwatch->grab().toImage() : QImage();
	ok &= expect(preparedSwatch && preparedSwatch->width() == 46
		&& containsColor(preparedImage, classifyTrackState(TrackOperationalState::Prepared).color)
		&& containsColor(preparedImage, freeTrackVisual().color),
		"permissive signalling swatch mirrors the renderer state underlay and base rail");
	auto* trainSwatch = legend.findChild<QWidget*>("mapKeySwatch6");
	const QImage trainImage = trainSwatch ? trainSwatch->grab().toImage() : QImage();
	ok &= expect(trainSwatch && containsColor(trainImage, QColor("#26313B"))
			&& containsColor(trainImage, classifyTrainType("IC", "IC 2201").fill),
		"train swatch mirrors the compact on-track badge and classified plate");
	auto* stationSwatch = legend.findChild<QWidget*>("mapKeySwatch8");
	const QImage stationImage = stationSwatch ? stationSwatch->grab().toImage() : QImage();
	ok &= expect(stationSwatch && containsColorNear(stationImage, QColor(210, 215, 220), 30)
			&& !containsColor(stationImage, QColor("#5078D2")),
		"station swatch mirrors the gray on-track circular marker instead of the SVG tile");
	auto* stopSignalSwatch = legend.findChild<QWidget*>("mapKeySwatch9");
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
			ok &= expect(entry.iconResource == classifyStation().iconResource,
				"station entry uses the uniform renderer classification");
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
	ok &= expect(previewEntries.at(3).color == QColor(242, 170, 70)
			&& previewEntries.at(3).lineWidth == 4,
		"preview selected-track key matches the highlighted path");

	return ok ? 0 : 1;
}
