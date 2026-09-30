#include "telemetry/TelemetryOperation.h"
#include <QApplication>
#include <QAction>
#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTimer>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

using namespace telemetry;

static QJsonArray records(const QString& path) {
    QFile file(path);
    if (!file.exists()) return {};
    assert(file.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(file.readAll()).object().value("events").toArray();
}
static int count(const QJsonArray& records, const QString& name, Category category) {
    int result = 0;
    for (auto record : records) {
        const auto object = record.toObject().value("event").toObject();
        TelemetryEvent parsed;
        assert(readEvent(object, category, &parsed));
        auto privateField = object;
        privateField.insert("private", "secret");
        assert(!readEvent(privateField, category, &parsed));
        if (object.value("name").toString() == name) ++result;
    }
    return result;
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    assert(durationBucket(0) == Duration::Under1s);
    assert(durationBucket(999) == Duration::Under1s);
    assert(durationBucket(1000) == Duration::From1sTo10s);
    assert(durationBucket(9999) == Duration::From1sTo10s);
    assert(durationBucket(10000) == Duration::From10sTo1m);
    assert(durationBucket(59999) == Duration::From10sTo1m);
    assert(durationBucket(60000) == Duration::From1mTo10m);
    assert(durationBucket(599999) == Duration::From1mTo10m);
    assert(durationBucket(600000) == Duration::TenMinutesOrMore);
    QTemporaryDir directory;
    const QString settingsFile = directory.filePath("consent.ini");
    QSettings settings(settingsFile, QSettings::IniFormat);
    TelemetryContext context{true, true, true, "https://127.0.0.1:19497/collect", "1"};
    TelemetryConsent consent(settings, context);
    assert(consent.save(false, false));
    QSemaphore polled, proceed, drained, exited;
    TelemetrySender::TestOptions options;
    options.settingsFactory = [settingsFile] { return std::make_unique<QSettings>(settingsFile, QSettings::IniFormat); };
    options.monotonicMs = [] { return 0; }; // Durable observation, not transport.
    options.afterPoll = [&] {
        polled.release();
        assert(proceed.tryAcquire(1, 10000));
        QTimer::singleShot(0, [&] { drained.release(); });
    };
    options.onWorkerExit = [&] { exited.release(); };
    TelemetrySender sender(context, {"1.2.3", "macos", "arm64"}, directory.filePath("queue"), options);
    QObject::connect(&consent, &TelemetryConsent::revoked, &consent,
        [&](bool usage, bool diagnostic) { sender.invalidateConsent(usage, diagnostic); }, Qt::DirectConnection);
    assert(polled.tryAcquire(1, 3000));
    const auto cycle = [&] {
        proceed.release(); assert(drained.tryAcquire(1, 3000));
        sender.requestConsentRefresh(); assert(polled.tryAcquire(1, 3000));
    };
    const auto historical = OperationObservation(&sender);
    assert(consent.save(true, true)); cycle();
    historical.sceneOpened(SceneKind::Local);
    historical.failure(telemetry::Operation::SceneOpen, Error::InvalidInput);
    const auto operation = OperationObservation(&sender);
    operation.sceneOpened(SceneKind::Local);
    SimulationObservation completed(operation);
    completed.begin(); completed.begin();
    completed.finish(SimulationObservation::Outcome::Completed, 1000);
    completed.finish(SimulationObservation::Outcome::Failed, 1000, Error::InternalFailure);
    SimulationObservation rejected(operation);
    rejected.reject(Error::InvalidInput); rejected.reject(Error::InvalidInput); rejected.begin();
    SimulationObservation cancelled(operation);
    cancelled.begin(); cancelled.finish(SimulationObservation::Outcome::Cancelled, 999);
    cancelled.finish(SimulationObservation::Outcome::Completed, 999);
    SimulationObservation handledFailure(operation); // Typed helper only; not an engine failure boundary.
    handledFailure.begin(); handledFailure.finish(SimulationObservation::Outcome::Failed, 600000, Error::InternalFailure);
    operation.exportFinished(ExportKind::Csv, true, true);
    operation.exportFinished(ExportKind::Csv, true, true); // Distinct repeated action.
    operation.exportFinished(ExportKind::Png, false, true, Error::IoFailure);
    operation.exportFinished(ExportKind::Png, false, false, Error::InternalFailure);
    operation.exportFinished({}, true, true); // Directory/scenario have no usage kind.
    operation.failure(telemetry::Operation::Export, {}); // Unmapped cause is omitted.
    QMainWindow window;
    QMenu menu;
    auto* first = new QDockWidget("Private first", &window);
    auto* second = new QDockWidget("Private second", &window);
    first->setObjectName("firstEditor");
    second->setObjectName("secondEditor");
    window.addDockWidget(Qt::LeftDockWidgetArea, first);
    window.addDockWidget(Qt::LeftDockWidgetArea, second);
    first->hide(); second->hide();
    auto* action = registerEditor(first, &menu, [&] { return OperationObservation(&sender); });
    registerEditor(second, &menu, [&] { return OperationObservation(&sender); });
    const QByteArray hiddenLayout = window.saveState();
    window.show(); app.processEvents();
    action->trigger(); assert(!first->isHidden());
    revealEditor(first, operation); // No count for focus.
    window.tabifyDockWidget(first, second);
    revealEditor(second, operation); // One explicit hidden-to-open transition.
    first->raise(); second->raise(); app.processEvents(); // Tab switches do not count.
    window.hide(); window.show(); app.processEvents();
    const QByteArray openLayout = window.saveState();
    first->close(); assert(first->isHidden());
    action->trigger(); assert(!first->isHidden()); // Close/reopen counts once.
    assert(window.restoreState(hiddenLayout)); app.processEvents();
    assert(first->isHidden() && second->isHidden());
    assert(window.restoreState(openLayout)); app.processEvents();
    assert(!first->isHidden() && !second->isHidden()); // Restoration is not a user reveal.
    cycle();
    const auto usagePath = directory.filePath("queue/usage.json");
    const auto diagnosticPath = directory.filePath("queue/diagnostics.json");
    const auto usage = records(usagePath);
    const auto diagnostic = records(diagnosticPath);
    assert(usage.size() == 12 && diagnostic.size() == 4);
    assert(count(usage, "scene.opened", Category::Usage) == 1);
    assert(count(usage, "editor.opened", Category::Usage) == 3);
    assert(count(usage, "simulation.started", Category::Usage) == 3);
    assert(count(usage, "simulation.completed", Category::Usage) == 1);
    assert(count(usage, "simulation.failed", Category::Usage) == 1);
    assert(count(usage, "export.completed", Category::Usage) == 2);
    assert(count(usage, "export.failed", Category::Usage) == 1);
    assert(count(diagnostic, "operation.failed", Category::Diagnostics) == 4);
    // Old operations cannot rebind to either category after revoke/re-enable.
    assert(consent.save(false, false)); cycle();
    assert(consent.save(true, true)); cycle();
    operation.exportFinished(ExportKind::Csv, false, true, Error::IoFailure);
    cycle();
    assert(records(usagePath).isEmpty() && records(diagnosticPath).isEmpty());
    const auto fresh = OperationObservation(&sender);
    assert(consent.save(false, true)); cycle();
    fresh.exportFinished(ExportKind::Csv, false, true, Error::IoFailure);
    cycle();
    assert(records(usagePath).isEmpty());
    assert(records(diagnosticPath).size() == 1); // Unrelated usage revocation preserves diagnostics.
    sender.stop(); proceed.release();
    assert(exited.tryAcquire(1, 3000));
}
