#include "editors/TrainUnitSourceLinker.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <functional>
#include <string>
#include <utility>
#include <vector>

// The question of the linker is a real message box. This answers it with the button of that name as soon as it is
// shown, after running `beforeAnswer`. `prompted` tells whether a question was asked. A question that nobody expects
// is answered too, so that a wrong test fails and does not wait for ever.
class DialogAnswerer {
public:
	explicit DialogAnswerer(QString choice, std::function<void()> beforeAnswer = {})
		: m_choice(std::move(choice)), m_beforeAnswer(std::move(beforeAnswer)) {
		QObject::connect(&m_timer, &QTimer::timeout, &m_timer, [this] { answer(); });
		m_timer.start(20);
	}

	bool prompted = false;
	QString text;
	QString informativeText;

private:
	void answer() {
		for (QWidget* widget : QApplication::topLevelWidgets()) {
			auto* dialog = qobject_cast<QMessageBox*>(widget);
			if (!dialog || !dialog->isVisible())
				continue;
			prompted = true;
			text = dialog->text();
			informativeText = dialog->informativeText();
			if (m_beforeAnswer)
				m_beforeAnswer();
			for (QAbstractButton* button : dialog->buttons()) {
				if (button->text() == m_choice)
					button->click();
			}
			m_timer.stop();
			return;
		}
	}

	QString m_choice;
	std::function<void()> m_beforeAnswer;
	QTimer m_timer;
};

// A parameter file: the nine values of a rolling stock unit. Only the length varies.
static QByteArray physicalFile(double length) {
	return QByteArray("151000 0 0 36.111111111111 0.75 1.45 0.004 0.75 ") + QByteArray::number(length) + "\n";
}

static QByteArray tractionFile(double effort) {
	return QByteArray("0 40 ") + QByteArray::number(effort) + " 0 0\n";
}

static bool writeFile(const QString& path, const QByteArray& bytes) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

static SceneTrainUnit trainUnit(const char* id) {
	SceneTrainUnit unit;
	unit.id = id;
	unit.hasPhysical = true;
	unit.physical.mass_of_traction_unit_kg = 80000.0;
	unit.physical.max_speed_ms = 30.0;
	unit.physical.length_m = 60.0;
	unit.tractionCurve.push_back({0.0, 30.0, 100000.0, 0.0, 0.0});
	return unit;
}

static SceneModel unitScene() {
	SceneModel scene;
	scene.name = "Units";
	scene.trainUnits = {trainUnit("u1"), trainUnit("u2")};
	return scene;
}

// What the linker reports.
struct Events {
	int statusChanged = 0;
	int linksCleared = 0;
	int commitRequests = 0;
	int commitAllRequests = 0;
	QStringList messages;
	std::vector<QStringList> reloaded;
	std::vector<std::string> documentChanges;

	Events(TrainUnitSourceLinker& linker, SceneDocument& document) {
		QObject::connect(&linker, &TrainUnitSourceLinker::statusChanged, &linker, [this] { ++statusChanged; });
		QObject::connect(&linker, &TrainUnitSourceLinker::linksCleared, &linker, [this] { ++linksCleared; });
		QObject::connect(&linker, &TrainUnitSourceLinker::commitPendingEditsRequested, &linker, [this] { ++commitRequests; });
		QObject::connect(
			&linker, &TrainUnitSourceLinker::commitAllPendingEditsRequested, &linker, [this] { ++commitAllRequests; });
		QObject::connect(&linker, &TrainUnitSourceLinker::statusMessage, &linker,
			[this](const QString& message, int) { messages << message; });
		QObject::connect(&linker, &TrainUnitSourceLinker::reloaded, &linker,
			[this](const QStringList& units) { reloaded.push_back(units); });
		QObject::connect(&document, &SceneDocument::changed, &linker,
			[this](SceneDocument::ChangeKinds) { documentChanges.push_back("changed"); });
	}
};

static const SceneTrainUnit& unitOf(const SceneDocument& document, std::size_t index) {
	return document.model().trainUnits[index];
}

