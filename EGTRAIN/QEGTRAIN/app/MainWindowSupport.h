#pragma once

#include "diagrams/RunExports.h"
#include "diagrams/RunResults.h"
#include "graphics/AnnotationPlacement.h"
#include "graphics/NetworkView.h"
#include "graphics/VisualPolish.h"
#include "graphics/items/SignalItem.h"
#include "scene/TrackPreview.h"
#include "util/CsvWriter.h"
#include <QAbstractItemModel>
#include <QComboBox>
#include <QList>
#include <QMetaObject>
#include <QPointF>
#include <QScrollBar>
#include <QString>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// What MainWindow.cpp and the scripted runs in tests/e2e share.

// The name of a train in the train list, to which the entry text adds a state.
constexpr int kFollowNameRole = Qt::UserRole + 1;
// Widest the status sentence about Follow gets. It shows the state of a train with a name of 25
// characters and its scheduled time, and the start of the part about Follow. Longer text is elided.
constexpr int kFollowStatusMaxWidth = 640;
// The key of the setting behind the Advanced Details switch.
constexpr const char* kAdvancedDetailsKey = "advancedDetails";

// The state of a signal head in tooltips, the inspector and the scripted runs.
inline QString signalAspectLabel(const SignalItem* signal) {
	if (signal->failed())
		return QStringLiteral("Failed");
	switch (classifySignalCue(signal->aspectCode())) {
		case SignalCueKind::Stop:
			return QStringLiteral("Stop");
		case SignalCueKind::Caution:
			return QStringLiteral("Caution");
		case SignalCueKind::Proceed:
			return QStringLiteral("Proceed");
		case SignalCueKind::Neutral:
		default:
			return QStringLiteral("Unavailable");
	}
}

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

inline std::string joinIncidentIds(const std::vector<std::string>& ids) {
	std::string result;
	for (const std::string& id : ids) {
		if (!result.empty())
			result += ";";
		result += id;
	}
	return result;
}

inline std::string terminationOutcome(bool requested, bool terminated) {
	if (!requested)
		return "not requested";
	return terminated ? "terminated at destination" : "not terminated at destination";
}

// Run summary: per-train start, end, travel time, and energy totals, plus a
// network totals row.
inline std::string buildRunSummaryCsv(const RunResults& results) {
	std::vector<std::vector<std::string>> rows;
	for (const TrainRunResult& t : results.trains) {
		rows.push_back({t.trainId,
			t.operatingCode,
			csv::formatDouble(t.performancePercent),
			csv::formatDouble(t.appliedMaximumSpeedKmh),
			csvValue(t.startSeconds),
			csvValue(t.endSeconds),
			csvValue(t.travelSeconds),
			csvValue(t.energyConsumedKWh),
			csvValue(t.energyWithRegenKWh),
			csvValue(t.substationKWh),
			csvValue(t.substationWithRegenKWh),
			joinIncidentIds(t.directIncidentIds),
			csvValue(t.firstDirectIncidentTime),
			csvValue(t.firstDirectIncidentLocation),
			terminationOutcome(t.destinationTerminationRequested, t.destinationTerminated)});
	}
	rows.push_back({"Network total",
		std::string(csv::kMissingValue),
		std::string(csv::kMissingValue),
		std::string(csv::kMissingValue),
		csvValue(results.networkStartSeconds),
		csvValue(results.networkEndSeconds),
		csvValue(results.networkTravelSeconds),
		csvValue(results.energyConsumedKWh),
		csvValue(results.energyWithRegenKWh),
		csvValue(results.substationKWh),
		csvValue(results.substationWithRegenKWh),
		std::string(csv::kMissingValue),
		std::string(csv::kMissingValue),
		std::string(csv::kMissingValue),
		std::string(csv::kMissingValue)});
	if (results.trains.empty())
		return std::string();
	return csv::makeDocument(
		{"Train", "Operating code", "Performance [%]", "Applied maximum speed [km/h]", "Start[s]", "End[s]", "Travel time[s]", "Energy consumed[kWh]",
			"Energy with regen[kWh]", "Substation[kWh]", "Substation with regen[kWh]", "Incident IDs",
			"First direct time[s]", "First direct location[m]", "Termination outcome"},
		rows);
}
