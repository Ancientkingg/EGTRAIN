#include "graphics/items/StationOverlayItem.h"

#include "graphics/items/TrackLineItem.h"

#include <QGlyphRun>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QLineF>
#include <QPaintDevice>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRawFont>
#include <QStyleOptionGraphicsItem>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {
constexpr qreal kSymbolPixels = 16.0;
constexpr qreal kLabelPixels = 11.0;
constexpr qreal kLabelGap = 8.0;
constexpr qreal kNameCollisionGap = 2.0;
// A name meets the pictogram of a neighbour at the same height along the artwork line.
// It covers that pictogram only when they share more than this many pixels both ways.
constexpr qreal kPictogramOverlapPixels = 2.0;
// The station mark is light, stationMarkFill(), with a halo of this many logical pixels in canvasColor().
constexpr qreal kMarkHaloPixels = 2.0;
// The top of the name is this many mark frames below the footprint of the squares, and its bottom as many above the pin.
constexpr qreal kNearNameGap = 0.25;
// A station node or platform bar farther than this many mark frames at scene size from the anchor is at another place
// of the station, such as a platform node on another line, and gets no area and no say in where the mark and the name
// go.
constexpr qreal kStationPlaceFrames = 32.0;

// Enlarges item by factor about pivot in one change and moves pivot by shift. The item transform acts
// after the item scale, so pivot and shift are in the coordinates of the scaled item (its parent without pos).
void enlargeAbout(QGraphicsItem* item, const QPointF& pivot, qreal factor, const QPointF& shift = QPointF()) {
	item->setTransform(QTransform()
			.translate(pivot.x() + shift.x(), pivot.y() + shift.y())
			.scale(factor, factor)
			.translate(-pivot.x(), -pivot.y()));
}

// The shift for enlargeAbout() that puts pivot of item at scenePoint.
QPointF shiftTo(const QGraphicsItem* item, const QPointF& pivot, const QPointF& scenePoint) {
	const QPointF target = item->parentItem() ? item->parentItem()->mapFromScene(scenePoint) : scenePoint;
	return target - item->pos() - pivot;
}

// The eight directions around a point, 45 degrees apart.
constexpr qreal kDiagonal = 0.70710678118654752;
constexpr QPointF kHaloDirections[] = {{1.0, 0.0}, {kDiagonal, kDiagonal}, {0.0, 1.0}, {-kDiagonal, kDiagonal},
	{-1.0, 0.0}, {-kDiagonal, -kDiagonal}, {0.0, -1.0}, {kDiagonal, -kDiagonal}};
// Qt draws a glyph up to this many device pixels from its glyph cache, and fills the outline of a larger one.
constexpr qreal kCachedGlyphPixels = 64.0;

// The halo of a StationNameItem, a child stacked behind it: a band of StationNameItem::HaloPixels on screen around each
// letter of the name in canvasColor(), drawn on every paint, so that it stays on the letters at any zoom and at any
// position of the view. While Qt draws the letters from its glyph cache, the band is the letters drawn sixteen times
// around their place, in the eight directions at half and at all of its width. Larger letters Qt fills as outlines,
// and there the band is a stroke along their outlines, which costs less than sixteen fills. The glyphs are taken from
// the document of the name at the first paint, and their outlines when the first stroke needs them.
class StationNameHalo : public QGraphicsItem {
public:
	enum { Type = UserType + 15 };

	// The halo of name, in a font of fontPixels pixels, which reaches at most reach, in the units of the name, past its
	// letters. Its bounds leave half as much again for the antialiased edge, a device pixel at most.
	StationNameHalo(QGraphicsTextItem* name, qreal fontPixels, qreal reach)
		: QGraphicsItem(name), m_fontPixels(fontPixels), m_reach(reach),
		  m_bounds(name->boundingRect().adjusted(-1.5 * reach, -1.5 * reach, 1.5 * reach, 1.5 * reach)) {
		setFlag(QGraphicsItem::ItemStacksBehindParent);
		setAcceptedMouseButtons(Qt::NoButton);
	}

	int type() const override { return Type; }
	QRectF boundingRect() const override { return m_bounds; }

