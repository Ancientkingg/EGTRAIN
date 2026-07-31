#include "graphics/VisualPolish.h"

#include <QGuiApplication>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>

#include "graphics/items/SignalItem.h"
#include "graphics/items/TrainBadgeItem.h"

#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

struct RenderedCue {
	int paintedPixels = 0;
	QRect bounds;
};

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

static RenderedCue renderCompactSignal(int aspectCode) {
	QImage image(32, 32, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	SignalItem signal(QRectF(-6.0, -8.0, 12.0, 16.0));
	signal.setAspectCode(aspectCode);
	signal.setCompact(true);
	{
		QPainter painter(&image);
		painter.translate(16.0, 16.0);
		signal.paint(&painter, nullptr, nullptr);
	}

	RenderedCue result;
	int left = image.width();
	int top = image.height();
	int right = -1;
	int bottom = -1;
	for (int y = 0; y < image.height(); ++y) {
		for (int x = 0; x < image.width(); ++x) {
			if (image.pixelColor(x, y).alpha() == 0)
				continue;
			++result.paintedPixels;
			left = qMin(left, x);
			top = qMin(top, y);
			right = qMax(right, x);
			bottom = qMax(bottom, y);
		}
	}
	if (right >= left && bottom >= top)
		result.bounds = QRect(QPoint(left, top), QPoint(right, bottom));
	return result;
}

int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QGuiApplication app(argc, argv);
	bool ok = true;
	const TrackVisual freeBase = freeTrackVisual();
	ok &= expect(freeBase.color == QColor("#A0ACB4"), "free track uses the neutral base color");
	ok &= expect(freeBase.width == 2, "free track uses one documented overview width");

	const TrackStateVisual freeTrack = classifyTrackState(TrackOperationalState::Free);
	const TrackStateVisual preparedTrack = classifyTrackState(TrackOperationalState::Prepared);
	const TrackStateVisual occupiedTrack = classifyTrackState(TrackOperationalState::Occupied);
	const TrackStateVisual blockedTrack = classifyTrackState(TrackOperationalState::Blocked);
	ok &= expect(freeTrack.style == Qt::NoPen && freeTrack.width == 0, "free track has no underlay");
	ok &= expect(preparedTrack.style == Qt::DashDotLine && preparedTrack.width == 6, "prepared track underlay");
	ok &= expect(occupiedTrack.style == Qt::SolidLine && occupiedTrack.width == 8, "occupied track underlay");
	ok &= expect(blockedTrack.style == Qt::DashLine && blockedTrack.width == 8, "blocked track has non-color cue");
	ok &= expect(preparedTrack.color == QColor("#4C8DAE"), "prepared track color");
	ok &= expect(occupiedTrack.color == QColor("#D05A47"), "occupied track color");
	ok &= expect(blockedTrack.color == QColor("#D6A13A"), "blocked track color");
	const QByteArray preparedMask = renderStrokeMask(preparedTrack.width, preparedTrack.style);
	const QByteArray occupiedMask = renderStrokeMask(occupiedTrack.width, occupiedTrack.style);
	const QByteArray blockedMask = renderStrokeMask(blockedTrack.width, blockedTrack.style);
	ok &= expect(preparedMask != occupiedMask && occupiedMask != blockedMask
		&& preparedMask != blockedMask,
		"track states remain distinguishable by stroke structure without color");
	ok &= expect(trackStatePriority(TrackOperationalState::Free) < trackStatePriority(TrackOperationalState::Prepared), "free track priority");
	ok &= expect(trackStatePriority(TrackOperationalState::Prepared) < trackStatePriority(TrackOperationalState::Occupied), "prepared track priority");
	ok &= expect(trackStatePriority(TrackOperationalState::Occupied) < trackStatePriority(TrackOperationalState::Blocked), "occupied track priority");

	const SignalVisual stopSignal = classifySignalAspect(0);
	const SignalVisual cautionSignal = classifySignalAspect(75);
	const SignalVisual proceed180Signal = classifySignalAspect(180);
	const SignalVisual proceed270Signal = classifySignalAspect(270);
	ok &= expect(stopSignal.lamp == QColor(Qt::red) && stopSignal.cue == SignalCueKind::Stop, "red stop signal cue");
	ok &= expect(cautionSignal.lamp == QColor(Qt::yellow) && cautionSignal.cue == SignalCueKind::Caution, "yellow caution signal cue");
	ok &= expect(proceed180Signal.lamp == QColor(Qt::green) && proceed180Signal.cue == SignalCueKind::Proceed, "green proceed signal cue 180");
	ok &= expect(proceed270Signal.lamp == QColor(Qt::green) && proceed270Signal.cue == SignalCueKind::Proceed, "green proceed signal cue 270");
	ok &= expect(classifySignalAspect(-1).iconResource == ":/icons/signal-neutral.svg", "neutral signal icon");
	ok &= expect(classifySignalAspect(0).iconResource == ":/icons/signal-stop.svg", "stop signal icon");
	ok &= expect(classifySignalAspect(75).iconResource == ":/icons/signal-caution.svg", "caution signal icon");
	ok &= expect(classifySignalAspect(180).iconResource == ":/icons/signal-proceed.svg", "proceed signal icon");
	const RenderedCue compactStop = renderCompactSignal(0);
	const RenderedCue compactCaution = renderCompactSignal(75);
	const RenderedCue compactProceed = renderCompactSignal(180);
	ok &= expect(compactStop.bounds.width() >= 7 && compactStop.bounds.height() >= 7,
		"compact stop cue stays prominent at overview zoom");
	ok &= expect(compactCaution.bounds.width() >= 7 && compactCaution.bounds.height() >= 7,
		"compact caution cue stays prominent at overview zoom");
	ok &= expect(compactProceed.bounds.width() <= 3 && compactProceed.bounds.height() <= 8,
		"compact proceed cue collapses to a subdued tick");
	ok &= expect(compactProceed.paintedPixels < compactStop.paintedPixels
		&& compactProceed.paintedPixels < compactCaution.paintedPixels,
		"repeated proceed cues carry less visual weight than stop and caution");

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
	const TrainVisual highSpeed = classifyTrainType("", "ICE 10");
	const TrainVisual passenger = classifyTrainType("", "regional");
	ok &= expect(passenger.fill == QColor("#9BA5AA") && passenger.outline == QColor("#4A5960"),
		"passenger train uses the neutral instrument palette");
	ok &= expect(sprinter.fill == QColor("#86AA96") && sprinter.outline == QColor("#3F6A54"),
		"sprinter train uses the muted green palette");
	ok &= expect(intercity.fill == QColor("#C6A86E") && intercity.outline == QColor("#765623"),
		"intercity train uses the muted brass palette");
	ok &= expect(highSpeed.fill == QColor("#86A6B9") && highSpeed.outline == QColor("#3E627A"),
		"high-speed train uses the steel blue palette");
	ok &= expect(freight.fill == QColor("#A99787") && freight.outline == QColor("#5D4C3F"),
		"freight train uses the muted brown palette");
	ok &= expect(intercity.fill != sprinter.fill && intercity.fill != freight.fill && sprinter.fill != freight.fill,
		"train category contrast");
	ok &= expect(intercity.shape == TrainBadgeShape::Capsule, "intercity badge shape");
	ok &= expect(sprinter.shape == TrainBadgeShape::Rounded, "sprinter badge shape");
	ok &= expect(freight.shape == TrainBadgeShape::Square, "freight badge shape");
	ok &= expect(intercity.shape != sprinter.shape && intercity.shape != freight.shape && sprinter.shape != freight.shape,
		"train category silhouettes");

	ok &= expect(classifyStation(true, 4).kind == StationVisualKind::Interchange, "interchange station classification");
	ok &= expect(classifyStation(true, 1).kind == StationVisualKind::Platform, "platform station classification");
	ok &= expect(classifyStation(false, 0).kind == StationVisualKind::StopMarker, "stop marker classification");
	ok &= expect(classifyStation(false, 0).iconResource == ":/icons/station-stop.svg", "stop station icon");
	ok &= expect(classifyStation(true, 1).iconResource == ":/icons/station-platform.svg", "platform station icon");
	ok &= expect(classifyStation(true, 4).iconResource == ":/icons/station-interchange.svg", "interchange station icon");

	ok &= expect(simulationSpeedLabel(0) == "Speed: fastest", "fastest speed label");
	ok &= expect(simulationSpeedLabel(250) == "Speed: 4.0x", "delayed speed label");
	ok &= expect(simulationSpeedLabel(10) == "Speed: 100x", "fast factor speed label");
	ok &= expect(simulationSpeedMode(0) == "Fastest", "fastest speed mode");
	ok &= expect(simulationSpeedMode(500) == "2.0x real time", "slowed speed mode");

	QFont badgeFont;
	badgeFont.setPointSize(9);
	badgeFont.setBold(true);
	const QFontMetricsF badgeMetrics(badgeFont);
	const QRectF denseBody(1.0, 1.0, 130.0, 26.0);
	const QString longIdentifier = "Intercity 2201 Northbound";
	const QString speedText = "0 km/h";
	const QRectF identifierRegion = TrainBadgeItem::identifierTextRect(
		denseBody, false, false, badgeMetrics, speedText);
	const QRectF speedRegion = TrainBadgeItem::speedTextRect(
		denseBody, false, false, badgeMetrics, speedText);
	const QRectF categoryIcon = TrainBadgeItem::iconRect(denseBody, false);
	const QString displayedIdentifier = TrainBadgeItem::elidedIdentifier(
		longIdentifier, badgeMetrics, identifierRegion);
	const QString serviceOne = TrainBadgeItem::elidedIdentifier(
		"H-Ballerup-Osterport-1", badgeMetrics, QRectF(0.0, 0.0, 90.0, 20.0));
	const QString serviceTwo = TrainBadgeItem::elidedIdentifier(
		"H-Ballerup-Osterport-2", badgeMetrics, QRectF(0.0, 0.0, 90.0, 20.0));
	TrainBadgeItem speedBadge;
	speedBadge.setSpeedText(speedText);
	speedBadge.setSpeedVisible(false);
	ok &= expect(!speedBadge.isSpeedVisible(), "train speed label can hide independently");
	speedBadge.setSpeedVisible(true);
	ok &= expect(speedBadge.isSpeedVisible(), "train speed label can restore independently");
	ok &= expect(badgeMetrics.horizontalAdvance(longIdentifier) > identifierRegion.width(),
		"long identifier needs elision");
	ok &= expect(badgeMetrics.horizontalAdvance(displayedIdentifier) <= identifierRegion.width(),
		"elided identifier fits its region");
	ok &= expect(serviceOne != serviceTwo && serviceOne.endsWith('1') && serviceTwo.endsWith('2'),
		"middle elision preserves distinguishing service suffixes");
	ok &= expect(identifierRegion.right() <= speedRegion.left(),
		"identifier and speed regions do not overlap");
	ok &= expect(categoryIcon.right() <= identifierRegion.left(),
		"train icon and identifier do not overlap");

	if (!ok)
		return 1;

	std::cout << "all VisualPolish tests passed\n";
	return 0;
}
