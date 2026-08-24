#include "graphics/VisualPolish.h"

#include <QGuiApplication>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>

#include "graphics/items/TrainBadgeItem.h"

#include <iostream>

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
	ok &= expect(preparedTrack.style == Qt::DashDotLine && preparedTrack.width == 5, "prepared track underlay");
	ok &= expect(occupiedTrack.style == Qt::SolidLine && occupiedTrack.width == 6, "occupied track underlay");
	ok &= expect(blockedTrack.style == Qt::DashLine && blockedTrack.width == 5, "blocked track has non-color cue");
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

	const StationVisual station = classifyStation();
	ok &= expect(station.iconResource == ":/icons/station.svg", "station uses the uniform station icon");

	ok &= expect(simulationSpeedLabel(0) == "Speed: fastest", "fastest speed label");
	ok &= expect(simulationSpeedLabel(250) == "Speed: 4.0x", "delayed speed label");
	ok &= expect(simulationSpeedLabel(10) == "Speed: 100x", "fast factor speed label");
	ok &= expect(simulationSpeedMode(0) == "Fastest", "fastest speed mode");
	ok &= expect(simulationSpeedMode(500) == "2.0x real time", "slowed speed mode");

	const QColor badgeSurface = TrainBadgeItem::badgeSurfaceColor();
	const QColor badgeText = TrainBadgeItem::badgePrimaryTextColor();
	ok &= expect(badgeSurface == QColor("#26313B") && badgeText == QColor("#F2F5F7"),
		"train badge uses the documented dark surface and bright primary text");
	ok &= expect(badgeText.lightness() - badgeSurface.lightness() >= 100,
		"train badge surface and primary text have strong luminance contrast");

	TrainBadgeItem badge;
	badge.setIdentifier("1725");
	badge.setTooltipDetails("Intercity 1725 northbound", "1725", "Intercity");
	badge.setSpeedText("102 km/h");
	using Presentation = TrainBadgeItem::Presentation;
	ok &= expect(TrainBadgeItem::presentationForZoom(1.0, false) == Presentation::Overview,
		"zoom below the identity threshold uses the overview marker");
	ok &= expect(TrainBadgeItem::presentationForZoom(
		TrainBadgeItem::identityZoomThreshold() - 0.01, false) == Presentation::Overview,
		"zoom just below the identity threshold stays overview");
	ok &= expect(TrainBadgeItem::presentationForZoom(
		TrainBadgeItem::identityZoomThreshold(), false) == Presentation::Identity,
		"the identity threshold itself enters identity mode");
	ok &= expect(TrainBadgeItem::presentationForZoom(2.4, false) == Presentation::Identity,
		"mid-range zoom keeps the identity chip");
	ok &= expect(TrainBadgeItem::presentationForZoom(
		TrainBadgeItem::detailedZoomThreshold() - 0.01, false) == Presentation::Identity,
		"zoom just below the detailed threshold keeps identity");
	ok &= expect(TrainBadgeItem::presentationForZoom(
		TrainBadgeItem::detailedZoomThreshold(), false) == Presentation::Detailed,
		"the detailed threshold itself enters detailed mode");
	ok &= expect(TrainBadgeItem::presentationForZoom(6.0, false) == Presentation::Detailed,
		"high zoom keeps the detailed label");
	ok &= expect(TrainBadgeItem::presentationForZoom(0.8, true) == Presentation::Detailed
		&& TrainBadgeItem::presentationForZoom(0.8, true)
			== TrainBadgeItem::presentationForZoom(6.0, true),
		"selected or followed trains are promoted to detailed at any zoom");
	const QRectF overview = badge.badgeRect();
	ok &= expect(overview.size() == TrainBadgeItem::markerSize(),
		"overview train overlay is an 18 by 16 marker");
	ok &= expect(!badge.showsIdentifier() && !badge.showsSpeed(),
		"overview marker hides identifier and speed");
	ok &= expect(badge.flags().testFlag(QGraphicsItem::ItemIgnoresTransformations),
		"train overlay ignores view transformations");
	ok &= expect(badge.acceptedMouseButtons() == Qt::NoButton,
		"train overlay remains non-interactive");

	badge.setPresentation(TrainBadgeItem::Presentation::Identity);
	const QRectF identity = badge.badgeRect();
	ok &= expect(identity.height() == 22.0 && identity.width() < TrainBadgeItem::maximumWidth(
		TrainBadgeItem::Presentation::Identity), "short identity chip is content-sized");
	ok &= expect(badge.showsIdentifier() && !badge.showsSpeed() && badge.speedTextRect().isEmpty(),
		"identity chip shows identity without speed");

	badge.setPresentation(TrainBadgeItem::Presentation::Detailed);
	const QRectF detailed = badge.badgeRect();
	ok &= expect(detailed.height() == 26.0 && detailed.width() <= TrainBadgeItem::maximumWidth(
		TrainBadgeItem::Presentation::Detailed), "detailed train label stays within 132 pixels");
	ok &= expect(badge.showsIdentifier() && badge.showsSpeed(),
		"detailed train label shows enabled speed");
	ok &= expect(badge.identifierTextRect().right() <= badge.speedTextRect().left(),
		"detailed identity and speed do not overlap");
	ok &= expect(overview.bottom() == identity.bottom() && identity.bottom() == detailed.bottom(),
		"all train overlay modes expand from one anchor");
	const qreal detailedWidthWithSpeed = detailed.width();
	badge.setSpeedVisible(false);
	ok &= expect(!badge.showsSpeed() && badge.badgeRect().width() < detailedWidthWithSpeed,
		"speed toggle removes detailed speed text and unused width");
	badge.setSpeedVisible(true);
	badge.setPromoted(true);
	ok &= expect(badge.isPromoted() && badge.zValue() > 5.0
		&& TrainBadgeItem::promotedBorderColor() == QColor("#315A70"),
		"promoted train label uses the application accent above ordinary overlays");

	const QPolygonF forwardNose = badge.directionNose();
	badge.setReversed(true);
	const QPolygonF reversedNose = badge.directionNose();
	ok &= expect(forwardNose.first().x() > forwardNose.at(1).x()
		&& reversedNose.first().x() < reversedNose.at(1).x(),
		"integrated train nose follows runtime direction");
	ok &= expect(badge.toolTip().contains("Intercity 1725 northbound")
		&& badge.toolTip().contains("Operating code: 1725")
		&& badge.toolTip().contains("Speed: 102 km/h")
		&& badge.toolTip().contains("Type: Intercity"),
		"train tooltip retains full operational details");

	badge.setIdentifier("H-Ballerup-Osterport-1");
	const QString elided = badge.displayedIdentifier();
	ok &= expect(elided != "H-Ballerup-Osterport-1" && elided.endsWith('1'),
		"long train identifiers use middle elision and preserve the suffix");

	if (!ok)
		return 1;

	std::cout << "all VisualPolish tests passed\n";
	return 0;
}
