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

// The property is read into a named variant: on a temporary, Qt 6.6 and later move the value out, and GCC 13 then reports
// a false array-bounds error in a release build.
SeriesMeta seriesMeta(const QAbstractSeries* series) {
	const QVariant meta = series->property(kMetaProperty);
	return meta.value<SeriesMeta>();
}

void describeAxis(QAbstractAxis* axis, const AxisMeta& meta) {
	axis->setProperty(kMetaProperty, QVariant::fromValue(meta));
}

AxisMeta axisMeta(const QAbstractAxis* axis) {
	const QVariant meta = axis->property(kMetaProperty);
	return meta.value<AxisMeta>();
}
