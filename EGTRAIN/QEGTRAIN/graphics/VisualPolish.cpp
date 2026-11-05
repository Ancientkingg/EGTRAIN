#include "graphics/VisualPolish.h"

#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPixmap>
#include <QPolygonF>
#include <QtMath>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace {

std::string lower(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
	return text;
}

bool containsAny(const std::string& text, std::initializer_list<const char*> needles) {
	for (const char* needle : needles) {
		if (text.find(needle) != std::string::npos)
			return true;
	}
	return false;
}

// The blue of the pin of a major station: the fill of the square of a major station.
const QColor kStationMarkAccent(80, 120, 210);
// The alpha of the area of a station: the highest step of 8 at which every track colour keeps a contrast of 3:1 on the
// area over the canvas.
constexpr int kStationAreaAlpha = 56;

// One part of a station mark: path filled with fill and an edge of edgeWidth in edgeColor, painted over the fill, or
// under it when edgeUnder is set, so that only its outer half shows.
struct MarkPart {
	MarkPart(const QPainterPath& area, const QColor& colour, const QColor& edge = QColor(), qreal width = 0.0,
		bool under = false)
		: path(area), fill(colour), edgeColor(edge), edgeWidth(width), edgeUnder(under) {}
	QPainterPath path;
	QColor fill;
	QColor edgeColor;
	qreal edgeWidth;
	bool edgeUnder;
};

