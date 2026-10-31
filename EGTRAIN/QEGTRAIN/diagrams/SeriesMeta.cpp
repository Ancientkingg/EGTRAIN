#include "diagrams/SeriesMeta.h"

#include <QMetaType>
#include <QVariant>
#include <QtCharts/QAbstractAxis>
#include <QtCharts/QAbstractSeries>

Q_DECLARE_METATYPE(SeriesMeta)
Q_DECLARE_METATYPE(AxisMeta)

namespace {

// The one dynamic property of a series or an axis, which holds its whole description.
constexpr char kMetaProperty[] = "egtrainMeta";

} // namespace

void describeSeries(QAbstractSeries* series, const SeriesMeta& meta) {
	series->setProperty(kMetaProperty, QVariant::fromValue(meta));
}

SeriesMeta seriesMeta(const QAbstractSeries* series) {
	return series->property(kMetaProperty).value<SeriesMeta>();
}

void describeAxis(QAbstractAxis* axis, const AxisMeta& meta) {
	axis->setProperty(kMetaProperty, QVariant::fromValue(meta));
}

AxisMeta axisMeta(const QAbstractAxis* axis) {
	return axis->property(kMetaProperty).value<AxisMeta>();
}
