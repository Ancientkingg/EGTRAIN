#include "app/CannotOpenMessage.h"
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

QTEST_GUILESS_MAIN(TestCannotOpenMessage)
#include "test_cannotopenmessage.moc"
