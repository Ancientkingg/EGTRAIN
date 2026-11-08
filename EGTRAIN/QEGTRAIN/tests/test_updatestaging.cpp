#include "update/WindowsStaging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

static bool writeFile(const QString& path, const QByteArray& contents) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

static QByteArray readFile(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return file.readAll();
}

static void populateInstallation(const QString& directory, const char* marker,
	const char* executableContents) {
	QDir().mkpath(QDir(directory).filePath(QStringLiteral("platforms")));
	QDir().mkpath(QDir(directory).filePath(QStringLiteral("imageformats")));
	QDir().mkpath(QDir(directory).filePath(QStringLiteral("Scenes")));
	for (const QString& name : WindowsStaging::requiredRuntimeFiles())
		writeFile(QDir(directory).filePath(name), executableContents);
	writeFile(QDir(directory).filePath(QStringLiteral("libzmq-mt-4_3_5.dll")), executableContents);
	writeFile(QDir(directory).filePath(QStringLiteral("Scenes/marker.txt")), marker);
}

static QString missingMessage(const QString& name) {
	return QStringLiteral("The Windows update package is missing a required file: %1.")
		.arg(name);
}

static const QString kLibzmq = QStringLiteral("libzmq-mt-4_3_5.dll");

class TestUpdateStaging : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void buildsStageFromPackage();
	void acceptsCompletePackage();
	void namesMissingFile_data();
	void namesMissingFile();
	void namesFirstMissingFileInFloorOrder();
	void matchesLibzmqByPattern();
	void checksManifestList();
	void rejectsPackageWithoutExecutable();

private:
	QString stage(const QStringList& removed, const QStringList& manifestFiles, const QStringList& added = {});

	QTemporaryDir m_temp;
	int m_packageCount = 0;
};

void TestUpdateStaging::initTestCase() {
	QVERIFY(m_temp.isValid());
}

// Stages a fresh copy of the fixture package after the given changes and
// returns the staging error, or a null string when staging succeeded.
QString TestUpdateStaging::stage(const QStringList& removed, const QStringList& manifestFiles,
	const QStringList& added) {
	const QString name = QStringLiteral("package-%1").arg(++m_packageCount);
	const QString package = QDir(m_temp.path()).filePath(name);
	populateInstallation(package, "new", "new");
	for (const QString& path : removed) {
		const QString full = QDir(package).filePath(path);
		if (QFileInfo(full).isDir())
			QDir(full).removeRecursively();
		else
			QFile::remove(full);
	}
	for (const QString& path : added) {
		QDir().mkpath(QFileInfo(QDir(package).filePath(path)).absolutePath());
		writeFile(QDir(package).filePath(path), "new");
	}
	QString stageError;
	const bool built = WindowsStaging::buildStage(package,
		QDir(m_temp.path()).filePath(name + QStringLiteral("-staged")), manifestFiles, &stageError);
	return built ? QString() : stageError;
}

void TestUpdateStaging::buildsStageFromPackage() {
	const QString current = QDir(m_temp.path()).filePath(QStringLiteral("current"));
	populateInstallation(current, "old", "old");
	QVERIFY2(writeFile(QDir(current).filePath(QStringLiteral("obsolete-runtime-file.dll")),
				 "old-dll"),
		"old installation fixture is writable");

	const QString extract = QDir(m_temp.path()).filePath(QStringLiteral("extract"));
	populateInstallation(extract, "new", "new");

	QString error;
	const QString staged = QDir(m_temp.path()).filePath(QStringLiteral("QEGTRAIN"));
	QVERIFY2(!QFileInfo(staged).exists(), "staging starts without an installation");
	QVERIFY2(WindowsStaging::buildStage(extract, staged, {}, &error),
		"staging builds from the extracted release package");
	// The new package owns the shipped Scenes.
	QCOMPARE(readFile(QDir(staged).filePath(QStringLiteral("Scenes/marker.txt"))), QByteArray("new"));
	QVERIFY2(!QFileInfo(QDir(staged).filePath(
							QStringLiteral("obsolete-runtime-file.dll")))
				 .exists(),
		"stale old installation files are dropped");
	// Staged runtime files come from the new package.
	QCOMPARE(readFile(QDir(staged).filePath(QStringLiteral("QEGTRAIN.exe"))), QByteArray("new"));

	error.clear();
	// Staging refuses to merge into an existing installation.
	QVERIFY(!WindowsStaging::buildStage(extract, staged, {}, &error));
	QCOMPARE(error, QStringLiteral("The Windows update staging location is not empty."));
}