	void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
		const qreal pixelsPerUnit = QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter->worldTransform());
		if (pixelsPerUnit <= 0.0)
			return;
		if (!m_laidOut) {
			m_runs = glyphRuns(static_cast<const QGraphicsTextItem*>(parentItem()));
			m_laidOut = true;
		}
		const qreal width = std::min(StationNameItem::HaloPixels / pixelsPerUnit, m_reach);
		const qreal devicePixelRatio = painter->device() ? painter->device()->devicePixelRatio() : 1.0;
		painter->save();
		if (m_fontPixels * pixelsPerUnit * devicePixelRatio <= kCachedGlyphPixels) {
			painter->setPen(canvasColor());
			for (const qreal reach : {width / 2.0, width})
				for (const QPointF& direction : kHaloDirections)
					for (const QGlyphRun& run : std::as_const(m_runs))
						painter->drawGlyphRun(direction * reach, run);
		} else {
			if (m_outlines.isEmpty())
				m_outlines = outlines(m_runs);
			painter->setRenderHint(QPainter::Antialiasing);
			painter->strokePath(m_outlines, QPen(canvasColor(), 2.0 * width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		}
		painter->restore();
	}

private:
	// The glyphs of the document of name, where it draws them. They come from each line, since a layout of a document
	// block holds no text of its own to take them from.
	static QList<QGlyphRun> glyphRuns(const QGraphicsTextItem* name) {
		QList<QGlyphRun> runs;
		for (QTextBlock block = name->document()->begin(); block.isValid(); block = block.next()) {
			const QTextLayout* layout = block.layout();
			for (int line = 0; line < layout->lineCount(); ++line)
				for (QGlyphRun run : layout->lineAt(line).glyphRuns()) {
					QList<QPointF> positions = run.positions();
					for (QPointF& position : positions)
						position += layout->position();
					run.setPositions(positions);
					runs.append(run);
				}
		}
		return runs;
	}

	// The outlines of the glyphs of runs, where they are drawn.
	static QPainterPath outlines(const QList<QGlyphRun>& runs) {
		QPainterPath path;
		for (const QGlyphRun& run : runs) {
			const QRawFont font = run.rawFont();
			const QList<quint32> glyphs = run.glyphIndexes();
			const QList<QPointF> positions = run.positions();
			for (qsizetype index = 0; index < glyphs.size() && index < positions.size(); ++index)
				path.addPath(font.pathForGlyph(glyphs.at(index)).translated(positions.at(index)));
		}
		return path;
	}

	qreal m_fontPixels;
	qreal m_reach;
	QRectF m_bounds;
	QList<QGlyphRun> m_runs;
	bool m_laidOut = false;
	QPainterPath m_outlines;
};

}

StationMarkItem::StationMarkItem(StationClass cls, int size, QGraphicsItem* parent)
	: QGraphicsPixmapItem(parent), m_class(cls) {
	QPixmap frame(size, size);
	frame.fill(Qt::transparent);
	setPixmap(frame);
	// The far mark can reach past the pixmap. Its halo and its edge are widest when the frame has its smallest size on
	// screen.
	const QRectF pixmapRect(offset(), QSizeF(size, size));
	const qreal halo = kMarkHaloPixels * size / StationOverlayItem::MinPictogramPixels;
	m_farBounds = pixmapRect.united(stationMarkBounds(pixmapRect, m_class, halo));
	m_farShape = stationMarkShape(pixmapRect, m_class, halo);
}

QRectF StationMarkItem::boundingRect() const {
	return m_nearLook ? QRectF() : m_farBounds;
}

QPainterPath StationMarkItem::shape() const {
	return m_nearLook ? QPainterPath() : m_farShape;
}

void StationMarkItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
	const qreal pixelsPerUnit = QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter->worldTransform());
	if (m_nearLook || pixelsPerUnit <= 0.0)
		return;
	paintStationMark(painter, QRectF(offset(), QSizeF(pixmap().size())), m_class, stationMarkFill(), canvasColor(),
		kMarkHaloPixels / pixelsPerUnit);
}

void StationMarkItem::setNearLook(bool nearLook) {
	if (m_nearLook == nearLook)
		return;
	prepareGeometryChange();
	m_nearLook = nearLook;
	update();
}

