#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

/// Usage statistics and diagnostic reports: the events, their local queue and the sender. The wire
/// format, the event vocabulary and the HTTP outcomes are in docs/telemetry/README.md, and the batch
/// schema is docs/telemetry/batch-v1.schema.json.
namespace telemetry {
/// The two categories, which are consented to, queued and sent separately.
enum class Category { Usage, ///< Usage statistics; a batch carries the installation ID.
	Diagnostics				 ///< Diagnostic reports; a batch carries no identifier.
};
/// Event names. The wire name is given for each.
enum class Name { SessionStarted, ///< session.started (usage).
	SceneOpened,				  ///< scene.opened (usage), with sceneKind.
	EditorOpened,				  ///< editor.opened (usage).
	SimulationStarted,			  ///< simulation.started (usage).
	SimulationCompleted,		  ///< simulation.completed (usage), with duration.
	SimulationFailed,			  ///< simulation.failed (usage), with duration.
	ExportCompleted,			  ///< export.completed (usage), with exportKind.
	ExportFailed,				  ///< export.failed (usage), with exportKind.
	OperationFailed				  ///< operation.failed (diagnostics), with operation and error.
};
/// How an opened scene is stored.
enum class SceneKind { Bundled, ///< A .egscene bundle file (bundled).
	Local						///< A scene directory (local).
};
/// Elapsed time of a run from its start to its end, in buckets.
enum class Duration { Under1s, ///< Under 1 s (under_1s).
	From1sTo10s,			   ///< 1 s to under 10 s (1s_to_10s).
	From10sTo1m,			   ///< 10 s to under 60 s (10s_to_1m).
	From1mTo10m,			   ///< 60 s to under 600 s (1m_to_10m).
	TenMinutesOrMore		   ///< 600 s or more (10m_or_more).
};
/// What an export writes.
enum class ExportKind { SceneBundle, ///< A .egscene bundle from Save or Save As (scene_bundle).
	Legacy,							 ///< Legacy input files (legacy). No interactive export uses it.
	Csv,							 ///< A CSV file with its provenance sidecar (csv).
	Png								 ///< A PNG image with its provenance sidecar (png).
};
/// The operation that failed in an operation.failed event.
enum class Operation { SceneOpen, ///< Opening a scene (scene_open).
	Simulation,					  ///< Preparing or running a simulation (simulation).
	Export						  ///< Writing an export (export).
};
/// Fixed error codes of an operation.failed event. No raw error text is ever sent.
enum class Error { InvalidInput, ///< Invalid content (invalid_input).
	UnsupportedFormat,			 ///< Unsupported file type or version (unsupported_format).
	IoFailure,					 ///< A file operation failed (io_failure).
	InternalFailure				 ///< Any other handled failure (internal_failure).
};

/// What a producer reports. The name selects exactly which of these enum properties is meaningful;
/// the others must keep their default values, or validInput() rejects the input.
struct TelemetryEventInput {
	Name name = Name::SessionStarted;				 ///< The event, which also sets its category.
	SceneKind sceneKind = SceneKind::Bundled;		 ///< Used by SceneOpened.
	Duration duration = Duration::Under1s;			 ///< Used by SimulationCompleted and SimulationFailed.
	ExportKind exportKind = ExportKind::SceneBundle; ///< Used by ExportCompleted and ExportFailed.
	Operation operation = Operation::SceneOpen;		 ///< Used by OperationFailed.
	Error error = Error::InvalidInput;				 ///< Used by OperationFailed.
};
/// One occurrence of an event.
struct TelemetryEvent {
	TelemetryEventInput input; ///< Name and properties.
	QString id;				   ///< Random lowercase UUIDv4, kept unchanged on every retry.
	QDateTime occurredAt;	   ///< Time of the occurrence, in UTC.
};
/// A usage event with the installation ID it is sent under.
struct UsageRecord {
	TelemetryEvent event;	///< A usage event.
	QString installationId; ///< Installation UUID that was enabled when the event was taken in.
};
/// A diagnostic event with the diagnostics consent generation it was taken in under.
struct DiagnosticRecord {
	TelemetryEvent event; ///< An operation.failed event.
	/// Consent generation (see TelemetryConsent::DiagnosticsObservation), stored but never sent.
	/// -1 is not a valid generation.
	int generation = -1;
};
/// The application fields of a batch.
struct Application {
	QString version;	  ///< Three dot-separated decimal numbers, such as 1.4.0.
	QString platform;	  ///< macos, windows or linux.
	QString architecture; ///< arm64 or x86_64.
};

/// True for a lowercase random UUIDv4 with the RFC variant bits.
bool validUuid(const QString& value);
/// True for a valid time in UTC whose wire timestamp is valid.
bool validUtc(const QDateTime& value);
/// True when the name belongs to the category, every value is in range and the properties that the
/// name does not use keep their defaults.
bool validInput(const TelemetryEventInput& input, Category category);
/// True when version, platform and architecture have the forms that the contract allows.
bool validApplication(const Application& application);
/// An event with a new random ID. occurredAt is taken as given.
TelemetryEvent createEvent(const TelemetryEventInput& input, const QDateTime& occurredAt);
/// The wire object of the event, with the time in UTC to the millisecond; an empty object when the
/// event is not valid for the category.
QJsonObject eventObject(const TelemetryEvent& event, Category category);
/// Reads a wire event object that has exactly the fields of the contract. Returns false and leaves
/// *event unchanged when the object is not valid for the category or event is null.
bool readEvent(const QJsonObject& object, Category category, TelemetryEvent* event);
/// The compact UTF-8 JSON body of one batch of 1 to 50 events of one category, prepared at sentAt.
/// installationId is required for usage and must be empty for diagnostics. Returns an empty array
/// when an argument is not valid or the body would exceed 65,536 bytes.
QByteArray batch(const QVector<TelemetryEvent>& events, Category category,
	const QString& installationId, const Application& application,
	const QDateTime& sentAt);
}
