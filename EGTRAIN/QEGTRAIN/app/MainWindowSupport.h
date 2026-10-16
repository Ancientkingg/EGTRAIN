#pragma once

#include "graphics/NetworkView.h"
#include <QAbstractItemModel>
#include <QComboBox>
#include <QList>
#include <QMetaObject>
#include <QScrollBar>
#include <QString>
#include <functional>

// What MainWindow.cpp and the scripted runs in tests/e2e share.

// The name of a train in the train list, to which the entry text adds a state.
constexpr int kFollowNameRole = Qt::UserRole + 1;
// Widest the status sentence about Follow gets. It shows the state of a train with a name of 25
// characters and its scheduled time, and the start of the part about Follow. Longer text is elided.
constexpr int kFollowStatusMaxWidth = 640;

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
