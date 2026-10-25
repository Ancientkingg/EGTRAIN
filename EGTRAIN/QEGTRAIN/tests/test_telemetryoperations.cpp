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
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <atomic>

using namespace telemetry;

// The events stored in path into result, none when the file does not exist; false when it cannot be opened.
static bool records(const QString& path, QJsonArray& result) {
	result = {};
	QFile file(path);
	if (!file.exists()) return true;
	if (!file.open(QIODevice::ReadOnly)) return false;
	result = QJsonDocument::fromJson(file.readAll()).object().value("events").toArray();
	return true;
}
// The number of records with this name, or -1 when a record cannot be read or is still read with a private field.
static int count(const QJsonArray& records, const QString& name, Category category) {
	int result = 0;
	for (auto record : records) {
		const auto object = record.toObject().value("event").toObject();
		TelemetryEvent parsed;
		if (!readEvent(object, category, &parsed)) return -1;
		auto privateField = object;
		privateField.insert("private", "secret");
		if (readEvent(privateField, category, &parsed)) return -1;
		if (object.value("name").toString() == name) ++result;
	}
	return result;
}

class TestTelemetryOperations : public QObject {
	Q_OBJECT

private slots:
	void editorMenuState();
	void durationBuckets_data();
	void durationBuckets();
	void operationsReachTheQueue();
};

// The check mark of an editor action stands for an editor that is open: a dock that is docked, floating or behind
// another tab, and not one that is hidden or closed, whichever way it got there.
void TestTelemetryOperations::editorMenuState() {
	QMainWindow window;
	QMenu menu;
	QDockWidget* docks[3];
	QAction* actions[3];
	for (int i = 0; i < 3; ++i) {
		docks[i] = new QDockWidget(QString("Editor %1").arg(i), &window);
		docks[i]->setObjectName(QString("editor%1").arg(i));
		docks[i]->setWidget(new QWidget);
		window.addDockWidget(Qt::RightDockWidgetArea, docks[i]);
		actions[i] = registerEditor(docks[i], &menu, {});
	}
	const auto settle = [] { QCoreApplication::processEvents(); };
	const auto checks = [&](bool first, bool second, bool third) {
		settle();
		QCOMPARE(actions[0]->isChecked(), first);
		QCOMPARE(actions[1]->isChecked(), second);
		QCOMPARE(actions[2]->isChecked(), third);
		QCOMPARE(!docks[0]->isHidden(), first);
		QCOMPARE(!docks[1]->isHidden(), second);
		QCOMPARE(!docks[2]->isHidden(), third);
	};
// A failed comparison returns from checks only, so the slot ends here after one.
#define CHECKS(first, second, third)            \
	do {                                        \
		checks(first, second, third);           \
		if (QTest::currentTestFailed()) return; \
	} while (false)
	CHECKS(true, true, true);
	// Hidden before the window was ever shown, as the editors that start closed are.
	docks[1]->hide();
	CHECKS(true, false, true);
	window.tabifyDockWidget(docks[0], docks[2]);
	window.show();
	CHECKS(true, false, true);
	const QByteArray hiddenLayout = window.saveState();
	docks[1]->show();
	CHECKS(true, true, true);
	docks[1]->setFloating(true);
	CHECKS(true, true, true);
	docks[1]->close();
	CHECKS(true, false, true);
	docks[1]->show();
	CHECKS(true, true, true);
	// Both docks of a tabbed pair are open, whichever tab is in front.
	for (QTabBar* tabs : window.findChildren<QTabBar*>()) {
		for (int tab = 0; tab < tabs->count(); ++tab) {
			tabs->setCurrentIndex(tab);
			CHECKS(true, true, true);
		}
	}
	docks[2]->hide();
	CHECKS(true, true, false);
	docks[2]->show();
	CHECKS(true, true, true);
	docks[1]->setFloating(false);
	QVERIFY(window.restoreState(hiddenLayout));
	CHECKS(true, false, true);
	docks[0]->hide();
	docks[2]->hide();
	CHECKS(false, false, false);
#undef CHECKS
}