// The linker keeps train units linked to their parameter and traction files, takes a changed file into the unit, asks
// when the file differs from a hand edit, and waits while a run uses the model.
class TestTrainUnitSourceLinker : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void withoutDocumentLinksNothing();
	void linksAndReloadsFiles();
	void handEditRaisesQuestion();
	void changeDuringRunWaits();
	void linkDuringRunWaits();
	void changeDuringRunNeedsAgreement();
	void sharedFileAndUnlinking();
	void otherSceneDropsLinks();
	void questionTakesEveryPendingEdit();

private:
	QTemporaryDir m_directory;
	QString m_physicalPath;
	QString m_tractionPath;
	QString m_otherPath;
};

void TestTrainUnitSourceLinker::initTestCase() {
	QVERIFY2(m_directory.isValid(), "a folder for the source files could be made");
	m_physicalPath = m_directory.filePath("physical.txt");
	m_tractionPath = m_directory.filePath("traction.txt");
	m_otherPath = m_directory.filePath("other.txt");
}

void TestTrainUnitSourceLinker::withoutDocumentLinksNothing() {
	// without a document nothing is linked
	TrainUnitSourceLinker linker;
	writeFile(m_physicalPath, physicalFile(71.0));
	linker.linkFile("u1", false, m_physicalPath);
	linker.processFile(m_physicalPath, false);
	linker.scheduleChange(m_physicalPath);
	// A linker without a document links nothing.
	QVERIFY(linker.linkOf("u1") == nullptr);
	QVERIFY(linker.watchedFiles().isEmpty());
	QVERIFY(!linker.hasPendingChanges());
}

