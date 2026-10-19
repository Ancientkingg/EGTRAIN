#pragma once

#include <QAbstractSpinBox>
#include <QChildEvent>
#include <QComboBox>
#include <QEvent>
#include <QObject>
#include <QSlider>
#include <QWidget>

/// Lets the mouse wheel change a combo box, spin box or slider only while it has keyboard focus. Otherwise the wheel
/// passes to the parent, so that it scrolls a form instead of changing a value on the way. The box takes focus
/// from a click or the tab key but not from the wheel.
namespace FocusWheel {

/// The event filter that guard installs; the box owns it.
class Filter : public QObject {
public:
	using QObject::QObject;

protected:
	/// Ignores a wheel event of a box without focus, so that it passes to the parent.
	bool eventFilter(QObject* watched, QEvent* event) override {
		auto* box = qobject_cast<QWidget*>(watched);
		if (event->type() != QEvent::Wheel || !box || box->hasFocus())
			return false;
		event->ignore();
		return true;
	}
};

/// Guards box: gives it strong focus and installs a Filter, once.
inline void guard(QWidget* box) {
	box->setFocusPolicy(Qt::StrongFocus);
	// A box that guards itself is visited again by guardAll; the property keeps it from getting a second filter.
	if (box->property("focusWheelGuard").toBool())
		return;
	box->setProperty("focusWheelGuard", true);
	box->installEventFilter(new Filter(box));
}

/// Whether the wheel changes the value of widget. A scroll bar is not one of them: it has to follow the wheel.
inline bool isValueField(const QWidget* widget) {
	return qobject_cast<const QComboBox*>(widget) || qobject_cast<const QAbstractSpinBox*>(widget) || qobject_cast<const QSlider*>(widget);
}

/// Guards every combo box, spin box and slider inside root.
inline void guardAll(QWidget* root) {
	for (auto* field : root->findChildren<QWidget*>()) {
		if (isValueField(field))
			guard(field);
	}
}

/// Keeps every field of root guarded, also the fields that a table, a tab or a later refresh adds after the call. A new
/// widget is guarded when it is polished, before it can be shown and meet the wheel.
class Watcher : public QObject {
public:
	/// Guards the fields of root and watches it and its descendants. root owns the watcher.
	explicit Watcher(QWidget* root) : QObject(root) {
		add(root);
	}

protected:
	/// Guards and watches a child widget when it is polished.
	bool eventFilter(QObject* watched, QEvent* event) override {
		if (event->type() == QEvent::ChildPolished) {
			if (auto* child = qobject_cast<QWidget*>(static_cast<QChildEvent*>(event)->child()))
				add(child);
		}
		return QObject::eventFilter(watched, event);
	}

private:
	void add(QWidget* widget) {
		if (isValueField(widget))
			guard(widget);
		if (widget->property("focusWheelWatched").toBool())
			return;
		widget->setProperty("focusWheelWatched", true);
		widget->installEventFilter(this);
		for (auto* child : widget->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
			add(child);
	}
};

/// Creates a Watcher for root, which owns it.
inline void watch(QWidget* root) {
	new Watcher(root);
}

} // namespace FocusWheel
