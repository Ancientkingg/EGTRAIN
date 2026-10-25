#include "telemetry/TelemetryQueue.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
using namespace telemetry;

// Reads the JSON object in path into object; false when the file cannot be opened or holds no object.
static bool readObject(const QString& path, QJsonObject& object) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) return false;
	const auto document = QJsonDocument::fromJson(file.readAll());
	object = document.object();
	return document.isObject();
}
static bool writeObject(const QString& path, const QJsonObject& object) {
	QSaveFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) > 0 && file.commit();
}

namespace {
const auto kNow = QDateTime::fromString(QStringLiteral("2026-04-01T12:00:00Z"), Qt::ISODate);
const QString kEndpointA = QStringLiteral("https://a.example/collect");
const QString kEndpointB = QStringLiteral("https://b.example/collect");
const Application kMetadata{QStringLiteral("1.2.3"), QStringLiteral("macos"), QStringLiteral("arm64")};
const QString kId = QStringLiteral("12345678-1234-4234-8234-123456789abc");

TelemetryEventInput diagnosticsInput() {
	TelemetryEventInput input;
	input.name = Name::OperationFailed;
	return input;
}
} // namespace

class TestTelemetryQueue : public QObject {
	Q_OBJECT

private slots:
	void disabledRecovery_data();
	void disabledRecovery();
	void temporaryBudget();
	void controlTimestampTypes_data();
	void controlTimestampTypes();
	void storedBytes();
	void validatesIdsAndApplications();
	void readsBackEveryName_data();
	void readsBackEveryName();
	void rejectsInvalidInputs();
	void acceptsSceneKinds_data();
	void acceptsSceneKinds();
	void acceptsDurations_data();
	void acceptsDurations();
	void acceptsExportKinds_data();
	void acceptsExportKinds();
	void acceptsOperationFailures_data();
	void acceptsOperationFailures();
	void rejectsMalformedEvents();
	void ownerFollowerAndRetirement();
	void purgesOtherStamps();
	void corruptControlDisables();
	void clockRollback();
	void checkpointsLastSeen();
	void pruneRemovesExpiredEvents();
	void damagedCategoryFile();
	void damagedStamp();
	void oversizedControlDisables();
	void boundedStorage();
	void interruptedWrite();
	void oversizedTemporaryDisables();
	void retirementAfterCrash();
	void retirementLedgerIsBounded();
};

void TestTelemetryQueue::disabledRecovery_data() {
	QTest::addColumn<bool>("retirement");
	QTest::newRow("failed write") << false;
	QTest::newRow("failed write after a retirement") << true;
}

void TestTelemetryQueue::disabledRecovery() {
	QFETCH(bool, retirement);
	QTemporaryDir directory;
	TelemetryQueue queue;
	const auto now = QDateTime::currentDateTimeUtc();
	const QString endpoint = QStringLiteral("https://127.0.0.1/collect");
	const QString id = QStringLiteral("12345678-1234-4234-8234-123456789abc");
	QCOMPARE(queue.open(directory.path(), endpoint, QStringLiteral("1"), now), TelemetryQueue::OpenResult::Owner);
	QVERIFY(queue.enqueueUsage({createEvent({}, now), id}, now));
	TelemetryEventInput diagnostic;
	diagnostic.name = Name::OperationFailed;
	QVERIFY(queue.enqueueDiagnostic({createEvent(diagnostic, now), 1}, now));
	const QString control = directory.filePath(QStringLiteral("control.json"));
	if (retirement) {
		QJsonObject object;
		QVERIFY(readObject(control, object));
		object.insert(QStringLiteral("retired"), QJsonArray{endpoint});
		QVERIFY(writeObject(control, object));
	}
	QJsonObject retainedControl;
	QVERIFY(readObject(control, retainedControl));
	queue.failNextWriteForTesting();
	QVERIFY(!queue.enqueueUsage({createEvent({}, now), id}, now));
	TelemetryQueue follower;
	QCOMPARE(follower.open(directory.path(), endpoint, QStringLiteral("1"), now.addDays(8)), TelemetryQueue::OpenResult::Follower);
	QJsonObject usage;
	QVERIFY(readObject(directory.filePath(QStringLiteral("usage.json")), usage));
	QCOMPARE(usage.value(QStringLiteral("events")).toArray().size(), 1);
	queue.close();
	QCOMPARE(queue.open(directory.path(), endpoint, QStringLiteral("1"), now.addDays(8)), TelemetryQueue::OpenResult::Disabled);
	QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("usage.json"))));
	QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("diagnostics.json"))));
	QVERIFY(QFile::exists(directory.filePath(QStringLiteral("disabled"))));
	QJsonObject controlAfter;
	QVERIFY(readObject(control, controlAfter));
	QCOMPARE(controlAfter, retainedControl);
	queue.close();
	QCOMPARE(queue.open(directory.path(), endpoint, QStringLiteral("1"), now.addDays(9)), TelemetryQueue::OpenResult::Disabled);
}

