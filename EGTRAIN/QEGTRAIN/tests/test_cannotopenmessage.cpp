#include "app/CannotOpenMessage.h"
#include "editors/EditorHelpers.h"
#include "scene/SceneCompatibility.h"
#include "scene/SceneModel.h"
#include "scene/SceneWriter.h"
#include "widgets/DisplayPath.h"

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

// The message for a case that cannot be opened names the file, says in a plain sentence what is wrong, and gives the
// internal error after that.
class TestCannotOpenMessage : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void damagedFile();
	void missingPath();
	void folders();
	void damagedSceneJson();
	void damagedScenarioFile();

private:
	QTemporaryDir m_temp;
};

void TestCannotOpenMessage::initTestCase() {
	QFile broken(QDir(m_temp.path()).filePath("broken.egscene"));
	QFile sceneJson(QDir(m_temp.path()).filePath("scene/scene.json"));
	QVERIFY2(m_temp.isValid(), "the test files could not be created");
	QVERIFY2(QDir(m_temp.path()).mkdir("plain"), "the test files could not be created");
	QVERIFY2(QDir(m_temp.path()).mkdir("scene"), "the test files could not be created");
	QVERIFY2(broken.open(QIODevice::WriteOnly), "the test files could not be created");
	broken.close();
	QVERIFY2(sceneJson.open(QIODevice::WriteOnly), "scene.json could not be created");
	sceneJson.close();
}

// The file name and a plain sentence come first, the internal error after them.
void TestCannotOpenMessage::damagedFile() {
	const QString brokenPath = QDir(m_temp.path()).filePath("broken.egscene");
	// Title.
	QCOMPARE(cannot_open::title(), QStringLiteral("Cannot Open Case"));
	// Text with an error.
	QCOMPARE(cannot_open::text(brokenPath, "error [scene.bundle.archive] failed finding central directory"),
		QStringLiteral("broken.egscene could not be opened.\nThe file is damaged or is not an EGTRAIN case.\n\n"
					   "error [scene.bundle.archive] failed finding central directory"));
	// Text without an error.
	QCOMPARE(cannot_open::text(brokenPath, {}), QStringLiteral("broken.egscene could not be opened.\nThe file is damaged or is not an EGTRAIN case."));
	QVERIFY2(!cannot_open::text(brokenPath, "x").contains(m_temp.path()), "the text shows the folder of the file");
}

// A path that cannot be found or read is not called damaged. A path without a file name is shown with the separators of
// the system.
void TestCannotOpenMessage::missingPath() {
	display_path::separator() = QLatin1Char('\\');
	const auto restoreSeparator = qScopeGuard([] { display_path::separator() = QDir::separator(); });
	// Text for a path that does not exist.
	QCOMPARE(cannot_open::text("/no/such/place/", {}), QStringLiteral("\\no\\such\\place\\ could not be opened.\nEGTRAIN could not read it."));
}

// A folder is not a damaged file: the sentence says what a case folder holds.
void TestCannotOpenMessage::folders() {
	// Text for a folder without scene.json.
	QCOMPARE(cannot_open::text(QDir(m_temp.path()).filePath("plain"), "error x"),
		QStringLiteral("plain could not be opened.\nThe folder holds no EGTRAIN case. A case folder holds a scene.json file.\n\nerror x"));
	// Text for a folder with a damaged scene.json.
	QCOMPARE(cannot_open::text(QDir(m_temp.path()).filePath("scene"), {}),
		QStringLiteral("scene could not be opened.\nThe folder holds a damaged or incomplete EGTRAIN case."));
}

// A scene.json that is cut off names its full path, the line and column and a fix, without the text of the parser.
void TestCannotOpenMessage::damagedSceneJson() {
	QVERIFY2(QDir(m_temp.path()).mkdir("cut"), "the folder could not be created");
	const QString folder = QDir(m_temp.path()).filePath("cut");
	QFile sceneJson(QDir(folder).filePath("scene.json"));
	QVERIFY2(sceneJson.open(QIODevice::WriteOnly), "scene.json could not be created");
	sceneJson.write("{\n  \"schema_version\": 2,\n  \"name\": ,\n");
	sceneJson.close();
	const SceneCompatibilityProbeResult probe = probeSceneCompatibility(folder.toStdString());
	QCOMPARE(probe.classification, SceneCompatibilityClass::Malformed);
	const QString text = cannot_open::text(folder, diagnosticsDialogText(probe.diagnostics));
	// The message, the fix and the file.
	QCOMPARE(text,
		QStringLiteral("cut could not be opened.\nThe folder holds a damaged or incomplete EGTRAIN case.\n\n"
					   "scene.json is not valid JSON (line 3, column 11).\n\n"
					   "Correct the file at that line or restore it from a copy.\nFile: ")
			+ display_path::format(QDir(folder).filePath("scene.json")));
	QVERIFY2(!text.contains("json.exception"), "the text shows the text of the parser");
}

// An import of a scenario file that is not JSON names the file.
void TestCannotOpenMessage::damagedScenarioFile() {
	QFile file(QDir(m_temp.path()).filePath("mine.json"));
	QVERIFY2(file.open(QIODevice::WriteOnly), "the scenario file could not be created");
	file.write("{ not json");
	file.close();
	const ScenarioLoadResult loaded = loadScenarioJson(file.fileName().toStdString());
	QVERIFY(!loaded.success());
	QCOMPARE(firstDiagnosticDialogText(loaded.diagnostics),
		QStringLiteral("mine.json is not valid JSON (line 1, column 4).\n\nCorrect the file at that line or restore it from a copy."));
}

QTEST_GUILESS_MAIN(TestCannotOpenMessage)
#include "test_cannotopenmessage.moc"
