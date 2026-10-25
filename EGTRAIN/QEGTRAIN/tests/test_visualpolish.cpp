#include "graphics/VisualPolish.h"

#include <QGuiApplication>
#include <QFontMetricsF>
#include <QImage>
#include <QMap>
#include <QPainter>

#include "graphics/NetworkPresenter.h"
#include "graphics/items/TrainBadgeItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/SignalGeometry.h"

#include <QTest>

#include <cmath>
#include <tuple>
#include <utility>

static QByteArray renderStrokeMask(int width, Qt::PenStyle style) {
	QImage image(96, 20, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	{
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing, false);
		QPen pen(Qt::black, width, style);
		painter.setPen(pen);
		painter.drawLine(QLineF(4.0, 10.0, 91.0, 10.0));
	}
	QByteArray mask;
	mask.reserve(image.width() * image.height());
	for (int y = 0; y < image.height(); ++y)
		for (int x = 0; x < image.width(); ++x)
			mask.append(image.pixelColor(x, y).alpha() > 0 ? '1' : '0');
	return mask;
}

// Paints a head that is `size` device pixels wide on a black background. The
// image is 4 pixels wider than the head and the head is in its middle.
static QImage renderHead(int code, bool failed, int size) {
	SignalItem head(QRectF(-10, -10, 20, 20));
	head.setAspectCode(code);
	head.setFailed(failed);
	QImage image(size + 4, size + 4, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::black);
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.translate(image.width() / 2.0, image.height() / 2.0);
	painter.scale(size / 20.0, size / 20.0);
	head.paint(&painter, nullptr, nullptr);
	return image;
}

