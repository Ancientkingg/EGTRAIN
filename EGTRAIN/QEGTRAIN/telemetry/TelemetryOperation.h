#pragma once

#include "telemetry/TelemetrySender.h"
#include <functional>
#include <optional>
#include <utility>

class QAction;
class QDockWidget;
class QMenu;

namespace telemetry {
/// The telemetry authorization of one user operation, captured when the operation starts, and the
/// events it may submit. It only reports: submitting never waits, fails or changes the operation.
/// A default-constructed observation submits nothing. The sender must outlive the operation and any
/// worker join.
class OperationObservation {
public:
	OperationObservation() = default;
	/// Captures the authorization of sender now. sender is not owned and may be null.
	explicit OperationObservation(TelemetrySender* sender) noexcept
		: sender_(sender), token_(sender ? sender->captureOperation() : TelemetrySender::OperationToken()) {}
	/// A scene was opened and installed for use.
	void sceneOpened(SceneKind kind) const noexcept;
	/// A hidden or closed editor dock was opened by the user.
	void editorOpened() const noexcept;
	/// A handled failure of operation. Submits operation.failed with error, or nothing when no error
	/// code applies.
	void failure(telemetry::Operation operation, std::optional<Error> error) const noexcept;
	/// The end of an export. Submits export.completed on success and export.failed when a write was
	/// attempted and failed, or no usage event when kind is empty. A failure also goes to failure()
	/// with error.
	void exportFinished(std::optional<ExportKind> kind, bool success, bool writeAttempted,
		std::optional<Error> error = {}) const noexcept;
	/// Hands input to the sender with the captured authorization. It is dropped when the category was
	/// not authorized at capture, consent changed since, or the sender is busy or full.
	void submit(const TelemetryEventInput& input) const noexcept {
		if (sender_) sender_->tryEnqueue(input, token_);
	}
private:
	TelemetrySender* sender_ = nullptr;
	TelemetrySender::OperationToken token_;
};
/// Captures a new OperationObservation when an operation starts. It may be empty.
using CaptureOperation = std::function<OperationObservation()>;
/// The bucket of a run duration given in milliseconds.
Duration durationBucket(qint64 elapsedMs) noexcept;

/// The usage events of one simulation run: simulation.started, then at most one completion or
/// failure. A run rejected before it starts yields only a diagnostic.
class SimulationObservation {
public:
	/// operation is the observation captured when the run was requested.
	explicit SimulationObservation(OperationObservation operation) : operation_(std::move(operation)) {}
	/// The run has started, past the engine's zero-train guard. Submits simulation.started once;
	/// ignored after reject() or finish().
	void begin() noexcept;
	/// The run was rejected before it started. Submits operation.failed for the simulation when error
	/// has a value; ignored after begin().
	void reject(std::optional<Error> error) noexcept;
	/// How a started run ended. Only real handled failures may use Failed: a run that an exception ended.
	enum class Outcome { Completed, ///< Normal return after postprocessing.
		Cancelled,					///< Stopped by the user; no event is submitted.
		Failed						///< A handled failure after the start.
	};
	/// The started run ended. elapsedMs is the monotonic time in milliseconds from the start to the
	/// end. Submits simulation.completed or simulation.failed with its duration bucket, and for
	/// Failed also operation.failed with error. Only the first call after begin() counts.
	void finish(Outcome outcome, qint64 elapsedMs, std::optional<Error> error = {}) noexcept;
private:
	OperationObservation operation_;
	bool started_ = false;
	bool terminal_ = false;
};

/// Adds a checkable action for dock to menu, owned by dock, whose check mark follows the shown state
/// of the dock. Checking it reveals the dock with revealEditor() and an observation from capture;
/// unchecking hides it, except for a dock open behind another tab, which it reveals. A separate
/// action observes the explicit state before show(), unlike the dock's built-in toggle action whose
/// internal triggered handler runs first.
QAction* registerEditor(QDockWidget* dock, QMenu* menu, const CaptureOperation& capture);
/// Shows and raises dock, and submits editor.opened when it was hidden. A null dock is ignored.
void revealEditor(QDockWidget* dock, const OperationObservation& operation);
}