// Paints the parts in order over a halo of haloWidth around all of them and their edges.
void paintMarkParts(QPainter* painter, const QList<MarkPart>& parts, const QColor& halo, qreal haloWidth) {
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing);
	for (const MarkPart& part : parts)
		painter->strokePath(part.path, QPen(halo, 2.0 * haloWidth + part.edgeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	for (const MarkPart& part : parts) {
		const QPen edge(part.edgeColor, part.edgeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
		if (part.edgeWidth > 0.0 && part.edgeUnder)
			painter->strokePath(part.path, edge);
		painter->fillPath(part.path, part.fill);
		if (part.edgeWidth > 0.0 && !part.edgeUnder)
			painter->strokePath(part.path, edge);
	}
	painter->restore();
}

QPainterPath disc(const QPointF& centre, qreal radius) {
	QPainterPath path;
	path.addEllipse(centre, radius, radius);
	return path;
}

// A map pin with its tip at tip: a circle of radius r whose centre is d above the tip, and the two tangents from the tip
// to the circle.
QPainterPath pinPath(const QPointF& tip, qreal r, qreal d) {
	const QPointF centre = tip - QPointF(0.0, d);
	const qreal tangent = qRadiansToDegrees(std::acos(r / d));
	QPainterPath path(tip);
	path.arcTo(QRectF(centre.x() - r, centre.y() - r, 2.0 * r, 2.0 * r), 270.0 - tangent, -(360.0 - 2.0 * tangent));
	path.closeSubpath();
	return path;
}

// The parts of the pin in frame, its tip on the bottom centre of frame. Edge widths are in logical pixels, taking
// haloWidth as two of them.
QList<MarkPart> pinParts(const QRectF& frame, StationClass cls, const QColor& fill, const QColor& halo, qreal haloWidth) {
	const qreal side = std::min(frame.width(), frame.height());
	const qreal pixel = haloWidth / 2.0;
	const qreal size = cls == StationClass::Halt ? 0.8 : (cls == StationClass::Major ? 1.15 : 1.0);
	const qreal r = 0.3 * side * size;
	const qreal d = 0.78 * side * size;
	const QPointF tip(frame.center().x(), frame.bottom());
	const QPainterPath body = pinPath(tip, r, d);
	const QPointF head = tip - QPointF(0.0, d);
	if (cls == StationClass::Halt)
		return {{body, halo, fill, 1.6 * pixel}, {disc(head, 0.38 * r), fill}};
	if (cls == StationClass::Major)
		return {{body, kStationMarkAccent, fill, 1.2 * pixel}, {disc(head, 0.42 * r), fill}};
	return {{body, fill}, {disc(head, 0.4 * r), halo}};
}

// The parts of the ring and bar centred in frame: a ring, or a disc for a major station, crossed by a blue bar for a
// station and a major station. The edge of the bar, under it, sets it off from the ring.
QList<MarkPart> ringAndBarParts(const QRectF& frame, StationClass cls, const QColor& fill, const QColor& halo,
	qreal haloWidth) {
	const qreal side = std::min(frame.width(), frame.height());
	const QPointF centre = frame.center();
	const qreal outer = 0.36 * side;
	QPainterPath ring = disc(centre, outer);
	if (cls != StationClass::Major)
		ring.addEllipse(centre, outer - 0.12 * side, outer - 0.12 * side);
	QList<MarkPart> parts{{ring, fill}};
	if (cls != StationClass::Halt) {
		QRectF bar(0.0, 0.0, 1.02 * side, 0.21 * side);
		bar.moveCenter(centre);
		QPainterPath path;
		path.addRect(bar);
		parts.append({path, kStationMarkAccent, halo, 0.75 * haloWidth, true});
	}
	return parts;
}

// The class mark centred in frame: an upright bar for a major station, a disc for a station and a ring for a halt.
QList<MarkPart> classMarkParts(const QRectF& frame, StationClass cls, const QColor& fill, qreal haloWidth) {
	const qreal side = std::min(frame.width(), frame.height());
	const QPointF centre = frame.center();
	QPainterPath mark;
	if (cls == StationClass::Major) {
		QRectF bar(0.0, 0.0, 0.32 * side, 0.8 * side);
		bar.moveCenter(centre);
		mark.addRoundedRect(bar, bar.width() / 3.0, bar.width() / 3.0);
	} else {
		const qreal radius = 0.35 * side;
		mark = disc(centre, radius);
		if (cls == StationClass::Halt) {
			const qreal hole = radius - std::max(0.16 * side, haloWidth);
			mark.addEllipse(centre, hole, hole);
		}
	}
	return {{mark, fill}};
}

// The parts of the mark of style in frame.
QList<MarkPart> stationMarkParts(const QRectF& frame, StationClass cls, StationMarkStyle style, const QColor& fill,
	const QColor& halo, qreal haloWidth) {
	switch (style) {
		case StationMarkStyle::RingAndBar:
			return ringAndBarParts(frame, cls, fill, halo, haloWidth);
		case StationMarkStyle::ClassMarks:
			return classMarkParts(frame, cls, fill, haloWidth);
		case StationMarkStyle::PlainDot: {
			// The only style that tells the classes apart by size alone.
			const qreal across = cls == StationClass::Halt ? 0.4 : (cls == StationClass::Major ? 0.72 : 0.56);
			return {{disc(frame.center(), across * std::min(frame.width(), frame.height()) / 2.0), fill}};
		}
		case StationMarkStyle::MapPin:
		default:
			return pinParts(frame, cls, fill, halo, haloWidth);
	}
}

// The convex hull of points in order around it (Andrew's monotone chain); fewer than three distinct points stay as they are.
QPolygonF convexHull(QList<QPointF> points) {
	std::sort(points.begin(), points.end(), [](const QPointF& left, const QPointF& right) {
		return left.x() < right.x() || (left.x() == right.x() && left.y() < right.y());
	});
	points.erase(std::unique(points.begin(), points.end()), points.end());
	if (points.size() < 3)
		return QPolygonF(std::move(points));
	const auto turn = [](const QPointF& origin, const QPointF& a, const QPointF& b) {
		return (a.x() - origin.x()) * (b.y() - origin.y()) - (a.y() - origin.y()) * (b.x() - origin.x());
	};
	QPolygonF hull;
	for (int pass = 0; pass < 2; ++pass) {
		const qsizetype start = hull.size();
		for (qsizetype index = 0; index < points.size(); ++index) {
			const QPointF& point = points.at(pass == 0 ? index : points.size() - 1 - index);
			while (hull.size() >= start + 2 && turn(hull.at(hull.size() - 2), hull.last(), point) <= 0.0)
				hull.removeLast();
			hull.append(point);
		}
		hull.removeLast();
	}
	return hull;
}

// The convex hull of points widened by width / 2 on every side: a dot for one point, a capsule for two.
QPainterPath widenedHull(const QList<QPointF>& points, qreal width) {
	const QPolygonF hull = convexHull(points);
	if (hull.size() == 1)
		return disc(hull.first(), width / 2.0);
	QPainterPath outline;
	outline.addPolygon(hull);
	if (hull.size() > 2)
		outline.closeSubpath();
	QPainterPathStroker stroker;
	stroker.setWidth(width);
	stroker.setCapStyle(Qt::RoundCap);
	stroker.setJoinStyle(Qt::RoundJoin);
	// The stroke of two points is the capsule. Around more it is a ring that the hull fills; the line between two points
	// has no inside, and as a path of its own it would get an edge across the capsule.
	if (hull.size() == 2)
		return stroker.createStroke(outline).simplified();
	QPainterPath area;
	area.addPolygon(hull);
	return stroker.createStroke(outline).united(area);
}

} // namespace

TrackVisual freeTrackVisual() {
	return classifyTrackSpeed(0.0);
}

TrackVisual classifyTrackSpeed(double speedLimitMetersPerSecond) {
	const double kmh = speedLimitMetersPerSecond * 3.6;
	if (kmh >= 200.0)
		return {QColor(30, 130, 210), 4};
	if (kmh >= 120.0)
		return {QColor(160, 160, 160), 3};
	return {QColor(120, 120, 120), 2};
}

QColor occupiedTrackColor() {
	return QColor(Qt::red);
}

TrackStateVisual classifyTrackState(TrackOperationalState state) {
	switch (state) {
		case TrackOperationalState::Prepared:
			return {QColor("#4C8DAE"), 5, Qt::DashDotLine};
		case TrackOperationalState::Occupied:
			return {occupiedTrackColor(), 6, Qt::SolidLine};
		case TrackOperationalState::Blocked:
			return {QColor("#D6A13A"), 5, Qt::DashLine};
		case TrackOperationalState::Free:
		default:
			return {Qt::transparent, 0, Qt::NoPen};
	}
}

TrainBadgeShape classifyTrainBadgeShape(TrainVisualKind kind) {
	switch (kind) {
		case TrainVisualKind::Intercity:
			return TrainBadgeShape::Capsule;
		case TrainVisualKind::Freight:
			return TrainBadgeShape::Square;
		case TrainVisualKind::Passenger:
		case TrainVisualKind::Sprinter:
		case TrainVisualKind::HighSpeed:
		default:
			return TrainBadgeShape::Rounded;
	}
}

int trainBadgeCornerRadius(TrainBadgeShape shape) {
	switch (shape) {
		case TrainBadgeShape::Capsule:
			return 8;
		case TrainBadgeShape::Rounded:
			return 3;
		case TrainBadgeShape::Square:
		default:
			return 0;
	}
}

int trackStatePriority(TrackOperationalState state) {
	switch (state) {
		case TrackOperationalState::Prepared:
			return 1;
		case TrackOperationalState::Occupied:
			return 2;
		case TrackOperationalState::Blocked:
			return 3;
		case TrackOperationalState::Free:
		default:
			return 0;
	}
}

QColor defaultTrainFill() {
	return QColor(235, 210, 55);
}

QColor defaultTrainOutline() {
	return QColor(110, 90, 20);
}

TrainVisual classifyTrainType(const std::string& type, const std::string& description) {
	std::string text = lower(type + " " + description);
	const QColor fill = defaultTrainFill();
	const QColor outline = defaultTrainOutline();
	if (containsAny(text, {"freight", "cargo", "goederen"}))
		return {TrainVisualKind::Freight, fill, outline, classifyTrainBadgeShape(TrainVisualKind::Freight), ":/icons/train-freight.svg"};
	if (containsAny(text, {"ice", "hst", "highspeed", "high speed"}))
		return {TrainVisualKind::HighSpeed, fill, outline, classifyTrainBadgeShape(TrainVisualKind::HighSpeed), ":/icons/train-high-speed.svg"};
	if (containsAny(text, {"sprinter", "spr"}))
		return {TrainVisualKind::Sprinter, fill, outline, classifyTrainBadgeShape(TrainVisualKind::Sprinter), ":/icons/train-sprinter.svg"};
	if (containsAny(text, {"intercity", " ic", "ic "}))
		return {TrainVisualKind::Intercity, fill, outline, classifyTrainBadgeShape(TrainVisualKind::Intercity), ":/icons/train-intercity.svg"};
	return {TrainVisualKind::Passenger, fill, outline, classifyTrainBadgeShape(TrainVisualKind::Passenger), ":/icons/train-passenger.svg"};
}

TrainVisual resolveTrainVisual(const std::string& type, const std::string& description, const QColor& serviceColor) {
	TrainVisual visual = classifyTrainType(type, description);
	if (serviceColor.isValid()) {
		visual.fill = serviceColor;
		visual.outline = serviceColor.darker(200);
	}
	return visual;
}

SignalCueKind classifySignalCue(int code) {
	// 751 is the second red of BACC: the train has to stop.
	if (code == 0 || code == 751)
		return SignalCueKind::Stop;
	if (code == 75)
		return SignalCueKind::Caution;
	if (code == 180 || code == 270)
		return SignalCueKind::Proceed;
	return SignalCueKind::Neutral;
}

SignalVisual classifySignalAspect(int code) {
	if (code == 0 || code == 751)
		return {QColor(Qt::red), classifySignalCue(code), ":/icons/signal-stop.svg"};
	if (code == 75)
		return {QColor(Qt::yellow), classifySignalCue(code), ":/icons/signal-caution.svg"};
	if (code == 180 || code == 270)
		return {QColor(Qt::green), classifySignalCue(code), ":/icons/signal-proceed.svg"};
	return {QColor(128, 128, 128), classifySignalCue(code), ":/icons/signal-neutral.svg"};
}

StationVisual classifyStation(bool hasPlatformId, int connectionCount) {
	if (connectionCount >= 3)
		return {QColor(80, 120, 210), QColor(30, 60, 130), StationClass::Major};
	if (hasPlatformId)
		return {QColor(70, 70, 70), QColor(30, 30, 30), StationClass::Station};
	return {QColor(120, 120, 120), QColor(70, 70, 70), StationClass::Halt};
}

void paintStationMark(QPainter* painter, const QRectF& frame, StationClass cls, StationMarkStyle style, const QColor& fill,
	const QColor& halo, qreal haloWidth) {
	paintMarkParts(painter, stationMarkParts(frame, cls, style, fill, halo, haloWidth), halo, haloWidth);
}

QPointF stationMarkAnchor(const QRectF& frame, StationMarkStyle style) {
	return style == StationMarkStyle::MapPin ? QPointF(frame.center().x(), frame.bottom()) : frame.center();
}

QRectF stationMarkBounds(const QRectF& frame, StationClass cls, StationMarkStyle style, qreal haloWidth) {
	QRectF bounds;
	for (const MarkPart& part : stationMarkParts(frame, cls, style, Qt::white, Qt::black, haloWidth)) {
		const qreal grow = haloWidth + part.edgeWidth / 2.0;
		bounds |= part.path.boundingRect().adjusted(-grow, -grow, grow, grow);
	}
	return bounds;
}

QPainterPath stationMarkShape(const QRectF& frame, StationClass cls, StationMarkStyle style, qreal haloWidth) {
	// The parts with their edges. A ring is two circles drawn the same way round, so with the winding rule its hole is
	// part of the shape.
	QPainterPath shape;
	for (const MarkPart& part : stationMarkParts(frame, cls, style, Qt::white, Qt::black, haloWidth)) {
		QPainterPath area = part.path;
		area.setFillRule(Qt::WindingFill);
		shape = shape.united(area);
		if (part.edgeWidth > 0.0) {
			QPainterPathStroker stroker;
			stroker.setWidth(part.edgeWidth);
			stroker.setJoinStyle(Qt::RoundJoin);
			shape = shape.united(stroker.createStroke(part.path));
		}
	}
	return shape;
}

QRectF stationMarkFrameIn(const QRectF& area, StationClass cls, StationMarkStyle style, qreal haloWidth) {
	// Every class takes the frame in which the marks of all three fit, so that the classes keep their sizes: in the
	// styles but the class marks that is the frame of the mark of a major station. The mark grows with the frame and its
	// halo does not, measured on a frame of about the size that fits.
	const QRectF reference(0.0, 0.0, std::min(area.width(), area.height()), std::min(area.width(), area.height()));
	QRectF mark;
	QRectF painted;
	for (const StationClass each : {StationClass::Halt, StationClass::Station, StationClass::Major}) {
		mark |= stationMarkBounds(reference, each, style, 0.0);
		painted |= stationMarkBounds(reference, each, style, haloWidth);
	}
	const qreal side = reference.width()
		* std::min((area.width() - (painted.width() - mark.width())) / mark.width(),
			(area.height() - (painted.height() - mark.height())) / mark.height());
	// A pin is not centred in its frame, so the frame moves to centre what it paints.
	const QRectF frame(0.0, 0.0, side, side);
	return frame.translated(area.center() - stationMarkBounds(frame, cls, style, haloWidth).center());
}

QRectF stationFootprintRect(const QList<QPointF>& centres, StationClass cls, qreal side) {
	if (centres.isEmpty())
		return QRectF();
	const qreal grow = (cls == StationClass::Halt ? 0.15 : (cls == StationClass::Major ? 0.265 : 0.255)) * side;
	const QRectF box = cls == StationClass::Halt ? QRectF(centres.first(), QSizeF()) : QPolygonF(centres).boundingRect();
	return box.adjusted(-grow, -grow, grow, grow);
}

QPainterPath stationAreaShape(const QList<QPointF>& centres, qreal width) {
	return centres.isEmpty() ? QPainterPath() : widenedHull(centres, width);
}

QColor stationAreaColor() {
	QColor colour = kStationMarkAccent;
	colour.setAlpha(kStationAreaAlpha);
	return colour;
}

QColor canvasColor() {
	return QColor(16, 26, 34);
}

QColor stationMarkFill() {
	return QColor(232, 238, 242);
}

QIcon stationMarkIcon(StationClass cls, StationMarkStyle style) {
	QIcon icon;
	for (const int size : {16, 32}) {
		QPixmap pixmap(size, size);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		// A pin and a bar reach past their frame, so the mark is fitted to the pixmap by what it paints.
		const qreal halo = size / 16.0;
		paintStationMark(&painter, stationMarkFrameIn(QRectF(0.0, 0.0, size, size), cls, style, halo), cls, style,
			stationMarkFill(), canvasColor(), halo);
		painter.end();
		icon.addPixmap(pixmap);
	}
	return icon;
}

// One simulated second renders every delayMs, so the nominal playback factor
// against real time is 1000 / delayMs; zero delay runs unthrottled.
QString simulationSpeedLabel(int delayMs) {
	if (delayMs <= 0)
		return "Fastest";
	const double factor = 1000.0 / delayMs;
	if (factor >= 10.0)
		return QString("%1x").arg(qRound(factor));
	return QString("%1x").arg(factor, 0, 'f', 1);
}
