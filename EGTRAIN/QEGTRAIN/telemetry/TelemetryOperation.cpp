#include "telemetry/TelemetryOperation.h"
#include <QAction>
#include <QDockWidget>
#include <QMenu>

namespace telemetry {
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
QAction* registerEditor(QDockWidget* dock, QMenu* menu, CaptureOperation capture) {
    auto* action = new QAction(dock->windowTitle(), dock);
    action->setCheckable(true);
    action->setChecked(!dock->isHidden());
    menu->addAction(action);
    QObject::connect(dock, &QDockWidget::visibilityChanged, action, [dock, action](bool) {
        action->setChecked(!dock->isHidden());
    });
    QObject::connect(action, &QAction::triggered, dock, [dock, capture](bool checked) {
        const OperationObservation operation = capture ? capture() : OperationObservation();
        if (checked) revealEditor(dock, operation);
        else dock->hide();
    });
    return action;
}
}
