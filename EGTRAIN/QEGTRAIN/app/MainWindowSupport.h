#pragma once

#include "graphics/AnnotationPlacement.h"
#include "graphics/NetworkView.h"
#include "scene/TrackPreview.h"
#include <QAbstractItemModel>
#include <QComboBox>
#include <QList>
#include <QMetaObject>
#include <QPointF>
#include <QScrollBar>
#include <QString>
#include <cstdio>
#include <functional>
#include <string>

// What MainWindow.cpp and the scripted runs in tests/e2e share.

// The name of a train in the train list, to which the entry text adds a state.
constexpr int kFollowNameRole = Qt::UserRole + 1;
// Widest the status sentence about Follow gets. It shows the state of a train with a name of 25
// characters and its scheduled time, and the start of the part about Follow. Longer text is elided.
constexpr int kFollowStatusMaxWidth = 640;
// The kind of scene item that a row of the loaded data tree stands for.
constexpr int kLoadedDataTargetTypeRole = Qt::UserRole;
// The key of the setting behind the Advanced Details switch.
constexpr const char* kAdvancedDetailsKey = "advancedDetails";

// Seconds since midnight of an "hh:mm:ss" base time; 0 when the text is empty or not a time of day.
inline int baseTimeToSeconds(const std::string& hhmmss) {
	if (hhmmss.empty())
		return 0;
	int h = 0, m = 0, s = 0;
	char extra = 0;
	if (std::sscanf(hhmmss.c_str(), "%d:%d:%d%c", &h, &m, &s, &extra) != 3)
		return 0;
	if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59)
		return 0;
	return h * 3600 + m * 60 + s;
}

// The speed slider reads left-to-right as slow-to-fast; the worker wants a
// per-step delay, so the delay is the distance from the fast end.
constexpr int kMaxStepDelayMs = 500;
inline int stepDelayForSlider(int sliderValue) {
	return kMaxStepDelayMs - sliderValue;
}

// What the train list reports while 'frames' shows frames that change no state. Every row gets a
// marker text first. The list is left alone only if it reports no change and each marker is still
// there, so a row that is written again with the text it already has is noticed too.
struct FollowListUnderFrames {
	int changes = 0;
	bool rowsKept = true;
};

inline FollowListUnderFrames showFramesOverMarkedList(QComboBox* list, const std::function<void()>& frames) {
	const QString marker = QStringLiteral("not written");
	for (int row = 0; row < list->count(); ++row)
		list->setItemText(row, marker);
	FollowListUnderFrames result;
	const auto countChange = [&result]() { ++result.changes; };
	const QAbstractItemModel* model = list->model();
	const QList<QMetaObject::Connection> counters{
		QObject::connect(model, &QAbstractItemModel::dataChanged, list, countChange),
		QObject::connect(model, &QAbstractItemModel::rowsInserted, list, countChange),
		QObject::connect(model, &QAbstractItemModel::rowsRemoved, list, countChange),
		QObject::connect(model, &QAbstractItemModel::modelReset, list, countChange)};
	frames();
	for (const QMetaObject::Connection& counter : counters)
		QObject::disconnect(counter);
	for (int row = 0; row < list->count(); ++row)
		result.rowsKept = result.rowsKept && list->itemText(row) == marker;
	return result;
}

// Pans the view by a third of its width, as a drag of the user does, to the side that has room.
inline void panViewAway(NetworkView* view) {
	QScrollBar* bar = view->horizontalScrollBar();
	const int distance = view->viewport()->width() / 3;
	bar->setValue(bar->value() + distance <= bar->maximum() ? bar->value() + distance : bar->value() - distance);
}

inline bool previewPointAtX(const TrackPreviewLine& line, double x, qreal offset, QPointF& point) {
	TrackPreviewPoint candidate;
	if (!trackPreviewPointAtX(line, x, candidate))
		return false;
	point = QPointF(candidate.x, candidate.y + offset);
	return true;
}

inline QPointF stationDecorationOffset(const std::vector<SceneStationView>& views,
	const std::string& stationId, const std::string& name) {
	const auto shift = annotationStationShift(views, stationId);
	return QPointF(shift.first, shift.second + stationNamedYOffset(name));
}

inline bool previewSignalNormal(const TrackPreviewLine& line, double rawX, QPointF& normal) {
	std::pair<double, double> direction;
	if (!annotationSignalNormal(line, rawX, direction))
		return false;
	normal = QPointF(direction.first, direction.second);
	return true;
}