void TestTelemetryQueue::temporaryBudget() {
	QTemporaryDir directory;
	TelemetryQueue queue;
	const auto now = QDateTime::currentDateTimeUtc();
	const QString endpoint = QStringLiteral("https://127.0.0.1/collect");
	const QString id = QStringLiteral("12345678-1234-4234-8234-123456789abc");
	QCOMPARE(queue.open(directory.path(), endpoint, QStringLiteral("1"), now), TelemetryQueue::OpenResult::Owner);
	QVERIFY(queue.enqueueUsage({createEvent({}, now), id}, now));
	const QString usage = directory.filePath(QStringLiteral("usage.json"));
	QJsonObject original;
	QVERIFY(readObject(usage, original));
	qint64 used = 0;
	for (const auto& file : QDir(directory.path()).entryInfoList(QDir::Files | QDir::Hidden)) used += file.size();
	// The eventual replacement fits, but its temporary coexistence does not.
	QFile leftover(directory.filePath(QStringLiteral("usage.json.orphan")));
	QVERIFY(leftover.open(QIODevice::WriteOnly));
	const qint64 available = 300;
	const qint64 fill = 1024 * 1024 - used - available;
	QCOMPARE(leftover.write(QByteArray(int(fill), 'x')), fill);
	leftover.close();
	QVERIFY(!queue.enqueueUsage({createEvent({}, now), id}, now));
	QVERIFY(!queue.healthy());
	QJsonObject kept;
	QVERIFY(readObject(usage, kept));
	QCOMPARE(kept, original);
	used = 0;
	for (const auto& file : QDir(directory.path()).entryInfoList(QDir::Files | QDir::Hidden)) used += file.size();
	QCOMPARE_LE(used, qint64(1024 * 1024));
}

void TestTelemetryQueue::controlTimestampTypes_data() {
	QTest::addColumn<QString>("field");
	QTest::addColumn<QJsonValue>("value");
	for (const QString& field : {QStringLiteral("last_seen"), QStringLiteral("next")}) {
		QTest::addRow("%s number", qPrintable(field)) << field << QJsonValue(42);
		QTest::addRow("%s null", qPrintable(field)) << field << QJsonValue(QJsonValue::Null);
		QTest::addRow("%s false", qPrintable(field)) << field << QJsonValue(false);
		QTest::addRow("%s array", qPrintable(field)) << field << QJsonValue(QJsonArray{});
	}
}

void TestTelemetryQueue::controlTimestampTypes() {
	QFETCH(QString, field);
	QFETCH(QJsonValue, value);
	QTemporaryDir directory;
	TelemetryQueue queue;
	const auto now = QDateTime::currentDateTimeUtc();
	const QString endpoint = QStringLiteral("https://127.0.0.1/collect");
	QCOMPARE(queue.open(directory.path(), endpoint, QStringLiteral("1"), now), TelemetryQueue::OpenResult::Owner);
	queue.close();
	const QString path = directory.filePath(QStringLiteral("control.json"));
	QJsonObject object;
	QVERIFY(readObject(path, object));
	if (field == QStringLiteral("last_seen")) object.insert(field, value);
	else {
		auto retry = object.value(QStringLiteral("retry")).toObject();
		retry.insert(field, value);
		object.insert(QStringLiteral("retry"), retry);
	}
	QVERIFY(writeObject(path, object));
	QCOMPARE(queue.open(directory.path(), endpoint, QStringLiteral("1"), now), TelemetryQueue::OpenResult::Disabled);
}

