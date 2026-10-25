#include "app/DialogFolders.h"

#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

namespace {
const QString kDocuments = QStringLiteral("C:/Users/me/Documents");
const QString kHome = QStringLiteral("C:/Users/me");
const QString kProgram = QStringLiteral("C:/Program Files/EGTRAIN");
} // namespace

// The file dialogs start in the folder of the case, unless the case is one of the installed examples, and never in the
// installation folder.
class TestDialogFolders : public QObject {
	Q_OBJECT

private slots:
	void startFolder_data();
	void startFolder();
	void openStartFolder_data();
	void openStartFolder();
	void relativePathsAndWorkingDirectory();
};

void TestDialogFolders::startFolder_data() {
	QTest::addColumn<QString>("casePath");
	QTest::addColumn<QString>("applicationDir");
	QTest::addColumn<QString>("documents");
	QTest::addColumn<QString>("home");
	QTest::addColumn<QString>("expected");

	// Windows paths: backslashes, another letter case, dots and the prefix of long paths.
	QTest::newRow("installed scene with backslashes")
		<< QStringLiteral("C:\\Program Files\\EGTRAIN\\Scenes\\Paimpol") << kProgram << kDocuments << kHome << kDocuments;
	QTest::newRow("installed scene in another letter case")
		<< QStringLiteral("c:\\program files\\egtrain\\scenes\\paimpol") << kProgram << kDocuments << kHome << kDocuments;
	QTest::newRow("installed scene with the installation folder in backslashes")
		<< QStringLiteral("C:\\PROGRAM FILES\\EGTRAIN\\SCENES\\Paimpol") << QStringLiteral("c:\\Program Files\\EGTRAIN") << kDocuments << kHome
		<< kDocuments;
	QTest::newRow("installed bundle") << QStringLiteral("C:\\Program Files\\EGTRAIN\\Scenes\\sample.egscene") << kProgram << kDocuments << kHome
									  << kDocuments;
	QTest::newRow("installed scene through dots")
		<< QStringLiteral("C:\\Program Files\\EGTRAIN\\Scenes\\..\\Scenes\\Paimpol") << kProgram << kDocuments << kHome << kDocuments;
	QTest::newRow("installed scene with the long path prefix")
		<< QStringLiteral("\\\\?\\C:\\Program Files\\EGTRAIN\\Scenes\\Paimpol") << kProgram << kDocuments << kHome << kDocuments;
	QTest::newRow("bundle next to the program") << QStringLiteral("C:\\Program Files\\EGTRAIN\\my.egscene") << kProgram << kDocuments << kHome
												<< kDocuments;
	QTest::newRow("installation folder") << QStringLiteral("C:\\Program Files\\EGTRAIN\\") << kProgram << kDocuments << kHome << kDocuments;
	QTest::newRow("installation folder with a trailing slash")
		<< QStringLiteral("C:/Program Files/EGTRAIN/Scenes/Paimpol") << QStringLiteral("C:/Program Files/EGTRAIN/") << kDocuments << kHome
		<< kDocuments;

	// A case outside the installation: its folder.
	QTest::newRow("bundle outside the installation")
		<< QStringLiteral("D:\\Cases\\Mine\\case.egscene") << kProgram << kDocuments << kHome << QStringLiteral("D:/Cases/Mine");
	QTest::newRow("scene folder outside the installation")
		<< QStringLiteral("d:\\cases\\mine\\Scene Folder") << kProgram << kDocuments << kHome << QStringLiteral("d:/cases/mine");
	QTest::newRow("prefix sibling of the installation") << QStringLiteral("C:\\Program Files\\EGTRAIN-copy\\x.egscene") << kProgram << kDocuments
														<< kHome << QStringLiteral("C:/Program Files/EGTRAIN-copy");
	QTest::newRow("case reached through dots") << QStringLiteral("C:\\Program Files\\EGTRAIN\\..\\Cases\\x.egscene") << kProgram << kDocuments
											   << kHome << QStringLiteral("C:/Program Files/Cases");
	QTest::newRow("bundle at the root of a drive") << QStringLiteral("D:\\case.egscene") << kProgram << kDocuments << kHome << QStringLiteral("D:/");

	// No case, and no Documents folder.
	QTest::newRow("no case") << QString() << kProgram << kDocuments << kHome << kDocuments;
	QTest::newRow("no case and no Documents") << QString() << kProgram << QString() << kHome << kHome;
	QTest::newRow("installed scene and no Documents")
		<< QStringLiteral("C:\\Program Files\\EGTRAIN\\Scenes\\Paimpol") << kProgram << QString() << kHome << kHome;
	QTest::newRow("unknown installation folder") << QStringLiteral("D:\\Cases\\x.egscene") << QString() << kDocuments << kHome
												 << QStringLiteral("D:/Cases");

	// The folders of the other layouts: a macOS bundle and a Linux prefix.
	QTest::newRow("scene in the bundle resources")
		<< QStringLiteral("/Applications/EGTRAIN.app/Contents/Resources/Scenes/Paimpol") << QStringLiteral("/Applications/EGTRAIN.app/Contents/MacOS")
		<< QStringLiteral("/Users/me/Documents") << QStringLiteral("/Users/me") << QStringLiteral("/Users/me/Documents");
	QTest::newRow("scene in the Linux share folder") << QStringLiteral("/usr/share/EGTRAIN/Scenes/Paimpol") << QStringLiteral("/usr/bin")
													 << QStringLiteral("/home/me/Documents") << QStringLiteral("/home/me")
													 << QStringLiteral("/home/me/Documents");
	QTest::newRow("bundle outside the Linux prefix") << QStringLiteral("/home/me/cases/x.egscene") << QStringLiteral("/usr/bin")
													 << QStringLiteral("/home/me/Documents") << QStringLiteral("/home/me")
													 << QStringLiteral("/home/me/cases");
}

