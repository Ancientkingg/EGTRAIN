#include "editors/TrainUnitSourceLinker.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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
	QStringList messages;
	std::vector<QStringList> reloaded;
	std::vector<std::string> documentChanges;

	Events(TrainUnitSourceLinker& linker, SceneDocument& document) {
		QObject::connect(&linker, &TrainUnitSourceLinker::statusChanged, &linker, [this] { ++statusChanged; });
		QObject::connect(&linker, &TrainUnitSourceLinker::linksCleared, &linker, [this] { ++linksCleared; });
		QObject::connect(&linker, &TrainUnitSourceLinker::commitPendingEditsRequested, &linker, [this] { ++commitRequests; });
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

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = true;
	QTemporaryDir directory;
	ok &= expect(directory.isValid(), "a folder for the source files could be made");
	const QString physicalPath = directory.filePath("physical.txt");
	const QString tractionPath = directory.filePath("traction.txt");
	const QString otherPath = directory.filePath("other.txt");

	{
		// without a document nothing is linked
		TrainUnitSourceLinker linker;
		writeFile(physicalPath, physicalFile(71.0));
		linker.linkFile("u1", false, physicalPath);
		linker.processFile(physicalPath, false);
		linker.scheduleChange(physicalPath);
		ok &= expect(linker.linkOf("u1") == nullptr && linker.watchedFiles().isEmpty() && !linker.hasPendingChanges(),
			"a linker without a document links nothing");
	}

	{
		SceneDocument document;
		TrainUnitSourceLinker linker;
		linker.setDocument(&document);
		Events events(linker, document);
		writeFile(physicalPath, physicalFile(71.0));
		linker.linkFile("u1", false, physicalPath);
		ok &= expect(linker.linkOf("u1") == nullptr && events.statusChanged == 0,
			"a linker whose document holds no scene links nothing");
		document.replace(unitScene(), false);
		events.statusChanged = 0;

		// link
		linker.linkFile("nothing", false, physicalPath);
		ok &= expect(linker.linkOf("nothing") == nullptr, "a unit that does not exist cannot be linked");
		const quint64 revision = document.revision();
		linker.linkFile("u1", false, physicalPath);
		const TrainUnitSourceLinker::Link* link = linker.linkOf("u1");
		ok &= expect(link && link->dataPath == physicalPath && link->tractionPath.isEmpty()
				&& link->dataStatus == "Linked; waiting for reload" && !link->dataDeferred,
			"a linked file is remembered with a status that says it waits for the reload");
		ok &= expect(link && link->acceptedPhysical && link->acceptedPhysical->length_m == 60.0,
			"the values of the unit when it was linked are the accepted ones");
		ok &= expect(events.statusChanged == 1 && document.revision() == revision && !document.isDirty(),
			"linking reports a new status and does not change the document");
		ok &= expect(linker.watchedFiles().contains(physicalPath) && linker.watchedDirectories().contains(directory.path())
				&& linker.hasPendingChanges(),
			"the file and its folder are watched and the file waits to be taken");

		// the first reload
		linker.processChanges();
		ok &= expect(events.commitRequests == 1, "pending edits are taken into the model before the file is compared with it");
		ok &= expect(unitOf(document, 0).physical.length_m == 71.0 && unitOf(document, 0).physical.max_speed_ms == 36.111111111111
				&& unitOf(document, 0).physical.mass_of_traction_unit_kg == 151000.0 && unitOf(document, 0).hasPhysical
				&& unitOf(document, 1).physical.length_m == 60.0,
			"the values of the file replace those of the linked unit and no other");
		ok &= expect(document.isDirty() && document.revision() == revision + 1 && document.lastChangeLabel() == "Reload linked source"
				&& events.documentChanges.size() == 1,
			"a reload that changes a unit marks the document changed once");
		link = linker.linkOf("u1");
		ok &= expect(link && link->dataStatus == "Reloaded" && !linker.hasPendingChanges(), "the status says the file was reloaded");
		ok &= expect(events.reloaded.size() == 1 && events.reloaded[0].isEmpty(),
			"a reload of the parameters reports no change of a traction curve");

		// an unchanged file
		document.markSaved();
		linker.retry("u1", false);
		ok &= expect(linker.linkOf("u1")->dataStatus == "Retrying" && linker.hasPendingChanges(), "a retry waits to be taken");
		linker.processChanges();
		ok &= expect(linker.linkOf("u1")->dataStatus == "Up to date" && !document.isDirty() && document.revision() == revision + 1,
			"the same values again leave the document alone");
		ok &= expect(events.reloaded.size() == 2, "a file is reported as handled even when nothing changed");

		// a changed file
		writeFile(physicalPath, physicalFile(72.0));
		linker.scheduleChange(physicalPath);
		ok &= expect(linker.hasPendingChanges() && unitOf(document, 0).physical.length_m == 71.0,
			"a change of the file waits for the debounce before it is taken");
		linker.processChanges();
		ok &= expect(unitOf(document, 0).physical.length_m == 72.0 && linker.linkOf("u1")->dataStatus == "Reloaded" && document.isDirty(),
			"a changed file is taken into the unit");
		writeFile(physicalPath, physicalFile(73.0));
		linker.scheduleChange(directory.path());
		linker.processChanges();
		ok &= expect(unitOf(document, 0).physical.length_m == 73.0, "a change of the folder of a linked file counts as a change of the file");
		linker.scheduleChange(otherPath);
		ok &= expect(!linker.hasPendingChanges(), "a change of a file that is not linked is ignored");

		// the traction file
		writeFile(tractionPath, tractionFile(210000.0));
		linker.linkFile("u1", true, tractionPath);
		linker.processChanges();
		ok &= expect(unitOf(document, 0).tractionCurve.size() == 1 && unitOf(document, 0).tractionCurve[0][2] == 210000.0
				&& unitOf(document, 0).tractionCurve[0][1] == 40.0 && linker.linkOf("u1")->tractionStatus == "Reloaded",
			"a traction file replaces the curve of the unit");
		ok &= expect(events.reloaded.back() == QStringList({"u1"}), "a changed traction curve is reported with its unit");
		ok &= expect(linker.linkOf("u1")->dataPath == physicalPath && linker.linkOf("u1")->dataStatus == "Reloaded",
			"the parameter link of the unit is not touched by the traction file");

		// rejected content
		events.messages.clear();
		const double acceptedEffort = unitOf(document, 0).tractionCurve[0][2];
		writeFile(tractionPath, "0 40 220000 0 0\npartial\n");
		linker.scheduleChange(tractionPath);
		linker.processChanges();
		ok &= expect(unitOf(document, 0).tractionCurve[0][2] == acceptedEffort && linker.linkOf("u1")->tractionStatus.startsWith("Rejected")
				&& linker.linkOf("u1")->tractionStatus.endsWith("; Retry available"),
			"a malformed file does not replace accepted values");
		ok &= expect(events.messages.size() == 1 && events.messages[0].startsWith("Source reload rejected: "),
			"a rejected file is reported to the user");
		const quint64 revisionAfterRejected = document.revision();
		linker.scheduleChange(tractionPath);
		linker.processChanges();
		ok &= expect(document.revision() == revisionAfterRejected && events.messages.size() == 1,
			"a rejected file is not tried again until it changes");

		// the source disappears and comes back
		QFile::remove(tractionPath);
		linker.scheduleChange(tractionPath);
		linker.processChanges();
		ok &= expect(unitOf(document, 0).tractionCurve[0][2] == acceptedEffort && linker.linkOf("u1")->tractionStatus.startsWith("Rejected"),
			"a source that is gone leaves the accepted values and says it was rejected");
		writeFile(tractionPath, tractionFile(230000.0));
		linker.scheduleChange(tractionPath);
		linker.processChanges();
		ok &= expect(unitOf(document, 0).tractionCurve[0][2] == 230000.0 && linker.linkOf("u1")->tractionStatus == "Reloaded",
			"a source that comes back is taken");

		// a source that was never there
		linker.linkFile("u2", false, otherPath);
		linker.processChanges();
		ok &= expect(linker.linkOf("u2")->dataStatus.startsWith("Rejected") && unitOf(document, 1).physical.length_m == 60.0,
			"a linked file that does not exist is rejected and changes nothing");
	}

	{
		// a hand edit that differs from the file
		SceneDocument document;
		TrainUnitSourceLinker linker;
		linker.setDocument(&document);
		Events events(linker, document);
		document.replace(unitScene(), false);
		writeFile(physicalPath, physicalFile(71.0));
		linker.linkFile("u1", false, physicalPath);
		linker.processChanges();
		ok &= expect(unitOf(document, 0).physical.length_m == 71.0, "the file is taken as the starting point");

		{
			DialogAnswerer answer("Keep local");
			writeFile(physicalPath, physicalFile(72.0));
			linker.scheduleChange(physicalPath);
			linker.processChanges();
			ok &= expect(!answer.prompted && unitOf(document, 0).physical.length_m == 72.0,
				"a changed file is taken without a question when nothing was edited by hand");
		}
		document.mutableModel().trainUnits[0].physical.length_m = 99.0;
		document.markChanged(SceneDocument::TrainUnits);
		{
			DialogAnswerer answer("Keep local");
			writeFile(physicalPath, physicalFile(74.0));
			linker.scheduleChange(physicalPath);
			linker.processChanges();
			ok &= expect(answer.prompted && answer.text.contains("parameter source") && answer.text.contains("u1")
					&& answer.informativeText.contains("Local edits conflict in u1"),
				"a changed file that differs from a hand edit raises a question that names the unit");
			ok &= expect(unitOf(document, 0).physical.length_m == 99.0 && linker.linkOf("u1")->dataStatus == "Kept local; Retry available",
				"answering Keep local leaves the hand edit");
		}
		{
			DialogAnswerer answer("Keep local");
			linker.scheduleChange(physicalPath);
			linker.processChanges();
			ok &= expect(!answer.prompted && unitOf(document, 0).physical.length_m == 99.0,
				"a file that was kept out is not asked about again until it changes");
		}
		{
			DialogAnswerer answer("Reload");
			linker.retry("u1", false);
			linker.processChanges();
			ok &= expect(answer.prompted && unitOf(document, 0).physical.length_m == 74.0 && linker.linkOf("u1")->dataStatus == "Reloaded",
				"a retry asks again, and answering Reload takes the file");
		}
		document.mutableModel().trainUnits[0].physical.length_m = 75.0;
		document.markChanged(SceneDocument::TrainUnits);
		{
			DialogAnswerer answer("Keep local");
			writeFile(physicalPath, physicalFile(75.0));
			linker.scheduleChange(physicalPath);
			linker.processChanges();
			ok &= expect(!answer.prompted && linker.linkOf("u1")->dataStatus == "Up to date",
				"a hand edit that equals the file is no conflict");
		}

		// the scene changes while the question is open
		document.mutableModel().trainUnits[0].physical.length_m = 99.0;
		document.markChanged(SceneDocument::TrainUnits);
		{
			DialogAnswerer answer("Reload", [&document] { document.markChanged(SceneDocument::Services); });
			writeFile(physicalPath, physicalFile(76.0));
			linker.scheduleChange(physicalPath);
			linker.processChanges();
			ok &= expect(answer.prompted && unitOf(document, 0).physical.length_m == 99.0,
				"an answer to a question about an older scene is not applied");
			ok &= expect(linker.hasPendingChanges(), "the file is queued again after a stale answer");
		}

		// a link that is made again while the question is open
		{
			DialogAnswerer answer("Reload", [&linker] { linker.unlink("u1", false); });
			linker.processChanges();
			ok &= expect(answer.prompted && unitOf(document, 0).physical.length_m == 99.0 && linker.linkOf("u1") == nullptr,
				"an answer about a link that has been removed is not applied");
		}
	}

	{
		// a run uses the model
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
		ok &= expect(linker.linkOf("u1")->dataStatus == "Changed during run; pending reload" && linker.linkOf("u1")->dataDeferred,
			"a change during a run is marked");
		linker.processChanges();
		linker.processFile(physicalPath, false);
		ok &= expect(unitOf(document, 0).physical.length_m == 71.0 && events.reloaded.size() == reloadedBeforeRun
				&& linker.hasPendingChanges(),
			"nothing is taken while a run uses the model, and the change is kept");

		// the file is the same again when the run ends
		writeFile(physicalPath, physicalFile(71.0));
		document.setEditable(true);
		{
			DialogAnswerer answer("Keep local");
			linker.processChanges();
			ok &= expect(!answer.prompted && unitOf(document, 0).physical.length_m == 71.0
					&& linker.linkOf("u1")->dataStatus == "Up to date" && !linker.linkOf("u1")->dataDeferred,
				"a file that is the same again after the run needs no question");
		}
	}

	{
		// a link that is made during a run
		SceneDocument document;
		TrainUnitSourceLinker linker;
		linker.setDocument(&document);
		document.replace(unitScene(), false);
		document.setEditable(false);
		writeFile(physicalPath, physicalFile(71.0));
		linker.linkFile("u1", false, physicalPath);
		ok &= expect(linker.linkOf("u1")->dataStatus == "Linked; pending reload after run" && linker.linkOf("u1")->dataDeferred,
			"a file that is linked during a run waits for the end of the run");
		linker.retry("u1", false);
		ok &= expect(linker.linkOf("u1")->dataStatus == "Retry pending after run" && linker.linkOf("u1")->dataDeferred,
			"a retry during a run waits for the end of the run");
		linker.processChanges();
		ok &= expect(unitOf(document, 0).physical.length_m == 60.0, "nothing is taken while a run uses the model");
		document.setEditable(true);
		{
			DialogAnswerer answer("Reload");
			linker.processChanges();
			ok &= expect(answer.prompted && unitOf(document, 0).physical.length_m == 71.0,
				"a file that was linked during a run is taken after the run when the user agrees");
		}
	}

	{
		// a change during a run is only taken when the user agrees
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
			DialogAnswerer answer("Keep local");
			linker.processChanges();
			ok &= expect(answer.prompted && answer.informativeText.contains("during a run") && unitOf(document, 0).physical.length_m == 71.0,
				"a file that changed during a run raises a question, and Keep local leaves the unit");
		}
		{
			DialogAnswerer answer("Reload");
			linker.retry("u1", false);
			linker.processChanges();
			ok &= expect(!answer.prompted && unitOf(document, 0).physical.length_m == 81.0,
				"a retry after the run takes the file without a question");
		}
	}

	{
		// two units share a file
		SceneDocument document;
		TrainUnitSourceLinker linker;
		linker.setDocument(&document);
		document.replace(unitScene(), false);
		writeFile(physicalPath, physicalFile(71.0));
		linker.linkFile("u1", false, physicalPath);
		linker.linkFile("u2", false, physicalPath);
		linker.processChanges();
		ok &= expect(unitOf(document, 0).physical.length_m == 71.0 && unitOf(document, 1).physical.length_m == 71.0,
			"a file that two units are linked to is taken into both");
		linker.unlink("u1", false);
		ok &= expect(linker.linkOf("u1") == nullptr && linker.linkOf("u2") && linker.watchedFiles().contains(physicalPath),
			"unlinking one unit keeps the watch of the file for the other");
		linker.unlink("u1", false);
		linker.unlink("nothing", true);
		linker.unlink("u2", true);
		ok &= expect(linker.linkOf("u2") && linker.linkOf("u2")->dataPath == physicalPath,
			"unlinking something that is not linked changes nothing");
		linker.unlink("u2", false);
		ok &= expect(linker.linkOf("u2") == nullptr && linker.watchedFiles().isEmpty() && linker.watchedDirectories().isEmpty(),
			"unlinking the last link ends the watching");

		// a link survives a rename and ends with the unit
		linker.linkFile("u1", false, physicalPath);
		linker.linkFile("u1", true, tractionPath);
		linker.unlink("u1", true);
		ok &= expect(linker.linkOf("u1") && linker.linkOf("u1")->tractionPath.isEmpty() && linker.linkOf("u1")->dataPath == physicalPath,
			"unlinking the traction keeps the link of the parameters");
		linker.renameUnit("u1", "u1_renamed");
		ok &= expect(linker.linkOf("u1") == nullptr && linker.linkOf("u1_renamed")
				&& linker.linkOf("u1_renamed")->dataPath == physicalPath && linker.watchedFiles().contains(physicalPath),
			"a link moves with its unit when the unit is renamed");
		linker.forgetUnit("u1_renamed");
		ok &= expect(linker.linkOf("u1_renamed") == nullptr && linker.watchedFiles().isEmpty(),
			"a link ends with its unit");
	}

	{
		// another scene drops the links
		SceneDocument document;
		TrainUnitSourceLinker linker;
		linker.setDocument(&document);
		Events events(linker, document);
		document.replace(unitScene(), false);
		writeFile(physicalPath, physicalFile(71.0));
		linker.linkFile("u1", false, physicalPath);
		ok &= expect(linker.hasPendingChanges() && !linker.watchedFiles().isEmpty(), "a link is made");
		events.linksCleared = 0;
		document.replace(unitScene(), false);
		ok &= expect(linker.linkOf("u1") == nullptr && !linker.hasPendingChanges() && linker.watchedFiles().isEmpty()
				&& linker.watchedDirectories().isEmpty() && events.linksCleared == 1,
			"replacing the document drops the links, the pending changes and the watches");
		linker.processChanges();
		ok &= expect(unitOf(document, 0).physical.length_m == 60.0, "nothing is taken after the links are dropped");

		// a linker that is detached no longer follows the document
		linker.linkFile("u1", false, physicalPath);
		linker.setDocument(nullptr);
		document.replace(unitScene(), false);
		ok &= expect(linker.linkOf("u1") != nullptr, "a detached linker does not follow the document");
	}

	if (!ok)
		return 1;
	std::cout << "all train unit source linker tests passed\n";
	return 0;
}
