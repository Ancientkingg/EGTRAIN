#include "telemetry/TelemetryQueue.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QJsonObject>
#include <QTemporaryDir>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
using namespace telemetry;

static QJsonObject readObject(const QString& path) {
	QFile file(path);
	assert(file.open(QIODevice::ReadOnly));
	const auto document = QJsonDocument::fromJson(file.readAll());
	assert(document.isObject());
	return document.object();
}
static void writeObject(const QString& path, const QJsonObject& object) {
	QSaveFile file(path);
	assert(file.open(QIODevice::WriteOnly));
	assert(file.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) > 0);
	assert(file.commit());
}
static void testDisabledRecovery() {
	for (bool retirement : {false, true}) {
		QTemporaryDir directory;
		TelemetryQueue queue;
		const auto now = QDateTime::currentDateTimeUtc();
		const QString endpoint = QStringLiteral("https://127.0.0.1/collect");
		const QString id = QStringLiteral("12345678-1234-4234-8234-123456789abc");
		assert(queue.open(directory.path(), endpoint, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
		assert(queue.enqueueUsage({createEvent({}, now), id}, now));
		TelemetryEventInput diagnostic;
		diagnostic.name = Name::OperationFailed;
		assert(queue.enqueueDiagnostic({createEvent(diagnostic, now), 1}, now));
		const QString control = directory.filePath(QStringLiteral("control.json"));
		if (retirement) {
			auto object = readObject(control);
			object.insert(QStringLiteral("retired"), QJsonArray{endpoint});
			writeObject(control, object);
		}
		const auto retainedControl = readObject(control);
		queue.failNextWriteForTesting();
		assert(!queue.enqueueUsage({createEvent({}, now), id}, now));
		TelemetryQueue follower;
		assert(follower.open(directory.path(), endpoint, QStringLiteral("1"), now.addDays(8)) == TelemetryQueue::OpenResult::Follower);
		assert(readObject(directory.filePath(QStringLiteral("usage.json"))).value(QStringLiteral("events")).toArray().size() == 1);
		queue.close();
		assert(queue.open(directory.path(), endpoint, QStringLiteral("1"), now.addDays(8)) == TelemetryQueue::OpenResult::Disabled);
		assert(!QFile::exists(directory.filePath(QStringLiteral("usage.json"))));
		assert(!QFile::exists(directory.filePath(QStringLiteral("diagnostics.json"))));
		assert(QFile::exists(directory.filePath(QStringLiteral("disabled"))));
		assert(readObject(control) == retainedControl);
		queue.close();
		assert(queue.open(directory.path(), endpoint, QStringLiteral("1"), now.addDays(9)) == TelemetryQueue::OpenResult::Disabled);
	}
}
static void testTemporaryBudget() {
	QTemporaryDir directory;
	TelemetryQueue queue;
	const auto now = QDateTime::currentDateTimeUtc();
	const QString endpoint = QStringLiteral("https://127.0.0.1/collect");
	const QString id = QStringLiteral("12345678-1234-4234-8234-123456789abc");
	assert(queue.open(directory.path(), endpoint, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	assert(queue.enqueueUsage({createEvent({}, now), id}, now));
	const QString usage = directory.filePath(QStringLiteral("usage.json"));
	const auto original = readObject(usage);
	qint64 used = 0;
	for (const auto& file : QDir(directory.path()).entryInfoList(QDir::Files | QDir::Hidden)) used += file.size();
	// The eventual replacement fits, but its temporary coexistence does not.
	QFile leftover(directory.filePath(QStringLiteral("usage.json.orphan")));
	assert(leftover.open(QIODevice::WriteOnly));
	const qint64 available = 300;
	const qint64 fill = 1024 * 1024 - used - available;
	assert(leftover.write(QByteArray(int(fill), 'x')) == fill);
	leftover.close();
	assert(!queue.enqueueUsage({createEvent({}, now), id}, now));
	assert(!queue.healthy() && readObject(usage) == original);
	used = 0;
	for (const auto& file : QDir(directory.path()).entryInfoList(QDir::Files | QDir::Hidden)) used += file.size();
	assert(used <= 1024 * 1024);
}
static void testControlTimestampTypes() {
	for (const QString& field : {QStringLiteral("last_seen"), QStringLiteral("next")}) {
		for (const QJsonValue& value : {QJsonValue(42), QJsonValue(QJsonValue::Null), QJsonValue(false), QJsonValue(QJsonArray{})}) {
			QTemporaryDir directory;
			TelemetryQueue queue;
			const auto now = QDateTime::currentDateTimeUtc();
			const QString endpoint = QStringLiteral("https://127.0.0.1/collect");
			assert(queue.open(directory.path(), endpoint, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
			queue.close();
			const QString path = directory.filePath(QStringLiteral("control.json"));
			auto object = readObject(path);
			if (field == QStringLiteral("last_seen")) object.insert(field, value);
			else {
				auto retry = object.value(QStringLiteral("retry")).toObject();
				retry.insert(field, value);
				object.insert(QStringLiteral("retry"), retry);
			}
			writeObject(path, object);
			assert(queue.open(directory.path(), endpoint, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Disabled);
		}
	}
}

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	if (argc == 2) {
		const QByteArray selected(argv[1]);
		if (selected == "disabled") testDisabledRecovery();
		else if (selected == "budget") testTemporaryBudget();
		else {
			assert(selected == "timestamps");
			testControlTimestampTypes();
		}
		return 0;
	}
	testDisabledRecovery();
	testTemporaryBudget();
	testControlTimestampTypes();
	QTemporaryDir directory;
	assert(directory.isValid());
	const auto now = QDateTime::fromString(QStringLiteral("2026-04-01T12:00:00Z"), Qt::ISODate);
	const QString a = QStringLiteral("https://a.example/collect");
	const QString b = QStringLiteral("https://b.example/collect");
	const Application metadata{QStringLiteral("1.2.3"), QStringLiteral("macos"), QStringLiteral("arm64")};
	const QString id = QStringLiteral("12345678-1234-4234-8234-123456789abc");
	assert(validUuid(id) && !validUuid(id.toUpper()) && !validUuid(id + QStringLiteral("\n")));
	assert(validApplication(metadata) && !validApplication({QStringLiteral("01.2.3"), QStringLiteral("macos"), QStringLiteral("arm64")})
		&& !validApplication({QStringLiteral("1.2.3\n"), QStringLiteral("macos"), QStringLiteral("arm64")}));
	for (int n = 0; n <= static_cast<int>(Name::OperationFailed); ++n) {
		TelemetryEventInput input;
		input.name = static_cast<Name>(n);
		const Category category = input.name == Name::OperationFailed ? Category::Diagnostics : Category::Usage;
		assert(validInput(input, category));
		const TelemetryEvent event{input, id, now};
		TelemetryEvent parsed;
		assert(readEvent(eventObject(event, category), category, &parsed));
		assert(parsed.id == id);
		const QByteArray wire = batch({event}, category, category == Category::Usage ? id : QString(), metadata, now);
		assert(!wire.isEmpty() && wire.size() <= 65536);
		const auto envelope = QJsonDocument::fromJson(wire).object();
		assert(envelope.keys().size() == (category == Category::Usage ? 6 : 5));
		assert(category == Category::Usage ? envelope.value(QStringLiteral("installation_id")).toString() == id
										   : !wire.contains("installation_id"));
		auto unknown = eventObject(event, category);
		unknown.insert(QStringLiteral("private"), QStringLiteral("secret"));
		assert(!readEvent(unknown, category, &parsed));
	}
	TelemetryEventInput invalid;
	invalid.name = static_cast<Name>(55);
	assert(!validInput(invalid, Category::Usage));
	invalid = {};
	invalid.sceneKind = static_cast<SceneKind>(55);
	assert(!validInput(invalid, Category::Usage));
	invalid = {};
	invalid.exportKind = ExportKind::Csv;
	assert(!validInput(invalid, Category::Usage));
	TelemetryEventInput scene;
	scene.name = Name::SceneOpened;
	scene.sceneKind = SceneKind::Local;
	assert(validInput(scene, Category::Usage));
	for (int n = 0; n < 2; ++n) {
		scene.sceneKind = static_cast<SceneKind>(n);
		assert(validInput(scene, Category::Usage));
	}
	for (int n = 0; n < 5; ++n) {
		TelemetryEventInput completed;
		completed.name = Name::SimulationCompleted;
		completed.duration = static_cast<Duration>(n);
		assert(validInput(completed, Category::Usage));
		completed.name = Name::SimulationFailed;
		assert(validInput(completed, Category::Usage));
	}
	for (int n = 0; n < 4; ++n) {
		TelemetryEventInput exported;
		exported.name = Name::ExportCompleted;
		exported.exportKind = static_cast<ExportKind>(n);
		assert(validInput(exported, Category::Usage));
		exported.name = Name::ExportFailed;
		assert(validInput(exported, Category::Usage));
	}
	for (int operation = 0; operation < 3; ++operation)
		for (int error = 0; error < 4; ++error) {
			TelemetryEventInput failed;
			failed.name = Name::OperationFailed;
			failed.operation = static_cast<Operation>(operation);
			failed.error = static_cast<Error>(error);
			assert(validInput(failed, Category::Diagnostics));
			auto event = createEvent(failed, now);
			assert(!batch({event}, Category::Diagnostics, QString(), metadata, now).contains("installation_id"));
		}
	TelemetryEvent malformed;
	assert(!readEvent(QJsonObject{{QStringLiteral("event_id"), id}, {QStringLiteral("occurred_at"), QStringLiteral("2026-02-30T12:00:00Z")},
						  {QStringLiteral("name"), QStringLiteral("session.started")}, {QStringLiteral("properties"), QJsonObject{}}},
		Category::Usage, &malformed));
	const auto eventWithLf = QJsonObject{{QStringLiteral("event_id"), id + QStringLiteral("\n")},
		{QStringLiteral("occurred_at"), QStringLiteral("2026-04-01T12:00:00Z")},
		{QStringLiteral("name"), QStringLiteral("session.started")}, {QStringLiteral("properties"), QJsonObject{}}};
	assert(!readEvent(eventWithLf, Category::Usage, &malformed));
	auto timeWithLf = eventWithLf;
	timeWithLf.insert(QStringLiteral("event_id"), id);
	timeWithLf.insert(QStringLiteral("occurred_at"), QStringLiteral("2026-04-01T12:00:00Z\n"));
	assert(!readEvent(timeWithLf, Category::Usage, &malformed));
	TelemetryQueue queue;
	assert(queue.open(directory.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	TelemetryQueue follower;
	assert(follower.open(directory.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Follower);
	const auto usage = createEvent({}, now);
	TelemetryEventInput diagnosticsInput;
	diagnosticsInput.name = Name::OperationFailed;
	const auto diagnostic = createEvent(diagnosticsInput, now);
	assert(queue.enqueueUsage({usage, id}, now));
	assert(queue.enqueueDiagnostic({diagnostic, 2}, now));
	assert(queue.nextExpiryUtc() == now.addDays(7));
	assert(!queue.enqueueUsage({createEvent({}, now.addDays(-8)), id}, now));
	auto prepared = queue.prepareBatch(Category::Diagnostics, QStringLiteral("2"), metadata, now);
	assert(prepared.token.ids.size() == 1 && prepared.token.ids.first() == diagnostic.id);
	assert(!prepared.bytes.contains("installation_id") && !prepared.bytes.contains(id.toUtf8()));
	auto usageBatch = queue.prepareBatch(Category::Usage, id, metadata, now);
	assert(usageBatch.token.ids.size() == 1 && usageBatch.bytes.contains(id.toUtf8()));
	RetryState retry;
	retry.failures = 2;
	retry.nextEligibleUtc = now.addSecs(120);
	assert(queue.commitRetry(retry, now));
	queue.close();
	assert(follower.open(directory.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	assert(follower.retryState().failures == 2);
	assert(follower.prepareBatch(Category::Usage, id, metadata, now).token.ids.first() == usage.id);
	assert(!follower.discardBatch(usageBatch.token));
	assert(follower.discardBatch(follower.prepareBatch(Category::Usage, id, metadata, now).token));
	assert(follower.prepareBatch(Category::Usage, id, metadata, now).bytes.isEmpty());
	assert(follower.retireExactEndpoint(a));
	follower.close();
	assert(queue.open(directory.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Retired);
	queue.close();
	assert(queue.open(directory.path(), b, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	assert(queue.prepareBatch(Category::Diagnostics, QStringLiteral("2"), metadata, now).bytes.isEmpty());
	queue.close();
	assert(queue.open(directory.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Retired);
	queue.close();
	QTemporaryDir stamps;
	assert(queue.open(stamps.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	const QString oldId = QStringLiteral("23456789-1234-4234-8234-123456789abc");
	assert(queue.enqueueUsage({createEvent({}, now), oldId}, now));
	assert(queue.enqueueUsage({usage, id}, now));
	assert(queue.enqueueDiagnostic({createEvent(diagnosticsInput, now), 1}, now));
	assert(queue.enqueueDiagnostic({diagnostic, 2}, now));
	assert(!queue.purgeOtherStamps(Category::Usage, QStringLiteral("invalid")));
	assert(!queue.purgeOtherStamps(Category::Diagnostics, QStringLiteral("02")));
	assert(queue.purgeOtherStamps(Category::Usage, id));
	assert(queue.purgeOtherStamps(Category::Diagnostics, QStringLiteral("2")));
	queue.close();
	assert(queue.open(stamps.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	assert(queue.prepareBatch(Category::Usage, oldId, metadata, now).bytes.isEmpty());
	assert(queue.prepareBatch(Category::Diagnostics, QStringLiteral("1"), metadata, now).bytes.isEmpty());
	assert(queue.prepareBatch(Category::Usage, id, metadata, now).token.ids == QVector<QString>{usage.id});
	assert(queue.prepareBatch(Category::Diagnostics, QStringLiteral("2"), metadata, now).token.ids == QVector<QString>{diagnostic.id});
	queue.close();
	QTemporaryDir corrupt;
	assert(corrupt.isValid());
	QFile bad(corrupt.path() + QStringLiteral("/control.json"));
	assert(bad.open(QIODevice::WriteOnly));
	assert(bad.write("{") == 1);
	bad.close();
	assert(queue.open(corrupt.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Disabled);
	queue.close();
	QTemporaryDir rollback;
	assert(rollback.isValid());
	assert(queue.open(rollback.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	assert(queue.enqueueUsage({usage, id}, now));
	assert(queue.prune(now.addSecs(-1)));
	assert(queue.prepareBatch(Category::Usage, id, metadata, now).bytes.isEmpty());
	assert(queue.retryState().nextEligibleUtc == now.addSecs(-1).addSecs(86400));
	queue.close();
	QTemporaryDir checkpoint;
	assert(queue.open(checkpoint.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	const auto checkpointPath = checkpoint.filePath(QStringLiteral("control.json"));
	auto lastSeen = [&]() {
		QFile file(checkpointPath);
		assert(file.open(QIODevice::ReadOnly));
		return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("last_seen")).toString();
	};
	const auto initialCheckpoint = lastSeen();
	assert(queue.prune(now.addSecs(30)) && lastSeen() == initialCheckpoint);
	assert(queue.prune(now.addSecs(60)) && lastSeen() == now.addSecs(60).toString(Qt::ISODateWithMs));
	queue.close();
	QTemporaryDir expired;
	assert(queue.open(expired.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	assert(queue.enqueueUsage({usage, id}, now));
	assert(queue.prune(now.addDays(7)));
	assert(!queue.nextExpiryUtc().isValid());
	assert(queue.prepareBatch(Category::Usage, id, metadata, now.addDays(7)).bytes.isEmpty());
	queue.close();
	QTemporaryDir malformedCategory;
	assert(queue.open(malformedCategory.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	queue.close();
	QFile damaged(malformedCategory.path() + QStringLiteral("/usage.json"));
	assert(damaged.open(QIODevice::WriteOnly));
	assert(damaged.write("truncated") == 9);
	damaged.close();
	assert(queue.open(malformedCategory.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	assert(queue.prepareBatch(Category::Usage, id, metadata, now).bytes.isEmpty());
	queue.close();
	assert(QFile::remove(malformedCategory.path() + QStringLiteral("/control.json")));
	assert(queue.open(malformedCategory.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Disabled);
	queue.close();
	QTemporaryDir malformedStamp;
	assert(queue.open(malformedStamp.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	assert(queue.enqueueUsage({usage, id}, now));
	queue.close();
	QFile stampFile(malformedStamp.filePath(QStringLiteral("usage.json")));
	assert(stampFile.open(QIODevice::ReadOnly));
	auto categoryObject = QJsonDocument::fromJson(stampFile.readAll()).object();
	stampFile.close();
	auto entries = categoryObject.value(QStringLiteral("events")).toArray();
	auto badEntry = entries.first().toObject();
	badEntry.insert(QStringLiteral("stamp"), id + QStringLiteral("\n"));
	entries[0] = badEntry;
	categoryObject.insert(QStringLiteral("events"), entries);
	QSaveFile stampReplacement(stampFile.fileName());
	assert(stampReplacement.open(QIODevice::WriteOnly));
	assert(stampReplacement.write(QJsonDocument(categoryObject).toJson(QJsonDocument::Compact)) > 0);
	assert(stampReplacement.commit());
	assert(queue.open(malformedStamp.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	assert(queue.prepareBatch(Category::Usage, id, metadata, now).bytes.isEmpty());
	queue.close();
	QTemporaryDir oversized;
	QFile huge(oversized.path() + QStringLiteral("/control.json"));
	assert(huge.open(QIODevice::WriteOnly));
	assert(huge.write(QByteArray(65537, 'x')) == 65537);
	huge.close();
	assert(queue.open(oversized.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Disabled);
	queue.close();
	QTemporaryDir bounded;
	assert(queue.open(bounded.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	for (int i = 0; i < 520; ++i) assert(queue.enqueueUsage({createEvent({}, now.addMSecs(i)), id}, now.addSecs(1)));
	auto first = queue.prepareBatch(Category::Usage, id, metadata, now.addSecs(1));
	assert(first.token.ids.size() == 50 && first.bytes.size() <= 65536);
	QFile usageFile(bounded.path() + QStringLiteral("/usage.json"));
	assert(usageFile.size() <= 192 * 1024 && usageFile.open(QIODevice::ReadOnly));
	const auto stored = QJsonDocument::fromJson(usageFile.readAll()).object().value(QStringLiteral("events")).toArray();
	assert(stored.size() == 512
		&& stored.first().toObject().value(QStringLiteral("event")).toObject().value(QStringLiteral("occurred_at")).toString()
			== QStringLiteral("2026-04-01T12:00:00.008Z"));
	usageFile.close();
	assert(queue.enqueueUsage({createEvent({}, now.addMSecs(10)), id}, now.addSecs(1)));
	assert(usageFile.open(QIODevice::ReadOnly));
	const auto afterOldest = QJsonDocument::fromJson(usageFile.readAll()).object().value(QStringLiteral("events")).toArray();
	assert(afterOldest.size() == 512
		&& afterOldest.first().toObject().value(QStringLiteral("event")).toObject().value(QStringLiteral("occurred_at")).toString()
			== QStringLiteral("2026-04-01T12:00:00.009Z"));
	usageFile.close();
	for (int i = 0; i < 520; ++i)
		assert(queue.enqueueDiagnostic({createEvent(diagnosticsInput, now.addMSecs(i)), 2}, now.addSecs(1)));
	QFile diagnosticsFile(bounded.path() + QStringLiteral("/diagnostics.json"));
	assert(diagnosticsFile.size() <= 192 * 1024 && diagnosticsFile.open(QIODevice::ReadOnly));
	const auto retainedDiagnostics = QJsonDocument::fromJson(diagnosticsFile.readAll()).object().value(QStringLiteral("events")).toArray();
	assert(retainedDiagnostics.size() == 512);
	assert(
		retainedDiagnostics.first().toObject().value(QStringLiteral("event")).toObject().value(QStringLiteral("occurred_at")).toString()
		== QStringLiteral("2026-04-01T12:00:00.008Z"));
	diagnosticsFile.close();
	qint64 totalBytes = 0;
	for (const auto& file : QDir(bounded.path()).entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot))
		totalBytes += file.size();
	assert(totalBytes <= 1024 * 1024);
	QFile controlFile(bounded.path() + QStringLiteral("/control.json"));
	assert(controlFile.size() <= 64 * 1024);
	assert(queue.discardBatch(first.token));
	assert(queue.prepareBatch(Category::Usage, id, metadata, now.addSecs(1)).token.ids.size() == 50);
	queue.close();
	QTemporaryDir interrupted;
	QFile orphan(interrupted.path() + QStringLiteral("/usage.json.ABCDEF"));
	assert(orphan.open(QIODevice::WriteOnly));
	assert(orphan.write("incomplete") == 10);
	orphan.close();
	assert(queue.open(interrupted.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	assert(queue.enqueueUsage({usage, id}, now));
	queue.close();
	assert(queue.open(interrupted.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	assert(queue.prepareBatch(Category::Usage, id, metadata, now).token.ids.first() == usage.id);
#ifdef EGTRAIN_TELEMETRY_TEST_HOOK
	queue.failNextWriteForTesting();
	assert(!queue.enqueueUsage({createEvent({}, now), id}, now));
	assert(!queue.healthy());
#endif
	queue.close();
	QTemporaryDir oversizedTemporary;
	QFile leftover(oversizedTemporary.filePath(QStringLiteral("usage.json.XXXXXX")));
	assert(leftover.open(QIODevice::WriteOnly));
	assert(leftover.write(QByteArray(1024 * 1024, 'x')) == 1024 * 1024);
	leftover.close();
	assert(queue.open(oversizedTemporary.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Disabled);
	queue.close();
	QTemporaryDir crashRetirement;
	assert(queue.open(crashRetirement.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	assert(queue.enqueueUsage({usage, id}, now));
	assert(queue.enqueueDiagnostic({diagnostic, 2}, now));
	// Simulate termination after the retirement control commit but before either category replacement.
	QFile control(crashRetirement.path() + QStringLiteral("/control.json"));
	assert(control.open(QIODevice::ReadOnly));
	auto controlObject = QJsonDocument::fromJson(control.readAll()).object();
	control.close();
	controlObject.insert(QStringLiteral("retired"), QJsonArray{a});
	QSaveFile retirement(crashRetirement.path() + QStringLiteral("/control.json"));
	assert(retirement.open(QIODevice::WriteOnly));
	assert(retirement.write(QJsonDocument(controlObject).toJson(QJsonDocument::Compact)) > 0);
	assert(retirement.commit());
	queue.close();
	assert(queue.open(crashRetirement.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Retired);
	queue.close();
	for (const QString& name : {QStringLiteral("usage.json"), QStringLiteral("diagnostics.json")}) {
		QFile file(crashRetirement.filePath(name));
		assert(file.open(QIODevice::ReadOnly));
		assert(QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("events")).toArray().isEmpty());
	}
	assert(queue.open(crashRetirement.path(), b, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	assert(queue.prepareBatch(Category::Usage, id, metadata, now).bytes.isEmpty());
	assert(queue.enqueueUsage({createEvent({}, now), id}, now));
	queue.close();
	assert(queue.open(crashRetirement.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Retired);
	queue.close();
	assert(queue.open(crashRetirement.path(), b, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	assert(queue.prepareBatch(Category::Usage, id, metadata, now).bytes.isEmpty());
	queue.close();
	QTemporaryDir ledger;
	assert(queue.open(ledger.path(), a, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	for (int i = 0; i < 16; ++i) {
		const QString endpoint = QStringLiteral("https://example.test/") + QString::number(i);
		queue.close();
		assert(queue.open(ledger.path(), endpoint, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
		assert(queue.retireExactEndpoint(endpoint));
	}
	queue.close();
	assert(queue.open(ledger.path(), b, QStringLiteral("1"), now) == TelemetryQueue::OpenResult::Owner);
	assert(!queue.retireExactEndpoint(b));
	assert(!queue.healthy());
	queue.close();
	return 0;
}