// The bytes that each enqueue leaves in usage.json and diagnostics.json, up to and past the limit of 512 events.
void TestTelemetryQueue::storedBytes() {
	const auto base = QDateTime::fromString(QStringLiteral("2026-04-01T12:00:00Z"), Qt::ISODate);
	const QString endpoint = QStringLiteral("https://a.example/collect");
	const QString installation = QStringLiteral("12345678-1234-4234-8234-123456789abc");
	constexpr int kEvents = 516;
	// In each group of four the last two events occur before the first two, and the two of a pair at the same time.
	// The last event occurs before every other one.
	const auto occurred = [&](int i) {
		return i == kEvents - 1 ? base.addMSecs(-1) : base.addMSecs((i / 4) * 2 + (i % 4 < 2 ? 1 : 0));
	};
	const auto id = [](int i) { return QStringLiteral("00000000-0000-4000-8000-%1").arg(i, 12, 10, QLatin1Char('0')); };
	const auto usageInput = [](int i) {
		TelemetryEventInput input;
		if (i % 3 == 1) {
			input.name = Name::SceneOpened;
			input.sceneKind = SceneKind::Local;
		} else if (i % 3 == 2) {
			input.name = Name::ExportCompleted;
			input.exportKind = ExportKind::Csv;
		}
		return input;
	};
	TelemetryEventInput diagnosticInput;
	diagnosticInput.name = Name::OperationFailed;
	diagnosticInput.operation = Operation::Export;
	diagnosticInput.error = Error::IoFailure;
	const QString usageRecords[] = {
		QStringLiteral(R"({"event":{"event_id":"%1","name":"session.started","occurred_at":"%2","properties":{}},)"
					   R"("stamp":"12345678-1234-4234-8234-123456789abc"})"),
		QStringLiteral(R"({"event":{"event_id":"%1","name":"scene.opened","occurred_at":"%2","properties":{"scene_kind":"local"}},)"
					   R"("stamp":"12345678-1234-4234-8234-123456789abc"})"),
		QStringLiteral(R"({"event":{"event_id":"%1","name":"export.completed","occurred_at":"%2","properties":{"export_kind":"csv"}},)"
					   R"("stamp":"12345678-1234-4234-8234-123456789abc"})")};
	const QString diagnosticRecord = QStringLiteral(
		R"({"event":{"event_id":"%1","name":"operation.failed","occurred_at":"%2","properties":{"error_code":"io_failure","operation_code":"export"}},"stamp":"7"})");
	// The newest 512 of the first count events, the oldest first and events of the same time in the order of enqueueing.
	const auto expected = [&](Category category, int count) {
		QVector<int> order;
		for (int i = 0; i < count; ++i) order.append(i);
		std::stable_sort(order.begin(), order.end(), [&](int left, int right) { return occurred(left) < occurred(right); });
		QStringList records;
		for (int k = std::max(0, count - 512); k < count; ++k) {
			const int i = order[k];
			const QString& record = category == Category::Usage ? usageRecords[i % 3] : diagnosticRecord;
			records.append(record.arg(id(i), occurred(i).toString(Qt::ISODateWithMs)));
		}
		return (QStringLiteral(R"({"endpoint":"https://a.example/collect","events":[)") + records.join(QLatin1Char(','))
			+ QStringLiteral(R"(],"format":1,"terms":"1"})"))
			.toUtf8();
	};
	QTemporaryDir directory;
	TelemetryQueue queue;
	QCOMPARE(queue.open(directory.path(), endpoint, QStringLiteral("1"), base), TelemetryQueue::OpenResult::Owner);
	// The bytes of the file name into contents; false when the file cannot be opened.
	const auto stored = [&](const QString& name, QByteArray& contents) {
		QFile file(directory.filePath(name));
		if (!file.open(QIODevice::ReadOnly)) return false;
		contents = file.readAll();
		return true;
	};
	// moc cannot parse these raw strings inside a check macro, so they are constants.
	const QByteArray kFirstTwoDiagnostics(R"({"endpoint":"https://a.example/collect","events":[)"
										  R"({"event":{"event_id":"00000000-0000-4000-8000-000000000000","name":"operation.failed",)"
										  R"("occurred_at":"2026-04-01T12:00:00.001Z","properties":{"error_code":"io_failure","operation_code":"export"}},"stamp":"7"},)"
										  R"({"event":{"event_id":"00000000-0000-4000-8000-000000000001","name":"operation.failed",)"
										  R"("occurred_at":"2026-04-01T12:00:00.001Z","properties":{"error_code":"io_failure","operation_code":"export"}},"stamp":"7"})"
										  R"(],"format":1,"terms":"1"})");
	const QByteArray kFirstFourUsage(R"({"endpoint":"https://a.example/collect","events":[)"
									 R"({"event":{"event_id":"00000000-0000-4000-8000-000000000002","name":"export.completed",)"
									 R"("occurred_at":"2026-04-01T12:00:00.000Z","properties":{"export_kind":"csv"}},"stamp":"12345678-1234-4234-8234-123456789abc"},)"
									 R"({"event":{"event_id":"00000000-0000-4000-8000-000000000003","name":"session.started",)"
									 R"("occurred_at":"2026-04-01T12:00:00.000Z","properties":{}},"stamp":"12345678-1234-4234-8234-123456789abc"},)"
									 R"({"event":{"event_id":"00000000-0000-4000-8000-000000000000","name":"session.started",)"
									 R"("occurred_at":"2026-04-01T12:00:00.001Z","properties":{}},"stamp":"12345678-1234-4234-8234-123456789abc"},)"
									 R"({"event":{"event_id":"00000000-0000-4000-8000-000000000001","name":"scene.opened",)"
									 R"("occurred_at":"2026-04-01T12:00:00.001Z","properties":{"scene_kind":"local"}},"stamp":"12345678-1234-4234-8234-123456789abc"})"
									 R"(],"format":1,"terms":"1"})");
	const QByteArray kOldestKept(R"({"endpoint":"https://a.example/collect","events":[{"event":{"event_id":"00000000-0000-4000-8000-000000000001",)");
	QByteArray bytes;
	for (int i = 0; i < kEvents; ++i) {
		QVERIFY(queue.enqueueUsage({{usageInput(i), id(i), occurred(i)}, installation}, base.addSecs(1)));
		QVERIFY(stored(QStringLiteral("usage.json"), bytes));
		QCOMPARE(bytes, expected(Category::Usage, i + 1));
		QVERIFY(queue.enqueueDiagnostic({{diagnosticInput, id(i), occurred(i)}, 7}, base.addSecs(1)));
		QVERIFY(stored(QStringLiteral("diagnostics.json"), bytes));
		QCOMPARE(bytes, expected(Category::Diagnostics, i + 1));
		if (i == 1) {
			QVERIFY(stored(QStringLiteral("diagnostics.json"), bytes));
			QCOMPARE(bytes, kFirstTwoDiagnostics);
		}
		if (i == 3) {
			QVERIFY(stored(QStringLiteral("usage.json"), bytes));
			QCOMPARE(bytes, kFirstFourUsage);
		}
	}
	// The event that occurred first gave way at once, and the oldest kept is the second of the first pair.
	QVERIFY(stored(QStringLiteral("usage.json"), bytes));
	QVERIFY(bytes.startsWith(kOldestKept));
	queue.close();
}

void TestTelemetryQueue::validatesIdsAndApplications() {
	QVERIFY(validUuid(kId));
	QVERIFY(!validUuid(kId.toUpper()));
	QVERIFY(!validUuid(kId + QStringLiteral("\n")));
	QVERIFY(validApplication(kMetadata));
	QVERIFY(!validApplication({QStringLiteral("01.2.3"), QStringLiteral("macos"), QStringLiteral("arm64")}));
	QVERIFY(!validApplication({QStringLiteral("1.2.3\n"), QStringLiteral("macos"), QStringLiteral("arm64")}));
}

void TestTelemetryQueue::readsBackEveryName_data() {
	QTest::addColumn<int>("n");
	for (int n = 0; n <= static_cast<int>(Name::OperationFailed); ++n) QTest::addRow("name %d", n) << n;
}

void TestTelemetryQueue::readsBackEveryName() {
	QFETCH(int, n);
	TelemetryEventInput input;
	input.name = static_cast<Name>(n);
	const Category category = input.name == Name::OperationFailed ? Category::Diagnostics : Category::Usage;
	QVERIFY(validInput(input, category));
	const TelemetryEvent event{input, kId, kNow};
	TelemetryEvent parsed;
	QVERIFY(readEvent(eventObject(event, category), category, &parsed));
	QCOMPARE(parsed.id, kId);
	const QByteArray wire = batch({event}, category, category == Category::Usage ? kId : QString(), kMetadata, kNow);
	QVERIFY(!wire.isEmpty());
	QCOMPARE_LE(wire.size(), 65536);
	const auto envelope = QJsonDocument::fromJson(wire).object();
	QCOMPARE(envelope.keys().size(), category == Category::Usage ? 6 : 5);
	if (category == Category::Usage) QCOMPARE(envelope.value(QStringLiteral("installation_id")).toString(), kId);
	else QVERIFY(!wire.contains("installation_id"));
	auto unknown = eventObject(event, category);
	unknown.insert(QStringLiteral("private"), QStringLiteral("secret"));
	QVERIFY(!readEvent(unknown, category, &parsed));
}

void TestTelemetryQueue::rejectsInvalidInputs() {
	TelemetryEventInput invalid;
	invalid.name = static_cast<Name>(55);
	QVERIFY(!validInput(invalid, Category::Usage));
	invalid = {};
	invalid.sceneKind = static_cast<SceneKind>(55);
	QVERIFY(!validInput(invalid, Category::Usage));
	invalid = {};
	invalid.exportKind = ExportKind::Csv;
	QVERIFY(!validInput(invalid, Category::Usage));
}

void TestTelemetryQueue::acceptsSceneKinds_data() {
	QTest::addColumn<int>("n");
	QTest::newRow("bundled") << 0;
	QTest::newRow("local") << 1;
}

void TestTelemetryQueue::acceptsSceneKinds() {
	QFETCH(int, n);
	TelemetryEventInput scene;
	scene.name = Name::SceneOpened;
	scene.sceneKind = static_cast<SceneKind>(n);
	QVERIFY(validInput(scene, Category::Usage));
}

void TestTelemetryQueue::acceptsDurations_data() {
	QTest::addColumn<int>("n");
	for (int n = 0; n < 5; ++n) QTest::addRow("duration %d", n) << n;
}

void TestTelemetryQueue::acceptsDurations() {
	QFETCH(int, n);
	TelemetryEventInput completed;
	completed.name = Name::SimulationCompleted;
	completed.duration = static_cast<Duration>(n);
	QVERIFY(validInput(completed, Category::Usage));
	completed.name = Name::SimulationFailed;
	QVERIFY(validInput(completed, Category::Usage));
}

void TestTelemetryQueue::acceptsExportKinds_data() {
	QTest::addColumn<int>("n");
	for (int n = 0; n < 4; ++n) QTest::addRow("export kind %d", n) << n;
}

void TestTelemetryQueue::acceptsExportKinds() {
	QFETCH(int, n);
	TelemetryEventInput exported;
	exported.name = Name::ExportCompleted;
	exported.exportKind = static_cast<ExportKind>(n);
	QVERIFY(validInput(exported, Category::Usage));
	exported.name = Name::ExportFailed;
	QVERIFY(validInput(exported, Category::Usage));
}

void TestTelemetryQueue::acceptsOperationFailures_data() {
	QTest::addColumn<int>("operation");
	QTest::addColumn<int>("error");
	for (int operation = 0; operation < 3; ++operation)
		for (int error = 0; error < 4; ++error) QTest::addRow("operation %d, error %d", operation, error) << operation << error;
}

void TestTelemetryQueue::acceptsOperationFailures() {
	QFETCH(int, operation);
	QFETCH(int, error);
	TelemetryEventInput failed;
	failed.name = Name::OperationFailed;
	failed.operation = static_cast<Operation>(operation);
	failed.error = static_cast<Error>(error);
	QVERIFY(validInput(failed, Category::Diagnostics));
	auto event = createEvent(failed, kNow);
	QVERIFY(!batch({event}, Category::Diagnostics, QString(), kMetadata, kNow).contains("installation_id"));
}

void TestTelemetryQueue::rejectsMalformedEvents() {
	TelemetryEvent malformed;
	QVERIFY(!readEvent(QJsonObject{{QStringLiteral("event_id"), kId}, {QStringLiteral("occurred_at"), QStringLiteral("2026-02-30T12:00:00Z")},
						   {QStringLiteral("name"), QStringLiteral("session.started")}, {QStringLiteral("properties"), QJsonObject{}}},
		Category::Usage, &malformed));
	const auto eventWithLf = QJsonObject{{QStringLiteral("event_id"), kId + QStringLiteral("\n")},
		{QStringLiteral("occurred_at"), QStringLiteral("2026-04-01T12:00:00Z")},
		{QStringLiteral("name"), QStringLiteral("session.started")}, {QStringLiteral("properties"), QJsonObject{}}};
	QVERIFY(!readEvent(eventWithLf, Category::Usage, &malformed));
	auto timeWithLf = eventWithLf;
	timeWithLf.insert(QStringLiteral("event_id"), kId);
	timeWithLf.insert(QStringLiteral("occurred_at"), QStringLiteral("2026-04-01T12:00:00Z\n"));
	QVERIFY(!readEvent(timeWithLf, Category::Usage, &malformed));
}

void TestTelemetryQueue::ownerFollowerAndRetirement() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	TelemetryQueue queue;
	QCOMPARE(queue.open(directory.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	TelemetryQueue follower;
	QCOMPARE(follower.open(directory.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Follower);
	const auto usage = createEvent({}, kNow);
	const auto diagnostic = createEvent(diagnosticsInput(), kNow);
	QVERIFY(queue.enqueueUsage({usage, kId}, kNow));
	QVERIFY(queue.enqueueDiagnostic({diagnostic, 2}, kNow));
	QCOMPARE(queue.nextExpiryUtc(), kNow.addDays(7));
	QVERIFY(!queue.enqueueUsage({createEvent({}, kNow.addDays(-8)), kId}, kNow));
	auto prepared = queue.prepareBatch(Category::Diagnostics, QStringLiteral("2"), kMetadata, kNow);
	QCOMPARE(prepared.token.ids.size(), 1);
	QCOMPARE(prepared.token.ids.first(), diagnostic.id);
	QVERIFY(!prepared.bytes.contains("installation_id"));
	QVERIFY(!prepared.bytes.contains(kId.toUtf8()));
	auto usageBatch = queue.prepareBatch(Category::Usage, kId, kMetadata, kNow);
	QCOMPARE(usageBatch.token.ids.size(), 1);
	QVERIFY(usageBatch.bytes.contains(kId.toUtf8()));
	RetryState retry;
	retry.failures = 2;
	retry.nextEligibleUtc = kNow.addSecs(120);
	QVERIFY(queue.commitRetry(retry, kNow));
	queue.close();
	QCOMPARE(follower.open(directory.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	QCOMPARE(follower.retryState().failures, 2);
	QCOMPARE(follower.prepareBatch(Category::Usage, kId, kMetadata, kNow).token.ids.first(), usage.id);
	QVERIFY(!follower.discardBatch(usageBatch.token));
	QVERIFY(follower.discardBatch(follower.prepareBatch(Category::Usage, kId, kMetadata, kNow).token));
	QVERIFY(follower.prepareBatch(Category::Usage, kId, kMetadata, kNow).bytes.isEmpty());
	QVERIFY(follower.retireExactEndpoint(kEndpointA));
	follower.close();
	QCOMPARE(queue.open(directory.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Retired);
	queue.close();
	QCOMPARE(queue.open(directory.path(), kEndpointB, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	QVERIFY(queue.prepareBatch(Category::Diagnostics, QStringLiteral("2"), kMetadata, kNow).bytes.isEmpty());
	queue.close();
	QCOMPARE(queue.open(directory.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Retired);
	queue.close();
}

void TestTelemetryQueue::purgesOtherStamps() {
	QTemporaryDir stamps;
	TelemetryQueue queue;
	const auto usage = createEvent({}, kNow);
	const auto diagnostic = createEvent(diagnosticsInput(), kNow);
	QCOMPARE(queue.open(stamps.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	const QString oldId = QStringLiteral("23456789-1234-4234-8234-123456789abc");
	QVERIFY(queue.enqueueUsage({createEvent({}, kNow), oldId}, kNow));
	QVERIFY(queue.enqueueUsage({usage, kId}, kNow));
	QVERIFY(queue.enqueueDiagnostic({createEvent(diagnosticsInput(), kNow), 1}, kNow));
	QVERIFY(queue.enqueueDiagnostic({diagnostic, 2}, kNow));
	QVERIFY(!queue.purgeOtherStamps(Category::Usage, QStringLiteral("invalid")));
	QVERIFY(!queue.purgeOtherStamps(Category::Diagnostics, QStringLiteral("02")));
	QVERIFY(queue.purgeOtherStamps(Category::Usage, kId));
	QVERIFY(queue.purgeOtherStamps(Category::Diagnostics, QStringLiteral("2")));
	queue.close();
	QCOMPARE(queue.open(stamps.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	QVERIFY(queue.prepareBatch(Category::Usage, oldId, kMetadata, kNow).bytes.isEmpty());
	QVERIFY(queue.prepareBatch(Category::Diagnostics, QStringLiteral("1"), kMetadata, kNow).bytes.isEmpty());
	QCOMPARE(queue.prepareBatch(Category::Usage, kId, kMetadata, kNow).token.ids, QVector<QString>{usage.id});
	QCOMPARE(queue.prepareBatch(Category::Diagnostics, QStringLiteral("2"), kMetadata, kNow).token.ids, QVector<QString>{diagnostic.id});
	queue.close();
}

void TestTelemetryQueue::corruptControlDisables() {
	QTemporaryDir corrupt;
	QVERIFY(corrupt.isValid());
	QFile bad(corrupt.path() + QStringLiteral("/control.json"));
	QVERIFY(bad.open(QIODevice::WriteOnly));
	QCOMPARE(bad.write("{"), qint64(1));
	bad.close();
	TelemetryQueue queue;
	QCOMPARE(queue.open(corrupt.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Disabled);
	queue.close();
}

void TestTelemetryQueue::clockRollback() {
	QTemporaryDir rollback;
	QVERIFY(rollback.isValid());
	TelemetryQueue queue;
	const auto usage = createEvent({}, kNow);
	QCOMPARE(queue.open(rollback.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	QVERIFY(queue.enqueueUsage({usage, kId}, kNow));
	QVERIFY(queue.prune(kNow.addSecs(-1)));
	QVERIFY(queue.prepareBatch(Category::Usage, kId, kMetadata, kNow).bytes.isEmpty());
	QCOMPARE(queue.retryState().nextEligibleUtc, kNow.addSecs(-1).addSecs(86400));
	queue.close();
}

void TestTelemetryQueue::checkpointsLastSeen() {
	QTemporaryDir checkpoint;
	TelemetryQueue queue;
	QCOMPARE(queue.open(checkpoint.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	const auto checkpointPath = checkpoint.filePath(QStringLiteral("control.json"));
	// The last_seen value of control.json into value; false when the file cannot be opened.
	auto lastSeen = [&](QString& value) {
		QFile file(checkpointPath);
		if (!file.open(QIODevice::ReadOnly)) return false;
		value = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("last_seen")).toString();
		return true;
	};
	QString initialCheckpoint;
	QVERIFY(lastSeen(initialCheckpoint));
	QString seen;
	QVERIFY(queue.prune(kNow.addSecs(30)));
	QVERIFY(lastSeen(seen));
	QCOMPARE(seen, initialCheckpoint);
	QVERIFY(queue.prune(kNow.addSecs(60)));
	QVERIFY(lastSeen(seen));
	QCOMPARE(seen, kNow.addSecs(60).toString(Qt::ISODateWithMs));
	queue.close();
}

void TestTelemetryQueue::pruneRemovesExpiredEvents() {
	QTemporaryDir expired;
	TelemetryQueue queue;
	const auto usage = createEvent({}, kNow);
	QCOMPARE(queue.open(expired.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	QVERIFY(queue.enqueueUsage({usage, kId}, kNow));
	QVERIFY(queue.prune(kNow.addDays(7)));
	QVERIFY(!queue.nextExpiryUtc().isValid());
	QVERIFY(queue.prepareBatch(Category::Usage, kId, kMetadata, kNow.addDays(7)).bytes.isEmpty());
	queue.close();
}

void TestTelemetryQueue::damagedCategoryFile() {
	QTemporaryDir malformedCategory;
	TelemetryQueue queue;
	QCOMPARE(queue.open(malformedCategory.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	queue.close();
	QFile damaged(malformedCategory.path() + QStringLiteral("/usage.json"));
	QVERIFY(damaged.open(QIODevice::WriteOnly));
	QCOMPARE(damaged.write("truncated"), qint64(9));
	damaged.close();
	QCOMPARE(queue.open(malformedCategory.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	QVERIFY(queue.prepareBatch(Category::Usage, kId, kMetadata, kNow).bytes.isEmpty());
	queue.close();
	QVERIFY(QFile::remove(malformedCategory.path() + QStringLiteral("/control.json")));
	QCOMPARE(queue.open(malformedCategory.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Disabled);
	queue.close();
}

void TestTelemetryQueue::damagedStamp() {
	QTemporaryDir malformedStamp;
	TelemetryQueue queue;
	const auto usage = createEvent({}, kNow);
	QCOMPARE(queue.open(malformedStamp.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	QVERIFY(queue.enqueueUsage({usage, kId}, kNow));
	queue.close();
	QFile stampFile(malformedStamp.filePath(QStringLiteral("usage.json")));
	QVERIFY(stampFile.open(QIODevice::ReadOnly));
	auto categoryObject = QJsonDocument::fromJson(stampFile.readAll()).object();
	stampFile.close();
	auto entries = categoryObject.value(QStringLiteral("events")).toArray();
	auto badEntry = entries.first().toObject();
	badEntry.insert(QStringLiteral("stamp"), kId + QStringLiteral("\n"));
	entries[0] = badEntry;
	categoryObject.insert(QStringLiteral("events"), entries);
	QSaveFile stampReplacement(stampFile.fileName());
	QVERIFY(stampReplacement.open(QIODevice::WriteOnly));
	QVERIFY(stampReplacement.write(QJsonDocument(categoryObject).toJson(QJsonDocument::Compact)) > 0);
	QVERIFY(stampReplacement.commit());
	QCOMPARE(queue.open(malformedStamp.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	QVERIFY(queue.prepareBatch(Category::Usage, kId, kMetadata, kNow).bytes.isEmpty());
	queue.close();
}

void TestTelemetryQueue::oversizedControlDisables() {
	QTemporaryDir oversized;
	QFile huge(oversized.path() + QStringLiteral("/control.json"));
	QVERIFY(huge.open(QIODevice::WriteOnly));
	QCOMPARE(huge.write(QByteArray(65537, 'x')), qint64(65537));
	huge.close();
	TelemetryQueue queue;
	QCOMPARE(queue.open(oversized.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Disabled);
	queue.close();
}

void TestTelemetryQueue::boundedStorage() {
	QTemporaryDir bounded;
	TelemetryQueue queue;
	QCOMPARE(queue.open(bounded.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	for (int i = 0; i < 520; ++i) QVERIFY(queue.enqueueUsage({createEvent({}, kNow.addMSecs(i)), kId}, kNow.addSecs(1)));
	auto first = queue.prepareBatch(Category::Usage, kId, kMetadata, kNow.addSecs(1));
	QCOMPARE(first.token.ids.size(), 50);
	QCOMPARE_LE(first.bytes.size(), 65536);
	QFile usageFile(bounded.path() + QStringLiteral("/usage.json"));
	QCOMPARE_LE(usageFile.size(), qint64(192 * 1024));
	QVERIFY(usageFile.open(QIODevice::ReadOnly));
	const auto stored = QJsonDocument::fromJson(usageFile.readAll()).object().value(QStringLiteral("events")).toArray();
	QCOMPARE(stored.size(), 512);
	QCOMPARE(stored.first().toObject().value(QStringLiteral("event")).toObject().value(QStringLiteral("occurred_at")).toString(),
		QStringLiteral("2026-04-01T12:00:00.008Z"));
	usageFile.close();
	QVERIFY(queue.enqueueUsage({createEvent({}, kNow.addMSecs(10)), kId}, kNow.addSecs(1)));
	QVERIFY(usageFile.open(QIODevice::ReadOnly));
	const auto afterOldest = QJsonDocument::fromJson(usageFile.readAll()).object().value(QStringLiteral("events")).toArray();
	QCOMPARE(afterOldest.size(), 512);
	QCOMPARE(afterOldest.first().toObject().value(QStringLiteral("event")).toObject().value(QStringLiteral("occurred_at")).toString(),
		QStringLiteral("2026-04-01T12:00:00.009Z"));
	usageFile.close();
	for (int i = 0; i < 520; ++i)
		QVERIFY(queue.enqueueDiagnostic({createEvent(diagnosticsInput(), kNow.addMSecs(i)), 2}, kNow.addSecs(1)));
	QFile diagnosticsFile(bounded.path() + QStringLiteral("/diagnostics.json"));
	QCOMPARE_LE(diagnosticsFile.size(), qint64(192 * 1024));
	QVERIFY(diagnosticsFile.open(QIODevice::ReadOnly));
	const auto retainedDiagnostics = QJsonDocument::fromJson(diagnosticsFile.readAll()).object().value(QStringLiteral("events")).toArray();
	QCOMPARE(retainedDiagnostics.size(), 512);
	QCOMPARE(
		retainedDiagnostics.first().toObject().value(QStringLiteral("event")).toObject().value(QStringLiteral("occurred_at")).toString(),
		QStringLiteral("2026-04-01T12:00:00.008Z"));
	diagnosticsFile.close();
	qint64 totalBytes = 0;
	for (const auto& file : QDir(bounded.path()).entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot))
		totalBytes += file.size();
	QCOMPARE_LE(totalBytes, qint64(1024 * 1024));
	QFile controlFile(bounded.path() + QStringLiteral("/control.json"));
	QCOMPARE_LE(controlFile.size(), qint64(64 * 1024));
	QVERIFY(queue.discardBatch(first.token));
	QCOMPARE(queue.prepareBatch(Category::Usage, kId, kMetadata, kNow.addSecs(1)).token.ids.size(), 50);
	queue.close();
}

void TestTelemetryQueue::interruptedWrite() {
	QTemporaryDir interrupted;
	TelemetryQueue queue;
	const auto usage = createEvent({}, kNow);
	QFile orphan(interrupted.path() + QStringLiteral("/usage.json.ABCDEF"));
	QVERIFY(orphan.open(QIODevice::WriteOnly));
	QCOMPARE(orphan.write("incomplete"), qint64(10));
	orphan.close();
	QCOMPARE(queue.open(interrupted.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	QVERIFY(queue.enqueueUsage({usage, kId}, kNow));
	queue.close();
	QCOMPARE(queue.open(interrupted.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	QCOMPARE(queue.prepareBatch(Category::Usage, kId, kMetadata, kNow).token.ids.first(), usage.id);
#ifdef EGTRAIN_TELEMETRY_TEST_HOOK
	queue.failNextWriteForTesting();
	QVERIFY(!queue.enqueueUsage({createEvent({}, kNow), kId}, kNow));
	QVERIFY(!queue.healthy());
#endif
	queue.close();
}

void TestTelemetryQueue::oversizedTemporaryDisables() {
	QTemporaryDir oversizedTemporary;
	QFile leftover(oversizedTemporary.filePath(QStringLiteral("usage.json.XXXXXX")));
	QVERIFY(leftover.open(QIODevice::WriteOnly));
	QCOMPARE(leftover.write(QByteArray(1024 * 1024, 'x')), qint64(1024 * 1024));
	leftover.close();
	TelemetryQueue queue;
	QCOMPARE(queue.open(oversizedTemporary.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Disabled);
	queue.close();
}

void TestTelemetryQueue::retirementAfterCrash() {
	QTemporaryDir crashRetirement;
	TelemetryQueue queue;
	const auto usage = createEvent({}, kNow);
	const auto diagnostic = createEvent(diagnosticsInput(), kNow);
	QCOMPARE(queue.open(crashRetirement.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	QVERIFY(queue.enqueueUsage({usage, kId}, kNow));
	QVERIFY(queue.enqueueDiagnostic({diagnostic, 2}, kNow));
	// Simulate termination after the retirement control commit but before either category replacement.
	QFile control(crashRetirement.path() + QStringLiteral("/control.json"));
	QVERIFY(control.open(QIODevice::ReadOnly));
	auto controlObject = QJsonDocument::fromJson(control.readAll()).object();
	control.close();
	controlObject.insert(QStringLiteral("retired"), QJsonArray{kEndpointA});
	QSaveFile retirement(crashRetirement.path() + QStringLiteral("/control.json"));
	QVERIFY(retirement.open(QIODevice::WriteOnly));
	QVERIFY(retirement.write(QJsonDocument(controlObject).toJson(QJsonDocument::Compact)) > 0);
	QVERIFY(retirement.commit());
	queue.close();
	QCOMPARE(queue.open(crashRetirement.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Retired);
	queue.close();
	for (const QString& name : {QStringLiteral("usage.json"), QStringLiteral("diagnostics.json")}) {
		QFile file(crashRetirement.filePath(name));
		QVERIFY(file.open(QIODevice::ReadOnly));
		QVERIFY(QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("events")).toArray().isEmpty());
	}
	QCOMPARE(queue.open(crashRetirement.path(), kEndpointB, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	QVERIFY(queue.prepareBatch(Category::Usage, kId, kMetadata, kNow).bytes.isEmpty());
	QVERIFY(queue.enqueueUsage({createEvent({}, kNow), kId}, kNow));
	queue.close();
	QCOMPARE(queue.open(crashRetirement.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Retired);
	queue.close();
	QCOMPARE(queue.open(crashRetirement.path(), kEndpointB, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	QVERIFY(queue.prepareBatch(Category::Usage, kId, kMetadata, kNow).bytes.isEmpty());
	queue.close();
}

void TestTelemetryQueue::retirementLedgerIsBounded() {
	QTemporaryDir ledger;
	TelemetryQueue queue;
	QCOMPARE(queue.open(ledger.path(), kEndpointA, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	for (int i = 0; i < 16; ++i) {
		const QString endpoint = QStringLiteral("https://example.test/") + QString::number(i);
		queue.close();
		QCOMPARE(queue.open(ledger.path(), endpoint, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
		QVERIFY(queue.retireExactEndpoint(endpoint));
	}
	queue.close();
	QCOMPARE(queue.open(ledger.path(), kEndpointB, QStringLiteral("1"), kNow), TelemetryQueue::OpenResult::Owner);
	QVERIFY(!queue.retireExactEndpoint(kEndpointB));
	QVERIFY(!queue.healthy());
	queue.close();
}

QTEST_GUILESS_MAIN(TestTelemetryQueue)
#include "test_telemetryqueue.moc"