StationNameItem::StationNameItem(const QString& text, const QFont& font, QGraphicsItem* parent)
	: QGraphicsTextItem(text, parent) {
	setFont(font);
	setDefaultTextColor(Qt::white);
	setZValue(NameZValue);
	setAcceptedMouseButtons(Qt::NoButton);
	// The halo is widest at the smallest size of the name on screen.
	const qreal pixels = font.pixelSize() > 0 ? font.pixelSize() : QFontMetricsF(font).height();
	m_halo = new StationNameHalo(this, pixels, HaloPixels * pixels / StationOverlayItem::MinNamePixels);
}

StationOverlayItem::StationOverlayItem(const QString& stationName, const QPointF& stableAnchor,
	const StationVisual& visual, int degree, QGraphicsItem* parent)
	: QGraphicsItem(parent), m_stationName(stationName), m_stableAnchor(stableAnchor),
	  m_visual(visual), m_degree(std::max(0, degree)), m_endpoint(degree == 1) {
	setFlag(QGraphicsItem::ItemIgnoresTransformations);
	setFlag(QGraphicsItem::ItemIsSelectable);
	setAcceptedMouseButtons(Qt::NoButton);
	setZValue(3.5);
	m_labelFont.setPixelSize(static_cast<int>(kLabelPixels));
	m_displayName = displayName(m_stationName);
	m_symbolRect = QRectF(-kSymbolPixels / 2.0, -kSymbolPixels / 2.0, kSymbolPixels, kSymbolPixels);
	rebuildGeometry();
	setPos(m_stableAnchor);
}

StationOverlayItem::~StationOverlayItem() = default;

QString StationOverlayItem::displayName() const {
	return m_displayName;
}

QString StationOverlayItem::displayName(const QString& stationName) {
	QString spaced;
	spaced.reserve(stationName.size() + 4);
	for (int i = 0; i < stationName.size(); ++i) {
		const QChar current = stationName.at(i);
		if (i > 0 && current.isUpper()) {
			const QChar previous = stationName.at(i - 1);
			const bool startsWord = previous.isLower();
			int precedingUppercaseRun = 0;
			for (int j = i - 1; j >= 0 && stationName.at(j).isUpper(); --j)
				++precedingUppercaseRun;
			const bool endsAcronym = precedingUppercaseRun >= 2
				&& i + 1 < stationName.size() && stationName.at(i + 1).isLower();
			if (startsWord || endsAcronym)
				spaced.append(QLatin1Char(' '));
		}
		spaced.append(current);
	}
	return spaced;
}

QString StationOverlayItem::displayName(const std::string& stationName) {
	return displayName(QString::fromStdString(stationName));
}

void StationOverlayItem::setSourceIdentities(const QList<SourceIdentity>& identities) {
	m_sourceIdentities = identities;
}

bool StationOverlayItem::matchesSourceIdentity(double nodeId, int track) const {
	return std::any_of(m_sourceIdentities.cbegin(), m_sourceIdentities.cend(),
		[nodeId, track](const SourceIdentity& identity) {
			return identity.nodeId == nodeId && identity.track == track;
		});
}

double StationOverlayItem::sourceNodeId() const {
	return m_sourceIdentities.isEmpty() ? 0.0 : m_sourceIdentities.first().nodeId;
}

int StationOverlayItem::sourceTrack() const {
	return m_sourceIdentities.isEmpty() ? -1 : m_sourceIdentities.first().track;
}

StationOverlayItem::ViewportPlacement StationOverlayItem::placementForSide(
	LabelSide side, const QPointF& deviceAnchor, const QRectF& viewportInset) const {
	const QRectF rawSymbol = m_symbolRect.translated(deviceAnchor);
	const QRectF rawLabel = labelRectForSide(side).translated(deviceAnchor);
	const QRectF rawCombined = rawSymbol.united(rawLabel);
	const QRectF clampReach = viewportInset.adjusted(-rawCombined.width(), -rawCombined.height(),
		rawCombined.width(), rawCombined.height());
	const auto overflowFor = [&viewportInset](const QRectF& rect) {
		return std::max<qreal>(0.0, viewportInset.left() - rect.left())
			+ std::max<qreal>(0.0, rect.right() - viewportInset.right())
			+ std::max<qreal>(0.0, viewportInset.top() - rect.top())
			+ std::max<qreal>(0.0, rect.bottom() - viewportInset.bottom());
	};
	QPointF offset;
	if (clampReach.contains(deviceAnchor)) {
		if (rawCombined.left() < viewportInset.left())
			offset.rx() += viewportInset.left() - rawCombined.left();
		else if (rawCombined.right() > viewportInset.right())
			offset.rx() += viewportInset.right() - rawCombined.right();
		if (rawCombined.top() < viewportInset.top())
			offset.ry() += viewportInset.top() - rawCombined.top();
		else if (rawCombined.bottom() > viewportInset.bottom())
			offset.ry() += viewportInset.bottom() - rawCombined.bottom();
	}
	ViewportPlacement placement;
	placement.side = side;
	placement.offset = offset;
	placement.symbolRect = rawSymbol.translated(offset);
	placement.labelRect = rawLabel.translated(offset);
	placement.combinedRect = rawCombined.translated(offset);
	placement.overflow = overflowFor(rawCombined);
	placement.fitsBeforeClamp = viewportInset.contains(rawCombined);
	placement.fits = viewportInset.contains(placement.combinedRect);
	return placement;
}

