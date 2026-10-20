#include "telemetry/TelemetryOperation.h"
#include <QAction>
#include <QDockWidget>
#include <QEvent>
#include <QMenu>

namespace telemetry {
namespace {
// Keeps the check mark of an editor action at the explicit shown state of its dock. The signal
// QDockWidget::visibilityChanged is not sent for a dock that is hidden before it was ever shown, so
// the dock is watched for the events of show() and hide(), which are sent in every case.
class DockCheckSync : public QObject {
public:
	DockCheckSync(QDockWidget* dock, QAction* action) : QObject(action), m_dock(dock), m_action(action) {
		dock->installEventFilter(this);
	}
	bool eventFilter(QObject* watched, QEvent* event) override {
		if (watched == m_dock && (event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent))
			m_action->setChecked(!m_dock->isHidden());
		return false;
	}

private:
	QDockWidget* m_dock;
	QAction* m_action;
};
} // namespace

void OperationObservation::sceneOpened(SceneKind kind) const noexcept {
	TelemetryEventInput input;
	input.name = Name::SceneOpened;
	input.sceneKind = kind;
	submit(input);
}
void OperationObservation::editorOpened() const noexcept {
	TelemetryEventInput input;
	input.name = Name::EditorOpened;
	submit(input);
}
void OperationObservation::failure(telemetry::Operation operation, std::optional<Error> error) const noexcept {
	if (!error) return;
	TelemetryEventInput input;
	input.name = Name::OperationFailed;
	input.operation = operation;
	input.error = *error;
	submit(input);
}
void OperationObservation::exportFinished(std::optional<ExportKind> kind, bool success, bool writeAttempted,
	std::optional<Error> error) const noexcept {
	if (kind && (success || writeAttempted)) {
		TelemetryEventInput input;
		input.name = success ? Name::ExportCompleted : Name::ExportFailed;
		input.exportKind = *kind;
		submit(input);
	}
	if (!success) failure(telemetry::Operation::Export, error);
}
Duration durationBucket(qint64 elapsedMs) noexcept {
	if (elapsedMs < 1000) return Duration::Under1s;
	if (elapsedMs < 10000) return Duration::From1sTo10s;
	if (elapsedMs < 60000) return Duration::From10sTo1m;
	if (elapsedMs < 600000) return Duration::From1mTo10m;
	return Duration::TenMinutesOrMore;
}
void SimulationObservation::begin() noexcept {
	if (started_ || terminal_) return;
	started_ = true;
	TelemetryEventInput input;
	input.name = Name::SimulationStarted;
	operation_.submit(input);
}
void SimulationObservation::reject(std::optional<Error> error) noexcept {
	if (terminal_ || started_) return;
	terminal_ = true;
	operation_.failure(telemetry::Operation::Simulation, error);
}
void SimulationObservation::finish(Outcome outcome, qint64 elapsedMs, std::optional<Error> error) noexcept {
	if (terminal_ || !started_) return;
	terminal_ = true;
	if (outcome == Outcome::Cancelled) return;
	TelemetryEventInput input;
	input.name = outcome == Outcome::Completed ? Name::SimulationCompleted : Name::SimulationFailed;
	input.duration = durationBucket(elapsedMs);
	operation_.submit(input);
	if (outcome == Outcome::Failed) operation_.failure(telemetry::Operation::Simulation, error);
}
void revealEditor(QDockWidget* dock, const OperationObservation& operation) {
	if (!dock) return;
	const bool wasHidden = dock->isHidden();
	dock->show();
	dock->raise();
	if (wasHidden) operation.editorOpened();
}
QAction* registerEditor(QDockWidget* dock, QMenu* menu, const CaptureOperation& capture) {
	auto* action = new QAction(dock->windowTitle(), dock);
	action->setCheckable(true);
	action->setChecked(!dock->isHidden());
	menu->addAction(action);
	new DockCheckSync(dock, action);
	QObject::connect(action, &QAction::triggered, dock, [dock, capture](bool checked) {
		const OperationObservation operation = capture ? capture() : OperationObservation();
		if (checked) revealEditor(dock, operation);
		else dock->hide();
	});
	return action;
}
}
