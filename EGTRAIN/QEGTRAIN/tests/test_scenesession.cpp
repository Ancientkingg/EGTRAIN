#include "app/SceneSession.h"

#include "editors/SceneDocument.h"
#include "scene/SceneBundle.h"
#include "scene/SceneModel.h"
#include "scene/SceneWriter.h"
#include "support/FakePrompts.h"

#include <QApplication>
#include <QByteArray>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <functional>
#include <string>

namespace {

// The window as the session sees it: no run, no startup timing and no dialogs.
class FakeHost : public SceneSession::Host {
public:
	telemetry::OperationObservation captureTelemetryOperation() const noexcept override { return {}; }
	bool dialogsSuppressed() const override { return true; }
	void commitPendingEdits() override {}
	bool afterRunStopped(std::function<void()>) override { return false; }
	void bringToFront() override {}
	void offerUpdateCheck() override {}
	QString recoveredSceneSource() const override { return {}; }
	SceneLoadResult readScene(const QString& path, bool& identityOk) override {
		identityOk = true;
		return loadScenePath(path.toStdString());
	}
	bool sceneOpened(const SceneSession::OpenedScene&) override { return true; }
};

// A session in a window of its own, whose questions the prompts answer. A save marks the document saved, as the window
// does.
struct Fixture {
	FakePrompts prompts;
	FakeHost host;
	SceneDocument document;
	QMainWindow window;
	SceneSession* session = new SceneSession(&window, &document, host);

	Fixture() {
		session->setPrompts(&prompts);
		QObject::connect(session, &SceneSession::sceneSaved, &document, [this] { document.markSaved(); });
	}
};

QByteArray readFile(const QString& path) {
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString& path, const QByteArray& bytes) {
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool appendToFile(const QString& path, const QByteArray& bytes) {
	QFile file(path);
	return file.open(QIODevice::Append) && file.write(bytes) == bytes.size();
}

// A legacy case of one track, one station, one route and one service in `folder`.
bool writeLegacyCase(const QDir& folder) {
	return writeFile(folder.filePath("TrackLines/B0/NodiCumPari.txt"), "0 0 0\n1 2 0\n")
		&& writeFile(folder.filePath("TrackLines/Stations.txt"), "0\tOrigin\n")
		&& writeFile(folder.filePath("Routes/Route0.txt"), "@0-B0@\n")
		&& writeFile(folder.filePath("trainNames.txt"), "Service.txt\n")
		&& writeFile(folder.filePath("Trains/Service.txt"),
			"Service 0 99999999 0 /Service.dat /Service-effort.dat /TimeTable/Service.txt\n")
		&& writeFile(folder.filePath("Service.dat"), "1 2 3 4 5 6 7 8 9\n")
		&& writeFile(folder.filePath("Service-effort.dat"), "0 1 2 3 4\n1 2 3 4 5\n")
		&& writeFile(folder.filePath("TimeTable/Service.txt"), "Origin 2 -1 10\n");
}

// A new case named `name`, as a folder at `path`.
bool writeFolderCase(const QString& path, const std::string& name) {
	SceneModel scene = makeNewSceneModel();
	scene.name = name;
	return saveScene(scene, path.toStdString()).success();
}

// A new case named `name`, as a bundle at `path`.
bool writeBundleCase(const QString& path, const std::string& name) {
	SceneModel scene = makeNewSceneModel();
	scene.name = name;
	return saveSceneBundle(scene, path.toStdString()).success();
}

// The name of the case in the scene folder or bundle at `path`, or an empty string when it cannot be read.
std::string caseName(const QString& path) {
	const SceneLoadResult loaded = loadScenePath(path.toStdString());
	return loaded.scene.name;
}

// An edit that leaves the document with unsaved changes.
void editDescription(SceneDocument& document, const std::string& description) {
	document.edit(SceneDocument::CaseSettings, "Edit case settings",
		[&description](SceneModel& scene) { scene.description = description; });
}

// Answers the next file dialog: a folder dialog with the folder `path`, a save dialog with the name of `path` typed into
// its name field in the folder of `path`.
void answerFileDialog(const QString& path) {
	auto* timer = new QTimer;
	timer->setInterval(10);
	QObject::connect(timer, &QTimer::timeout, timer, [timer, path] {
		for (QWidget* widget : QApplication::topLevelWidgets()) {
			auto* dialog = qobject_cast<QFileDialog*>(widget);
			if (dialog == nullptr || !dialog->isVisible())
				continue;
			timer->stop();
			timer->deleteLater();
			if (dialog->fileMode() == QFileDialog::Directory) {
				dialog->setDirectory(path);
				static_cast<QDialog*>(dialog)->done(QDialog::Accepted);
				return;
			}
			dialog->setDirectory(QFileInfo(path).absolutePath());
			if (auto* name = dialog->findChild<QLineEdit*>("fileNameEdit"))
				name->setText(QFileInfo(path).fileName());
			// Through QDialog, as QFileDialog::accept is protected.
			static_cast<QDialog*>(dialog)->accept();
			return;
		}
	});
	timer->start();
}

// Clicks Save in the next message box, the question about unsaved changes.
void answerSave() {
	auto* timer = new QTimer;
	timer->setInterval(10);
	QObject::connect(timer, &QTimer::timeout, timer, [timer] {
		for (QWidget* widget : QApplication::topLevelWidgets()) {
			auto* box = qobject_cast<QMessageBox*>(widget);
			if (box == nullptr || !box->isVisible() || box->button(QMessageBox::Save) == nullptr)
				continue;
			timer->stop();
			timer->deleteLater();
			box->button(QMessageBox::Save)->click();
			return;
		}
	});
	timer->start();
}

} // namespace

class TestSceneSession : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void importsIntoEmptyFolder();
	void importKeepsFolderWithOtherFiles();
	void importStopsAtFolderOfThatName();
	void importAsksBeforeReplacingCase();
	void saveAsFolderAsksBeforeReplacingCase();
	void saveAsFolderKeepsViewsOfFailedSave();
	void saveAsksWhenFolderChangedOnDisk();
	void saveAsksWhenBundleChangedOnDisk();
	void unsavedSaveStopsWhenCaseChangedOnDisk();
	void saveAsAsksBeforeSuffixReplacesFile();
	void saveAsAsksForFileOrFolder();

private:
	QTemporaryDir m_settingsDir;
};