void TestUpdateStaging::acceptsCompletePackage() {
	QVERIFY2(stage({}, {}).isNull(), "a complete package passes the runtime floor");
}

void TestUpdateStaging::namesMissingFile_data() {
	QTest::addColumn<QString>("removed");
	QTest::addColumn<QString>("reported");
	QTest::newRow("libzmq*.dll") << kLibzmq << QStringLiteral("libzmq*.dll");
	QTest::newRow("Scenes") << QStringLiteral("Scenes") << QStringLiteral("Scenes");
	for (const QString& name : WindowsStaging::requiredRuntimeFiles()) {
		if (name != QStringLiteral("QEGTRAIN.exe"))
			QTest::newRow(qPrintable(name)) << name << name;
	}
}

void TestUpdateStaging::namesMissingFile() {
	QFETCH(QString, removed);
	QFETCH(QString, reported);
	// Staging names the missing file.
	QCOMPARE(stage({removed}, {}), missingMessage(reported));
}

void TestUpdateStaging::namesFirstMissingFileInFloorOrder() {
	// Staging names the first missing file in floor order.
	QCOMPARE(stage({QStringLiteral("Qt6Gui.dll"), QStringLiteral("Qt6Core.dll")}, {}),
		missingMessage(QStringLiteral("Qt6Core.dll")));
}

void TestUpdateStaging::matchesLibzmqByPattern() {
	QVERIFY2(stage({kLibzmq}, {}, {QStringLiteral("libzmq-mt-5_0_0.dll")}).isNull(),
		"the libzmq pattern accepts another version name");
	// The libzmq pattern needs the libzmq prefix.
	QCOMPARE(stage({kLibzmq}, {}, {QStringLiteral("zmq.dll")}), missingMessage(QStringLiteral("libzmq*.dll")));
}

void TestUpdateStaging::checksManifestList() {
	const QStringList listed = {QStringLiteral("QEGTRAIN.exe"), QStringLiteral("d3dcompiler_47.dll"),
		QStringLiteral("styles/qmodernwindowsstyle.dll")};
	QVERIFY2(stage({}, listed, {QStringLiteral("d3dcompiler_47.dll"), QStringLiteral("styles/qmodernwindowsstyle.dll")}).isNull(),
		"a manifest list passes when every file is staged");
	// Staging names the first manifest file that is missing.
	QCOMPARE(stage({}, listed, {QStringLiteral("d3dcompiler_47.dll")}),
		missingMessage(QStringLiteral("styles/qmodernwindowsstyle.dll")));
	// A manifest entry must be a file.
	QCOMPARE(stage({}, {QStringLiteral("Scenes")}), missingMessage(QStringLiteral("Scenes")));
}

void TestUpdateStaging::rejectsPackageWithoutExecutable() {
	const QString noExecutable = QDir(m_temp.path()).filePath(QStringLiteral("no-executable"));
	QDir().mkpath(noExecutable);
	QString error;
	// Staging rejects a package without QEGTRAIN.exe.
	QVERIFY(!WindowsStaging::buildStage(noExecutable,
		QDir(m_temp.path()).filePath(QStringLiteral("staged-no-executable")), {}, &error));
	QCOMPARE(error, QStringLiteral("The Windows update package does not contain QEGTRAIN.exe."));
}

QTEST_GUILESS_MAIN(TestUpdateStaging)
#include "test_updatestaging.moc"