void TestTrainUnitSourceLinker::linksAndReloadsFiles() {
	const QString& physicalPath = m_physicalPath;
	const QString& tractionPath = m_tractionPath;
	const QString& otherPath = m_otherPath;
	SceneDocument document;
	TrainUnitSourceLinker linker;
	linker.setDocument(&document);
	Events events(linker, document);
	writeFile(physicalPath, physicalFile(71.0));
	linker.linkFile("u1", false, physicalPath);
	// A linker whose document holds no scene links nothing.
	QVERIFY(linker.linkOf("u1") == nullptr);
	QCOMPARE(events.statusChanged, 0);
	document.replace(unitScene(), false);
	events.statusChanged = 0;

	// link
	linker.linkFile("nothing", false, physicalPath);
	QVERIFY2(linker.linkOf("nothing") == nullptr, "a unit that does not exist cannot be linked");
	const quint64 revision = document.revision();
	linker.linkFile("u1", false, physicalPath);
	const TrainUnitSourceLinker::Link* link = linker.linkOf("u1");
	// A linked file is remembered with a status that says it waits for the reload.
	if (link == nullptr)
		QFAIL("a linked file is remembered with a status that says it waits for the reload");
	QCOMPARE(link->dataPath, physicalPath);
	QVERIFY(link->tractionPath.isEmpty());
	QCOMPARE(link->dataStatus, QStringLiteral("Linked"));
	QVERIFY(!link->dataDeferred);
	// The values of the unit when it was linked are the accepted ones.
	QVERIFY(link->acceptedPhysical.has_value());
	QCOMPARE_EQ(link->acceptedPhysical->length_m, 60.0);
	// Linking reports a new status and does not change the document.
	QCOMPARE(events.statusChanged, 1);
	QCOMPARE(document.revision(), revision);
	QVERIFY(!document.isDirty());
	// The file and its folder are watched and the file waits to be taken.
	QVERIFY(linker.watchedFiles().contains(physicalPath));
	QVERIFY(linker.watchedDirectories().contains(m_directory.path()));
	QVERIFY(linker.hasPendingChanges());

	// the first reload
	linker.processChanges();
	// Pending edits are taken into the model before the file is compared with it.
	QCOMPARE(events.commitRequests, 1);
	// The values of the file replace those of the linked unit and no other.
	QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 71.0);
	QCOMPARE_EQ(unitOf(document, 0).physical.max_speed_ms, 36.111111111111);
	QCOMPARE_EQ(unitOf(document, 0).physical.mass_of_traction_unit_kg, 151000.0);
	QVERIFY(unitOf(document, 0).hasPhysical);
	QCOMPARE_EQ(unitOf(document, 1).physical.length_m, 60.0);
	// A reload that changes a unit marks the document changed once.
	QVERIFY(document.isDirty());
	QCOMPARE(document.revision(), revision + 1);
	QCOMPARE(document.lastChangeLabel(), QStringLiteral("Reload linked source"));
	QCOMPARE(events.documentChanges.size(), std::size_t(1));
	link = linker.linkOf("u1");
	// The status says the file was reloaded.
	if (link == nullptr)
		QFAIL("the status says the file was reloaded");
	QCOMPARE(link->dataStatus, QStringLiteral("Reloaded"));
	QVERIFY(!linker.hasPendingChanges());
	// A reload of the parameters reports no change of a traction curve.
	QCOMPARE(events.reloaded.size(), std::size_t(1));
	QVERIFY(events.reloaded[0].isEmpty());

	// an unchanged file
	document.markSaved();
	linker.retry("u1", false);
	// A retry waits to be taken.
	QCOMPARE(linker.linkOf("u1")->dataStatus, QStringLiteral("Retrying"));
	QVERIFY(linker.hasPendingChanges());
	linker.processChanges();
	// The same values again leave the document alone.
	QCOMPARE(linker.linkOf("u1")->dataStatus, QStringLiteral("Up to date"));
	QVERIFY(!document.isDirty());
	QCOMPARE(document.revision(), revision + 1);
	// A file is reported as handled even when nothing changed.
	QCOMPARE(events.reloaded.size(), std::size_t(2));

	// a changed file
	writeFile(physicalPath, physicalFile(72.0));
	linker.scheduleChange(physicalPath);
	// A change of the file waits for the debounce before it is taken.
	QVERIFY(linker.hasPendingChanges());
	QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 71.0);
	linker.processChanges();
	// A changed file is taken into the unit.
	QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 72.0);
	QCOMPARE(linker.linkOf("u1")->dataStatus, QStringLiteral("Reloaded"));
	QVERIFY(document.isDirty());
	writeFile(physicalPath, physicalFile(73.0));
	linker.scheduleChange(m_directory.path());
	linker.processChanges();
	// A change of the folder of a linked file counts as a change of the file.
	QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 73.0);
	linker.scheduleChange(otherPath);
	QVERIFY2(!linker.hasPendingChanges(), "a change of a file that is not linked is ignored");

	// the traction file
	writeFile(tractionPath, tractionFile(210000.0));
	linker.linkFile("u1", true, tractionPath);
	linker.processChanges();
	// A traction file replaces the curve of the unit.
	QCOMPARE(unitOf(document, 0).tractionCurve.size(), std::size_t(1));
	QCOMPARE_EQ(unitOf(document, 0).tractionCurve[0][2], 210000.0);
	QCOMPARE_EQ(unitOf(document, 0).tractionCurve[0][1], 40.0);
	QCOMPARE(linker.linkOf("u1")->tractionStatus, QStringLiteral("Reloaded"));
	// A changed traction curve is reported with its unit.
	QCOMPARE(events.reloaded.back(), QStringList({"u1"}));
	// The parameter link of the unit is not touched by the traction file.
	QCOMPARE(linker.linkOf("u1")->dataPath, physicalPath);
	QCOMPARE(linker.linkOf("u1")->dataStatus, QStringLiteral("Reloaded"));

	// rejected content
	events.messages.clear();
	const double acceptedEffort = unitOf(document, 0).tractionCurve[0][2];
	writeFile(tractionPath, "0 40 220000 0 0\npartial\n");
	linker.scheduleChange(tractionPath);
	linker.processChanges();
	// A malformed file does not replace accepted values.
	QCOMPARE_EQ(unitOf(document, 0).tractionCurve[0][2], acceptedEffort);
	QVERIFY(linker.linkOf("u1")->tractionStatus.startsWith("Not loaded: "));
	// A rejected file is reported to the user.
	QCOMPARE(events.messages.size(), 1);
	QVERIFY(events.messages[0].startsWith("Could not reload the file: "));
	const quint64 revisionAfterRejected = document.revision();
	linker.scheduleChange(tractionPath);
	linker.processChanges();
	// A rejected file is not tried again until it changes.
	QCOMPARE(document.revision(), revisionAfterRejected);
	QCOMPARE(events.messages.size(), 1);

	// the source disappears and comes back
	QFile::remove(tractionPath);
	linker.scheduleChange(tractionPath);
	linker.processChanges();
	// A source that is gone leaves the accepted values and says it was rejected.
	QCOMPARE_EQ(unitOf(document, 0).tractionCurve[0][2], acceptedEffort);
	QVERIFY(linker.linkOf("u1")->tractionStatus.startsWith("Not loaded: "));
	writeFile(tractionPath, tractionFile(230000.0));
	linker.scheduleChange(tractionPath);
	linker.processChanges();
	// A source that comes back is taken.
	QCOMPARE_EQ(unitOf(document, 0).tractionCurve[0][2], 230000.0);
	QCOMPARE(linker.linkOf("u1")->tractionStatus, QStringLiteral("Reloaded"));

	// a source that was never there
	linker.linkFile("u2", false, otherPath);
	linker.processChanges();
	// A linked file that does not exist is rejected and changes nothing.
	QVERIFY(linker.linkOf("u2")->dataStatus.startsWith("Not loaded: "));
	QCOMPARE_EQ(unitOf(document, 1).physical.length_m, 60.0);
}