StationOverlayItem::ViewportPlacement StationOverlayItem::preferredViewportPlacement(
	const QPointF& deviceAnchor, const QRectF& viewportInset) const {
	const ViewportPlacement right = placementForSide(LabelSide::Right, deviceAnchor, viewportInset);
	const ViewportPlacement left = placementForSide(LabelSide::Left, deviceAnchor, viewportInset);
	if (right.fitsBeforeClamp || (!left.fitsBeforeClamp && right.overflow <= left.overflow))
		return right;
	return left;
}

QRectF StationOverlayItem::combinedRect() const {
	return m_labelRect.united(m_symbolRect);
}

QRectF StationOverlayItem::boundingRect() const {
	return combinedRect();
}

QPainterPath StationOverlayItem::shape() const {
	QPainterPath path;
	if (m_sceneDecoration)
		return path;
	path.addRect(m_symbolRect);
	if (isLabelVisible())
		path.addRect(m_labelRect);
	return path;
}

void StationOverlayItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
	Q_UNUSED(option);
	Q_UNUSED(widget);
	if (m_sceneDecoration)
		return;
	painter->save();
	// The pin reaches past its frame, so it is fitted to the symbol by what it paints.
	paintStationMark(painter, stationMarkFrameIn(m_symbolRect, m_visual.cls, kMarkHaloPixels), m_visual.cls,
		stationMarkFill(), canvasColor(), kMarkHaloPixels);
	if (isLabelVisible()) {
		painter->setPen(Qt::white);
		painter->setFont(m_labelFont);
		painter->drawText(m_labelRect, Qt::AlignVCenter | Qt::AlignLeft, m_displayName);
	}
	painter->restore();
}

void StationOverlayItem::setNameVisible(bool visible) {
	if (m_nameVisible == visible)
		return;
	m_nameVisible = visible;
	update();
}

void StationOverlayItem::setFollowed(bool followed) {
	if (m_followed == followed)
		return;
	m_followed = followed;
	update();
}

void StationOverlayItem::setNetworkDegree(int degree, bool interchange, bool endpoint) {
	degree = std::max(0, degree);
	const bool nextInterchange = interchange || degree >= 3;
	const bool nextEndpoint = endpoint || degree == 1;
	if (m_degree == degree && m_interchange == nextInterchange && m_endpoint == nextEndpoint)
		return;
	m_degree = degree;
	m_interchange = nextInterchange;
	m_endpoint = nextEndpoint;
	update();
}

bool StationOverlayItem::isInterchange() const {
	return m_interchange || m_degree >= 3;
}

bool StationOverlayItem::isEndpoint() const {
	return m_endpoint || m_degree == 1;
}

