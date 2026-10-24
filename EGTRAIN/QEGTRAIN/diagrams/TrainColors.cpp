// SceneModel has a member named signals, so its header comes before the Qt headers,
// which define that word as a macro.
#include "scene/SceneModel.h"

#include "diagrams/TrainColors.h"

#include <QBrush>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QStringList>
#include <QVariant>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QLineSeries>

#include <array>

QHash<QString, QColor> trainColorsForRun(const RunResults& run, const SceneModel& scene) {
	QHash<QString, QColor> colors;
	for (const TrainRunResult& train : run.trains) {
		int red = 0, green = 0, blue = 0;
		if (sceneParseVisualizationColor(sceneServiceVisualizationColor(scene, train.serviceId), &red, &green, &blue))
			colors.insert(QString::fromStdString(train.trainId), QColor(red, green, blue));
	}
	return colors;
}

QHash<QString, QString> trainServicesForRun(const RunResults& run) {
	QHash<QString, QString> services;
	for (const TrainRunResult& train : run.trains)
		if (!train.serviceId.empty()) services.insert(QString::fromStdString(train.trainId), QString::fromStdString(train.serviceId));
	return services;
}

namespace {
// The palette of the trains, checked for colour-blind readers in the order given.
const std::array<QColor, 6> kPalette{QColor(0x2a, 0x78, 0xd6), QColor(0xc2, 0x44, 0x8a), QColor(0x4c, 0x8a, 0x12),
	QColor(0x61, 0x27, 0xb1), QColor(0xe3, 0x5b, 0x6b), QColor(0x84, 0x2a, 0x65)};
const QColor kManyTrains(0x8a, 0x97, 0xa1);	 // every shown train while no colour can be keyed
const QColor kHiddenTrain(0xc3, 0xcb, 0xd1); // a hidden train, as its row in the Trains list draws it

constexpr qsizetype kMostKeyed = static_cast<qsizetype>(kPalette.size()); // trains or services up to which colours are keyed
} // namespace

TrainColourPlan trainColourPlan(const QVector<TrainColourInput>& trains, const QHash<QString, QColor>& serviceColours,
	const QString& selectedTrain) {
	QStringList ids;
	for (const TrainColourInput& train : trains) ids << train.id;
	const auto isCopy = [&ids](const TrainColourInput& train) { return !train.copyOf.isEmpty() && ids.contains(train.copyOf); };
	QStringList shownByCopy;
	QString selected = selectedTrain;
	for (const TrainColourInput& train : trains) {
		if (!isCopy(train)) continue;
		if (train.shown) shownByCopy << train.copyOf;
		if (train.id == selectedTrain) selected = train.copyOf;
	}
	QStringList all, shown, allServices, shownServices;
	QHash<QString, QString> serviceOf;
	for (const TrainColourInput& train : trains) {
		if (isCopy(train)) continue;
		const QString service = train.service.isEmpty() ? train.id : train.service;
		all << train.id;
		serviceOf.insert(train.id, service);
		if (!allServices.contains(service)) allServices << service;
		if (!train.shown && !shownByCopy.contains(train.id)) continue;
		shown << train.id;
		if (!shownServices.contains(service)) shownServices << service;
	}
	TrainColourPlan plan;
	for (const QString& id : all) plan.colours.insert(id, kHiddenTrain);
	// The colour of the service in the case wins over the palette.
	const auto colourOf = [&serviceColours](const QString& id, qsizetype slot) {
		const QColor own = serviceColours.value(id);
		return own.isValid() ? own : kPalette[static_cast<std::size_t>(slot)];
	};
	if (shown.size() <= kMostKeyed) {
		plan.mode = TrainColourPlan::Mode::PerTrain;
		const QStringList& order = all.size() <= kMostKeyed ? all : shown;
		for (const QString& id : shown) plan.colours.insert(id, colourOf(id, order.indexOf(id)));
	} else if (shownServices.size() <= kMostKeyed) {
		plan.mode = TrainColourPlan::Mode::PerService;
		const QStringList& order = allServices.size() <= kMostKeyed ? allServices : shownServices;
		for (const QString& id : shown) plan.colours.insert(id, colourOf(id, order.indexOf(serviceOf.value(id))));
	} else {
		plan.mode = TrainColourPlan::Mode::Grey;
		for (const QString& id : shown) plan.colours.insert(id, kManyTrains);
		if (shown.contains(selected)) plan.colours.insert(selected, colourOf(selected, 0));
	}
	// A copy takes the colour of its train, and a hidden train that only its copy showed is light grey again.
	for (const TrainColourInput& train : trains)
		if (isCopy(train)) plan.colours.insert(train.id, train.shown ? plan.colours.value(train.copyOf) : kHiddenTrain);
	for (const TrainColourInput& train : trains)
		if (!isCopy(train) && !train.shown) plan.colours.insert(train.id, kHiddenTrain);
	return plan;
}

