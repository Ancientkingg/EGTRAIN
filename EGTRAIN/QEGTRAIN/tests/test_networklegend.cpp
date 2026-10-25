#include "widgets/NetworkLegendWidget.h"

#include "graphics/VisualPolish.h"
#include "graphics/items/SelectionCueItem.h"
#include "graphics/items/StationOverlayItem.h"

#include <QApplication>
#include <QImage>
#include <QLabel>
#include <QMap>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTest>
#include <QTextLayout>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <cstdlib>

static bool containsColor(const QImage& image, const QColor& color) {
	for (int y = 0; y < image.height(); ++y)
		for (int x = 0; x < image.width(); ++x)
			if (image.pixelColor(x, y).rgb() == color.rgb())
				return true;
	return false;
}

// The colour of most opaque pixels of image, then those of the light, dark and blue colours of the pins and of white
// that three opaque pixels or more have.
static QString pinColours(const QImage& image) {
	QMap<QRgb, int> counts;
	for (int y = 0; y < image.height(); ++y)
		for (int x = 0; x < image.width(); ++x)
			if (const QColor colour = image.pixelColor(x, y); colour.alpha() == 255)
				++counts[colour.rgb()];
	QRgb most = 0;
	for (auto count = counts.cbegin(); count != counts.cend(); ++count)
		if (count.value() > counts.value(most))
			most = count.key();
	QString colours = QColor(most).name();
	for (const QColor& colour : {stationMarkFill(), canvasColor(), QColor(80, 120, 210), QColor(Qt::white)})
		if (counts.value(colour.rgb()) >= 3)
			colours += " " + colour.name();
	return colours;
}

// Colour of the swatch pixel at a position in widget coordinates.
static QColor swatchPixel(const QImage& image, int x, int y) {
	const qreal scale = image.devicePixelRatio();
	return image.pixelColor(static_cast<int>(x * scale), static_cast<int>(y * scale));
}

// Tracks, three trains and three stations of the default look, signals and passengers.
static NetworkLegendContent caseContent() {
	NetworkLegendContent content;
	content.hasTracks = true;
	content.trains = {
		{defaultTrainFill(), defaultTrainOutline(), QString()},
		{defaultTrainFill(), defaultTrainOutline(), QString()},
		{defaultTrainFill(), defaultTrainOutline(), QString()}};
	content.stationVisuals = {
		classifyStation(true),
		classifyStation(true),
		classifyStation(true)};
	content.hasSignals = true;
	content.hasPassengers = true;
	return content;
}

// A shown map key of caseContent().
struct LegendFixture {
	NetworkLegendWidget legend;

	LegendFixture() {
		legend.setCaseContent(caseContent());
		legend.show();
		QApplication::processEvents();
	}
};

// Train rows: one "Train" row for the default colour, then one row per other colour.
static QVector<NetworkLegendEntry> trainRows(const NetworkLegendWidget& widget) {
	QVector<NetworkLegendEntry> rows;
	for (const NetworkLegendEntry& entry : widget.entries())
		if (entry.kind == NetworkLegendEntryKind::Train)
			rows << entry;
	return rows;
}

static NetworkLegendTrain defaultTrain() {
	return {defaultTrainFill(), defaultTrainOutline(), QString()};
}

static const QColor kBlue(40, 130, 210);
static const QColor kGreen(40, 170, 110);
static const QColor kRed(200, 60, 50);

class TestNetworkLegend : public QObject {
	Q_OBJECT

private slots:
	void startsExpanded();
	void listsCaseEntries();
	void drawsSwatches();
	void removesDuplicateEntries();
	void collapsesAndExpands();
	void rowsStayNarrow();
	void previewKeyExplainsSignalAspects();
	void stationSwatchesShowPinOfClass();
	void defaultTrainsShareOneRow();
	void customColoursGetRowsOfTheirOwn();
	void servicesOfOneColourShareRow();
	void ordersRowsByServiceId();
	void shortensLongListOfServices();
	void noTrainsGiveNoTrainRows();
	void wrappedTextFitsRail();
};