// Paints with paint, antialiased as the canvas paints, on the canvas colour, under a scale at which 20 scene units are
// size logical pixels, on an image of the device pixel ratio dpr. The image is 4 logical pixels wider than that, rounded
// up, and the origin is in its middle.
template <typename Paint>
static QImage paintOnCanvas(qreal size, Paint paint, qreal dpr = 1.0) {
	const int side = static_cast<int>(std::ceil(size)) + 4;
	QImage image(static_cast<int>(side * dpr), static_cast<int>(side * dpr), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(dpr);
	image.fill(canvasColor());
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.translate(side / 2.0, side / 2.0);
	painter.scale(size / 20.0, size / 20.0);
	paint(painter);
	return image;
}

// A head 20 scene units wide with the outline that the canvas gives it, painted as paintOnCanvas() does.
static QImage renderCanvasHead(int code, bool failed, qreal size, qreal dpr = 1.0) {
	return paintOnCanvas(
		size,
		[&](QPainter& painter) {
			SignalItem head(QRectF(-10, -10, 20, 20));
			head.setPen(signalHeadPen());
			head.setAspectCode(code);
			head.setFailed(failed);
			head.paint(&painter, nullptr, nullptr);
		},
		dpr);
}

// A green lamp 20 scene units wide in the hairline of the reference, painted as paintOnCanvas() does.
static QImage hairlineLamp(qreal size, qreal dpr = 1.0) {
	return paintOnCanvas(
		size,
		[](QPainter& painter) {
			painter.setPen(signalHeadPen());
			painter.setBrush(QColor(Qt::green));
			painter.drawEllipse(QRectF(-10, -10, 20, 20));
		},
		dpr);
}

// Number of pixels of the mark colour (dark, or white for the failed cross) on
// the face of the lamp, away from its outline.
static int markPixels(const QImage& image, bool light = false) {
	const double c = image.width() / 2.0, r = (image.width() - 4) / 2.0 * 0.9;
	const QColor markColor = light ? QColor(Qt::white) : QColor(30, 30, 30);
	int count = 0;
	for (int y = 0; y < image.height(); ++y)
		for (int x = 0; x < image.width(); ++x)
			count += std::hypot(x + 0.5 - c, y + 0.5 - c) <= r && image.pixelColor(x, y) == markColor;
	return count;
}

// Number of pixels of image that are nearer to colour than to the canvas colour.
static int lampPixels(const QImage& image, const QColor& colour) {
	const auto distance = [](const QColor& a, const QColor& b) {
		return std::hypot(a.red() - b.red(), a.green() - b.green(), a.blue() - b.blue());
	};
	int count = 0;
	for (int y = 0; y < image.height(); ++y)
		for (int x = 0; x < image.width(); ++x)
			count += distance(image.pixelColor(x, y), colour) < distance(image.pixelColor(x, y), canvasColor());
	return count;
}

static int pixelsOfColor(const QImage& image, const QColor& color) {
	int count = 0;
	for (int y = 0; y < image.height(); ++y)
		for (int x = 0; x < image.width(); ++x)
			count += image.pixelColor(x, y) == color;
	return count;
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

// The colours, line styles, icons and marks with which the network view shows tracks, signals, trains and stations.
class TestVisualPolish : public QObject {
	Q_OBJECT

private slots:
	void trackSpeedClasses();
	void trackStateUnderlays();
	void signalAspects();
	void trainKindsAndIcons();
	void trainColours_data();
	void trainColours();
	void serviceColour();
	void trainBadgeShapes();
	void stationClasses();
	void stationPins_data();
	void stationPins();
	void stationMenuIcons_data();
	void stationMenuIcons();
	void stationSquares();
	void speedLabels();
	void trainBadge();
	void signalGeometries();
	void signalHead();
	void headStatesWithoutColour();
	void canvasHeads();
};

void TestVisualPolish::trackSpeedClasses() {
	const TrackVisual freeBase = freeTrackVisual();
	// The local track uses historical gray.
	QCOMPARE(freeBase.color, QColor(120, 120, 120));
	// Historical high-speed boundary.
	QCOMPARE(classifyTrackSpeed(200.0 / 3.6).color, QColor(30, 130, 210));
	QCOMPARE(classifyTrackSpeed(200.0 / 3.6).width, 4);
	// Historical mainline boundary.
	QCOMPARE(classifyTrackSpeed(120.0 / 3.6).color, QColor(160, 160, 160));
	QCOMPARE(classifyTrackSpeed(120.0 / 3.6).width, 3);
	// Historical local boundary.
	QCOMPARE(classifyTrackSpeed(119.0 / 3.6).color, QColor(120, 120, 120));
	// The free track uses one documented overview width.
	QCOMPARE(freeBase.width, 2);
}

void TestVisualPolish::trackStateUnderlays() {
	const TrackStateVisual freeTrack = classifyTrackState(TrackOperationalState::Free);
	const TrackStateVisual permissiveTrack = classifyTrackState(TrackOperationalState::Prepared);
	const TrackStateVisual occupiedTrack = classifyTrackState(TrackOperationalState::Occupied);
	const TrackStateVisual blockedTrack = classifyTrackState(TrackOperationalState::Blocked);
	// A free track has no underlay.
	QCOMPARE(freeTrack.style, Qt::NoPen);
	QCOMPARE(freeTrack.width, 0);
	// Permissive signalling underlay.
	QCOMPARE(permissiveTrack.style, Qt::DashDotLine);
	QCOMPARE(permissiveTrack.width, 5);
	// Occupied track underlay.
	QCOMPARE(occupiedTrack.style, Qt::SolidLine);
	QCOMPARE(occupiedTrack.width, 6);
	// A blocked track has a non-color cue.
	QCOMPARE(blockedTrack.style, Qt::DashLine);
	QCOMPARE(blockedTrack.width, 5);
	// Permissive signalling color.
	QCOMPARE(permissiveTrack.color, QColor("#4C8DAE"));
	// Occupied track color.
	QCOMPARE(occupiedTrackColor(), QColor(255, 0, 0));
	QCOMPARE(occupiedTrack.color, occupiedTrackColor());
	// Blocked track color.
	QCOMPARE(blockedTrack.color, QColor("#D6A13A"));
	const QByteArray permissiveMask = renderStrokeMask(permissiveTrack.width, permissiveTrack.style);
	const QByteArray occupiedMask = renderStrokeMask(occupiedTrack.width, occupiedTrack.style);
	const QByteArray blockedMask = renderStrokeMask(blockedTrack.width, blockedTrack.style);
	// Track states remain distinguishable by stroke structure without color.
	QCOMPARE_NE(permissiveMask, occupiedMask);
	QCOMPARE_NE(occupiedMask, blockedMask);
	QCOMPARE_NE(permissiveMask, blockedMask);
	// Free track priority.
	QCOMPARE_LT(trackStatePriority(TrackOperationalState::Free), trackStatePriority(TrackOperationalState::Prepared));
	// Permissive signalling priority.
	QCOMPARE_LT(trackStatePriority(TrackOperationalState::Prepared), trackStatePriority(TrackOperationalState::Occupied));
	// Occupied track priority.
	QCOMPARE_LT(trackStatePriority(TrackOperationalState::Occupied), trackStatePriority(TrackOperationalState::Blocked));
}

void TestVisualPolish::signalAspects() {
	const SignalVisual stopSignal = classifySignalAspect(0);
	const SignalVisual cautionSignal = classifySignalAspect(75);
	const SignalVisual proceed180Signal = classifySignalAspect(180);
	const SignalVisual proceed270Signal = classifySignalAspect(270);
	// Red stop signal cue.
	QCOMPARE(stopSignal.lamp, QColor(Qt::red));
	QCOMPARE(stopSignal.cue, SignalCueKind::Stop);
	// Yellow caution signal cue.
	QCOMPARE(cautionSignal.lamp, QColor(Qt::yellow));
	QCOMPARE(cautionSignal.cue, SignalCueKind::Caution);
	// Green proceed signal cue 180.
	QCOMPARE(proceed180Signal.lamp, QColor(Qt::green));
	QCOMPARE(proceed180Signal.cue, SignalCueKind::Proceed);
	// Green proceed signal cue 270.
	QCOMPARE(proceed270Signal.lamp, QColor(Qt::green));
	QCOMPARE(proceed270Signal.cue, SignalCueKind::Proceed);
	// Code 751 is a stop.
	QCOMPARE(classifySignalAspect(751).lamp, QColor(Qt::red));
	QCOMPARE(classifySignalAspect(751).cue, SignalCueKind::Stop);
	QCOMPARE(classifySignalAspect(751).iconResource, QStringLiteral(":/icons/signal-stop.svg"));
	QCOMPARE(classifySignalCue(751), SignalCueKind::Stop);
	// Unknown codes are neither stop, caution nor proceed.
	QCOMPARE(classifySignalCue(-1), SignalCueKind::Neutral);
	QCOMPARE(classifySignalCue(42), SignalCueKind::Neutral);
	// Neutral signal icon.
	QCOMPARE(classifySignalAspect(-1).iconResource, QStringLiteral(":/icons/signal-neutral.svg"));
	// Stop signal icon.
	QCOMPARE(classifySignalAspect(0).iconResource, QStringLiteral(":/icons/signal-stop.svg"));
	// Caution signal icon.
	QCOMPARE(classifySignalAspect(75).iconResource, QStringLiteral(":/icons/signal-caution.svg"));
	// Proceed signal icon.
	QCOMPARE(classifySignalAspect(180).iconResource, QStringLiteral(":/icons/signal-proceed.svg"));
}

void TestVisualPolish::trainKindsAndIcons() {
	// Freight train classification.
	QCOMPARE(classifyTrainType("freight", "F01").kind, TrainVisualKind::Freight);
	// Intercity train classification.
	QCOMPARE(classifyTrainType("IC", "IC 2201").kind, TrainVisualKind::Intercity);
	// Sprinter train classification.
	QCOMPARE(classifyTrainType("", "sprinter 301").kind, TrainVisualKind::Sprinter);
	// High-speed train classification.
	QCOMPARE(classifyTrainType("", "ICE 10").kind, TrainVisualKind::HighSpeed);
	// Passenger train icon.
	QCOMPARE(classifyTrainType("", "regional").iconResource, QStringLiteral(":/icons/train-passenger.svg"));
	// Sprinter train icon.
	QCOMPARE(classifyTrainType("", "sprinter 301").iconResource, QStringLiteral(":/icons/train-sprinter.svg"));
	// Intercity train icon.
	QCOMPARE(classifyTrainType("IC", "IC 2201").iconResource, QStringLiteral(":/icons/train-intercity.svg"));
	// High-speed train icon.
	QCOMPARE(classifyTrainType("", "ICE 10").iconResource, QStringLiteral(":/icons/train-high-speed.svg"));
	// Freight train icon.
	QCOMPARE(classifyTrainType("freight", "F01").iconResource, QStringLiteral(":/icons/train-freight.svg"));
	// Default train colours.
	QCOMPARE(defaultTrainFill(), QColor(235, 210, 55));
	QCOMPARE(defaultTrainOutline(), QColor(110, 90, 20));
}

void TestVisualPolish::trainColours_data() {
	QTest::addColumn<QString>("typeText");
	QTest::addColumn<QString>("nameText");
	const std::pair<const char*, const char*> trainInputs[] = {
		{"freight", "F01"}, {"IC", "IC 2201"}, {"", "sprinter 301"}, {"", "ICE 10"}, {"", "regional"}};
	for (const auto& input : trainInputs)
		QTest::newRow(input.second) << QString::fromLatin1(input.first) << QString::fromLatin1(input.second);
}

void TestVisualPolish::trainColours() {
	QFETCH(QString, typeText);
	QFETCH(QString, nameText);
	const std::string type = typeText.toStdString();
	const std::string name = nameText.toStdString();
	const TrainVisual classified = classifyTrainType(type, name);
	const TrainVisual resolved = resolveTrainVisual(type, name);
	const TrainVisual invalid = resolveTrainVisual(type, name, QColor());
	// A classified train uses the default colours whatever its type.
	QCOMPARE(classified.fill, QColor(235, 210, 55));
	QCOMPARE(classified.outline, QColor(110, 90, 20));
	// A resolved train without a service colour uses the default colours.
	QCOMPARE(resolved.fill, QColor(235, 210, 55));
	QCOMPARE(resolved.outline, QColor(110, 90, 20));
	// An invalid service colour gives the default colours.
	QCOMPARE(invalid.fill, QColor(235, 210, 55));
	QCOMPARE(invalid.outline, QColor(110, 90, 20));
	// A resolved train keeps the classified kind, shape and icon.
	QCOMPARE(resolved.kind, classified.kind);
	QCOMPARE(resolved.shape, classified.shape);
	QCOMPARE(resolved.iconResource, classified.iconResource);
}

void TestVisualPolish::serviceColour() {
	const QColor serviceColor(40, 130, 210);
	const TrainVisual coloured = resolveTrainVisual("IC", "IC 2201", serviceColor);
	// A service colour sets the fill and a darker outline.
	QCOMPARE(coloured.fill, serviceColor);
	QCOMPARE(coloured.outline, serviceColor.darker(200));
	QCOMPARE_NE(coloured.outline, coloured.fill);
	// A service colour keeps the classified kind and icon.
	QCOMPARE(coloured.kind, TrainVisualKind::Intercity);
	QCOMPARE(coloured.iconResource, QStringLiteral(":/icons/train-intercity.svg"));
}

void TestVisualPolish::trainBadgeShapes() {
	const TrainVisual intercity = classifyTrainType("IC", "IC 2201");
	const TrainVisual sprinter = classifyTrainType("", "sprinter 301");
	const TrainVisual freight = classifyTrainType("freight", "F01");
	// Intercity badge shape.
	QCOMPARE(intercity.shape, TrainBadgeShape::Capsule);
	// Sprinter badge shape.
	QCOMPARE(sprinter.shape, TrainBadgeShape::Rounded);
	// Freight badge shape.
	QCOMPARE(freight.shape, TrainBadgeShape::Square);
	// Train category silhouettes.
	QCOMPARE_NE(intercity.shape, sprinter.shape);
	QCOMPARE_NE(intercity.shape, freight.shape);
	QCOMPARE_NE(sprinter.shape, freight.shape);
}

void TestVisualPolish::stationClasses() {
	// Three connections make a major station, a platform a station, neither a halt.
	QCOMPARE(classifyStation(false, 3).cls, StationClass::Major);
	QCOMPARE(classifyStation(true, 0).cls, StationClass::Station);
	QCOMPARE(classifyStation(false, 0).cls, StationClass::Halt);
}

// The pin in the frame (8, 24, 48, 48) with a halo of 4, so at twice the pixel size: its tip on the bottom centre
// (32, 72), its head of radius 0.3 of 48 times the size of the class, centred 0.78 of 48 times the size above the
// tip. The probes: the centre of the head, the body below it, the left edge of the head, the halo above the head,
// the halo under the tip and the canvas left of, right of and below the tip.
void TestVisualPolish::stationPins_data() {
	QTest::addColumn<int>("cls");
	QTest::addColumn<qreal>("size");
	QTest::addColumn<QByteArray>("expected");
	QTest::newRow("major station") << static_cast<int>(StationClass::Major) << qreal(1.15) << QByteArray("WBWDDTTT");
	QTest::newRow("station") << static_cast<int>(StationClass::Station) << qreal(1.0) << QByteArray("DWWDDTTT");
	QTest::newRow("halt") << static_cast<int>(StationClass::Halt) << qreal(0.8) << QByteArray("WDWDDTTT");
}

// The far mark is a pin with a dark halo and its tip on the bottom centre: blue with a light edge and dot for a major
// station, light with a dark dot for a station, dark with a light edge and dot for a halt.
void TestVisualPolish::stationPins() {
	QFETCH(int, cls);
	QFETCH(qreal, size);
	QFETCH(QByteArray, expected);
	// The colour class of a pixel of a mark painted white with a black halo: W white, B the blue of a major station, D the
	// halo, T transparent, ? anything else such as an edge that antialiasing blends.
	const auto pixelClass = [](const QImage& image, int x, int y) {
		const QColor colour = image.pixelColor(x, y);
		if (colour.alpha() == 0)
			return 'T';
		if (colour.alpha() < 255)
			return '?';
		if (colour.lightness() > 200)
			return 'W';
		if (colour.lightness() < 50)
			return 'D';
		return colour.blue() > colour.red() + 80 ? 'B' : '?';
	};
	const auto probeRow = [&pixelClass](const QImage& image, const QList<QPoint>& points) {
		QByteArray row;
		for (const QPoint& point : points)
			row.append(pixelClass(image, point.x(), point.y()));
		return row;
	};
	const int head = qRound(72.0 - 0.78 * 48.0 * size);
	const int radius = qRound(0.3 * 48.0 * size);
	QImage mark(64, 80, QImage::Format_ARGB32_Premultiplied);
	mark.fill(Qt::transparent);
	{
		QPainter painter(&mark);
		paintStationMark(&painter, QRectF(8.0, 24.0, 48.0, 48.0), static_cast<StationClass>(cls), Qt::white, Qt::black, 4.0);
	}
	const QList<QPoint> pinPoints{{32, head}, {32, head + radius * 7 / 10}, {33 - radius, head},
		{32, head - radius - 2}, {32, 74}, {24, 74}, {40, 74}, {32, 78}};
	QCOMPARE(probeRow(mark, pinPoints), expected);
}

void TestVisualPolish::stationMenuIcons_data() {
	QTest::addColumn<int>("classValue");
	QTest::newRow("major station") << static_cast<int>(StationClass::Major);
	QTest::newRow("station") << static_cast<int>(StationClass::Station);
	QTest::newRow("halt") << static_cast<int>(StationClass::Halt);
}

// The menu icon fits the whole pin with its halo and centres it: the outer columns stay empty, and the outer rows hold
// at most the dark halo, so that a pin cut at its top or its tip shows there. Only the pin of a major station is blue.
// At 32 pixels it has the colours of the pin of its class on the canvas.
void TestVisualPolish::stationMenuIcons() {
	QFETCH(int, classValue);
	const auto cls = static_cast<StationClass>(classValue);
	for (const int size : {16, 32}) {
		const QImage icon = stationMarkIcon(cls).pixmap(QSize(size, size)).toImage();
		const QRectF area(0.0, 0.0, size, size);
		const QRectF painted = stationMarkBounds(stationMarkFrameIn(area, cls, size / 16.0), cls, size / 16.0);
		// Next to nothing, or dark as the halo: the pins are light or blue where they meet the halo.
		const auto halo = [](const QColor& colour) { return colour.alpha() <= 8 || colour.value() < 96; };
		bool edgesEmpty = icon.width() == size && icon.height() == size;
		bool blue = false;
		for (int y = 0; y < icon.height(); ++y) {
			edgesEmpty = edgesEmpty && icon.pixelColor(0, y).alpha() == 0
				&& icon.pixelColor(icon.width() - 1, y).alpha() == 0;
			for (int x = 0; x < icon.width(); ++x) {
				blue = blue || icon.pixelColor(x, y) == QColor(80, 120, 210);
				if (y == 0 || y == icon.height() - 1)
					edgesEmpty = edgesEmpty && halo(icon.pixelColor(x, y));
			}
		}
		// The menu icon shows the whole pin, centred, blue only for a major station.
		QVERIFY2(edgesEmpty, qPrintable(QStringLiteral("at %1 pixels").arg(size)));
		QCOMPARE(blue, cls == StationClass::Major);
		QVERIFY2(area.adjusted(-0.01, -0.01, 0.01, 0.01).contains(painted), qPrintable(QStringLiteral("at %1 pixels").arg(size)));
		QCOMPARE_LT(QLineF(painted.center(), area.center()).length(), 0.01);
	}
	StationMarkItem canvasPin(cls, 60);
	QImage canvas(100, 100, QImage::Format_ARGB32_Premultiplied);
	canvas.fill(Qt::transparent);
	{
		QPainter painter(&canvas);
		painter.translate(20.0, 20.0);
		canvasPin.paint(&painter, nullptr, nullptr);
	}
	const QImage icon = stationMarkIcon(cls).pixmap(QSize(32, 32)).toImage();
	// The menu icon has the colours of the pin of its class on the canvas.
	QCOMPARE(pinColours(icon), pinColours(canvas));
}

void TestVisualPolish::stationSquares() {
	// Platform station square.
	QCOMPARE(classifyStation(true, 0).fill, QColor(70, 70, 70));
	// Interchange station square.
	QCOMPARE(classifyStation(false, 3).fill, QColor(80, 120, 210));
}

void TestVisualPolish::speedLabels() {
	// Fastest speed label.
	QCOMPARE(simulationSpeedLabel(0), QStringLiteral("Fastest"));
	// Delayed speed label.
	QCOMPARE(simulationSpeedLabel(250), QStringLiteral("4.0x"));
	// Fast factor speed label.
	QCOMPARE(simulationSpeedLabel(10), QStringLiteral("100x"));
}

void TestVisualPolish::trainBadge() {
	TrainBadgeItem badge;
	badge.setIdentifier("1725");
	badge.setTooltipDetails("Intercity 1725 northbound", "1725", "Intercity");
	badge.setSpeedText("102 km/h");
	// The badge has no shape and no tooltip.
	QVERIFY(badge.shape().isEmpty());
	QVERIFY(badge.toolTip().isEmpty());
	// The badge four times as large on a mainline track, with its position in the middle of the image.
	const QColor mainline = classifyTrackSpeed(120.0 / 3.6).color;
	const QTransform badgeScale = QTransform::fromTranslate(240.0, 120.0).scale(4.0, 4.0);
	const auto renderBadge = [&badge, &mainline, &badgeScale]() {
		QImage image(480, 240, QImage::Format_ARGB32_Premultiplied);
		image.fill(mainline);
		{
			QPainter painter(&image);
			painter.setTransform(badgeScale);
			badge.paint(&painter, nullptr, nullptr);
		}
		return image;
	};
	const auto drawsNothing = [&]() {
		return badge.boundingRect().isEmpty() && pixelsOfColor(renderBadge(), mainline) == 480 * 240;
	};
	QVERIFY2(drawsNothing(), "an Overview badge draws nothing");
	badge.setPresentation(TrainBadgeItem::Presentation::Identity);
	QVERIFY2(drawsNothing(), "an Identity badge draws nothing");
	badge.setPresentation(TrainBadgeItem::Presentation::Detailed);
	const QImage speedImage = renderBadge();
	// A Detailed badge draws the speed in white with a dark outline.
	QCOMPARE_GT(pixelsOfColor(speedImage, Qt::white), 100);
	QCOMPARE_GT(pixelsOfColor(speedImage, QColor(16, 26, 34)), 100);
	const QRectF speedBounds = badge.boundingRect();
	const QRect drawnBounds = badgeScale.mapRect(speedBounds).toAlignedRect();
	int outside = 0;
	for (int y = 0; y < speedImage.height(); ++y)
		for (int x = 0; x < speedImage.width(); ++x)
			outside += !drawnBounds.contains(x, y) && speedImage.pixelColor(x, y) != mainline;
	// The speed stays inside the bounds of the badge.
	QCOMPARE(outside, 0);
	// The speed is drawn above and right of the train.
	QCOMPARE_GT(speedBounds.left(), 0.0);
	QCOMPARE_LT(speedBounds.bottom(), 0.0);
	badge.setSpeedVisible(false);
	QVERIFY2(drawsNothing(), "a badge with the speed hidden draws nothing");
	badge.setSpeedVisible(true);
	badge.setSpeedText(QString());
	QVERIFY2(drawsNothing(), "a badge without a speed draws nothing");
	// The speed, promoted or not, stays under the station names, which stay readable.
	const qreal unpromoted = badge.zValue();
	badge.setPromoted(true);
	// The speed of a train, promoted or not, is stacked under the station names.
	QCOMPARE_LT(unpromoted, StationNameItem::NameZValue);
	QCOMPARE_LT(badge.zValue(), StationNameItem::NameZValue);
	badge.setPromoted(false);
}

void TestVisualPolish::signalGeometries() {
	const SignalGeometry horizontal = signalGeometry(QPointF(), QPointF(-8, 0),
		QPointF(8, 0), QPointF(0, 1), 150);
	// Interpolated horizontal endpoints place the reversed signal to the left.
	QCOMPARE(horizontal.reversedHead, QPointF(-8, -30));
	QCOMPARE(horizontal.forwardHead, QPointF(8, 30));
	const SignalGeometry diagonal = signalGeometry(QPointF(40, 20), QPointF(34, 12),
		QPointF(46, 28), QPointF(-0.8, 0.6), 150);
	// Non-horizontal endpoints preserve direction and authored normal.
	QCOMPARE(diagonal.reversedHead, QPointF(58, -6));
	QCOMPARE(diagonal.forwardHead, QPointF(22, 46));
	const qreal presentationScale = 0.45;
	const SignalGeometry measured = signalGeometry(QPointF(), QPointF(-8, 0),
		QPointF(8, 0), QPointF(0, 1), 150 * presentationScale);
	// The historical presentation conversion affects lateral separation, not physical longitudinal signal points.
	QCOMPARE_EQ(measured.reversedHead.x(), -8);
	QCOMPARE_EQ(measured.forwardHead.x(), 8);
	QCOMPARE_LT(std::fabs(measured.reversedHead.y() + 13.5), 1e-12);
	QCOMPARE_LT(std::fabs(measured.forwardHead.y() - 13.5), 1e-12);
	QCOMPARE_LT(std::fabs(measured.forwardBase.length() - 13.5), 1e-12);
	QCOMPARE_EQ(measured.forwardPost.length(), 8);
}

void TestVisualPolish::signalHead() {
	SignalItem head(QRectF(-10, -10, 20, 20));
	head.setAspectCode(0);
	QVERIFY2(!head.flags().testFlag(QGraphicsItem::ItemIgnoresTransformations),
		"signal heads scale with the scene");
	QVERIFY2(head.childItems().isEmpty(),
		"signal head has no invisible child for every paint to visit; the scene picks it in its pick circle");
	QImage signalImage(48, 48, QImage::Format_ARGB32_Premultiplied);
	signalImage.fill(Qt::black);
	{
		QPainter painter(&signalImage);
		painter.translate(24, 24);
		head.paint(&painter, nullptr, nullptr);
	}
	// The stop mark is a bar through the centre, so sample beside it. An individual stop head paints its own aspect
	// without sectors or direction ticks.
	QCOMPARE(signalImage.pixelColor(24, 24 - 5), QColor(Qt::red));
}

// Every state can be told apart without colour where a mark fits, and only the colour is left below 6 device pixels.
void TestVisualPolish::headStatesWithoutColour() {
	const QImage stop = renderHead(0, false, 48), bacc = renderHead(751, false, 48);
	const QImage caution = renderHead(75, false, 48), proceed = renderHead(180, false, 48);
	const QImage clear = renderHead(270, false, 48), unavailable = renderHead(-1, false, 48);
	const QImage failed = renderHead(270, true, 48);
	const int c = stop.width() / 2;
	// Stop and BACC second red heads are red.
	QCOMPARE(stop.pixelColor(c, c - 10), QColor(Qt::red));
	QCOMPARE(bacc.pixelColor(c, c - 10), QColor(Qt::red));
	// The caution head is yellow.
	QCOMPARE(caution.pixelColor(c, c - 10), QColor(Qt::yellow));
	// Codes 180 and 270 are green.
	QCOMPARE(proceed.pixelColor(c, c - 10), QColor(Qt::green));
	QCOMPARE(clear.pixelColor(c, c - 10), QColor(Qt::green));
	// A proceed head has no mark.
	QCOMPARE(markPixels(proceed), 0);
	QCOMPARE(markPixels(clear), 0);
	// A stop head has a bar through its centre.
	QCOMPARE(stop.pixelColor(c, c), QColor(30, 30, 30));
	QCOMPARE(stop.pixelColor(c + 10, c), QColor(30, 30, 30));
	QCOMPARE(stop.pixelColor(c + 10, c - 4), QColor(Qt::red));
	// A caution head has a dot in its centre and no bar.
	QCOMPARE(caution.pixelColor(c, c), QColor(30, 30, 30));
	QCOMPARE(caution.pixelColor(c + 10, c), QColor(Qt::yellow));
	// Stop and caution heads carry different marks.
	QCOMPARE_GT(markPixels(stop), 0);
	QCOMPARE_GT(markPixels(caution), 0);
	QCOMPARE_NE(markPixels(stop), markPixels(caution));
	QVERIFY2(stop == bacc, "code 751 is painted like code 0");
	// An unavailable head is an empty gray ring with a dash.
	QCOMPARE(unavailable.pixelColor(c, c - 10), QColor(Qt::black));
	QCOMPARE(pixelsOfColor(unavailable, QColor(Qt::red)) + pixelsOfColor(unavailable, QColor(Qt::yellow))
			+ pixelsOfColor(unavailable, QColor(Qt::green)),
		0);
	QCOMPARE_GT(pixelsOfColor(unavailable, QColor(150, 150, 150)), 40);
	QCOMPARE(unavailable.pixelColor(c, c), QColor(150, 150, 150));
	QVERIFY2(renderHead(42, false, 48) == unavailable, "a code that no aspect uses is painted as unavailable");
	// A failed head is red with a white cross, whatever its code.
	QCOMPARE(failed.pixelColor(c, c - 10), QColor(Qt::red));
	QCOMPARE(failed.pixelColor(c + 10, c), QColor(Qt::red));
	QCOMPARE(failed.pixelColor(c + 6, c + 6), QColor(Qt::white));
	QCOMPARE(failed.pixelColor(c - 6, c + 6), QColor(Qt::white));
	QCOMPARE(failed.pixelColor(c + 6, c - 6), QColor(Qt::white));
	QCOMPARE(failed.pixelColor(c - 6, c - 6), QColor(Qt::white));
	// A failed head ignores its code.
	QVERIFY(renderHead(-1, true, 48) == failed);
	QVERIFY(renderHead(0, true, 48) == failed);
	// A failed head differs from a stop head without colour.
	QCOMPARE_GT(markPixels(failed, true), 0);
	QCOMPARE(markPixels(stop, true), 0);
	for (const int code : {0, 75}) {
		// Marks appear at 6 device pixels and not below.
		QCOMPARE_GT(markPixels(renderHead(code, false, 6)), 0);
		QCOMPARE(markPixels(renderHead(code, false, 5)), 0);
	}
	// The dash and the cross follow the same 6 pixel rule.
	QCOMPARE(renderHead(-1, false, 6).pixelColor(5, 5), QColor(150, 150, 150));
	QCOMPARE(renderHead(-1, false, 5).pixelColor(4, 4), QColor(Qt::black));
	QCOMPARE(markPixels(renderHead(75, true, 5), true), 0);
	QCOMPARE_GT(markPixels(renderHead(75, true, 6), true), 0);
	// Colour remains below 6 device pixels.
	QCOMPARE(renderHead(0, false, 5).pixelColor(3, 3), QColor(Qt::red));
	QCOMPARE(renderHead(75, false, 5).pixelColor(3, 3), QColor(Qt::yellow));
	QCOMPARE(renderHead(180, false, 5).pixelColor(3, 3), QColor(Qt::green));
}

// On the canvas a hairline covers a lamp of under a pixel, so a lit or failed head below the mark size is a disc in its
// lamp colour (issue #1109). From 6 device pixels the head keeps the hairline of the reference.
void TestVisualPolish::canvasHeads() {
	const int ringPixels = lampPixels(renderCanvasHead(-1, false, 0.8), QColor(150, 150, 150));
	// An unavailable head of 0.8 device pixels shows its grey ring on the canvas.
	QCOMPARE_GT(ringPixels, 0);
	for (const auto& [code, failedHead, lamp] : {std::tuple<int, bool, QColor>{180, false, Qt::green}, {0, false, Qt::red},
			 {75, false, Qt::yellow}, {180, true, Qt::red}})
		// A lit or failed head of 0.8 device pixels shows its lamp colour as clearly as an unavailable head its ring.
		QCOMPARE_GE(lampPixels(renderCanvasHead(code, failedHead, 0.8), lamp), ringPixels);
	for (const qreal size : {6.0, 10.0})
		QVERIFY2(renderCanvasHead(180, false, size) == hairlineLamp(size),
			"a proceed head of 6 or 10 device pixels is the green lamp in the hairline of the reference");
	// On a screen of device pixel ratio 2 a proceed head has the hairline from 3 logical pixels, 6 device pixels.
	QVERIFY(renderCanvasHead(180, false, 3.0, 2.0) == hairlineLamp(3.0, 2.0));
	QVERIFY(renderCanvasHead(180, false, 2.9, 2.0) != hairlineLamp(2.9, 2.0));
}

// The test paints without widgets, as the network view does, on the offscreen platform also outside CTest. QTEST_MAIN
// would create a QApplication, so the program keeps this main.
int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QGuiApplication app(argc, argv);
	TestVisualPolish test;
	return QTest::qExec(&test, argc, argv);
}

#include "test_visualpolish.moc"