void TestSceneSession::initTestCase() {
	QVERIFY(m_settingsDir.isValid());
	// The recent cases go to the settings of the test.
	QCoreApplication::setOrganizationName("EGTRAIN-test");
	QCoreApplication::setApplicationName("test_scenesession");
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
	QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, m_settingsDir.path());
}

void TestSceneSession::importsIntoEmptyFolder() {
	QTemporaryDir root;
	QVERIFY(root.isValid());
	const QDir dir(root.path());
	QVERIFY(writeLegacyCase(QDir(dir.filePath("Line"))));
	QVERIFY(dir.mkdir("Empty"));
	Fixture f;
	f.session->importLegacyCase(dir.filePath("Line"), dir.filePath("Empty"), {});
	// An empty folder takes the case without a question, and the case opens from it.
	QCOMPARE(f.prompts.confirmCount, 0);
	QVERIFY(QFileInfo::exists(dir.filePath("Empty/scene.json")));
	QCOMPARE(QFileInfo(f.session->sceneDir()).canonicalFilePath(), QFileInfo(dir.filePath("Empty")).canonicalFilePath());
}

// The import replaces the folder it writes with everything in it, so a folder with other files is not that folder.
void TestSceneSession::importKeepsFolderWithOtherFiles() {
	QTemporaryDir root;
	QVERIFY(root.isValid());
	const QDir dir(root.path());
	QVERIFY(writeLegacyCase(QDir(dir.filePath("Line"))));
	QVERIFY(writeFile(dir.filePath("Cases/notes.txt"), "notes"));
	QVERIFY(writeFile(dir.filePath("Cases/.hidden"), "hidden"));
	Fixture f;
	f.session->importLegacyCase(dir.filePath("Line"), dir.filePath("Cases"), {});
	// The user is offered a folder named after the case, and No writes nothing.
	QCOMPARE(f.prompts.confirmCount, 1);
	QCOMPARE(f.prompts.confirmTitle, "Import Legacy Case");
	QVERIFY2(f.prompts.confirmQuestion.contains("is not empty. Import the case into a new folder Line inside it?"),
		qPrintable(f.prompts.confirmQuestion));
	QCOMPARE(readFile(dir.filePath("Cases/notes.txt")), QByteArray("notes"));
	QCOMPARE(QDir(dir.filePath("Cases")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden),
		QStringList({".hidden", "notes.txt"}));
	QVERIFY(!f.document.isLoaded());

	f.prompts.confirmAnswer = true;
	f.session->importLegacyCase(dir.filePath("Line"), dir.filePath("Cases"), {});
	// Yes imports into that new folder and leaves the other files where they are.
	QVERIFY(QFileInfo::exists(dir.filePath("Cases/Line/scene.json")));
	QCOMPARE(readFile(dir.filePath("Cases/notes.txt")), QByteArray("notes"));
	QCOMPARE(readFile(dir.filePath("Cases/.hidden")), QByteArray("hidden"));
	QCOMPARE(QFileInfo(f.session->sceneDir()).canonicalFilePath(), QFileInfo(dir.filePath("Cases/Line")).canonicalFilePath());
}

