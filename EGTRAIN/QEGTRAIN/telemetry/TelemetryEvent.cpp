#include "telemetry/TelemetryEvent.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>

namespace telemetry {
namespace {
const char* const names[] = {"session.started", "scene.opened", "editor.opened", "simulation.started",
                             "simulation.completed", "simulation.failed", "export.completed", "export.failed",
                             "operation.failed"};
const char* const scenes[] = {"bundled", "local"};
const char* const durations[] = {"under_1s", "1s_to_10s", "10s_to_1m", "1m_to_10m", "10m_or_more"};
const char* const exports[] = {"scene_bundle", "legacy", "csv", "png"};
const char* const operations[] = {"scene_open", "simulation", "export"};
const char* const errors[] = {"invalid_input", "unsupported_format", "io_failure", "internal_failure"};
template <typename T, size_t N> bool inRange(T value, const char* const (&)[N]) {
    return static_cast<int>(value) >= 0 && static_cast<int>(value) < static_cast<int>(N);
}
template <typename T, size_t N> bool parse(const QString& text, const char* const (&values)[N], T* result) {
    for (size_t i = 0; i < N; ++i) if (text == QLatin1String(values[i])) {
        *result = static_cast<T>(i); return true;
    }
    return false;
}
template <typename T, size_t N> QString text(T value, const char* const (&values)[N]) {
    return inRange(value, values) ? QString::fromLatin1(values[static_cast<int>(value)]) : QString();
}
bool utcText(const QString& value) {
    static const QRegularExpression format(QStringLiteral("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\\.[0-9]{1,9})?Z\\z"));
    if (value.size() < 20 || value.size() > 30 || !format.match(value).hasMatch()) return false;
    const auto parsed = QDateTime::fromString(value.left(19) + QStringLiteral("Z"), Qt::ISODate);
    return parsed.isValid() && parsed.timeSpec() == Qt::UTC;
}
QString timestamp(const QDateTime& date) { return date.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzzZ")); }
bool keys(const QJsonObject& obj, std::initializer_list<const char*> expected) {
    if (obj.size() != static_cast<int>(expected.size())) return false;
    for (const char* key : expected) if (!obj.contains(QLatin1String(key))) return false;
    return true;
}
}
bool validUuid(const QString& value) {
    static const QRegularExpression format(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}\\z"));
    return value.size() == 36 && format.match(value).hasMatch();
}
bool validUtc(const QDateTime& value) { return value.isValid() && value.timeSpec() == Qt::UTC && utcText(timestamp(value)); }
bool validInput(const TelemetryEventInput& input, Category category) {
    if (!inRange(input.name, names) || !inRange(input.sceneKind, scenes)
        || !inRange(input.duration, durations) || !inRange(input.exportKind, exports)
        || !inRange(input.operation, operations) || !inRange(input.error, errors)) return false;
    if (category == Category::Usage ? input.name == Name::OperationFailed : input.name != Name::OperationFailed) return false;
    const TelemetryEventInput defaults;
    return (input.name == Name::SceneOpened || input.sceneKind == defaults.sceneKind)
        && (input.name == Name::SimulationCompleted || input.name == Name::SimulationFailed || input.duration == defaults.duration)
        && (input.name == Name::ExportCompleted || input.name == Name::ExportFailed || input.exportKind == defaults.exportKind)
        && (input.name == Name::OperationFailed || (input.operation == defaults.operation && input.error == defaults.error));
}
bool validApplication(const Application& app) {
    static const QRegularExpression version(QStringLiteral("^(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})\\z"));
    return app.version.size() >= 5 && app.version.size() <= 29 && version.match(app.version).hasMatch()
        && (app.platform == QStringLiteral("macos") || app.platform == QStringLiteral("windows") || app.platform == QStringLiteral("linux"))
        && (app.architecture == QStringLiteral("arm64") || app.architecture == QStringLiteral("x86_64"));
}
TelemetryEvent createEvent(const TelemetryEventInput& input, const QDateTime& occurredAt) {
    return {input, QUuid::createUuid().toString(QUuid::WithoutBraces).toLower(), occurredAt};
}
QJsonObject eventObject(const TelemetryEvent& event, Category category) {
    if (!validInput(event.input, category) || !validUuid(event.id) || !validUtc(event.occurredAt)) return {};
    QJsonObject properties;
    switch (event.input.name) {
    case Name::SceneOpened: properties.insert(QStringLiteral("scene_kind"), text(event.input.sceneKind, scenes)); break;
    case Name::SimulationCompleted: case Name::SimulationFailed:
        properties.insert(QStringLiteral("duration_bucket"), text(event.input.duration, durations)); break;
    case Name::ExportCompleted: case Name::ExportFailed:
        properties.insert(QStringLiteral("export_kind"), text(event.input.exportKind, exports)); break;
    case Name::OperationFailed:
        properties.insert(QStringLiteral("operation_code"), text(event.input.operation, operations));
        properties.insert(QStringLiteral("error_code"), text(event.input.error, errors)); break;
    default: break;
    }
    return {{QStringLiteral("event_id"), event.id}, {QStringLiteral("occurred_at"), timestamp(event.occurredAt)},
            {QStringLiteral("name"), text(event.input.name, names)}, {QStringLiteral("properties"), properties}};
}
bool readEvent(const QJsonObject& object, Category category, TelemetryEvent* output) {
    if (!output || !keys(object, {"event_id", "occurred_at", "name", "properties"}) ||
        !object.value(QStringLiteral("properties")).isObject()) return false;
    TelemetryEvent event;
    const QString at = object.value(QStringLiteral("occurred_at")).toString();
    if (!validUuid(object.value(QStringLiteral("event_id")).toString()) || !utcText(at)
        || !parse(object.value(QStringLiteral("name")).toString(), names, &event.input.name)) return false;
    event.id = object.value(QStringLiteral("event_id")).toString();
    event.occurredAt = QDateTime::fromString(at, Qt::ISODateWithMs);
    if (!validUtc(event.occurredAt)) return false;
    const auto props = object.value(QStringLiteral("properties")).toObject();
    switch (event.input.name) {
    case Name::SceneOpened:
        if (!keys(props, {"scene_kind"}) || !parse(props.value(QStringLiteral("scene_kind")).toString(), scenes, &event.input.sceneKind)) return false;
        break;
    case Name::SimulationCompleted: case Name::SimulationFailed:
        if (!keys(props, {"duration_bucket"}) || !parse(props.value(QStringLiteral("duration_bucket")).toString(), durations, &event.input.duration)) return false;
        break;
    case Name::ExportCompleted: case Name::ExportFailed:
        if (!keys(props, {"export_kind"}) || !parse(props.value(QStringLiteral("export_kind")).toString(), exports, &event.input.exportKind)) return false;
        break;
    case Name::OperationFailed:
        if (!keys(props, {"operation_code", "error_code"})
            || !parse(props.value(QStringLiteral("operation_code")).toString(), operations, &event.input.operation)
            || !parse(props.value(QStringLiteral("error_code")).toString(), errors, &event.input.error)) return false;
        break;
    default: if (!props.isEmpty()) return false; break;
    }
    if (!validInput(event.input, category)) return false;
    *output = event;
    return true;
}
QByteArray batch(const QVector<TelemetryEvent>& events, Category category, const QString& id,
                 const Application& app, const QDateTime& sentAt) {
    if (events.isEmpty() || events.size() > 50 || !validApplication(app) || !validUtc(sentAt)
        || (category == Category::Usage ? !validUuid(id) : !id.isEmpty())) return {};
    QJsonArray entries;
    for (const auto& event : events) {
        const auto obj = eventObject(event, category);
        if (obj.isEmpty()) return {};
        entries.append(obj);
    }
    QJsonObject root{{QStringLiteral("schema_version"), 1},
                     {QStringLiteral("category"), category == Category::Usage ? QStringLiteral("usage") : QStringLiteral("diagnostics")},
                     {QStringLiteral("sent_at"), timestamp(sentAt)},
                     {QStringLiteral("application"),
                      QJsonObject{{QStringLiteral("name"), QStringLiteral("EGTRAIN")},
                                  {QStringLiteral("version"), app.version},
                                  {QStringLiteral("platform"), app.platform},
                                  {QStringLiteral("architecture"), app.architecture}}},
                     {QStringLiteral("events"), entries}};
    if (category == Category::Usage) root.insert(QStringLiteral("installation_id"), id);
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return bytes.size() <= 65536 ? bytes : QByteArray();
}
}
