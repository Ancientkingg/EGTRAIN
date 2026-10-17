#pragma once

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QEvent>
#include <QObject>
#include <QWheelEvent>
#include <QWidget>

// Lets the mouse wheel change a combo box or spin box only while it has keyboard focus. Otherwise the wheel
// passes to the parent, so that it scrolls a form instead of changing a value on the way. The box takes focus
// from a click or the tab key but not from the wheel.
namespace FocusWheel {

class Filter : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override {
		auto* box = qobject_cast<QWidget*>(watched);
		if (event->type() != QEvent::Wheel || !box || box->hasFocus())
			return false;
		event->ignore();
		return true;
	}
};

inline void guard(QWidget* box) {
	box->setFocusPolicy(Qt::StrongFocus);
	// The filter has no meta object of its own, so a child search cannot tell whether the box has one.
	if (box->property("focusWheelGuard").toBool())
		return;
	box->setProperty("focusWheelGuard", true);
	box->installEventFilter(new Filter(box));
}

// Guards every combo box and spin box inside root.
inline void guardAll(QWidget* root) {
	for (auto* combo : root->findChildren<QComboBox*>())
		guard(combo);
	for (auto* spin : root->findChildren<QAbstractSpinBox*>())
		guard(spin);
}

} // namespace FocusWheel