void TestTelemetryOperations::durationBuckets_data() {
	QTest::addColumn<qint64>("milliseconds");
	QTest::addColumn<int>("bucket");
	QTest::newRow("0 ms") << qint64(0) << int(Duration::Under1s);
	QTest::newRow("999 ms") << qint64(999) << int(Duration::Under1s);
	QTest::newRow("1000 ms") << qint64(1000) << int(Duration::From1sTo10s);
	QTest::newRow("9999 ms") << qint64(9999) << int(Duration::From1sTo10s);
	QTest::newRow("10000 ms") << qint64(10000) << int(Duration::From10sTo1m);
	QTest::newRow("59999 ms") << qint64(59999) << int(Duration::From10sTo1m);
	QTest::newRow("60000 ms") << qint64(60000) << int(Duration::From1mTo10m);
	QTest::newRow("599999 ms") << qint64(599999) << int(Duration::From1mTo10m);
	QTest::newRow("600000 ms") << qint64(600000) << int(Duration::TenMinutesOrMore);
}

void TestTelemetryOperations::durationBuckets() {
	QFETCH(qint64, milliseconds);
	QFETCH(int, bucket);
	QCOMPARE(int(durationBucket(milliseconds)), bucket);
}

void TestTelemetryOperations::operationsReachTheQueue() {
	QTemporaryDir directory;
	const QString settingsFile = directory.filePath("consent.ini");
	QSettings settings(settingsFile, QSettings::IniFormat);
	TelemetryContext context{true, true, true, "https://127.0.0.1:19497/collect", "1"};
	TelemetryConsent consent(settings, context);
	QVERIFY(consent.save(false, false));
	QSemaphore polled, proceed, drained, exited;
	// The worker cannot fail the slot, so it records a wait that timed out for the end of the slot.
	std::atomic<bool> proceedTimedOut{false};
	TelemetrySender::TestOptions options;
	options.settingsFactory = [settingsFile] { return std::make_unique<QSettings>(settingsFile, QSettings::IniFormat); };
	options.monotonicMs = [] { return 0; }; // Durable observation, not transport.
	options.afterPoll = [&] {
		polled.release();
		if (!proceed.tryAcquire(1, 10000)) proceedTimedOut = true;
		QTimer::singleShot(0, [&] { drained.release(); });
	};
	options.onWorkerExit = [&] { exited.release(); };
	TelemetrySender sender(context, {"1.2.3", "macos", "arm64"}, directory.filePath("queue"), options);
	QObject::connect(
		&consent, &TelemetryConsent::revoked, &consent,
		[&](bool usage, bool diagnostic) { sender.invalidateConsent(usage, diagnostic); }, Qt::DirectConnection);
	QVERIFY(polled.tryAcquire(1, 3000));
	const auto cycle = [&] {
		proceed.release();
		if (!drained.tryAcquire(1, 3000)) return false;
		sender.requestConsentRefresh();
		return polled.tryAcquire(1, 3000);
	};
	const auto historical = OperationObservation(&sender);
	QVERIFY(consent.save(true, true));
	QVERIFY(cycle());
	historical.sceneOpened(SceneKind::Local);
	historical.failure(telemetry::Operation::SceneOpen, Error::InvalidInput);
	const auto operation = OperationObservation(&sender);
	operation.sceneOpened(SceneKind::Local);
	SimulationObservation completed(operation);
	completed.begin();
	completed.begin();
	completed.finish(SimulationObservation::Outcome::Completed, 1000);
	completed.finish(SimulationObservation::Outcome::Failed, 1000, Error::InternalFailure);
	SimulationObservation rejected(operation);
	rejected.reject(Error::InvalidInput);
	rejected.reject(Error::InvalidInput);
	rejected.begin();
	SimulationObservation cancelled(operation);
	cancelled.begin();
	cancelled.finish(SimulationObservation::Outcome::Cancelled, 999);
	cancelled.finish(SimulationObservation::Outcome::Completed, 999);
	SimulationObservation handledFailure(operation); // Typed helper only; not an engine failure boundary.
	handledFailure.begin();
	handledFailure.finish(SimulationObservation::Outcome::Failed, 600000, Error::InternalFailure);
	operation.exportFinished(ExportKind::Csv, true, true);
	operation.exportFinished(ExportKind::Csv, true, true); // Distinct repeated action.
	operation.exportFinished(ExportKind::Png, false, true, Error::IoFailure);
	operation.exportFinished(ExportKind::Png, false, false, Error::InternalFailure);
	operation.exportFinished({}, true, true);			 // Directory/scenario have no usage kind.
	operation.failure(telemetry::Operation::Export, {}); // Unmapped cause is omitted.
	QMainWindow window;
	QMenu menu;
	auto* first = new QDockWidget("Private first", &window);
	auto* second = new QDockWidget("Private second", &window);
	first->setObjectName("firstEditor");
	second->setObjectName("secondEditor");
	window.addDockWidget(Qt::LeftDockWidgetArea, first);
	window.addDockWidget(Qt::LeftDockWidgetArea, second);
	first->hide();
	second->hide();
	auto* action = registerEditor(first, &menu, [&] { return OperationObservation(&sender); });
	registerEditor(second, &menu, [&] { return OperationObservation(&sender); });
	const QByteArray hiddenLayout = window.saveState();
	window.show();
	QCoreApplication::processEvents();
	action->trigger();
	QVERIFY(!first->isHidden());
	revealEditor(first, operation); // No count for focus.
	window.tabifyDockWidget(first, second);
	revealEditor(second, operation); // One explicit hidden-to-open transition.
	first->raise();
	second->raise();
	QCoreApplication::processEvents(); // Tab switches do not count.
	window.hide();
	window.show();
	QCoreApplication::processEvents();
	const QByteArray openLayout = window.saveState();
	first->close();
	QVERIFY(first->isHidden());
	action->trigger();
	QVERIFY(!first->isHidden()); // Close/reopen counts once.
	QVERIFY(window.restoreState(hiddenLayout));
	QCoreApplication::processEvents();
	QVERIFY(first->isHidden());
	QVERIFY(second->isHidden());
	QVERIFY(window.restoreState(openLayout));
	QCoreApplication::processEvents();
	// Restoration is not a user reveal.
	QVERIFY(!first->isHidden());
	QVERIFY(!second->isHidden());
	QVERIFY(cycle());
	const auto usagePath = directory.filePath("queue/usage.json");
	const auto diagnosticPath = directory.filePath("queue/diagnostics.json");
	QJsonArray usage;
	QVERIFY(records(usagePath, usage));
	QJsonArray diagnostic;
	QVERIFY(records(diagnosticPath, diagnostic));
	QCOMPARE(usage.size(), 12);
	QCOMPARE(diagnostic.size(), 4);
	QCOMPARE(count(usage, "scene.opened", Category::Usage), 1);
	QCOMPARE(count(usage, "editor.opened", Category::Usage), 3);
	QCOMPARE(count(usage, "simulation.started", Category::Usage), 3);
	QCOMPARE(count(usage, "simulation.completed", Category::Usage), 1);
	QCOMPARE(count(usage, "simulation.failed", Category::Usage), 1);
	QCOMPARE(count(usage, "export.completed", Category::Usage), 2);
	QCOMPARE(count(usage, "export.failed", Category::Usage), 1);
	QCOMPARE(count(diagnostic, "operation.failed", Category::Diagnostics), 4);
	// Old operations cannot rebind to either category after revoke/re-enable.
	QVERIFY(consent.save(false, false));
	QVERIFY(cycle());
	QVERIFY(consent.save(true, true));
	QVERIFY(cycle());
	operation.exportFinished(ExportKind::Csv, false, true, Error::IoFailure);
	QVERIFY(cycle());
	QVERIFY(records(usagePath, usage));
	QVERIFY(usage.isEmpty());
	QVERIFY(records(diagnosticPath, diagnostic));
	QVERIFY(diagnostic.isEmpty());
	const auto fresh = OperationObservation(&sender);
	QVERIFY(consent.save(false, true));
	QVERIFY(cycle());
	fresh.exportFinished(ExportKind::Csv, false, true, Error::IoFailure);
	QVERIFY(cycle());
	QVERIFY(records(usagePath, usage));
	QVERIFY(usage.isEmpty());
	QVERIFY(records(diagnosticPath, diagnostic));
	QCOMPARE(diagnostic.size(), 1); // Unrelated usage revocation preserves diagnostics.
	sender.stop();
	proceed.release();
	QVERIFY(exited.tryAcquire(1, 3000));
	QVERIFY2(!proceedTimedOut, "the worker waited longer than 10 s for the test to let it go on");
}

QTEST_MAIN(TestTelemetryOperations)
#include "test_telemetryoperations.moc"
