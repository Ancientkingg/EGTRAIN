#pragma once

#include "telemetry/TelemetrySender.h"
#include <functional>
#include <optional>

class QAction;
class QDockWidget;
class QMenu;

namespace telemetry {
// Local observation only. The sender must outlive the operation and any worker join.
class OperationObservation {
public:
    OperationObservation() = default;
    explicit OperationObservation(TelemetrySender* sender) noexcept
        : sender_(sender), token_(sender ? sender->captureOperation() : TelemetrySender::OperationToken()) {}
    void sceneOpened(SceneKind kind) const noexcept;
    void editorOpened() const noexcept;
    void failure(telemetry::Operation operation, std::optional<Error> error) const noexcept;
    void exportFinished(std::optional<ExportKind> kind, bool success, bool writeAttempted,
                        std::optional<Error> error = {}) const noexcept;
    void submit(const TelemetryEventInput& input) const noexcept {
        if (sender_) sender_->tryEnqueue(input, token_);
    }
private:
    TelemetrySender* sender_ = nullptr;
    TelemetrySender::OperationToken token_;
};
using CaptureOperation = std::function<OperationObservation()>;
Duration durationBucket(qint64 elapsedMs) noexcept;

class SimulationObservation {
public:
    explicit SimulationObservation(OperationObservation operation) : operation_(operation) {}
    void begin() noexcept;
    void reject(std::optional<Error> error) noexcept;
    // Only real handled failures may use Failed. The current engine has none.
    enum class Outcome { Completed, Cancelled, Failed };
    void finish(Outcome outcome, qint64 elapsedMs, std::optional<Error> error = {}) noexcept;
private:
    OperationObservation operation_;
    bool started_ = false;
    bool terminal_ = false;
};

// A separate action observes the explicit state before show(), unlike the dock's
// built-in toggle action whose internal triggered handler runs first.
QAction* registerEditor(QDockWidget* dock, QMenu* menu, CaptureOperation capture);
void revealEditor(QDockWidget* dock, const OperationObservation& operation);
}