// a hand edit that differs from the file
void TestTrainUnitSourceLinker::handEditRaisesQuestion() {
	const QString& physicalPath = m_physicalPath;
	SceneDocument document;
	TrainUnitSourceLinker linker;
	linker.setDocument(&document);
	Events events(linker, document);
	document.replace(unitScene(), false);
	writeFile(physicalPath, physicalFile(71.0));
	linker.linkFile("u1", false, physicalPath);
	linker.processChanges();
	// The file is taken as the starting point.
	QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 71.0);

	{
		DialogAnswerer answer("Keep Edits");
		writeFile(physicalPath, physicalFile(72.0));
		linker.scheduleChange(physicalPath);
		linker.processChanges();
		// A changed file is taken without a question when nothing was edited by hand.
		QVERIFY(!answer.prompted);
		QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 72.0);
	}
	document.mutableModel().trainUnits[0].physical.length_m = 99.0;
	document.markChanged(SceneDocument::TrainUnits);
	{
		DialogAnswerer answer("Keep Edits");
		writeFile(physicalPath, physicalFile(74.0));
		linker.scheduleChange(physicalPath);
		linker.processChanges();
		// A changed file that differs from a hand edit raises a question that names the unit.
		QVERIFY(answer.prompted);
		QVERIFY(answer.text.contains("parameter file"));
		QVERIFY(answer.text.contains("u1"));
		QVERIFY(answer.informativeText.contains("You also edited u1 here"));
		// Answering Keep Edits leaves the hand edit.
		QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 99.0);
		QCOMPARE(linker.linkOf("u1")->dataStatus, QStringLiteral("Kept your edits"));
	}
	{
		DialogAnswerer answer("Keep Edits");
		linker.scheduleChange(physicalPath);
		linker.processChanges();
		// A file that was kept out is not asked about again until it changes.
		QVERIFY(!answer.prompted);
		QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 99.0);
	}
	{
		DialogAnswerer answer("Reload");
		linker.retry("u1", false);
		linker.processChanges();
		// A retry asks again, and answering Reload takes the file.
		QVERIFY(answer.prompted);
		QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 74.0);
		QCOMPARE(linker.linkOf("u1")->dataStatus, QStringLiteral("Reloaded"));
	}
	document.mutableModel().trainUnits[0].physical.length_m = 75.0;
	document.markChanged(SceneDocument::TrainUnits);
	{
		DialogAnswerer answer("Keep Edits");
		writeFile(physicalPath, physicalFile(75.0));
		linker.scheduleChange(physicalPath);
		linker.processChanges();
		// A hand edit that equals the file is no conflict.
		QVERIFY(!answer.prompted);
		QCOMPARE(linker.linkOf("u1")->dataStatus, QStringLiteral("Up to date"));
	}

	// the scene changes while the question is open
	document.mutableModel().trainUnits[0].physical.length_m = 99.0;
	document.markChanged(SceneDocument::TrainUnits);
	{
		DialogAnswerer answer("Reload", [&document] { document.markChanged(SceneDocument::Services); });
		writeFile(physicalPath, physicalFile(76.0));
		linker.scheduleChange(physicalPath);
		linker.processChanges();
		// An answer to a question about an older scene is not applied.
		QVERIFY(answer.prompted);
		QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 99.0);
		QVERIFY2(linker.hasPendingChanges(), "the file is queued again after a stale answer");
	}

	// a link that is made again while the question is open
	{
		DialogAnswerer answer("Reload", [&linker] { linker.unlink("u1", false); });
		linker.processChanges();
		// An answer about a link that has been removed is not applied.
		QVERIFY(answer.prompted);
		QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 99.0);
		QVERIFY(linker.linkOf("u1") == nullptr);
	}
}