void TestNetworkLegend::startsExpanded() {
	LegendFixture f;
	NetworkLegendWidget& legend = f.legend;
	QVERIFY2(legend.isExpanded(), "map key starts expanded");
	auto* header = legend.findChild<QToolButton*>("mapKeyToggle");
	auto* body = legend.findChild<QWidget*>("mapKeyBody");
	// The map key header is visible.
	if (header == nullptr)
		QFAIL("the map key has no header");
	QVERIFY(header->isVisible());
	// The map key body is visible while expanded.
	if (body == nullptr)
		QFAIL("the map key has no body");
	QVERIFY(body->isVisible());
}

void TestNetworkLegend::listsCaseEntries() {
	LegendFixture f;
	const QVector<NetworkLegendEntry> entries = f.legend.entries();
	// Case content produces stable deduplicated entries.
	QCOMPARE(entries.size(), qsizetype(12));
	// Speed-class entries use the renderer base styles.
	QCOMPARE(entries.at(0).color, classifyTrackSpeed(200.0 / 3.6).color);
	QCOMPARE(entries.at(1).color, classifyTrackSpeed(120.0 / 3.6).color);
	QCOMPARE(entries.at(2).color, freeTrackVisual().color);
	QCOMPARE(entries.at(2).lineWidth, freeTrackVisual().width);
	QCOMPARE(entries.at(2).penStyle, Qt::SolidLine);
	// The occupied sections have a red row after the speed classes.
	QCOMPARE(entries.at(3).label, "Occupied section");
	QCOMPARE(entries.at(3).color, occupiedTrackColor());
	QCOMPARE(entries.at(3).lineWidth, freeTrackVisual().width);
	QCOMPARE(entries.at(3).penStyle, Qt::SolidLine);
	QVERIFY2(std::none_of(entries.cbegin(), entries.cend(), [](const NetworkLegendEntry& entry) {
		return entry.kind == NetworkLegendEntryKind::Track && entry.trackState != TrackOperationalState::Free;
	}),
		"nonvisual operational states have no map-key swatches");
	// Rows 6 to 8 are the aspects, 9 an unavailable and 10 a failed signal, 11 the passenger count.
	// Signal rows follow the station row and precede the passenger row.
	QCOMPARE(entries.at(6).label, "Stop signal");
	QCOMPARE(entries.at(7).label, "Caution signal");
	QCOMPARE(entries.at(8).label, "Proceed signal");
	QCOMPARE(entries.at(9).label, "Unavailable signal");
	QCOMPARE(entries.at(10).label, "Failed signal");
	QCOMPARE(entries.at(11).label, "Passenger count");
}

