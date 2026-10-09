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
	content.trains = {
		{defaultTrainFill(), defaultTrainOutline(), QString()},
		{defaultTrainFill(), defaultTrainOutline(), QString()},
		{defaultTrainFill(), defaultTrainOutline(), QString()}};
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
	ok &= expect(entries.size() == 11, "case content produces stable deduplicated entries");
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
	}),
		"nonvisual operational states have no map-key swatches");
	auto* trainSwatch = legend.findChild<QWidget*>("mapKeySwatch3");
	const QImage trainImage = trainSwatch ? trainSwatch->grab().toImage() : QImage();
	ok &= expect(trainSwatch && containsColor(trainImage, defaultTrainFill()),
		"train swatch mirrors historical locomotive fill");
	auto* stationSwatch = legend.findChild<QWidget*>("mapKeySwatch4");
	ok &= expect(stationSwatch && stationSwatch->size() == QSize(46, 18),
		"station swatch keeps its compact map-key footprint");
	auto* stopSignalSwatch = legend.findChild<QWidget*>("mapKeySwatch5");
	const QImage stopSignalImage = stopSignalSwatch ? stopSignalSwatch->grab().toImage() : QImage();
	ok &= expect(stopSignalSwatch && containsColor(stopSignalImage, QColor(Qt::red)),
		"signal swatch renders the historical red plate");
	// Rows 5 to 7 are the aspects, 8 an unavailable and 9 a failed signal, 10 the passenger count.
	ok &= expect(entries.at(5).label == "Stop signal" && entries.at(6).label == "Caution signal"
			&& entries.at(7).label == "Proceed signal" && entries.at(8).label == "Unavailable signal"
			&& entries.at(9).label == "Failed signal" && entries.at(10).label == "Passenger count",
		"signal rows follow the station row and precede the passenger row");
	auto* unavailableSwatch = legend.findChild<QWidget*>("mapKeySwatch8");
	const QImage unavailableImage = unavailableSwatch ? unavailableSwatch->grab().toImage() : QImage();
	ok &= expect(unavailableSwatch && containsColor(unavailableImage, QColor(150, 150, 150))
			&& !containsColor(unavailableImage, QColor(Qt::red)) && !containsColor(unavailableImage, QColor(Qt::green)),
		"unavailable signal swatch is an empty gray ring");
	auto* failedSwatch = legend.findChild<QWidget*>("mapKeySwatch9");
	const QImage failedImage = failedSwatch ? failedSwatch->grab().toImage() : QImage();
	ok &= expect(failedSwatch && containsColor(failedImage, QColor(Qt::red)) && containsColor(failedImage, QColor(Qt::white)),
		"failed signal swatch is a red lamp with a white cross");

	int trainCount = 0;
	int stationCount = 0;
	bool stopSignalFound = false;
	bool passengerFound = false;
	for (const NetworkLegendEntry& entry : entries) {
		if (entry.kind == NetworkLegendEntryKind::Train) {
			++trainCount;
			ok &= expect(entry.label == "Train" && entry.color == defaultTrainFill()
					&& entry.outlineColor == defaultTrainOutline(),
				"trains with the default colour share one Train row");
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
	ok &= expect(trainCount == 1, "duplicate train colours are removed");
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
	ok &= expect(previewEntries.size() == 10
			&& previewEntries.at(0).color == classifyTrackSpeed(200.0 / 3.6).color
			&& previewEntries.at(1).color == classifyTrackSpeed(120.0 / 3.6).color
			&& previewEntries.at(2).color == classifyTrackSpeed(0.0).color
			&& previewEntries.at(3).label == "Selected track"
			&& previewEntries.at(4).label == "Station"
			&& previewEntries.at(5).label == "Stop signal"
			&& previewEntries.at(6).label == "Caution signal"
			&& previewEntries.at(7).label == "Proceed signal"
			&& previewEntries.at(8).label == "Unavailable signal"
			&& previewEntries.at(9).label == "Failed signal",
		"preview key explains every operational signal aspect");
	ok &= expect(previewEntries.at(3).color == QColor(Qt::blue)
			&& previewEntries.at(3).lineWidth == 4,
		"preview selected-track key matches the highlighted path");

	// Train rows: one "Train" row for the default colour, then one row per other colour.
	const auto trainRows = [](const NetworkLegendWidget& widget) {
		QVector<NetworkLegendEntry> rows;
		for (const NetworkLegendEntry& entry : widget.entries())
			if (entry.kind == NetworkLegendEntryKind::Train)
				rows << entry;
		return rows;
	};
	const QColor blue(40, 130, 210);
	const QColor green(40, 170, 110);
	const QColor blueOutline = blue.darker(200);
	const QColor greenOutline = green.darker(200);
	const NetworkLegendTrain defaultTrain{defaultTrainFill(), defaultTrainOutline(), QString()};

	NetworkLegendWidget trainLegend;
	NetworkLegendContent trainContent;
	trainContent.trains = {defaultTrain, defaultTrain};
	trainLegend.setCaseContent(trainContent);
	QVector<NetworkLegendEntry> rows = trainRows(trainLegend);
	ok &= expect(trainLegend.entryLabels() == QStringList{"Train"}
			&& rows.size() == 1 && rows.at(0).color == defaultTrainFill()
			&& rows.at(0).outlineColor == defaultTrainOutline(),
		"trains that all use the default colour give one Train row");

	trainContent.trains = {{blue, blueOutline, "201-2"}, defaultTrain, {green, greenOutline, "105-1"}, defaultTrain};
	trainLegend.setCaseContent(trainContent);
	rows = trainRows(trainLegend);
	ok &= expect(trainLegend.entryLabels() == QStringList({"Train", "105-1", "201-2"}) && rows.size() == 3
			&& rows.at(0).color == defaultTrainFill() && rows.at(1).color == green
			&& rows.at(1).outlineColor == greenOutline && rows.at(2).color == blue
			&& rows.at(2).outlineColor == blueOutline,
		"default plus two custom colours give three rows labelled with the service ids");
	trainLegend.show();
	QApplication::processEvents();
	const QColor swatchColors[] = {defaultTrainFill(), green, blue};
	for (int row = 0; row < 3; ++row) {
		auto* swatch = trainLegend.findChild<QWidget*>(QString("mapKeySwatch%1").arg(row));
		const QImage image = swatch ? swatch->grab().toImage() : QImage();
		ok &= expect(swatch && containsColor(image, swatchColors[row]),
			"train swatch is filled with the colour of its row");
		ok &= expect(swatch && (row == 0 || !containsColor(image, defaultTrainFill())),
			"custom train swatch does not use the default fill");
	}

	trainContent.trains = {{blue, blueOutline, "201-2"}, {blue, blueOutline, "201-1"},
		{blue, blueOutline, "201-1"}};
	trainLegend.setCaseContent(trainContent);
	rows = trainRows(trainLegend);
	ok &= expect(rows.size() == 1 && rows.at(0).label == "201-1, 201-2" && rows.at(0).color == blue,
		"services with the same colour share one row, each id once and sorted");

	ok &= expect(trainLegend.entryLabels() == QStringList{"201-1, 201-2"},
		"custom colours alone give no Train row");
	auto* onlyCustomSwatch = trainLegend.findChild<QWidget*>("mapKeySwatch0");
	const QImage onlyCustomImage = onlyCustomSwatch ? onlyCustomSwatch->grab().toImage() : QImage();
	ok &= expect(onlyCustomSwatch && containsColor(onlyCustomImage, blue)
			&& !containsColor(onlyCustomImage, defaultTrainFill()),
		"the first swatch is the custom colour when there is no Train row");

	// Rows are ordered by the smallest service id of each row, in natural order,
	// whatever the colour values or the order of the trains.
	const QColor red(200, 60, 50);
	const QColor redOutline = red.darker(200);
	trainContent.trains = {{blue, blueOutline, "S10"}, {green, greenOutline, "S2"}};
	trainLegend.setCaseContent(trainContent);
	ok &= expect(trainLegend.entryLabels() == QStringList({"S2", "S10"}) && trainRows(trainLegend).at(0).color == green,
		"services S2 and S10 are ordered by number, not by text");
	trainContent.trains = {{green, greenOutline, "S2"}, {blue, blueOutline, "S10"}};
	trainLegend.setCaseContent(trainContent);
	ok &= expect(trainLegend.entryLabels() == QStringList({"S2", "S10"}),
		"the row order does not depend on the order of the trains");
	trainContent.trains = {{red, redOutline, "S10"}, {green, greenOutline, "S2"}, {red, redOutline, "S3"}};
	trainLegend.setCaseContent(trainContent);
	rows = trainRows(trainLegend);
	ok &= expect(trainLegend.entryLabels() == QStringList({"S2", "S3, S10"}) && rows.at(0).color == green
			&& rows.at(1).color == red,
		"a row is placed by its smallest service id and lists its ids in the same order");
	trainContent.trains = {defaultTrain, {red, redOutline, "201-10"}, {green, greenOutline, "201-9"}};
	trainLegend.setCaseContent(trainContent);
	ok &= expect(trainLegend.entryLabels() == QStringList({"Train", "201-9", "201-10"}),
		"the Train row stays first and occurrence numbers sort by value");

	trainContent.trains.clear();
	for (const char* id : {"S6", "S1", "S4", "S2", "S5", "S3"})
		trainContent.trains << NetworkLegendTrain{blue, blueOutline, id};
	trainLegend.setCaseContent(trainContent);
	rows = trainRows(trainLegend);
	ok &= expect(rows.size() == 1 && rows.at(0).label == "S1, S2, S3 and 3 more"
			&& rows.at(0).toolTip == "S1, S2, S3, S4, S5, S6",
		"a long list of service ids is shortened in the label and complete in the tooltip");
	auto* longLabel = trainLegend.findChild<QLabel*>("mapKeyEntry0");
	ok &= expect(longLabel && longLabel->toolTip() == "S1, S2, S3, S4, S5, S6",
		"the row tooltip lists every service id");

	trainContent.trains.clear();
	trainLegend.setCaseContent(trainContent);
	ok &= expect(trainRows(trainLegend).isEmpty(), "no trains give no train rows");

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