// a run uses the model
void TestTrainUnitSourceLinker::changeDuringRunWaits() {
	const QString& physicalPath = m_physicalPath;
	SceneDocument document;
	TrainUnitSourceLinker linker;
	linker.setDocument(&document);
	Events events(linker, document);
	document.replace(unitScene(), false);
	writeFile(physicalPath, physicalFile(71.0));
	linker.linkFile("u1", false, physicalPath);
	linker.processChanges();
	const std::size_t reloadedBeforeRun = events.reloaded.size();

	document.setEditable(false);
	writeFile(physicalPath, physicalFile(80.0));
	linker.scheduleChange(physicalPath);
	// A change during a run is marked.
	QCOMPARE(linker.linkOf("u1")->dataStatus, QStringLiteral("Changed. Reloads after the run"));
	QVERIFY(linker.linkOf("u1")->dataDeferred);
	linker.processChanges();
	linker.processFile(physicalPath, false);
	// Nothing is taken while a run uses the model, and the change is kept.
	QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 71.0);
	QCOMPARE(events.reloaded.size(), reloadedBeforeRun);
	QVERIFY(linker.hasPendingChanges());

	// the file is the same again when the run ends
	writeFile(physicalPath, physicalFile(71.0));
	document.setEditable(true);
	{
		DialogAnswerer answer("Keep Edits");
		linker.processChanges();
		// A file that is the same again after the run needs no question.
		QVERIFY(!answer.prompted);
		QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 71.0);
		QCOMPARE(linker.linkOf("u1")->dataStatus, QStringLiteral("Up to date"));
		QVERIFY(!linker.linkOf("u1")->dataDeferred);
	}
}

// a link that is made during a run
void TestTrainUnitSourceLinker::linkDuringRunWaits() {
	const QString& physicalPath = m_physicalPath;
	SceneDocument document;
	TrainUnitSourceLinker linker;
	linker.setDocument(&document);
	document.replace(unitScene(), false);
	document.setEditable(false);
	writeFile(physicalPath, physicalFile(71.0));
	linker.linkFile("u1", false, physicalPath);
	// A file that is linked during a run waits for the end of the run.
	QCOMPARE(linker.linkOf("u1")->dataStatus, QStringLiteral("Linked. Loads after the run"));
	QVERIFY(linker.linkOf("u1")->dataDeferred);
	linker.retry("u1", false);
	// A retry during a run waits for the end of the run.
	QCOMPARE(linker.linkOf("u1")->dataStatus, QStringLiteral("Retries after the run"));
	QVERIFY(linker.linkOf("u1")->dataDeferred);
	linker.processChanges();
	// Nothing is taken while a run uses the model.
	QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 60.0);
	document.setEditable(true);
	{
		DialogAnswerer answer("Reload");
		linker.processChanges();
		// A file that was linked during a run is taken after the run when the user agrees.
		QVERIFY(answer.prompted);
		QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 71.0);
	}
}