void TestSceneSession::importStopsAtFolderOfThatName() {
	QTemporaryDir root;
	QVERIFY(root.isValid());
	const QDir dir(root.path());
	QVERIFY(writeLegacyCase(QDir(dir.filePath("Line"))));
	QVERIFY(writeFile(dir.filePath("Cases/notes.txt"), "notes"));
	QVERIFY(writeFile(dir.filePath("Cases/Line/draft.txt"), "draft"));
	Fixture f;
	f.prompts.confirmAnswer = true;
	f.session->importLegacyCase(dir.filePath("Line"), dir.filePath("Cases"), {});
	// A folder of the name of the case that holds files stops the import before anything is written.
	QCOMPARE(f.prompts.errorTitle, "Cannot Import Legacy Case");
	QVERIFY2(f.prompts.errorMessage.contains("already exists and is not empty"), qPrintable(f.prompts.errorMessage));
	QCOMPARE(QDir(dir.filePath("Cases/Line")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden),
		QStringList({"draft.txt"}));
	QCOMPARE(readFile(dir.filePath("Cases/notes.txt")), QByteArray("notes"));
	QVERIFY(!QFileInfo::exists(dir.filePath("Cases/scene.json")));
	QVERIFY(!f.document.isLoaded());
}

void TestSceneSession::importAsksBeforeReplacingCase() {
	QTemporaryDir root;
	QVERIFY(root.isValid());
	const QDir dir(root.path());
	QVERIFY(writeLegacyCase(QDir(dir.filePath("Line"))));
	QVERIFY(writeFolderCase(dir.filePath("Other"), "Other case"));
	QVERIFY(writeFile(dir.filePath("Other/notes.txt"), "notes"));
	const QByteArray before = readFile(dir.filePath("Other/scene.json"));
	Fixture f;
	f.session->importLegacyCase(dir.filePath("Line"), dir.filePath("Other"), {});
	// A folder that holds another case is replaced only after a question, and No keeps the case.
	QCOMPARE(f.prompts.confirmCount, 1);
	QCOMPARE(f.prompts.confirmTitle, "Import Legacy Case");
	QVERIFY2(f.prompts.confirmQuestion.contains("holds another case"), qPrintable(f.prompts.confirmQuestion));
	QCOMPARE(readFile(dir.filePath("Other/scene.json")), before);
	QCOMPARE(readFile(dir.filePath("Other/notes.txt")), QByteArray("notes"));
	QVERIFY(!f.document.isLoaded());

	f.prompts.confirmAnswer = true;
	f.session->importLegacyCase(dir.filePath("Line"), dir.filePath("Other"), {});
	// Yes replaces the folder with the imported case.
	QCOMPARE(caseName(dir.filePath("Other")), "Line");
	QVERIFY(!QFileInfo::exists(dir.filePath("Other/notes.txt")));
}