bool StationOverlayItem::priorityLess(const StationOverlayItem& left, const StationOverlayItem& right,
	const QPointF& viewportCenter) {
	if (left.isSelected() != right.isSelected())
		return left.isSelected();
	if (left.isFollowed() != right.isFollowed())
		return left.isFollowed();
	if (left.m_platformCount != right.m_platformCount)
		return left.m_platformCount > right.m_platformCount;
	if (left.isInterchange() != right.isInterchange())
		return left.isInterchange();
	if (left.isEndpoint() != right.isEndpoint())
		return left.isEndpoint();
	const QPointF leftDelta = left.m_stableAnchor - viewportCenter;
	const QPointF rightDelta = right.m_stableAnchor - viewportCenter;
	const qreal leftDistance = leftDelta.x() * leftDelta.x() + leftDelta.y() * leftDelta.y();
	const qreal rightDistance = rightDelta.x() * rightDelta.x() + rightDelta.y() * rightDelta.y();
	if (!qFuzzyCompare(leftDistance + 1.0, rightDistance + 1.0))
		return leftDistance < rightDistance;
	const int nameOrder = QString::compare(left.m_stationName, right.m_stationName, Qt::CaseSensitive);
	if (nameOrder != 0)
		return nameOrder < 0;
	if (!qFuzzyCompare(left.m_stableAnchor.x() + 1.0, right.m_stableAnchor.x() + 1.0))
		return left.m_stableAnchor.x() < right.m_stableAnchor.x();
	return left.m_stableAnchor.y() < right.m_stableAnchor.y();
}

qreal StationOverlayItem::readableItemScale(qreal nativeSizePx, qreal presentationScale,
	qreal viewScale, qreal minPx) {
	if (nativeSizePx <= 0.0 || viewScale <= 0.0)
		return presentationScale;
	return std::max(presentationScale, minPx / (nativeSizePx * viewScale));
}

void StationOverlayItem::attachArtwork(QGraphicsPixmapItem* picture, QGraphicsTextItem* name,
	int platformCount) {
	m_picture = picture;
	m_name = name;
	m_platformCount = std::max(0, platformCount);
	m_appliedViewScale = 0.0;
	if (m_picture) {
		const QPixmap& pixmap = m_picture->pixmap();
		const QSizeF size = QSizeF(pixmap.size()) / pixmap.devicePixelRatio();
		m_pictureSceneScale = m_picture->scale() > 0.0 ? m_picture->scale() : 1.0;
		m_pictureNativePixels = size.height();
		m_pictureAnchor = m_picture->offset() + QPointF(size.width() / 2.0, size.height());
	}
	if (m_name) {
		const QFont font = m_name->font();
		m_nameSceneScale = m_name->scale() > 0.0 ? m_name->scale() : 1.0;
		m_nameNativePixels = font.pixelSize() > 0 ? font.pixelSize() : QFontMetricsF(font).height();
		m_nameAnchor = QPointF(m_name->boundingRect().width() / 2.0, 0.0);
	}
	if (!m_area && dynamic_cast<StationMarkItem*>(m_picture) && m_picture->scene()) {
		m_area = new QGraphicsPathItem;
		m_area->setPen(Qt::NoPen);
		m_area->setBrush(stationAreaColor());
		m_area->setZValue(AreaZValue);
		m_area->setAcceptedMouseButtons(Qt::NoButton);
		m_area->setVisible(false);
		m_picture->scene()->addItem(m_area);
	}
}

void StationOverlayItem::setNodeCentres(const QList<QPointF>& centres) {
	auto* mark = dynamic_cast<StationMarkItem*>(m_picture);
	if (!mark)
		return;
	const qreal reach = kStationPlaceFrames * m_pictureNativePixels * m_pictureSceneScale;
	QList<QPointF> place;
	for (const QPointF& centre : centres)
		if (QLineF(centre, m_stableAnchor).length() <= reach)
			place.append(centre);
	mark->setNodeCentres(place);
	// The mark and the name are placed again by the next applyViewScale().
	m_appliedViewScale = 0.0;
}

void StationOverlayItem::setPlatformBars(const QList<QPolygonF>& bars) {
	const qreal reach = kStationPlaceFrames * m_pictureNativePixels * m_pictureSceneScale;
	m_barCorners.clear();
	for (const QPolygonF& bar : bars)
		if (QLineF(bar.boundingRect().center(), m_stableAnchor).length() <= reach)
			m_barCorners.append(bar);
	// The area is drawn again by the next applyViewScale().
	m_appliedViewScale = 0.0;
}