void TestDialogFolders::startFolder() {
	QFETCH(QString, casePath);
	QFETCH(QString, applicationDir);
	QFETCH(QString, documents);
	QFETCH(QString, home);
	QFETCH(QString, expected);
	QCOMPARE(dialog_folders::startFolder(casePath, applicationDir, documents, home), expected);
}

// The Open dialogs: the folder above the open case, with no case the installed examples, otherwise the home folder.
void TestDialogFolders::openStartFolder_data() {
	QTest::addColumn<QString>("installedScenes");
	QTest::addColumn<QString>("expected");
	QTest::newRow("open dialog with no case starts in the installed examples")
		<< QStringLiteral("/Applications/EGTRAIN.app/Contents/Resources/Scenes") << QStringLiteral("/Applications/EGTRAIN.app/Contents/Resources/Scenes");
	QTest::newRow("open dialog with no installed examples") << QString() << QStringLiteral("/Users/me");
}

void TestDialogFolders::openStartFolder() {
	QFETCH(QString, installedScenes);
	QFETCH(QString, expected);
	QCOMPARE(dialog_folders::openStartFolder({}, installedScenes, QStringLiteral("/Users/me")), expected);
}

// A relative case path and the working directory of a Start Menu launch, which is the installation folder.
void TestDialogFolders::relativePathsAndWorkingDirectory() {
	QTemporaryDir temp;
	QVERIFY2(temp.isValid(), "cannot create temporary directory");
	const QString installation = temp.path() + "/installation";
	QVERIFY2(QDir().mkpath(installation + "/Scenes/Paimpol"), "cannot create installation");
	QVERIFY2(QDir().mkpath(temp.path() + "/cases/mine"), "cannot create cases");
	// Installed examples beside the program.
	QCOMPARE(dialog_folders::installedScenesFolder(installation), QDir::cleanPath(installation + "/Scenes"));
	// No installed examples.
	QCOMPARE(dialog_folders::installedScenesFolder(temp.path() + "/cases"), QString());
	// The Open dialog beside the case, with an absolute case path of this system: on Windows QFileInfo puts a drive in
	// front of a path without one.
	const QString caseFolder = QDir::cleanPath(temp.path() + "/cases");
	QCOMPARE(dialog_folders::openStartFolder(caseFolder + "/x.egscene", "/Applications/EGTRAIN.app/Contents/Resources/Scenes", "/Users/me"), caseFolder);
	const QString before = QDir::currentPath();
	QVERIFY2(QDir::setCurrent(installation), "cannot change the working directory");
	const auto restoreWorkingDirectory = qScopeGuard([&before] { QDir::setCurrent(before); });
	// Relative installed scene.
	QCOMPARE(dialog_folders::startFolder("Scenes/Paimpol", installation, kDocuments, kHome), kDocuments);
	const QString start = dialog_folders::startFolder();
	QVERIFY2(!dialog_folders::inInstallation(start, installation), "default start folder is inside the installation");
	QVERIFY2(QDir::cleanPath(start) != QDir::cleanPath(QDir::currentPath()), "default start folder is the working directory");
	QVERIFY2(dialog_folders::suggestedPath("run_summary.csv").endsWith("/run_summary.csv"), "suggested path lacks the file name");
	// Suggested path beside the case.
	QCOMPARE(QFileInfo(dialog_folders::suggestedPath("a.csv", temp.path() + "/cases/mine/case.egscene")).path(),
		QDir::cleanPath(temp.path() + "/cases/mine"));
	// Folder when there is no file name.
	QCOMPARE(dialog_folders::suggestedPath({}, temp.path() + "/cases/mine/case.egscene"), QDir::cleanPath(temp.path() + "/cases/mine"));
}

QTEST_GUILESS_MAIN(TestDialogFolders)
#include "test_dialogfolders.moc"