void TestSceneSession::saveAsFolderAsksBeforeReplacingCase() {
	QTemporaryDir root;
	QVERIFY(root.isValid());
	const QDir dir(root.path());
	QVERIFY(writeFolderCase(dir.filePath("Mine"), "My case"));
	QVERIFY(writeFolderCase(dir.filePath("Other"), "Other case"));
	const QByteArray before = readFile(dir.filePath("Other/scene.json"));
	Fixture f;
	QVERIFY(f.session->openSceneDirectory(dir.filePath("Mine")));
	const QString mine = f.session->sceneDir();

	answerFileDialog(dir.filePath("Other"));
	// Another case is replaced only after a question, and No keeps it.
	QVERIFY(!f.session->saveSceneAsToDirectory());
	QCOMPARE(f.prompts.confirmCount, 1);
	QCOMPARE(f.prompts.confirmTitle, "Save Case As Folder");
	QVERIFY2(f.prompts.confirmQuestion.contains("holds another case. Replace that case with this one?"),
		qPrintable(f.prompts.confirmQuestion));
	QCOMPARE(readFile(dir.filePath("Other/scene.json")), before);
	QCOMPARE(f.session->sceneDir(), mine);

	f.prompts.confirmAnswer = true;
	answerFileDialog(dir.filePath("Other"));
	// Yes writes the case there, and the folder becomes the case of the session.
	QVERIFY(f.session->saveSceneAsToDirectory());
	QCOMPARE(caseName(dir.filePath("Other")), "My case");
	QCOMPARE(QFileInfo(f.session->sceneDir()).canonicalFilePath(), QFileInfo(dir.filePath("Other")).canonicalFilePath());

	answerFileDialog(dir.filePath("Other"));
	// The folder of the open case is saved again without a question.
	QVERIFY(f.session->saveSceneAsToDirectory());
	QCOMPARE(f.prompts.confirmCount, 2);
}