void TestNetworkLegend::drawsSwatches() {
	LegendFixture f;
	NetworkLegendWidget& legend = f.legend;
	legend.setFixedWidth(180);
	QApplication::processEvents();
	auto* occupiedSwatch = legend.findChild<QWidget*>("mapKeySwatch3");
	// The occupied section swatch is a red line.
	if (occupiedSwatch == nullptr)
		QFAIL("the map key has no occupied section swatch");
	QVERIFY(containsColor(occupiedSwatch->grab().toImage(), occupiedTrackColor()));
	auto* trainSwatch = legend.findChild<QWidget*>("mapKeySwatch4");
	// The train swatch mirrors the historical locomotive fill.
	if (trainSwatch == nullptr)
		QFAIL("the map key has no train swatch");
	QVERIFY(containsColor(trainSwatch->grab().toImage(), defaultTrainFill()));
	auto* stationSwatch = legend.findChild<QWidget*>("mapKeySwatch5");
	// The station swatch keeps its compact map-key footprint.
	if (stationSwatch == nullptr)
		QFAIL("the map key has no station swatch");
	QCOMPARE(stationSwatch->size(), QSize(46, 18));
	auto* stopSignalSwatch = legend.findChild<QWidget*>("mapKeySwatch6");
	// The signal swatch renders the historical red plate.
	if (stopSignalSwatch == nullptr)
		QFAIL("the map key has no signal swatch");
	QVERIFY(containsColor(stopSignalSwatch->grab().toImage(), QColor(Qt::red)));
	auto* unavailableSwatch = legend.findChild<QWidget*>("mapKeySwatch9");
	// The lamp is a 12 pixel disc centred at (23, 9). The ring leaves the area inside it as the plain background.
	// The unavailable signal swatch is an empty gray ring.
	if (unavailableSwatch == nullptr)
		QFAIL("the map key has no unavailable signal swatch");
	const QImage unavailableImage = unavailableSwatch->grab().toImage();
	QVERIFY(containsColor(unavailableImage, QColor(150, 150, 150)));
	QVERIFY(!containsColor(unavailableImage, QColor(Qt::red)));
	QVERIFY(!containsColor(unavailableImage, QColor(Qt::green)));
	QCOMPARE(swatchPixel(unavailableImage, 23, 6), swatchPixel(unavailableImage, 0, 0));
	QCOMPARE(swatchPixel(unavailableImage, 23, 12), swatchPixel(unavailableImage, 0, 0));
	auto* failedSwatch = legend.findChild<QWidget*>("mapKeySwatch10");
	// Pixels at the four arm ends of the cross are white and the pixels between the arms are red.
	// The failed signal swatch is a red lamp with a white cross.
	if (failedSwatch == nullptr)
		QFAIL("the map key has no failed signal swatch");
	const QImage failedImage = failedSwatch->grab().toImage();
	QVERIFY(containsColor(failedImage, QColor(Qt::red)));
	QCOMPARE(swatchPixel(failedImage, 20, 6).rgb(), QColor(Qt::white).rgb());
	QCOMPARE(swatchPixel(failedImage, 25, 6).rgb(), QColor(Qt::white).rgb());
	QCOMPARE(swatchPixel(failedImage, 20, 11).rgb(), QColor(Qt::white).rgb());
	QCOMPARE(swatchPixel(failedImage, 25, 11).rgb(), QColor(Qt::white).rgb());
	QCOMPARE(swatchPixel(failedImage, 23, 5).rgb(), QColor(Qt::red).rgb());
	QCOMPARE(swatchPixel(failedImage, 27, 9).rgb(), QColor(Qt::red).rgb());
}

void TestNetworkLegend::removesDuplicateEntries() {
	LegendFixture f;
	int trainCount = 0;
	int stationCount = 0;
	bool stopSignalFound = false;
	bool passengerFound = false;
	for (const NetworkLegendEntry& entry : f.legend.entries()) {
		if (entry.kind == NetworkLegendEntryKind::Train) {
			++trainCount;
			// Trains with the default colour share one Train row.
			QCOMPARE(entry.label, "Train");
			QCOMPARE(entry.color, defaultTrainFill());
			QCOMPARE(entry.outlineColor, defaultTrainOutline());
		}
		if (entry.kind == NetworkLegendEntryKind::Station) {
			++stationCount;
			// The station entry shows the mark of its class.
			QCOMPARE(entry.stationClass, StationClass::Station);
			QCOMPARE(entry.label, "Station");
		}
		if (entry.signalCue == SignalCueKind::Stop) {
			stopSignalFound = true;
			// The signal entry uses the renderer classification.
			QCOMPARE(entry.iconResource, classifySignalAspect(0).iconResource);
		}
		if (entry.kind == NetworkLegendEntryKind::Passenger) {
			passengerFound = true;
			// The passenger entry uses the renderer icon.
			QCOMPARE(entry.iconResource, ":/icons/pax_icon.png");
		}
	}
	// Duplicate train colours are removed.
	QCOMPARE(trainCount, 1);
	// Duplicate station markers are removed.
	QCOMPARE(stationCount, 1);
	QVERIFY2(stopSignalFound, "signal cues are included");
	QVERIFY2(passengerFound, "passenger-load cue is included");
}