namespace {
// The look of the blocking times, from the mock-up of the diagram windows.
const QColor kNeutral(0x5b, 0x68, 0x71);	// the fill of every blocking time and the outline of its sample in the Key
const QColor kAmber(0xe3, 0xa2, 0x1a);		// a critical block
const QColor kAmberBrown(0x8f, 0x62, 0x00); // the hatch and the outline of a critical block
const QColor kConflict(0x9e, 0x1b, 0x1b);	// a conflict, its hatch and its outline

QColor withAlpha(QColor colour, double alpha) {
	colour.setAlphaF(static_cast<float>(alpha));
	return colour;
}

// A hatch: the neutral fill of a blocking time under `fill`, crossed by lines of `line` 0.55 px wide at 45 degrees, up to
// the right, and also down to the right when `cross` is set. The tile is 7 px square at two pixels per pixel, which puts
// the lines 4.9 px apart.
QBrush hatch(const QColor& fill, const QColor& line, bool cross) {
	constexpr double ratio = 2;
	constexpr double size = 7;
	QImage tile(static_cast<int>(size * ratio), static_cast<int>(size * ratio), QImage::Format_ARGB32_Premultiplied);
	tile.setDevicePixelRatio(ratio);
	tile.fill(Qt::transparent);
	QPainter painter(&tile);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.fillRect(QRectF(0, 0, size, size), withAlpha(kNeutral, 0.10));
	painter.fillRect(QRectF(0, 0, size, size), fill);
	painter.setPen(QPen(line, 0.55));
	// Each line and its copies one tile to either side, so that the hatch runs on across the tiles.
	for (const double shift : {-size, 0.0, size}) {
		painter.drawLine(QPointF(shift, size), QPointF(shift + size, 0));
		if (cross) painter.drawLine(QPointF(shift, 0), QPointF(shift + size, size));
	}
	painter.end();
	return QBrush(tile);
}
} // namespace

void styleBlockingTime(QAreaSeries* fill, QLineSeries* outline, BlockingTimeKind kind) {
	QBrush brush(withAlpha(kNeutral, 0.10));
	QPen pen(kNeutral, 1.4);
	QPen keyPen(withAlpha(kNeutral, 0.75), 1.4);
	QString name = QStringLiteral("Blocking time");
	if (kind == BlockingTimeKind::CriticalBlock) {
		brush = hatch(withAlpha(kAmber, 0.42), withAlpha(kAmberBrown, 0.85), false);
		pen = keyPen = QPen(kAmberBrown, 1.3);
		name = QStringLiteral("Critical block");
	} else if (kind == BlockingTimeKind::Conflict) {
		brush = hatch(withAlpha(kConflict, 0.14), withAlpha(kConflict, 0.9), true);
		pen = keyPen = QPen(kConflict, 1.4);
		name = QStringLiteral("Conflict");
	}
	// The outline of an ordinary blocking time takes the colour of its train, its fill stays neutral.
	const bool ownColour = kind != BlockingTimeKind::Ordinary;
	if (fill) {
		fill->setBrush(brush);
		fill->setPen(Qt::NoPen);
		fill->setProperty("keyKind", name);
		fill->setProperty("ownColour", true);
	}
	outline->setPen(pen);
	outline->setProperty("keyKind", name);
	outline->setProperty("ownColour", ownColour);
	outline->setProperty("keyPen", QVariant::fromValue(keyPen));
}
