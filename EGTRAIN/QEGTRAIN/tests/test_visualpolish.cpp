#include "graphics/VisualPolish.h"

#include <QGuiApplication>
#include <QFontMetricsF>
#include <QImage>
#include <QMap>
#include <QPainter>

#include "graphics/items/TrainBadgeItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/SignalGeometry.h"

#include <iostream>
#include <cmath>
#include <tuple>
#include <utility>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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

int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QGuiApplication app(argc, argv);
	bool ok = true;
	const TrackVisual freeBase = freeTrackVisual();
	ok &= expect(freeBase.color == QColor(120, 120, 120), "local track uses historical gray");
	ok &= expect(classifyTrackSpeed(200.0 / 3.6).color == QColor(30, 130, 210)
			&& classifyTrackSpeed(200.0 / 3.6).width == 4,
		"historical high-speed boundary");
	ok &= expect(classifyTrackSpeed(120.0 / 3.6).color == QColor(160, 160, 160)
			&& classifyTrackSpeed(120.0 / 3.6).width == 3,
		"historical mainline boundary");
	ok &= expect(classifyTrackSpeed(119.0 / 3.6).color == QColor(120, 120, 120), "historical local boundary");
	ok &= expect(freeBase.width == 2, "free track uses one documented overview width");

	const TrackStateVisual freeTrack = classifyTrackState(TrackOperationalState::Free);
	const TrackStateVisual permissiveTrack = classifyTrackState(TrackOperationalState::Prepared);
	const TrackStateVisual occupiedTrack = classifyTrackState(TrackOperationalState::Occupied);
	const TrackStateVisual blockedTrack = classifyTrackState(TrackOperationalState::Blocked);
	ok &= expect(freeTrack.style == Qt::NoPen && freeTrack.width == 0, "free track has no underlay");
	ok &= expect(permissiveTrack.style == Qt::DashDotLine && permissiveTrack.width == 5, "permissive signalling underlay");
	ok &= expect(occupiedTrack.style == Qt::SolidLine && occupiedTrack.width == 6, "occupied track underlay");
	ok &= expect(blockedTrack.style == Qt::DashLine && blockedTrack.width == 5, "blocked track has non-color cue");
	ok &= expect(permissiveTrack.color == QColor("#4C8DAE"), "permissive signalling color");
	ok &= expect(occupiedTrackColor() == QColor(255, 0, 0) && occupiedTrack.color == occupiedTrackColor(),
		"occupied track color");
	ok &= expect(blockedTrack.color == QColor("#D6A13A"), "blocked track color");
	const QByteArray permissiveMask = renderStrokeMask(permissiveTrack.width, permissiveTrack.style);
	const QByteArray occupiedMask = renderStrokeMask(occupiedTrack.width, occupiedTrack.style);
	const QByteArray blockedMask = renderStrokeMask(blockedTrack.width, blockedTrack.style);
	ok &= expect(permissiveMask != occupiedMask && occupiedMask != blockedMask
			&& permissiveMask != blockedMask,
		"track states remain distinguishable by stroke structure without color");
	ok &= expect(trackStatePriority(TrackOperationalState::Free) < trackStatePriority(TrackOperationalState::Prepared), "free track priority");
	ok &= expect(trackStatePriority(TrackOperationalState::Prepared) < trackStatePriority(TrackOperationalState::Occupied), "permissive signalling priority");
	ok &= expect(trackStatePriority(TrackOperationalState::Occupied) < trackStatePriority(TrackOperationalState::Blocked), "occupied track priority");

	const SignalVisual stopSignal = classifySignalAspect(0);
	const SignalVisual cautionSignal = classifySignalAspect(75);
	const SignalVisual proceed180Signal = classifySignalAspect(180);
	const SignalVisual proceed270Signal = classifySignalAspect(270);
	ok &= expect(stopSignal.lamp == QColor(Qt::red) && stopSignal.cue == SignalCueKind::Stop, "red stop signal cue");
	ok &= expect(cautionSignal.lamp == QColor(Qt::yellow) && cautionSignal.cue == SignalCueKind::Caution, "yellow caution signal cue");
	ok &= expect(proceed180Signal.lamp == QColor(Qt::green) && proceed180Signal.cue == SignalCueKind::Proceed, "green proceed signal cue 180");
	ok &= expect(proceed270Signal.lamp == QColor(Qt::green) && proceed270Signal.cue == SignalCueKind::Proceed, "green proceed signal cue 270");
	ok &= expect(classifySignalAspect(751).lamp == QColor(Qt::red) && classifySignalAspect(751).cue == SignalCueKind::Stop
			&& classifySignalAspect(751).iconResource == ":/icons/signal-stop.svg" && classifySignalCue(751) == SignalCueKind::Stop,
		"code 751 is a stop");
	ok &= expect(classifySignalCue(-1) == SignalCueKind::Neutral && classifySignalCue(42) == SignalCueKind::Neutral,
		"unknown codes are neither stop, caution nor proceed");
	ok &= expect(classifySignalAspect(-1).iconResource == ":/icons/signal-neutral.svg", "neutral signal icon");
	ok &= expect(classifySignalAspect(0).iconResource == ":/icons/signal-stop.svg", "stop signal icon");
	ok &= expect(classifySignalAspect(75).iconResource == ":/icons/signal-caution.svg", "caution signal icon");
	ok &= expect(classifySignalAspect(180).iconResource == ":/icons/signal-proceed.svg", "proceed signal icon");
	ok &= expect(classifyTrainType("freight", "F01").kind == TrainVisualKind::Freight, "freight train classification");
	ok &= expect(classifyTrainType("IC", "IC 2201").kind == TrainVisualKind::Intercity, "intercity train classification");
	ok &= expect(classifyTrainType("", "sprinter 301").kind == TrainVisualKind::Sprinter, "sprinter train classification");
	ok &= expect(classifyTrainType("", "ICE 10").kind == TrainVisualKind::HighSpeed, "high-speed train classification");
	ok &= expect(classifyTrainType("", "regional").iconResource == ":/icons/train-passenger.svg", "passenger train icon");
	ok &= expect(classifyTrainType("", "sprinter 301").iconResource == ":/icons/train-sprinter.svg", "sprinter train icon");
	ok &= expect(classifyTrainType("IC", "IC 2201").iconResource == ":/icons/train-intercity.svg", "intercity train icon");
	ok &= expect(classifyTrainType("", "ICE 10").iconResource == ":/icons/train-high-speed.svg", "high-speed train icon");
	ok &= expect(classifyTrainType("freight", "F01").iconResource == ":/icons/train-freight.svg", "freight train icon");
	const TrainVisual intercity = classifyTrainType("IC", "IC 2201");
	const TrainVisual sprinter = classifyTrainType("", "sprinter 301");
	const TrainVisual freight = classifyTrainType("freight", "F01");
	ok &= expect(defaultTrainFill() == QColor(235, 210, 55) && defaultTrainOutline() == QColor(110, 90, 20),
		"default train colours");
	const std::pair<const char*, const char*> trainInputs[] = {
		{"freight", "F01"}, {"IC", "IC 2201"}, {"", "sprinter 301"}, {"", "ICE 10"}, {"", "regional"}};
	for (const auto& input : trainInputs) {
		const TrainVisual classified = classifyTrainType(input.first, input.second);
		const TrainVisual resolved = resolveTrainVisual(input.first, input.second);
		const TrainVisual invalid = resolveTrainVisual(input.first, input.second, QColor());
		ok &= expect(classified.fill == QColor(235, 210, 55) && classified.outline == QColor(110, 90, 20),
			"classified train uses the default colours whatever its type");
		ok &= expect(resolved.fill == QColor(235, 210, 55) && resolved.outline == QColor(110, 90, 20),
			"resolved train without a service colour uses the default colours");
		ok &= expect(invalid.fill == QColor(235, 210, 55) && invalid.outline == QColor(110, 90, 20),
			"invalid service colour gives the default colours");
		ok &= expect(resolved.kind == classified.kind && resolved.shape == classified.shape
				&& resolved.iconResource == classified.iconResource,
			"resolved train keeps the classified kind, shape and icon");
	}
	const QColor serviceColor(40, 130, 210);
	const TrainVisual coloured = resolveTrainVisual("IC", "IC 2201", serviceColor);
	ok &= expect(coloured.fill == serviceColor && coloured.outline == serviceColor.darker(200)
			&& coloured.outline != coloured.fill,
		"service colour sets the fill and a darker outline");
	ok &= expect(coloured.kind == TrainVisualKind::Intercity
			&& coloured.iconResource == ":/icons/train-intercity.svg",
		"service colour keeps the classified kind and icon");
	ok &= expect(intercity.shape == TrainBadgeShape::Capsule, "intercity badge shape");
	ok &= expect(sprinter.shape == TrainBadgeShape::Rounded, "sprinter badge shape");
	ok &= expect(freight.shape == TrainBadgeShape::Square, "freight badge shape");
	ok &= expect(intercity.shape != sprinter.shape && intercity.shape != freight.shape && sprinter.shape != freight.shape,
		"train category silhouettes");

	ok &= expect(classifyStation(false, 3).cls == StationClass::Major && classifyStation(true, 0).cls == StationClass::Station
			&& classifyStation(false, 0).cls == StationClass::Halt,
		"three connections make a major station, a platform a station, neither a halt");
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
	// The pin in the frame (8, 24, 48, 48) with a halo of 4, so at twice the pixel size: its tip on the bottom centre
	// (32, 72), its head of radius 0.3 of 48 times the size of the class, centred 0.78 of 48 times the size above the
	// tip. The probes: the centre of the head, the body below it, the left edge of the head, the halo above the head,
	// the halo under the tip and the canvas left of, right of and below the tip.
	for (const auto& [cls, size, expected] :
		{std::tuple<StationClass, qreal, QByteArray>{StationClass::Major, 1.15, "WBWDDTTT"},
			std::tuple<StationClass, qreal, QByteArray>{StationClass::Station, 1.0, "DWWDDTTT"},
			std::tuple<StationClass, qreal, QByteArray>{StationClass::Halt, 0.8, "WDWDDTTT"}}) {
		const int head = qRound(72.0 - 0.78 * 48.0 * size);
		const int radius = qRound(0.3 * 48.0 * size);
		QImage mark(64, 80, QImage::Format_ARGB32_Premultiplied);
		mark.fill(Qt::transparent);
		{
			QPainter painter(&mark);
			paintStationMark(&painter, QRectF(8.0, 24.0, 48.0, 48.0), cls, Qt::white, Qt::black, 4.0);
		}
		const QList<QPoint> pinPoints{{32, head}, {32, head + radius * 7 / 10}, {33 - radius, head},
			{32, head - radius - 2}, {32, 74}, {24, 74}, {40, 74}, {32, 78}};
		ok &= expect(probeRow(mark, pinPoints) == expected,
			"the far mark is a pin with a dark halo and its tip on the bottom centre: blue with a light edge and dot for a "
			"major station, light with a dark dot for a station, dark with a light edge and dot for a halt");
	}
	// The menu icon fits the whole pin with its halo and centres it: the outer columns stay empty, and the outer rows hold
	// at most the dark halo, so that a pin cut at its top or its tip shows there. Only the pin of a major station is blue.
	// At 32 pixels it has the colours of the pin of its class on the canvas.
	for (const StationClass cls : {StationClass::Major, StationClass::Station, StationClass::Halt}) {
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
			ok &= expect(edgesEmpty && blue == (cls == StationClass::Major)
					&& area.adjusted(-0.01, -0.01, 0.01, 0.01).contains(painted)
					&& QLineF(painted.center(), area.center()).length() < 0.01,
				"the menu icon shows the whole pin, centred, blue only for a major station");
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
		ok &= expect(pinColours(icon) == pinColours(canvas), "the menu icon has the colours of the pin of its class on the canvas");
	}
	ok &= expect(classifyStation(true, 0).fill == QColor(70, 70, 70), "platform station square");
	ok &= expect(classifyStation(false, 3).fill == QColor(80, 120, 210), "interchange station square");

	ok &= expect(simulationSpeedLabel(0) == "Fastest", "fastest speed label");
	ok &= expect(simulationSpeedLabel(250) == "4.0x", "delayed speed label");
	ok &= expect(simulationSpeedLabel(10) == "100x", "fast factor speed label");
	ok &= expect(simulationSpeedMode(0) == "Fastest", "fastest speed mode");
	ok &= expect(simulationSpeedMode(500) == "2.0x real time", "slowed speed mode");

	TrainBadgeItem badge;
	badge.setIdentifier("1725");
	badge.setTooltipDetails("Intercity 1725 northbound", "1725", "Intercity");
	badge.setSpeedText("102 km/h");
	ok &= expect(badge.shape().isEmpty() && badge.toolTip().isEmpty(), "the badge has no shape and no tooltip");
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
	ok &= expect(drawsNothing(), "an Overview badge draws nothing");
	badge.setPresentation(TrainBadgeItem::Presentation::Identity);
	ok &= expect(drawsNothing(), "an Identity badge draws nothing");
	badge.setPresentation(TrainBadgeItem::Presentation::Detailed);
	const QImage speedImage = renderBadge();
	ok &= expect(pixelsOfColor(speedImage, Qt::white) > 100 && pixelsOfColor(speedImage, QColor(16, 26, 34)) > 100,
		"a Detailed badge draws the speed in white with a dark outline");
	const QRectF speedBounds = badge.boundingRect();
	const QRect drawnBounds = badgeScale.mapRect(speedBounds).toAlignedRect();
	int outside = 0;
	for (int y = 0; y < speedImage.height(); ++y)
		for (int x = 0; x < speedImage.width(); ++x)
			outside += !drawnBounds.contains(x, y) && speedImage.pixelColor(x, y) != mainline;
	ok &= expect(outside == 0, "the speed stays inside the bounds of the badge");
	ok &= expect(speedBounds.left() > 0.0 && speedBounds.bottom() < 0.0, "the speed is drawn above and right of the train");
	badge.setSpeedVisible(false);
	ok &= expect(drawsNothing(), "a badge with the speed hidden draws nothing");
	badge.setSpeedVisible(true);
	badge.setSpeedText(QString());
	ok &= expect(drawsNothing(), "a badge without a speed draws nothing");
	// The speed, promoted or not, stays under the station names, which stay readable.
	const bool underNames = badge.zValue() < StationNameItem::NameZValue;
	badge.setPromoted(true);
	ok &= expect(underNames && badge.zValue() < StationNameItem::NameZValue,
		"the speed of a train, promoted or not, is stacked under the station names");
	badge.setPromoted(false);

	const SignalGeometry horizontal = signalGeometry(QPointF(), QPointF(-8, 0),
		QPointF(8, 0), QPointF(0, 1), 150);
	ok &= expect(horizontal.reversedHead == QPointF(-8, -30)
			&& horizontal.forwardHead == QPointF(8, 30),
		"interpolated horizontal endpoints place reversed signal to the left");
	const SignalGeometry diagonal = signalGeometry(QPointF(40, 20), QPointF(34, 12),
		QPointF(46, 28), QPointF(-0.8, 0.6), 150);
	ok &= expect(diagonal.reversedHead == QPointF(58, -6)
			&& diagonal.forwardHead == QPointF(22, 46),
		"non-horizontal endpoints preserve direction and authored normal");
	const qreal presentationScale = 0.45;
	const SignalGeometry measured = signalGeometry(QPointF(), QPointF(-8, 0),
		QPointF(8, 0), QPointF(0, 1), 150 * presentationScale);
	ok &= expect(measured.reversedHead.x() == -8 && measured.forwardHead.x() == 8
			&& std::fabs(measured.reversedHead.y() + 13.5) < 1e-12
			&& std::fabs(measured.forwardHead.y() - 13.5) < 1e-12
			&& std::fabs(measured.forwardBase.length() - 13.5) < 1e-12
			&& measured.forwardPost.length() == 8,
		"historical presentation conversion affects lateral separation, not physical longitudinal signal points");
	SignalItem head(QRectF(-10, -10, 20, 20));
	head.setAspectCode(0);
	ok &= expect(!head.flags().testFlag(QGraphicsItem::ItemIgnoresTransformations),
		"signal heads scale with the scene");
	ok &= expect(head.childItems().size() == 1
			&& head.childItems().first()->flags().testFlag(QGraphicsItem::ItemIgnoresTransformations),
		"signal retains an invisible device-space semantic target");
	QImage signalImage(48, 48, QImage::Format_ARGB32_Premultiplied);
	signalImage.fill(Qt::black);
	{
		QPainter painter(&signalImage);
		painter.translate(24, 24);
		head.paint(&painter, nullptr, nullptr);
	}
	// The stop mark is a bar through the centre, so sample beside it.
	ok &= expect(signalImage.pixelColor(24, 24 - 5) == QColor(Qt::red),
		"individual stop head paints its own aspect without sectors or direction ticks");

	// Every state can be told apart without colour where a mark fits, and only
	// the colour is left below 6 device pixels.
	const QImage stop = renderHead(0, false, 48), bacc = renderHead(751, false, 48);
	const QImage caution = renderHead(75, false, 48), proceed = renderHead(180, false, 48);
	const QImage clear = renderHead(270, false, 48), unavailable = renderHead(-1, false, 48);
	const QImage failed = renderHead(270, true, 48);
	const int c = stop.width() / 2;
	ok &= expect(stop.pixelColor(c, c - 10) == QColor(Qt::red) && bacc.pixelColor(c, c - 10) == QColor(Qt::red),
		"stop and BACC second red heads are red");
	ok &= expect(caution.pixelColor(c, c - 10) == QColor(Qt::yellow), "caution head is yellow");
	ok &= expect(proceed.pixelColor(c, c - 10) == QColor(Qt::green) && clear.pixelColor(c, c - 10) == QColor(Qt::green),
		"codes 180 and 270 are green");
	ok &= expect(markPixels(proceed) == 0 && markPixels(clear) == 0, "a proceed head has no mark");
	ok &= expect(stop.pixelColor(c, c) == QColor(30, 30, 30) && stop.pixelColor(c + 10, c) == QColor(30, 30, 30) && stop.pixelColor(c + 10, c - 4) == QColor(Qt::red),
		"a stop head has a bar through its centre");
	ok &= expect(caution.pixelColor(c, c) == QColor(30, 30, 30) && caution.pixelColor(c + 10, c) == QColor(Qt::yellow),
		"a caution head has a dot in its centre and no bar");
	ok &= expect(markPixels(stop) > 0 && markPixels(caution) > 0 && markPixels(stop) != markPixels(caution),
		"stop and caution heads carry different marks");
	ok &= expect(stop == bacc, "code 751 is painted like code 0");
	ok &= expect(unavailable.pixelColor(c, c - 10) == QColor(Qt::black)
			&& pixelsOfColor(unavailable, QColor(Qt::red)) + pixelsOfColor(unavailable, QColor(Qt::yellow))
					+ pixelsOfColor(unavailable, QColor(Qt::green))
				== 0
			&& pixelsOfColor(unavailable, QColor(150, 150, 150)) > 40 && unavailable.pixelColor(c, c) == QColor(150, 150, 150),
		"an unavailable head is an empty gray ring with a dash");
	ok &= expect(renderHead(42, false, 48) == unavailable, "a code that no aspect uses is painted as unavailable");
	ok &= expect(failed.pixelColor(c, c - 10) == QColor(Qt::red) && failed.pixelColor(c + 10, c) == QColor(Qt::red)
			&& failed.pixelColor(c + 6, c + 6) == QColor(Qt::white) && failed.pixelColor(c - 6, c + 6) == QColor(Qt::white)
			&& failed.pixelColor(c + 6, c - 6) == QColor(Qt::white) && failed.pixelColor(c - 6, c - 6) == QColor(Qt::white),
		"a failed head is red with a white cross, whatever its code");
	ok &= expect(renderHead(-1, true, 48) == failed && renderHead(0, true, 48) == failed, "a failed head ignores its code");
	ok &= expect(markPixels(failed, true) > 0 && markPixels(stop, true) == 0, "a failed head differs from a stop head without colour");
	for (const int code : {0, 75})
		ok &= expect(markPixels(renderHead(code, false, 6)) > 0 && markPixels(renderHead(code, false, 5)) == 0,
			"marks appear at 6 device pixels and not below");
	ok &= expect(renderHead(-1, false, 6).pixelColor(5, 5) == QColor(150, 150, 150)
			&& renderHead(-1, false, 5).pixelColor(4, 4) == QColor(Qt::black)
			&& markPixels(renderHead(75, true, 5), true) == 0 && markPixels(renderHead(75, true, 6), true) > 0,
		"the dash and the cross follow the same 6 pixel rule");
	ok &= expect(renderHead(0, false, 5).pixelColor(3, 3) == QColor(Qt::red) && renderHead(75, false, 5).pixelColor(3, 3) == QColor(Qt::yellow)
			&& renderHead(180, false, 5).pixelColor(3, 3) == QColor(Qt::green),
		"colour remains below 6 device pixels");
	if (!ok)
		return 1;

	std::cout << "all VisualPolish tests passed\n";
	return 0;
}