void TestNetworkLegend::collapsesAndExpands() {
	LegendFixture f;
	NetworkLegendWidget& legend = f.legend;
	auto* header = legend.findChild<QToolButton*>("mapKeyToggle");
	auto* body = legend.findChild<QWidget*>("mapKeyBody");
	const QStringList labelsBeforeCollapse = legend.entryLabels();
	legend.setExpanded(false);
	QApplication::processEvents();
	// The map key collapses.
	QVERIFY(!legend.isExpanded());
	if (body == nullptr)
		QFAIL("the map key has no body");
	QVERIFY(!body->isVisible());
	// The map key header remains visible while collapsed.
	if (header == nullptr)
		QFAIL("the map key has no header");
	QVERIFY(header->isVisible());
	legend.setExpanded(true);
	const QStringList labelsAfterExpand = legend.entryLabels();
	// Collapsing does not change map key entries.
	QCOMPARE(labelsAfterExpand.size(), labelsBeforeCollapse.size());
	QVERIFY(std::equal(labelsAfterExpand.cbegin(), labelsAfterExpand.cend(), labelsBeforeCollapse.cbegin()));
}

void TestNetworkLegend::rowsStayNarrow() {
	LegendFixture f;
	// A map key row stays within 160 logical pixels.
	for (QLabel* row : f.legend.findChildren<QLabel*>(QRegularExpression("^mapKeyEntry")))
		QCOMPARE_LE(row->maximumWidth(), 160);
}

void TestNetworkLegend::previewKeyExplainsSignalAspects() {
	LegendFixture f;
	NetworkLegendWidget& legend = f.legend;
	NetworkLegendContent previewContent;
	previewContent.hasTracks = true;
	previewContent.showOperationalTrackStates = false;
	previewContent.hasSelectedTrack = true;
	previewContent.stationVisuals = {classifyStation(true)};
	previewContent.hasSignals = true;
	legend.setCaseContent(previewContent);
	const QVector<NetworkLegendEntry> previewEntries = legend.entries();
	// The preview key explains every operational signal aspect.
	QCOMPARE(previewEntries.size(), qsizetype(10));
	QCOMPARE(previewEntries.at(0).color, classifyTrackSpeed(200.0 / 3.6).color);
	QCOMPARE(previewEntries.at(1).color, classifyTrackSpeed(120.0 / 3.6).color);
	QCOMPARE(previewEntries.at(2).color, classifyTrackSpeed(0.0).color);
	QCOMPARE(previewEntries.at(3).label, "Selected track");
	QCOMPARE(previewEntries.at(4).label, "Station");
	QCOMPARE(previewEntries.at(5).label, "Stop signal");
	QCOMPARE(previewEntries.at(6).label, "Caution signal");
	QCOMPARE(previewEntries.at(7).label, "Proceed signal");
	QCOMPARE(previewEntries.at(8).label, "Unavailable signal");
	QCOMPARE(previewEntries.at(9).label, "Failed signal");
	// The preview selected-track key matches the highlighted path.
	QCOMPARE(previewEntries.at(3).color, kSelectionCueColor);
	QCOMPARE(previewEntries.at(3).lineWidth, 4);
	QVERIFY2(!legend.entryLabels().contains("Occupied section"), "the track preview has no row for the occupied sections");
}

