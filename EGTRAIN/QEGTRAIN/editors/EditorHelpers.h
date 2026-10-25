#pragma once

#include "editors/SceneDocument.h"
#include "scene/SceneDiagnostic.h"
#include "widgets/CompactDoubleSpinBox.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

/// Whether the field or a widget inside it has the focus. A number field of a panel takes the text that is typed in it
/// only then, so that committing the pending editors keeps a value that the user did not edit.
inline bool hasEditorFocus(const QWidget* editor) {
	const QWidget* focus = QApplication::focusWidget();
	return editor && (editor->hasFocus() || (focus && editor->isAncestorOf(focus)));
}

/// The value that a number field shows for a stored value. A stored value outside the range is shown at the nearest
/// limit, and committing it from an untouched field is not an edit.
inline double shownValue(const QDoubleSpinBox* edit, double stored) {
	return std::clamp(stored, edit->minimum(), edit->maximum());
}

/// A field of a number of 0 or more, such as a time in seconds, that keeps every digit of a stored value. Its value
/// changes when editing finishes, not for each key.
inline CompactDoubleSpinBox* exactNumberEdit(const char* objectName, QWidget* parent) {
	auto* edit = new CompactDoubleSpinBox(parent);
	edit->setObjectName(objectName);
	edit->setRange(0.0, std::numeric_limits<double>::max());
	edit->setDecimals(std::numeric_limits<double>::max_digits10);
	edit->setKeyboardTracking(false);
	return edit;
}

/// The first error among the diagnostics, else the first diagnostic, else null.
inline const SceneDiagnostic* firstDiagnostic(const std::vector<SceneDiagnostic>& diagnostics) {
	const auto error = std::find_if(diagnostics.begin(), diagnostics.end(),
		[](const SceneDiagnostic& diagnostic) { return diagnostic.severity == SceneSeverity::Error; });
	if (error != diagnostics.end())
		return &*error;
	return diagnostics.empty() ? nullptr : &diagnostics.front();
}

/// The message and the fix of firstDiagnostic, for a dialog; empty without a diagnostic.
inline QString firstDiagnosticDialogText(const std::vector<SceneDiagnostic>& diagnostics) {
	const SceneDiagnostic* diagnostic = firstDiagnostic(diagnostics);
	return diagnostic ? QString::fromStdString(toDialogText(*diagnostic)) : QString();
}

/// What a generated service is called in lists: its operating code, or the service id when it has none, its route and
/// its scheduled entry in seconds from the base time, such as "1723 (route r1, +600 s)". `occurrence` counts from 1.
inline QString generatedServiceLabel(const SceneService& service, int occurrence) {
	const std::string code = sceneServiceOccurrenceOperatingCode(service, occurrence);
	const double entry = sceneServiceScheduledEntry(service, occurrence);
	const QString entryText = std::isfinite(entry)
		? QString("+%1 s").arg(QString::number(entry, 'g', 12))
		: QStringLiteral("no entry time");
	return QString("%1 (route %2, %3)")
		.arg(QString::fromStdString(code.empty() ? service.id : code), QString::fromStdString(service.route), entryText);
}