// The save writes views.json from the model, so a save that fails leaves the views.json of the chosen folder alone.
void TestSceneSession::saveAsFolderKeepsViewsOfFailedSave() {
#ifdef Q_OS_WIN
	QSKIP("Windows has no folder that its owner cannot write to");
#else
	QTemporaryDir root;
	QVERIFY(root.isValid());
	const QDir dir(root.path());
	QVERIFY(writeFolderCase(dir.filePath("Mine"), "My case"));
	QVERIFY(writeFile(dir.filePath("Locked/Target/views.json"), "{\"own\": true}\n"));
	Fixture f;
	QVERIFY(f.session->openSceneDirectory(dir.filePath("Mine")));
	QVERIFY(writeFile(dir.filePath("Mine/views.json"), "{\"mine\": true}\n"));
	// The save stages the case next to the target, which a folder that cannot be written to refuses.
	const QString locked = dir.filePath("Locked");
	const QFileDevice::Permissions permissions = QFile::permissions(locked);
	QVERIFY(QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
	const auto restore = qScopeGuard([locked, permissions] { QFile::setPermissions(locked, permissions); });
	if (QFile(QDir(locked).filePath("probe")).open(QIODevice::WriteOnly))
		QSKIP("this process can write into a folder without write permission");

	f.prompts.confirmAnswer = true;
	answerFileDialog(dir.filePath("Locked/Target"));
	QVERIFY(!f.session->saveSceneAsToDirectory());
	// The save failed and said so, and the target keeps its own views.json.
	QCOMPARE(f.prompts.errorTitle, "Cannot Save Case");
	QCOMPARE(readFile(dir.filePath("Locked/Target/views.json")), QByteArray("{\"own\": true}\n"));
#endif
}

void TestSceneSession::saveAsksWhenFolderChangedOnDisk() {
	QTemporaryDir root;
	QVERIFY(root.isValid());
	const QDir dir(root.path());
	QVERIFY(writeFolderCase(dir.filePath("Mine"), "My case"));
	Fixture f;
	QVERIFY(f.session->openSceneDirectory(dir.filePath("Mine")));
	editDescription(f.document, "first");
	// A case that nobody else changed is saved without a question.
	QVERIFY(f.session->saveSceneToCurrentDir());
	QCOMPARE(f.prompts.confirmCount, 0);
	QVERIFY(!f.document.isDirty());

	const QString services = dir.filePath("Mine/services.json");
	QVERIFY(appendToFile(services, "\n"));
	const QByteArray edited = readFile(services);
	editDescription(f.document, "second");
	// A file changed on disk since the save is replaced only after a question, and No keeps it and the unsaved edit.
	QVERIFY(!f.session->saveSceneToCurrentDir());
	QCOMPARE(f.prompts.confirmCount, 1);
	QCOMPARE(f.prompts.confirmTitle, "Case Changed on Disk");
	QVERIFY2(f.prompts.confirmQuestion.contains("Replace the case on disk with this version?")
			&& f.prompts.confirmQuestion.contains("Save As"),
		qPrintable(f.prompts.confirmQuestion));
	QCOMPARE(readFile(services), edited);
	QVERIFY(f.document.isDirty());

	f.prompts.confirmAnswer = true;
	// Yes saves over it.
	QVERIFY(f.session->saveSceneToCurrentDir());
	QCOMPARE(f.prompts.confirmCount, 2);
	QVERIFY(readFile(services) != edited);
	QVERIFY(!f.document.isDirty());

	f.prompts.confirmAnswer = false;
	QVERIFY(QFile::remove(dir.filePath("Mine/scenarios.json")));
	editDescription(f.document, "third");
	// A scene file that is gone counts as a change too.
	QVERIFY(!f.session->saveSceneToCurrentDir());
	QCOMPARE(f.prompts.confirmCount, 3);
	QVERIFY(!QFileInfo::exists(dir.filePath("Mine/scenarios.json")));

	QVERIFY(writeFolderCase(dir.filePath("Fresh"), "Fresh case"));
	QVERIFY(f.session->openSceneDirectory(dir.filePath("Fresh")));
	QVERIFY(writeFile(dir.filePath("Fresh/passengers.json"), "{\"passengers\": []}\n"));
	editDescription(f.document, "fourth");
	// So does a scene file that was added.
	QVERIFY(!f.session->saveSceneToCurrentDir());
	QCOMPARE(f.prompts.confirmCount, 4);
}

void TestSceneSession::saveAsksWhenBundleChangedOnDisk() {
	QTemporaryDir root;
	QVERIFY(root.isValid());
	const QString bundle = QDir(root.path()).filePath("Mine.egscene");
	QVERIFY(writeBundleCase(bundle, "My case"));
	Fixture f;
	QVERIFY(f.session->openSceneDirectory(bundle));
	QVERIFY(f.session->sceneIsBundle());
	editDescription(f.document, "first");
	// A bundle that nobody else changed is saved without a question.
	QVERIFY(f.session->saveSceneToCurrentDir());
	QCOMPARE(f.prompts.confirmCount, 0);

	QVERIFY(writeBundleCase(bundle, "The case of another window"));
	const QByteArray replaced = readFile(bundle);
	editDescription(f.document, "second");
	// A bundle written by someone else since is replaced only after a question, and No keeps it.
	QVERIFY(!f.session->saveSceneToCurrentDir());
	QCOMPARE(f.prompts.confirmCount, 1);
	QCOMPARE(f.prompts.confirmTitle, "Case Changed on Disk");
	QCOMPARE(readFile(bundle), replaced);
	QVERIFY(f.document.isDirty());

	f.prompts.confirmAnswer = true;
	// Yes saves over it.
	QVERIFY(f.session->saveSceneToCurrentDir());
	QCOMPARE(caseName(bundle), "My case");
}

// The window closes, or opens another case, only when maybeSaveScene() returns true.
void TestSceneSession::unsavedSaveStopsWhenCaseChangedOnDisk() {
	QTemporaryDir root;
	QVERIFY(root.isValid());
	const QDir dir(root.path());
	QVERIFY(writeFolderCase(dir.filePath("Mine"), "My case"));
	Fixture f;
	QVERIFY(f.session->openSceneDirectory(dir.filePath("Mine")));
	QVERIFY(appendToFile(dir.filePath("Mine/services.json"), "\n"));
	editDescription(f.document, "mine");

	answerSave();
	// Save in the question about unsaved changes, then No to the case changed on disk, keeps the window and the edit.
	QVERIFY(!f.session->maybeSaveScene());
	QCOMPARE(f.prompts.confirmCount, 1);
	QCOMPARE(f.prompts.confirmTitle, "Case Changed on Disk");
	QVERIFY(f.document.isDirty());
}

// The save dialog has seen only the name without .egscene, so Save As asks before the name with it replaces a file.
void TestSceneSession::saveAsAsksBeforeSuffixReplacesFile() {
	QTemporaryDir root;
	QVERIFY(root.isValid());
	const QDir dir(root.path());
	QVERIFY(writeFolderCase(dir.filePath("Mine"), "My case"));
	QVERIFY(writeBundleCase(dir.filePath("Other.egscene"), "Other case"));
	const QByteArray before = readFile(dir.filePath("Other.egscene"));
	Fixture f;
	QVERIFY(f.session->openSceneDirectory(dir.filePath("Mine")));
	const QString mine = f.session->sceneDir();
	// Case File... in the question about the format.
	f.prompts.chosenButton = 0;

	answerFileDialog(dir.filePath("Other"));
	f.session->saveSceneAs();
	// The question names the file, and No writes nothing.
	QCOMPARE(f.prompts.confirmCount, 1);
	QCOMPARE(f.prompts.confirmTitle, "Save Case As");
	QCOMPARE(f.prompts.confirmQuestion, "Other.egscene already exists. Replace it?");
	QCOMPARE(readFile(dir.filePath("Other.egscene")), before);
	QCOMPARE(f.session->sceneDir(), mine);

	f.prompts.confirmAnswer = true;
	answerFileDialog(dir.filePath("Other"));
	f.session->saveSceneAs();
	// Yes replaces the file, which becomes the case of the session.
	QCOMPARE(caseName(dir.filePath("Other.egscene")), "My case");
	QCOMPARE(QFileInfo(f.session->sceneDir()).canonicalFilePath(), QFileInfo(dir.filePath("Other.egscene")).canonicalFilePath());

	answerFileDialog(dir.filePath("New"));
	f.session->saveSceneAs();
	// A name whose file does not exist yet is saved without a question.
	QCOMPARE(f.prompts.confirmCount, 2);
	QVERIFY(QFileInfo::exists(dir.filePath("New.egscene")));
}

// A save dialog chooses a file or a folder, so Save As asks for the format first, with the format of the case as the
// default answer, and then saves as the answer says.
void TestSceneSession::saveAsAsksForFileOrFolder() {
	QTemporaryDir root;
	QVERIFY(root.isValid());
	const QDir dir(root.path());
	QVERIFY(writeFolderCase(dir.filePath("Mine"), "My case"));
	Fixture f;
	QVERIFY(f.session->openSceneDirectory(dir.filePath("Mine")));

	f.prompts.chosenButton = 0;
	answerFileDialog(dir.filePath("Copy.egscene"));
	f.session->saveSceneAs();
	// The question offers both formats, with a case folder first for a folder, and Case File... saves a bundle.
	QCOMPARE(f.prompts.buttonCount, 1);
	QCOMPARE(f.prompts.buttonTitle, "Save As");
	QCOMPARE(f.prompts.offeredButtons, QStringList({"Case File...", "Case Folder..."}));
	QCOMPARE(f.prompts.defaultButton, 1);
	QCOMPARE(caseName(dir.filePath("Copy.egscene")), "My case");
	QCOMPARE(QFileInfo(f.session->sceneDir()).canonicalFilePath(), QFileInfo(dir.filePath("Copy.egscene")).canonicalFilePath());
	QVERIFY(f.session->sceneIsBundle());

	f.prompts.chosenButton = 1;
	QVERIFY(QDir().mkpath(dir.filePath("Copy")));
	answerFileDialog(dir.filePath("Copy"));
	f.session->saveSceneAs();
	// A bundle has a case file first, and Case Folder... saves a folder.
	QCOMPARE(f.prompts.buttonCount, 2);
	QCOMPARE(f.prompts.defaultButton, 0);
	QCOMPARE(caseName(dir.filePath("Copy")), "My case");
	QCOMPARE(QFileInfo(f.session->sceneDir()).canonicalFilePath(), QFileInfo(dir.filePath("Copy")).canonicalFilePath());
	QVERIFY(!f.session->sceneIsBundle());

	f.prompts.chosenButton = -1;
	editDescription(f.document, "not saved");
	const QByteArray folder = readFile(dir.filePath("Copy/scene.json"));
	f.session->saveSceneAs();
	// Cancel saves nothing.
	QCOMPARE(f.prompts.buttonCount, 3);
	QCOMPARE(f.prompts.defaultButton, 1);
	QVERIFY(f.document.isDirty());
	QCOMPARE(readFile(dir.filePath("Copy/scene.json")), folder);
	QCOMPARE(QFileInfo(f.session->sceneDir()).canonicalFilePath(), QFileInfo(dir.filePath("Copy")).canonicalFilePath());

	Fixture fresh;
	fresh.session->newScene();
	fresh.session->saveSceneAs();
	// A new case, which has no location, has a case file first.
	QCOMPARE(fresh.prompts.buttonCount, 1);
	QCOMPARE(fresh.prompts.defaultButton, 0);
	QVERIFY(fresh.session->sceneDir().isEmpty());
}

QTEST_MAIN(TestSceneSession)
#include "test_scenesession.moc"