void TestNetworkLegend::stationSwatchesShowPinOfClass() {
	NetworkLegendWidget stationLegend;
	NetworkLegendContent stationContent;
	stationContent.stationVisuals = {classifyStation(), classifyStation(true), classifyStation(false, 3), classifyStation()};
	stationLegend.setCaseContent(stationContent);
	// The map key has one row for each station class on the canvas.
	QCOMPARE(stationLegend.entryLabels(), QStringList({"Major station", "Station", "Halt"}));
	QCOMPARE(stationLegend.entries().at(0).stationClass, StationClass::Major);
	QCOMPARE(stationLegend.entries().at(2).stationClass, StationClass::Halt);
	// Each station swatch shows the whole pin of its class, centred and clear of the edges, blue only for a major
	// station. The classes keep their sizes: the pin of a halt is shorter than that of a major station.
	stationLegend.show();
	QApplication::processEvents();
	QList<int> pinHeights;
	for (int row = 0; row < 3; ++row) {
		const QWidget* swatch = stationLegend.findChild<QWidget*>(QString("mapKeySwatch%1").arg(row));
		if (swatch == nullptr)
			QFAIL("the map key has no station swatch");
		QImage image(46, 18, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		const_cast<QWidget*>(swatch)->render(&image, QPoint(), QRegion(), QWidget::DrawChildren);
		// Some opaque pixel darker than mid grey: the dark halo that outlines the pin on the light panel.
		bool dark = false, blue = false;
		QRect painted;
		for (int y = 0; y < image.height(); ++y)
			for (int x = 0; x < image.width(); ++x) {
				const QColor colour = image.pixelColor(x, y);
				if (colour.alpha() > 0)
					painted |= QRect(x, y, 1, 1);
				dark = dark || (colour.alpha() == 255 && colour.value() < 128);
				blue = blue || colour == QColor(80, 120, 210);
			}
		const bool major = stationLegend.entries().at(row).stationClass == StationClass::Major;
		// A station swatch shows the whole pin of its class, centred.
		QCOMPARE(swatch->size(), QSize(46, 18));
		QCOMPARE_GT(painted.left(), 0);
		QCOMPARE_GT(painted.top(), 0);
		QCOMPARE_LT(painted.right(), 45);
		QCOMPARE_LT(painted.bottom(), 17);
		QCOMPARE_LE(std::abs((painted.left() + painted.right()) / 2.0 - 22.5), 1.0);
		QCOMPARE_LE(std::abs((painted.top() + painted.bottom()) / 2.0 - 8.5), 1.0);
		QVERIFY(dark);
		QCOMPARE(blue, major);
		pinHeights.append(painted.height());
		// The swatch, drawn four times as large, has the colours of the pin of its class on the canvas.
		QImage large(46 * 4, 18 * 4, QImage::Format_ARGB32_Premultiplied);
		large.setDevicePixelRatio(4.0);
		large.fill(Qt::transparent);
		const_cast<QWidget*>(swatch)->render(&large, QPoint(), QRegion(), QWidget::DrawChildren);
		StationMarkItem canvasPin(stationLegend.entries().at(row).stationClass, 60);
		QImage canvas(100, 100, QImage::Format_ARGB32_Premultiplied);
		canvas.fill(Qt::transparent);
		{
			QPainter painter(&canvas);
			painter.translate(20.0, 20.0);
			canvasPin.paint(&painter, nullptr, nullptr);
		}
		// A station swatch has the colours of the pin of its class on the canvas.
		QCOMPARE(pinColours(large), pinColours(canvas));
	}
	// The pin of a halt is shorter than the pin of a major station in the map key.
	QCOMPARE(stationLegend.entries().at(0).stationClass, StationClass::Major);
	QCOMPARE(pinHeights.size(), qsizetype(3));
	QCOMPARE_LT(pinHeights.at(2), pinHeights.at(0));
}

void TestNetworkLegend::defaultTrainsShareOneRow() {
	NetworkLegendWidget trainLegend;
	NetworkLegendContent trainContent;
	trainContent.trains = {defaultTrain(), defaultTrain()};
	trainLegend.setCaseContent(trainContent);
	const QVector<NetworkLegendEntry> rows = trainRows(trainLegend);
	// Trains that all use the default colour give one Train row.
	QCOMPARE(trainLegend.entryLabels(), QStringList{"Train"});
	QCOMPARE(rows.size(), qsizetype(1));
	QCOMPARE(rows.at(0).color, defaultTrainFill());
	QCOMPARE(rows.at(0).outlineColor, defaultTrainOutline());
}

void TestNetworkLegend::customColoursGetRowsOfTheirOwn() {
	NetworkLegendWidget trainLegend;
	NetworkLegendContent trainContent;
	trainContent.trains = {{kBlue, kBlue.darker(200), "201-2"}, defaultTrain(), {kGreen, kGreen.darker(200), "105-1"}, defaultTrain()};
	trainLegend.setCaseContent(trainContent);
	const QVector<NetworkLegendEntry> rows = trainRows(trainLegend);
	// Default plus two custom colours give three rows labelled with the service ids.
	QCOMPARE(trainLegend.entryLabels(), QStringList({"Train", "105-1", "201-2"}));
	QCOMPARE(rows.size(), qsizetype(3));
	QCOMPARE(rows.at(0).color, defaultTrainFill());
	QCOMPARE(rows.at(1).color, kGreen);
	QCOMPARE(rows.at(1).outlineColor, kGreen.darker(200));
	QCOMPARE(rows.at(2).color, kBlue);
	QCOMPARE(rows.at(2).outlineColor, kBlue.darker(200));
	trainLegend.show();
	QApplication::processEvents();
	const QColor swatchColors[] = {defaultTrainFill(), kGreen, kBlue};
	for (int row = 0; row < 3; ++row) {
		auto* swatch = trainLegend.findChild<QWidget*>(QString("mapKeySwatch%1").arg(row));
		if (swatch == nullptr)
			QFAIL("the map key has no train swatch");
		const QImage image = swatch->grab().toImage();
		QVERIFY2(containsColor(image, swatchColors[row]), "train swatch is filled with the colour of its row");
		QVERIFY2(row == 0 || !containsColor(image, defaultTrainFill()), "custom train swatch does not use the default fill");
	}
}

void TestNetworkLegend::servicesOfOneColourShareRow() {
	NetworkLegendWidget trainLegend;
	trainLegend.show();
	QApplication::processEvents();
	NetworkLegendContent trainContent;
	trainContent.trains = {{kBlue, kBlue.darker(200), "201-2"}, {kBlue, kBlue.darker(200), "201-1"},
		{kBlue, kBlue.darker(200), "201-1"}};
	trainLegend.setCaseContent(trainContent);
	const QVector<NetworkLegendEntry> rows = trainRows(trainLegend);
	// Services with the same colour share one row, each id once and sorted.
	QCOMPARE(rows.size(), qsizetype(1));
	QCOMPARE(rows.at(0).label, "201-1, 201-2");
	QCOMPARE(rows.at(0).color, kBlue);

	// Custom colours alone give no Train row.
	QCOMPARE(trainLegend.entryLabels(), QStringList{"201-1, 201-2"});
	auto* onlyCustomSwatch = trainLegend.findChild<QWidget*>("mapKeySwatch0");
	// The first swatch is the custom colour when there is no Train row.
	if (onlyCustomSwatch == nullptr)
		QFAIL("the map key has no train swatch");
	const QImage onlyCustomImage = onlyCustomSwatch->grab().toImage();
	QVERIFY(containsColor(onlyCustomImage, kBlue));
	QVERIFY(!containsColor(onlyCustomImage, defaultTrainFill()));
}

// Rows are ordered by the smallest service id of each row, in natural order, whatever the colour values or the order
// of the trains.
void TestNetworkLegend::ordersRowsByServiceId() {
	NetworkLegendWidget trainLegend;
	trainLegend.show();
	QApplication::processEvents();
	NetworkLegendContent trainContent;
	trainContent.trains = {{kBlue, kBlue.darker(200), "S10"}, {kGreen, kGreen.darker(200), "S2"}};
	trainLegend.setCaseContent(trainContent);
	// Services S2 and S10 are ordered by number, not by text.
	QCOMPARE(trainLegend.entryLabels(), QStringList({"S2", "S10"}));
	QCOMPARE(trainRows(trainLegend).at(0).color, kGreen);
	trainContent.trains = {{kGreen, kGreen.darker(200), "S2"}, {kBlue, kBlue.darker(200), "S10"}};
	trainLegend.setCaseContent(trainContent);
	// The row order does not depend on the order of the trains.
	QCOMPARE(trainLegend.entryLabels(), QStringList({"S2", "S10"}));
	trainContent.trains = {{kRed, kRed.darker(200), "S10"}, {kGreen, kGreen.darker(200), "S2"}, {kRed, kRed.darker(200), "S3"}};
	trainLegend.setCaseContent(trainContent);
	const QVector<NetworkLegendEntry> rows = trainRows(trainLegend);
	// A row is placed by its smallest service id and lists its ids in the same order.
	QCOMPARE(trainLegend.entryLabels(), QStringList({"S2", "S3, S10"}));
	QCOMPARE(rows.at(0).color, kGreen);
	QCOMPARE(rows.at(1).color, kRed);
	trainContent.trains = {defaultTrain(), {kRed, kRed.darker(200), "201-10"}, {kGreen, kGreen.darker(200), "201-9"}};
	trainLegend.setCaseContent(trainContent);
	// The Train row stays first and occurrence numbers sort by value.
	QCOMPARE(trainLegend.entryLabels(), QStringList({"Train", "201-9", "201-10"}));
}

void TestNetworkLegend::shortensLongListOfServices() {
	NetworkLegendWidget trainLegend;
	trainLegend.show();
	QApplication::processEvents();
	NetworkLegendContent trainContent;
	for (const char* id : {"S6", "S1", "S4", "S2", "S5", "S3"})
		trainContent.trains << NetworkLegendTrain{kBlue, kBlue.darker(200), id};
	trainLegend.setCaseContent(trainContent);
	const QVector<NetworkLegendEntry> rows = trainRows(trainLegend);
	// A long list of service ids is shortened in the label and complete in the tooltip.
	QCOMPARE(rows.size(), qsizetype(1));
	QCOMPARE(rows.at(0).label, "S1, S2, S3 and 3 more");
	QCOMPARE(rows.at(0).toolTip, "S1, S2, S3, S4, S5, S6");
	auto* longLabel = trainLegend.findChild<QLabel*>("mapKeyEntry0");
	// The row tooltip lists every service id.
	if (longLabel == nullptr)
		QFAIL("the map key has no row label");
	QCOMPARE(longLabel->toolTip(), "S1, S2, S3, S4, S5, S6");
}

void TestNetworkLegend::noTrainsGiveNoTrainRows() {
	NetworkLegendWidget trainLegend;
	trainLegend.show();
	QApplication::processEvents();
	NetworkLegendContent trainContent;
	trainContent.trains = {defaultTrain()};
	trainLegend.setCaseContent(trainContent);
	trainContent.trains.clear();
	trainLegend.setCaseContent(trainContent);
	QVERIFY2(trainRows(trainLegend).isEmpty(), "no trains give no train rows");
}

void TestNetworkLegend::wrappedTextFitsRail() {
	// Resizing the rail or changing the font must not clip wrapped speed ranges.
	QScrollArea rail;
	rail.setWidgetResizable(true);
	auto* constrainedLegend = new NetworkLegendWidget;
	constrainedLegend->setCaseContent(caseContent());
	rail.setWidget(constrainedLegend);
	rail.show();
	for (int fontSize : {11, 18}) {
		rail.setStyleSheet(QString("QWidget#mapKeyBody QLabel { font-size: %1px; }").arg(fontSize));
		for (int width : {180, 230}) {
			rail.setFixedSize(width, 200);
			QApplication::processEvents();
			QApplication::processEvents();
			for (QLabel* label : constrainedLegend->findChildren<QLabel*>(QRegularExpression("^mapKeyEntry"))) {
				// Wrapped map key text fits after width and font changes.
				QCOMPARE_GE(label->height(), label->heightForWidth(label->width()));
				QVERIFY2(label->parentWidget()->rect().contains(label->geometry()), "wrapped map key text stays within its own row");
			}
		}
	}

	// The label wraps its text as a QTextLayout with word wrap does. No width up to its maximum starts a line inside km/h.
	auto* highSpeed = constrainedLegend->findChild<QLabel*>("mapKeyEntry0");
	// The first row is the high speed track.
	if (highSpeed == nullptr)
		QFAIL("the map key has no first row");
	QVERIFY(highSpeed->text().startsWith("High speed track"));
	const QString text = highSpeed->text();
	const int unitStart = text.indexOf("km");
	const int unitEnd = text.indexOf('h', unitStart);
	bool unitWhole = unitStart > 0 && unitEnd > unitStart;
	for (int width = 1; width <= highSpeed->maximumWidth(); ++width) {
		QTextLayout layout(text, highSpeed->font());
		layout.beginLayout();
		for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
			line.setLineWidth(width);
			unitWhole = unitWhole && (line.textStart() <= unitStart || line.textStart() > unitEnd);
		}
		layout.endLayout();
	}
	QVERIFY2(unitWhole, "the high speed row never breaks a line inside km/h");
}

QTEST_MAIN(TestNetworkLegend)
#include "test_networklegend.moc"