// a change during a run is only taken when the user agrees
void TestTrainUnitSourceLinker::changeDuringRunNeedsAgreement() {
	const QString& physicalPath = m_physicalPath;
	SceneDocument document;
	TrainUnitSourceLinker linker;
	linker.setDocument(&document);
	document.replace(unitScene(), false);
	writeFile(physicalPath, physicalFile(71.0));
	linker.linkFile("u1", false, physicalPath);
	linker.processChanges();
	document.setEditable(false);
	writeFile(physicalPath, physicalFile(81.0));
	linker.scheduleChange(physicalPath);
	document.setEditable(true);
	{
		DialogAnswerer answer("Keep Edits");
		linker.processChanges();
		// A file that changed during a run raises a question, and Keep Edits leaves the unit.
		QVERIFY(answer.prompted);
		QCOMPARE(answer.informativeText, QStringLiteral("It changed during the run. Reload it or keep your edits?"));
		QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 71.0);
	}
	{
		DialogAnswerer answer("Reload");
		linker.retry("u1", false);
		linker.processChanges();
		// A retry after the run takes the file without a question.
		QVERIFY(!answer.prompted);
		QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 81.0);
	}
}

// two units share a file
void TestTrainUnitSourceLinker::sharedFileAndUnlinking() {
	const QString& physicalPath = m_physicalPath;
	const QString& tractionPath = m_tractionPath;
	SceneDocument document;
	TrainUnitSourceLinker linker;
	linker.setDocument(&document);
	document.replace(unitScene(), false);
	writeFile(physicalPath, physicalFile(71.0));
	linker.linkFile("u1", false, physicalPath);
	linker.linkFile("u2", false, physicalPath);
	linker.processChanges();
	// A file that two units are linked to is taken into both.
	QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 71.0);
	QCOMPARE_EQ(unitOf(document, 1).physical.length_m, 71.0);
	linker.unlink("u1", false);
	// Unlinking one unit keeps the watch of the file for the other.
	QVERIFY(linker.linkOf("u1") == nullptr);
	QVERIFY(linker.linkOf("u2") != nullptr);
	QVERIFY(linker.watchedFiles().contains(physicalPath));
	linker.unlink("u1", false);
	linker.unlink("nothing", true);
	linker.unlink("u2", true);
	// Unlinking something that is not linked changes nothing.
	if (linker.linkOf("u2") == nullptr)
		QFAIL("unlinking something that is not linked changes nothing");
	QCOMPARE(linker.linkOf("u2")->dataPath, physicalPath);
	linker.unlink("u2", false);
	// Unlinking the last link ends the watching.
	QVERIFY(linker.linkOf("u2") == nullptr);
	QVERIFY(linker.watchedFiles().isEmpty());
	QVERIFY(linker.watchedDirectories().isEmpty());

	// a link survives a rename and ends with the unit
	linker.linkFile("u1", false, physicalPath);
	linker.linkFile("u1", true, tractionPath);
	linker.unlink("u1", true);
	// Unlinking the traction keeps the link of the parameters.
	if (linker.linkOf("u1") == nullptr)
		QFAIL("unlinking the traction keeps the link of the parameters");
	QVERIFY(linker.linkOf("u1")->tractionPath.isEmpty());
	QCOMPARE(linker.linkOf("u1")->dataPath, physicalPath);
	linker.renameUnit("u1", "u1_renamed");
	// A link moves with its unit when the unit is renamed.
	QVERIFY(linker.linkOf("u1") == nullptr);
	if (linker.linkOf("u1_renamed") == nullptr)
		QFAIL("a link moves with its unit when the unit is renamed");
	QCOMPARE(linker.linkOf("u1_renamed")->dataPath, physicalPath);
	QVERIFY(linker.watchedFiles().contains(physicalPath));
	linker.forgetUnit("u1_renamed");
	// A link ends with its unit.
	QVERIFY(linker.linkOf("u1_renamed") == nullptr);
	QVERIFY(linker.watchedFiles().isEmpty());
}

