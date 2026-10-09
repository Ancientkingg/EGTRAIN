#include "graphics/VisualPolish.h"

#include <QGuiApplication>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>

#include "graphics/items/TrainBadgeItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/SignalGeometry.h"

#include <iostream>
#include <cmath>
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

int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QGuiApplication app(argc, argv);
	bool ok = true;
	const TrackVisual freeBase = freeTrackVisual();
	ok &= expect(freeBase.color == QColor(120, 120, 120), "local track uses historical gray");
	ok &= expect(classifyTrackSpeed(200.0 / 3.6).color == QColor(30, 130, 210)
			&& classifyTrackSpeed(200.0 / 3.6).width == 4,
		"historical high-speed boundary");
	ok &= expect(classifyTrackSpeed(120.0 / 3.6).color == QColor(80, 80, 80)
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
	ok &= expect(occupiedTrack.color == QColor("#D05A47"), "occupied track color");
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

	const StationVisual station = classifyStation();
	ok &= expect(station.iconResource == ":/icons/station.svg", "station uses the shared building pictogram");
	ok &= expect(classifyStation(true, 0).fill == QColor(70, 70, 70), "platform station square");
	ok &= expect(classifyStation(false, 3).fill == QColor(80, 120, 210), "interchange station square");

	ok &= expect(simulationSpeedLabel(0) == "Speed: fastest", "fastest speed label");
	ok &= expect(simulationSpeedLabel(250) == "Speed: 4.0x", "delayed speed label");
	ok &= expect(simulationSpeedLabel(10) == "Speed: 100x", "fast factor speed label");
	ok &= expect(simulationSpeedMode(0) == "Fastest", "fastest speed mode");
	ok &= expect(simulationSpeedMode(500) == "2.0x real time", "slowed speed mode");

	TrainBadgeItem badge;
	badge.setIdentifier("1725");
	badge.setTooltipDetails("Intercity 1725 northbound", "1725", "Intercity");
	badge.setSpeedText("102 km/h");
	ok &= expect(badge.shape().isEmpty() && badge.toolTip().isEmpty(),
		"unpainted tracking badge has no ghost shape or tooltip");

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
