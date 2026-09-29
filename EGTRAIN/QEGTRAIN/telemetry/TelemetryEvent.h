#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace telemetry {
enum class Category { Usage, Diagnostics };
enum class Name { SessionStarted, SceneOpened, EditorOpened, SimulationStarted,
                  SimulationCompleted, SimulationFailed, ExportCompleted, ExportFailed,
                  OperationFailed };
enum class SceneKind { Bundled, Local };
enum class Duration { Under1s, From1sTo10s, From10sTo1m, From1mTo10m, TenMinutesOrMore };
enum class ExportKind { SceneBundle, Legacy, Csv, Png };
enum class Operation { SceneOpen, Simulation, Export };
enum class Error { InvalidInput, UnsupportedFormat, IoFailure, InternalFailure };

// The name selects exactly which of these enum properties is meaningful.
struct TelemetryEventInput {
    Name name = Name::SessionStarted;
    SceneKind sceneKind = SceneKind::Bundled;
    Duration duration = Duration::Under1s;
    ExportKind exportKind = ExportKind::SceneBundle;
    Operation operation = Operation::SceneOpen;
    Error error = Error::InvalidInput;
};
struct TelemetryEvent {
    TelemetryEventInput input;
    QString id;
    QDateTime occurredAt;
};
struct UsageRecord { TelemetryEvent event; QString installationId; };
struct DiagnosticRecord { TelemetryEvent event; int generation = -1; };
struct Application { QString version; QString platform; QString architecture; };

bool validUuid(const QString& value);
bool validUtc(const QDateTime& value);
bool validInput(const TelemetryEventInput& input, Category category);
bool validApplication(const Application& application);
TelemetryEvent createEvent(const TelemetryEventInput& input, const QDateTime& occurredAt);
QJsonObject eventObject(const TelemetryEvent& event, Category category);
bool readEvent(const QJsonObject& object, Category category, TelemetryEvent* event);
QByteArray batch(const QVector<TelemetryEvent>& events, Category category,
                 const QString& installationId, const Application& application,
                 const QDateTime& sentAt);
}