void StationOverlayItem::applyViewScale(qreal viewScale, bool nearLook) {
	// A plain picture and a mark without station squares keep the far look, so that they stay to be seen and clicked. In
	// the near look such a mark keeps its pin and its name at their smallest size on screen.
	auto* mark = dynamic_cast<StationMarkItem*>(m_picture);
	const bool nearMark = nearLook && mark && !mark->nodeCentres().isEmpty();
	const bool keptPin = nearLook && mark && mark->nodeCentres().isEmpty();
	viewScale = std::abs(viewScale);
	if (viewScale > 0.0 && (viewScale != m_appliedViewScale || nearMark != m_appliedNearLook || keptPin != m_appliedKeptPin)) {
		m_appliedViewScale = viewScale;
		m_appliedNearLook = nearMark;
		m_appliedKeptPin = keptPin;
		placeArtwork(viewScale, nearMark, keptPin);
	}
	// The area follows the mark, which the station layer and the parent of the mark can hide.
	if (m_area)
		m_area->setVisible(m_picture && m_picture->isVisible());
}

namespace {
// The top edge at x of the convex hull of centres: the smallest y over the pairs of centres whose x range holds x, where a
// pair of one x, a centre with itself included, counts at that x. x is first kept in the x range of the centres, which a
// rounded x can leave by a little.
qreal hullTopAt(const QList<QPointF>& centres, qreal x) {
	const auto [left, right] = std::minmax_element(centres.cbegin(), centres.cend(),
		[](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
	x = std::clamp(x, left->x(), right->x());
	qreal top = std::numeric_limits<qreal>::infinity();
	for (qsizetype first = 0; first < centres.size(); ++first)
		for (qsizetype second = first; second < centres.size(); ++second) {
			const QPointF& a = centres.at(first);
			const QPointF& b = centres.at(second);
			if (x < std::min(a.x(), b.x()) || x > std::max(a.x(), b.x()))
				continue;
			top = std::min(top, a.x() == b.x() ? std::min(a.y(), b.y()) : a.y() + (b.y() - a.y()) * (x - a.x()) / (b.x() - a.x()));
		}
	return top;
}
} // namespace

// A station mark and its name are placed by the node centres of the mark, or by the anchor without them, in both
// looks: the frame of the far look stands on the top edge of the hull of the centres at the x of the centre of the
// footprint of the squares, and the name sits a small gap under the footprint, centred under it, so that it keeps its
// place when the look switches. The frame of a station or major station stands on the top centre of the box of its
// centres for a single centre, a row or a stack, and a halt stands on its first square. A plain picture and its name
// stay at their artwork point. A kept pin, that of a mark without squares in the near look, and its name have their
// smallest size on screen.
void StationOverlayItem::placeArtwork(qreal viewScale, bool nearLook, bool keptPin) {
	const auto itemScale = [viewScale, keptPin](qreal nativeSizePx, qreal presentationScale, qreal minPx) {
		return keptPin && nativeSizePx > 0.0 ? minPx / (nativeSizePx * viewScale)
											 : readableItemScale(nativeSizePx, presentationScale, viewScale, minPx);
	};
	auto* mark = dynamic_cast<StationMarkItem*>(m_picture);
	QList<QPointF> centres;
	if (mark)
		centres = mark->nodeCentres().isEmpty() ? QList<QPointF>{m_stableAnchor} : mark->nodeCentres();
	qreal side = 0.0;
	QRectF marks;
	QPointF foot;
	qreal markTop = 0.0;
	m_markSceneRect = QRectF();
	m_squaresSceneRect = QRectF();
	if (m_picture) {
		const qreal scale = itemScale(m_pictureNativePixels, m_pictureSceneScale, MinPictogramPixels);
		side = m_pictureNativePixels * scale;
		if (mark) {
			const qreal halo = kMarkHaloPixels / viewScale;
			marks = stationFootprintRect(centres, mark->stationClass(), side);
			foot = QPointF(marks.center().x(), hullTopAt(centres, marks.center().x()));
			// The far bounds scaled from the pixmap to the frame that stands on the hull of the centres.
			const QRectF pixmapRect(m_picture->offset(), QSizeF(m_picture->pixmap().size()));
			const QRectF frame(foot.x() - side / 2.0, foot.y() - side, side, side);
			const QRectF far = mark->farBoundingRect();
			const qreal k = side / pixmapRect.height();
			m_markSceneRect = QRectF(frame.topLeft() + (far.topLeft() - pixmapRect.topLeft()) * k, far.size() * k);
			// Above the mark counts from the painted top of the pin, its halo included.
			markTop = m_markSceneRect.top();
			m_squaresSceneRect = mark->nodeCentres().isEmpty() ? QRectF() : marks.adjusted(-halo, -halo, halo, halo);
			m_markSceneRect |= m_squaresSceneRect;
			// The area covers the platform bars in the near look, where they show.
			if (m_area)
				m_area->setPath(stationAreaShape(nearLook ? centres + m_barCorners : centres, 2.0 * AreaMarginPixels / viewScale));
		}
		// The mark stands in its place in both looks.
		const QPointF pivot = m_pictureAnchor * m_pictureSceneScale;
		QPointF shift;
		if (mark) {
			mark->setNearLook(nearLook);
			shift = shiftTo(m_picture, pivot, foot);
		}
		enlargeAbout(m_picture, pivot, scale / m_pictureSceneScale, shift);
	}
	if (m_name) {
		const qreal scale = itemScale(m_nameNativePixels, m_nameSceneScale, MinNamePixels);
		const QPointF pivot = m_nameAnchor * m_nameSceneScale;
		m_nameFactor = scale / m_nameSceneScale;
		m_nameSpots.clear();
		QPointF shift;
		if (mark) {
			// The spots in the order they are tried, each a small gap from the footprint or the pin, and last above a frame
			// on the top of the box of the centres at the x of the footprint, where the frame of a row or a stack stands.
			// A mark that draws nothing in the near look has neither pin nor frame there, so its name goes above the
			// footprint instead and has no last spot.
			const qreal gap = kNearNameGap * side;
			const bool pinDrawn = !mark->isNearLook();
			const auto highest = std::min_element(centres.cbegin(), centres.cend(),
				[](const QPointF& a, const QPointF& b) { return a.y() < b.y(); });
			const qreal margin = m_name->document()->documentMargin() * scale;
			const QSizeF size = m_name->boundingRect().size() * scale;
			m_nameTextRect = QRectF(-size.width() / 2.0 + margin, margin, size.width() - 2.0 * margin,
				size.height() - 2.0 * margin);
			m_nameSpots = {QPointF(marks.center().x(), marks.bottom() + gap),
				QPointF(marks.center().x(), (pinDrawn ? markTop : marks.top()) - gap - size.height()),
				QPointF(marks.right() + gap + size.width() / 2.0, marks.center().y() - size.height() / 2.0),
				QPointF(marks.left() - gap - size.width() / 2.0, marks.center().y() - size.height() / 2.0)};
			if (pinDrawn)
				m_nameSpots.append(QPointF(marks.center().x(), highest->y() - side - gap - size.height()));
			shift = shiftTo(m_name, pivot, m_nameSpots.first());
		}
		m_nameSpotChosen = m_nameSpots.isEmpty();
		enlargeAbout(m_name, pivot, m_nameFactor, shift);
	}
}

void StationOverlayItem::chooseNameSpot(const QList<QPair<const StationOverlayItem*, QRectF>>& pictograms,
	const QList<QPair<const QGraphicsItem*, QRectF>>& tracks) {
	m_nameSpotChosen = true;
	if (m_nameSpots.isEmpty())
		return;
	const auto free = [&](const QPointF& spot) {
		const QRectF text = m_nameTextRect.translated(spot);
		for (const auto& pictogram : pictograms)
			if (pictogram.first != this && pictogram.second.intersects(text))
				return false;
		QPainterPath area;
		area.addRect(text);
		return std::none_of(tracks.cbegin(), tracks.cend(), [&](const QPair<const QGraphicsItem*, QRectF>& track) {
			return track.second.intersects(text) && track.first->collidesWithPath(track.first->mapFromScene(area));
		});
	};
	const auto spot = std::find_if(m_nameSpots.cbegin(), m_nameSpots.cend(), free);
	const QPointF pivot = m_nameAnchor * m_nameSceneScale;
	enlargeAbout(m_name, pivot, m_nameFactor, shiftTo(m_name, pivot, spot == m_nameSpots.cend() ? m_nameSpots.first() : *spot));
}

QRectF StationOverlayItem::pictogramSceneRect() const {
	if (!m_markSceneRect.isNull())
		return m_markSceneRect;
	return m_picture ? m_picture->sceneBoundingRect() : QRectF();
}

void StationOverlayItem::resolveNameCollisions(const QList<StationOverlayItem*>& stations,
	const QTransform& sceneToViewport) {
	// Overlaps are tested in device pixels. The priority order uses a fixed reference point,
	// so panning cannot reorder it.
	const QTransform scaling(sceneToViewport.m11(), sceneToViewport.m12(),
		sceneToViewport.m21(), sceneToViewport.m22(), 0.0, 0.0);
	QList<StationOverlayItem*> ordered;
	// The spots avoid every pictogram, also a hidden one: the names are not placed again when it shows. The hide test
	// takes the drawn pictograms on screen: of a mark in the near look the squares, which show, without the pin.
	QList<QPair<const StationOverlayItem*, QRectF>> scenePictograms;
	QList<QPair<const StationOverlayItem*, QRectF>> pictograms;
	for (StationOverlayItem* station : stations) {
		if (!station)
			continue;
		if (station->m_name)
			ordered.append(station);
		if (!station->m_picture)
			continue;
		scenePictograms.append({station, station->pictogramSceneRect()});
		if (station->m_picture->isVisible())
			pictograms.append({station,
				scaling.mapRect(station->m_appliedNearLook ? station->m_squaresSceneRect : scenePictograms.last().second)});
	}
	// The visible track lines with their scene bounds, collected once for the names that are placed again: a query
	// of the scene for each spot costs more than the whole test.
	QList<QPair<const QGraphicsItem*, QRectF>> tracks;
	const auto waiting = std::find_if(ordered.cbegin(), ordered.cend(),
		[](const StationOverlayItem* station) { return !station->m_nameSpotChosen; });
	if (waiting != ordered.cend() && (*waiting)->m_name->scene())
		for (const QGraphicsItem* item : (*waiting)->m_name->scene()->items())
			if (qgraphicsitem_cast<const TrackLineItem*>(item) && item->isVisible())
				tracks.append({item, item->sceneBoundingRect()});
	for (StationOverlayItem* station : std::as_const(ordered))
		if (!station->m_nameSpotChosen)
			station->chooseNameSpot(scenePictograms, tracks);
	std::stable_sort(ordered.begin(), ordered.end(),
		[](const StationOverlayItem* left, const StationOverlayItem* right) {
			return priorityLess(*left, *right, QPointF());
		});
	QList<QRectF> placed;
	for (StationOverlayItem* station : ordered) {
		const QRectF name = scaling.mapRect(station->m_name->sceneBoundingRect());
		const QRectF rect = name.adjusted(-kNameCollisionGap, -kNameCollisionGap, kNameCollisionGap, kNameCollisionGap);
		const bool keep = station->isSelected() || station->isFollowed();
		// A pictogram is never hidden, so a name gives way to the pictogram of any other
		// station that it covers, as well as to the name of a station that ranks higher.
		const bool hidden = !keep
			&& (std::any_of(placed.cbegin(), placed.cend(),
					[&rect](const QRectF& other) { return rect.intersects(other); })
				|| std::any_of(pictograms.cbegin(), pictograms.cend(),
					[&name, station](const QPair<const StationOverlayItem*, QRectF>& pictogram) {
						const QRectF shared = name.intersected(pictogram.second);
						return pictogram.first != station && shared.width() > kPictogramOverlapPixels
							&& shared.height() > kPictogramOverlapPixels;
					}));
		station->m_nameHiddenByCollision = hidden;
		if (!hidden)
			placed.append(rect);
	}
}

void StationOverlayItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
	event->ignore();
}

void StationOverlayItem::rebuildGeometry() {
	m_labelRect = labelRectForSide(LabelSide::Right);
}

QRectF StationOverlayItem::labelRectForSide(LabelSide side) const {
	const QFontMetricsF metrics(m_labelFont);
	const qreal width = metrics.horizontalAdvance(m_displayName);
	const qreal height = metrics.height();
	const qreal gap = kLabelGap;
	if (side == LabelSide::Right)
		return QRectF(m_symbolRect.right() + gap, -height / 2.0, width, height);
	if (side == LabelSide::Left)
		return QRectF(m_symbolRect.left() - gap - width, -height / 2.0, width, height);
	if (side == LabelSide::Above)
		return QRectF(-width / 2.0, m_symbolRect.top() - gap - height, width, height);
	return QRectF(-width / 2.0, m_symbolRect.bottom() + gap, width, height);
}