// another scene drops the links
void TestTrainUnitSourceLinker::otherSceneDropsLinks() {
	const QString& physicalPath = m_physicalPath;
	SceneDocument document;
	TrainUnitSourceLinker linker;
	linker.setDocument(&document);
	Events events(linker, document);
	document.replace(unitScene(), false);
	writeFile(physicalPath, physicalFile(71.0));
	linker.linkFile("u1", false, physicalPath);
	// A link is made.
	QVERIFY(linker.hasPendingChanges());
	QVERIFY(!linker.watchedFiles().isEmpty());
	events.linksCleared = 0;
	document.replace(unitScene(), false);
	// Replacing the document drops the links, the pending changes and the watches.
	QVERIFY(linker.linkOf("u1") == nullptr);
	QVERIFY(!linker.hasPendingChanges());
	QVERIFY(linker.watchedFiles().isEmpty());
	QVERIFY(linker.watchedDirectories().isEmpty());
	QCOMPARE(events.linksCleared, 1);
	linker.processChanges();
	// Nothing is taken after the links are dropped.
	QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 60.0);

	// a linker that is detached no longer follows the document
	linker.linkFile("u1", false, physicalPath);
	linker.setDocument(nullptr);
	document.replace(unitScene(), false);
	QVERIFY2(linker.linkOf("u1") != nullptr, "a detached linker does not follow the document");
}

// what the linker takes before it compares and before it asks
void TestTrainUnitSourceLinker::questionTakesEveryPendingEdit() {
	const QString& physicalPath = m_physicalPath;
	SceneDocument document;
	TrainUnitSourceLinker linker;
	linker.setDocument(&document);
	Events events(linker, document);
	document.replace(unitScene(), false);
	// Another editor held text, which changes the scene when every pending edit is taken.
	QObject::connect(&linker, &TrainUnitSourceLinker::commitAllPendingEditsRequested, &linker,
		[&document] { document.markChanged(SceneDocument::Services, "Edit service"); });
	writeFile(physicalPath, physicalFile(71.0));
	linker.linkFile("u1", false, physicalPath);
	linker.processChanges();
	// A reload without a question asks for the edits of the units only.
	QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 71.0);
	QCOMPARE(events.commitRequests, 1);
	QCOMPARE(events.commitAllRequests, 0);

	writeFile(m_directory.filePath("notes.txt"), "notes\n");
	linker.scheduleChange(m_directory.path());
	linker.processChanges();
	// A change of another file in the folder asks for no edits.
	QCOMPARE(events.commitRequests, 1);
	QCOMPARE(events.commitAllRequests, 0);

	document.mutableModel().trainUnits[0].physical.length_m = 99.0;
	document.markChanged(SceneDocument::TrainUnits);
	{
		DialogAnswerer answer("Reload");
		writeFile(physicalPath, physicalFile(72.0));
		linker.scheduleChange(physicalPath);
		linker.processChanges();
		// The question about a hand edit asks for every pending edit first, and the change of the scene that this makes
		// does not make the answer stale.
		QVERIFY(answer.prompted);
		QCOMPARE(events.commitRequests, 2);
		QCOMPARE(events.commitAllRequests, 1);
		QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 72.0);
		QCOMPARE(linker.linkOf("u1")->dataStatus, QStringLiteral("Reloaded"));
	}

	document.setEditable(false);
	writeFile(physicalPath, physicalFile(73.0));
	linker.scheduleChange(physicalPath);
	document.setEditable(true);
	{
		DialogAnswerer answer("Reload");
		linker.processChanges();
		// So does the question about a change during a run.
		QVERIFY(answer.prompted);
		QCOMPARE(events.commitAllRequests, 2);
		QCOMPARE_EQ(unitOf(document, 0).physical.length_m, 73.0);
	}
}

QTEST_MAIN(TestTrainUnitSourceLinker)
#include "test_trainunitsourcelinker.moc"
