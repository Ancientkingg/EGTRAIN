#include "editors/CompositionPanel.h"

#include "diagrams/TractionCurve.h"

#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QString>
#include <QStringList>

#include <iostream>
#include <string>
#include <utility>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static SceneTrainUnit trainUnit(const char* id, bool withCurve, const char* dataFile = "") {
	SceneTrainUnit unit;
	unit.id = id;
	unit.sourceDataFile = dataFile;
	if (withCurve)
		unit.tractionCurve.push_back({0.0, 10.0, 1000.0, 0.0, 0.0});
	return unit;
}

static SceneComposition composition(const char* id, std::vector<std::string> units) {
	SceneComposition result;
	result.id = id;
	result.units = std::move(units);
	return result;
}

static SceneService service(const char* id, const char* compositionId) {
	SceneService result;
	result.id = id;
	result.composition = compositionId;
	return result;
}

// Two compositions; c1 is used by two services and c2 by one.
static SceneModel compositionScene() {
	SceneModel scene;
	scene.name = "Compositions";
	scene.trainUnits = {trainUnit("u1", true, "u1.xml"), trainUnit("u2", false), trainUnit("u3", false)};
	scene.compositions = {composition("c1", {"u1", "u2"}), composition("c2", {"u1"})};
	scene.services = {service("s1", "c1"), service("s2", "c1"), service("s3", "c2")};
	return scene;
}

// Answers the questions of the panel and records the messages, without a dialog.
class FakePrompts : public UserPrompts {
public:
	QString errorTitle;
	QString errorMessage;
	bool errorWarning = false;
	int errorCount = 0;
	int confirmCount = 0;
	QString confirmQuestion;
	bool confirmAnswer = false;
	QString chosenUnit;
	QStringList offeredUnits;

	void showError(QWidget*, const QString& title, const QString& message, bool warningIcon = false) override {
		++errorCount;
		errorTitle = title;
		errorMessage = message;
		errorWarning = warningIcon;
	}
	bool confirm(QWidget*, const QString&, const QString& question) override {
		++confirmCount;
		confirmQuestion = question;
		return confirmAnswer;
	}
	QString chooseItem(QWidget*, const QString&, const QString&, const QStringList& items) override {
		offeredUnits = items;
		return chosenUnit;
	}
};

// The order in which a panel and its document report an edit.
struct Events {
	std::vector<std::string> log;
	SceneDocument::ChangeKinds kinds;
	QStringList plotted;

	Events(CompositionPanel& panel, SceneDocument& document) {
		QObject::connect(&document, &SceneDocument::changed, &panel, [this](SceneDocument::ChangeKinds changedKinds) {
			log.push_back("documentChanged");
			kinds = changedKinds;
		});
		QObject::connect(&panel, &CompositionPanel::compositionIdsChanged, &panel, [this] { log.push_back("idsChanged"); });
		QObject::connect(&panel, &CompositionPanel::changed, &panel, [this] { log.push_back("panelChanged"); });
		QObject::connect(&panel, &CompositionPanel::tractionPlotRequested, &panel,
			[this](const QString& unitId) { plotted << unitId; });
	}
};

using Log = std::vector<std::string>;

static QStringList items(const QListWidget* list) {
	QStringList result;
	for (int row = 0; row < list->count(); ++row)
		result << list->item(row)->text();
	return result;
}

// The role that the stylesheet rules give a message label, "" when it has none; "inline style" when the label colours itself.
static std::string role(const QLabel* label) {
	if (!label->styleSheet().isEmpty())
		return "inline style";
	return label->property("dialogStatus").toString().toStdString();
}

static std::vector<std::string> serviceCompositions(const SceneDocument& document) {
	std::vector<std::string> result;
	for (const SceneService& item : document.model().services)
		result.push_back(item.composition);
	return result;
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = true;
	const Log edit({"documentChanged", "panelChanged"});
	const Log editIds({"documentChanged", "idsChanged", "panelChanged"});

	{
		CompositionPanel panel;
		ok &= expect(panel.listWidget()->count() == 0 && !panel.listWidget()->isEnabled() && !panel.addButton()->isEnabled()
				&& !panel.idEdit()->isEnabled() && !panel.addUnitButton()->isEnabled(),
			"a panel without a document shows nothing and offers nothing");
		ok &= expect(panel.emptyStateLabel()->isHidden(), "a panel without a scene does not point to Add Composition, which is disabled");
		ok &= expect(panel.unitSourceDataLabel()->wordWrap() && panel.unitSourceDataLabel()->textFormat() == Qt::PlainText
				&& panel.unitSourceTractionLabel()->wordWrap() && panel.unitSourceTractionLabel()->textFormat() == Qt::PlainText,
			"the source labels show their paths as wrapped plain text");
		SceneDocument document;
		panel.setDocument(&document);
		panel.addComposition();
		panel.duplicateComposition();
		panel.deleteComposition();
		panel.addUnit();
		panel.commitIdEdit();
		ok &= expect(!panel.addButton()->isEnabled() && document.revision() == 0 && !document.isDirty()
				&& document.model().compositions.empty(),
			"a panel stays disabled while its document holds no scene, and its actions change nothing");
	}

	{
		SceneDocument document;
		CompositionPanel panel;
		FakePrompts prompts;
		panel.setPrompts(&prompts);
		panel.setDocument(&document);
		Events events(panel, document);

		// refresh
		document.replace(compositionScene(), false);
		ok &= expect(items(panel.listWidget()) == QStringList({"c1", "c2"}) && panel.listWidget()->currentRow() == 0
				&& panel.listWidget()->isEnabled() && panel.addButton()->isEnabled(),
			"a new scene is listed and its first composition is selected");
		ok &= expect(panel.idEdit()->text() == "c1" && panel.idEdit()->isEnabled()
				&& items(panel.unitsListWidget()) == QStringList({"u1", "u2"}) && panel.unitsListWidget()->currentRow() == -1,
			"the selected composition shows its id and its units, with no unit selected");
		ok &= expect(panel.emptyStateLabel()->isHidden() && panel.emptyStateLabel()->text().isEmpty(),
			"a selected composition has no empty-state message");
		ok &= expect(panel.duplicateButton()->isEnabled() && panel.deleteButton()->isEnabled() && panel.addUnitButton()->isEnabled()
				&& !panel.removeUnitButton()->isEnabled() && !panel.moveUnitUpButton()->isEnabled()
				&& !panel.moveUnitDownButton()->isEnabled() && !panel.plotTractionButton()->isEnabled(),
			"the unit buttons wait for a unit to be selected");
		ok &= expect(events.log.empty() && !document.isDirty() && document.revision() == 1, "showing a scene edits nothing");

		// the selected unit
		panel.unitsListWidget()->setCurrentRow(0);
		ok &= expect(panel.unitSourceDataLabel()->text() == "u1.xml" && panel.unitSourceTractionLabel()->text() == "(none)"
				&& panel.plotTractionButton()->isEnabled() && panel.unitWarningLabel()->text().isEmpty()
				&& panel.unitWarningLabel()->isHidden()
				&& !panel.moveUnitUpButton()->isEnabled() && panel.moveUnitDownButton()->isEnabled()
				&& panel.removeUnitButton()->isEnabled(),
			"a unit with a traction curve shows its sources and can be plotted");
		panel.unitsListWidget()->setCurrentRow(1);
		ok &= expect(panel.unitSourceDataLabel()->text() == "(none)" && !panel.plotTractionButton()->isEnabled()
				&& panel.unitWarningLabel()->text() == QString::fromStdString(tractionAssociationWarning(true, false))
				&& !panel.unitWarningLabel()->isHidden() && role(panel.unitWarningLabel()) == "warning"
				&& panel.moveUnitUpButton()->isEnabled() && !panel.moveUnitDownButton()->isEnabled(),
			"a unit without a traction curve cannot be plotted and says why");
		document.mutableModel().trainUnits[1].sourceDataFile = "u2.xml";
		panel.refreshUnitDetails();
		ok &= expect(panel.unitSourceDataLabel()->text() == "u2.xml" && events.log.empty(),
			"the sources of the selected unit are shown again on request");
		panel.unitsListWidget()->setCurrentRow(0);
		panel.plotTractionButton()->click();
		ok &= expect(events.plotted == QStringList({"u1"}) && events.log.empty(), "the plot of the selected unit is requested by id");

		// dropping a rename that is rejected: an empty id and an used id
		const quint64 revisionBeforeRejected = document.revision();
		panel.idEdit()->setText("");
		panel.commitIdEdit();
		ok &= expect(panel.idEdit()->text() == "c1" && document.model().compositions[0].id == "c1",
			"an empty id is dropped and the field shows the old id");
		panel.idEdit()->setText("c2");
		panel.commitIdEdit();
		ok &= expect(panel.idEdit()->text() == "c1" && document.model().compositions[0].id == "c1"
				&& document.model().compositions[1].id == "c2",
			"an id that another composition has is dropped and the field shows the old id");
		panel.idEdit()->setText("  c1  ");
		panel.commitIdEdit();
		ok &= expect(document.revision() == revisionBeforeRejected && events.log.empty() && !document.isDirty(),
			"rejected and unchanged ids do not touch the document");

		// rename
		panel.idEdit()->setText("c1_renamed");
		panel.commitIdEdit();
		ok &= expect(document.model().compositions[0].id == "c1_renamed" && document.isDirty()
				&& document.revision() == revisionBeforeRejected + 1 && document.lastChangeLabel() == "Rename composition",
			"a rename is written to the document and marks it changed");
		ok &= expect(serviceCompositions(document) == std::vector<std::string>({"c1_renamed", "c1_renamed", "c2"}),
			"a rename rewrites the services that use the composition and no others");
		ok &= expect(events.log == editIds && events.kinds.testFlag(SceneDocument::Compositions)
				&& events.kinds.testFlag(SceneDocument::Services),
			"a rename reports the composition and the service change, and the changed composition ids");
		ok &= expect(items(panel.listWidget()) == QStringList({"c1_renamed", "c2"}) && panel.listWidget()->currentRow() == 0
				&& panel.idEdit()->text() == "c1_renamed",
			"a rename keeps the selection and shows the new id in the list");

		// pending edits
		events.log.clear();
		panel.idEdit()->setText("c1_pending");
		ok &= expect(document.model().compositions[0].id == "c1_renamed", "text that is still being typed is not in the document");
		panel.commitPendingEditors();
		ok &= expect(document.model().compositions[0].id == "c1_pending" && events.log == editIds
				&& serviceCompositions(document)[0] == "c1_pending",
			"committing the pending editors takes the id that is being typed");

		// cancel: text that was typed and not finished is dropped when the panel is refreshed
		events.log.clear();
		document.markSaved();
		const quint64 revisionBeforeCancel = document.revision();
		panel.idEdit()->setText("abandoned");
		panel.refresh();
		ok &= expect(panel.idEdit()->text() == "c1_pending" && document.model().compositions[0].id == "c1_pending"
				&& !document.isDirty() && document.revision() == revisionBeforeCancel && events.log.empty(),
			"refreshing drops unfinished text and leaves the document as it is");

		// units: add
		events.log.clear();
		prompts.chosenUnit = "";
		panel.addUnit();
		ok &= expect(prompts.offeredUnits == QStringList({"u1", "u2", "u3"}) && document.model().compositions[0].units.size() == 2
				&& events.log.empty(),
			"cancelling the choice of a unit adds nothing");
		prompts.chosenUnit = "u3";
		panel.addUnit();
		ok &= expect(document.model().compositions[0].units == std::vector<std::string>({"u1", "u2", "u3"})
				&& items(panel.unitsListWidget()) == QStringList({"u1", "u2", "u3"}) && panel.unitsListWidget()->currentRow() == 2
				&& events.log == edit && events.kinds == SceneDocument::Compositions,
			"a chosen unit is added at the end and selected");

		// units: move and remove
		events.log.clear();
		panel.unitsListWidget()->setCurrentRow(2);
		panel.moveUnitUp();
		ok &= expect(document.model().compositions[0].units == std::vector<std::string>({"u1", "u3", "u2"})
				&& panel.unitsListWidget()->currentRow() == 1 && events.log == edit,
			"a unit moves up and stays selected");
		panel.moveUnitDown();
		panel.moveUnitDown();
		ok &= expect(document.model().compositions[0].units == std::vector<std::string>({"u1", "u2", "u3"})
				&& panel.unitsListWidget()->currentRow() == 2 && !panel.moveUnitDownButton()->isEnabled(),
			"a unit moves down until it is last");
		events.log.clear();
		panel.moveUnitDown();
		panel.unitsListWidget()->setCurrentRow(0);
		panel.moveUnitUp();
		ok &= expect(document.model().compositions[0].units == std::vector<std::string>({"u1", "u2", "u3"}) && events.log.empty(),
			"a unit cannot move past either end");
		panel.unitsListWidget()->setCurrentRow(1);
		panel.removeUnit();
		ok &= expect(document.model().compositions[0].units == std::vector<std::string>({"u1", "u3"})
				&& panel.unitsListWidget()->currentRow() == 1 && events.log == edit,
			"a removed unit leaves the next unit selected");
		panel.removeUnit();
		ok &= expect(document.model().compositions[0].units == std::vector<std::string>({"u1"})
				&& panel.unitsListWidget()->currentRow() == 0,
			"removing the last unit selects the new last unit");
		panel.removeUnit();
		ok &= expect(document.model().compositions[0].units.empty() && panel.unitsListWidget()->currentRow() == -1
				&& !panel.removeUnitButton()->isEnabled(),
			"removing the only unit leaves none selected");
		events.log.clear();
		panel.removeUnit();
		ok &= expect(events.log.empty(), "removing without a selected unit changes nothing");

		// add and duplicate
		events.log.clear();
		panel.addComposition();
		ok &= expect(document.model().compositions.size() == 3 && document.model().compositions[2].id == "new_composition"
				&& panel.listWidget()->currentRow() == 2 && panel.idEdit()->text() == "new_composition" && events.log == editIds
				&& events.kinds == SceneDocument::Compositions,
			"a new composition is added at the end and selected");
		panel.addComposition();
		ok &= expect(document.model().compositions[3].id == "new_composition_2", "a new composition gets an unused id");
		events.log.clear();
		panel.listWidget()->setCurrentRow(1);
		panel.duplicateComposition();
		ok &= expect(items(panel.listWidget()) == QStringList({"c1_pending", "c2", "c2_copy", "new_composition", "new_composition_2"})
				&& document.model().compositions[2].units == document.model().compositions[1].units
				&& panel.listWidget()->currentRow() == 2 && events.log == editIds,
			"a duplicate follows its original, has its units and is selected");

		// delete
		events.log.clear();
		panel.listWidget()->setCurrentRow(0);
		panel.deleteComposition();
		ok &= expect(prompts.errorCount == 1 && prompts.errorWarning && prompts.errorTitle == "Cannot Delete Composition"
				&& prompts.errorMessage.contains("c1_pending") && prompts.errorMessage.contains("s1")
				&& prompts.errorMessage.contains("s2") && !prompts.errorMessage.contains("s3") && prompts.confirmCount == 0,
			"a composition that services use is refused, naming the services");
		ok &= expect(document.model().compositions.size() == 5 && events.log.empty(), "a refused delete changes nothing");
		panel.listWidget()->setCurrentRow(3);
		prompts.confirmAnswer = false;
		panel.deleteComposition();
		ok &= expect(prompts.confirmCount == 1 && prompts.confirmQuestion == "Delete composition 'new_composition'?"
				&& document.model().compositions.size() == 5 && events.log.empty(),
			"answering No keeps the composition");
		prompts.confirmAnswer = true;
		panel.deleteComposition();
		ok &= expect(items(panel.listWidget()) == QStringList({"c1_pending", "c2", "c2_copy", "new_composition_2"})
				&& panel.listWidget()->currentRow() == 3 && events.log == editIds,
			"answering Yes deletes the composition and keeps a row selected");
		events.log.clear();
		panel.deleteComposition();
		panel.deleteComposition();
		panel.deleteComposition();
		ok &= expect(items(panel.listWidget()) == QStringList({"c1_pending", "c2"}) && prompts.errorCount == 2
				&& panel.listWidget()->currentRow() == 1,
			"deleting from the end stops at the composition that a service uses");

		// a run does not close the compositions
		document.setEditable(false);
		events.log.clear();
		panel.addComposition();
		ok &= expect(document.model().compositions.size() == 3 && events.log == editIds,
			"compositions can be edited while the document is closed for edits");
		document.setEditable(true);

		// another scene replaces unfinished text without an edit
		events.log.clear();
		panel.idEdit()->setText("abandoned");
		panel.unitsListWidget()->setCurrentRow(-1);
		SceneModel other;
		other.compositions = {composition("other", {})};
		document.replace(other, false);
		ok &= expect(items(panel.listWidget()) == QStringList({"other"}) && panel.idEdit()->text() == "other"
				&& panel.unitsListWidget()->count() == 0 && !panel.addUnitButton()->isEnabled() && events.log.empty(),
			"opening another scene replaces unfinished text without an edit, and adding units needs rolling stock units");

		// detach
		panel.setDocument(nullptr);
		ok &= expect(panel.listWidget()->count() == 0 && !panel.listWidget()->isEnabled(), "a detached panel shows nothing");
		const quint64 revisionDetached = document.revision();
		panel.addComposition();
		document.replace(compositionScene(), false);
		ok &= expect(panel.listWidget()->count() == 0 && document.revision() == revisionDetached + 1,
			"a detached panel neither edits nor follows the document");
	}

	{
		auto* document = new SceneDocument;
		CompositionPanel panel;
		panel.setDocument(document);
		document->replace(compositionScene(), false);
		delete document;
		panel.idEdit()->setText("after the document");
		panel.commitIdEdit();
		panel.addComposition();
		panel.refresh();
		ok &= expect(panel.listWidget()->count() == 0 && !panel.addButton()->isEnabled(),
			"a panel whose document is gone shows nothing and edits nothing");
	}

	{
		// the empty state follows the selection
		SceneDocument document;
		CompositionPanel panel;
		FakePrompts prompts;
		prompts.confirmAnswer = true;
		panel.setPrompts(&prompts);
		panel.setDocument(&document);
		document.replace(SceneModel(), false);
		ok &= expect(panel.emptyStateLabel()->text() == "Select a composition or use Add Composition."
				&& !panel.emptyStateLabel()->isHidden() && panel.emptyStateLabel()->property("dialogHelp").toBool()
				&& panel.addButton()->isEnabled() && !panel.idEdit()->isEnabled(),
			"a scene without compositions says what to select, and the Add button it points to is enabled");
		panel.addComposition();
		ok &= expect(panel.emptyStateLabel()->isHidden() && panel.idEdit()->isEnabled(),
			"adding a composition removes the message");
		panel.deleteComposition();
		ok &= expect(!panel.emptyStateLabel()->isHidden() && !panel.idEdit()->isEnabled(),
			"deleting the only composition brings the message back");
		panel.setDocument(nullptr);
		ok &= expect(panel.emptyStateLabel()->isHidden(), "a detached panel does not point to the disabled Add button");
	}

	{
		// the list of units says when it is empty
		SceneDocument document;
		CompositionPanel panel;
		FakePrompts prompts;
		panel.setPrompts(&prompts);
		panel.setDocument(&document);
		ok &= expect(panel.unitsEmptyStateLabel()->isHidden(), "a panel without a scene has no message about units");
		document.replace(compositionScene(), false);
		ok &= expect(panel.unitsEmptyStateLabel()->isHidden() && panel.unitsEmptyStateLabel()->text().isEmpty(),
			"a composition with units has no message about units");
		panel.addComposition();
		ok &= expect(panel.unitsListWidget()->count() == 0 && panel.unitsEmptyStateLabel()->text() == "This composition has no units. Use Add Unit."
				&& !panel.unitsEmptyStateLabel()->isHidden() && panel.unitsEmptyStateLabel()->property("dialogHelp").toBool(),
			"a composition without units points to Add Unit");
		prompts.chosenUnit = "u1";
		panel.addUnit();
		ok &= expect(panel.unitsEmptyStateLabel()->isHidden(), "adding a unit removes the message about units");
		panel.removeUnit();
		ok &= expect(!panel.unitsEmptyStateLabel()->isHidden(), "removing the last unit brings the message back");
		SceneModel bare;
		bare.compositions = {composition("c1", {})};
		document.replace(bare, false);
		ok &= expect(panel.unitsEmptyStateLabel()->text() == "This composition has no units. Add a rolling stock unit first."
				&& !panel.addUnitButton()->isEnabled(),
			"a scene without rolling stock units says to add one first");
		document.replace(SceneModel(), false);
		ok &= expect(panel.unitsEmptyStateLabel()->isHidden(), "a scene without compositions has no message about units");
	}

	for (const double scale : {1.0, 1.5}) {
		// the buttons of the unit list are in two rows and fit a narrow panel
		SceneDocument document;
		CompositionPanel panel;
		QFont font = panel.font();
		font.setPointSizeF(font.pointSizeF() * scale);
		panel.setFont(font);
		panel.setDocument(&document);
		document.replace(compositionScene(), false);
		panel.resize(panel.minimumSizeHint());
		panel.show();
		QApplication::processEvents();
		const QList<QPushButton*> buttons = {panel.addUnitButton(), panel.removeUnitButton(), panel.moveUnitUpButton(), panel.moveUnitDownButton()};
		bool inside = true;
		for (const QPushButton* button : buttons) {
			const QRect placed(button->mapTo(&panel, QPoint(0, 0)), button->size());
			inside = inside && panel.rect().contains(placed) && button->width() >= button->minimumSizeHint().width();
		}
		const QString label = QString(" at %1 times the font").arg(scale);
		ok &= expect(inside, qPrintable("the unit buttons are whole inside the panel at its minimum width" + label));
		ok &= expect(buttons[0]->y() == buttons[1]->y() && buttons[2]->y() == buttons[3]->y() && buttons[2]->y() > buttons[0]->y()
				&& buttons[0]->x() == buttons[2]->x() && buttons[1]->x() == buttons[3]->x(),
			qPrintable("the unit buttons are in two rows" + label));
	}

	{
		// a unit that the scene does not define is an error, not a note
		SceneDocument document;
		CompositionPanel panel;
		panel.setDocument(&document);
		SceneModel scene = compositionScene();
		scene.compositions[0].units.push_back("missing");
		document.replace(scene, false);
		ok &= expect(panel.unitWarningLabel()->isHidden() && panel.unitWarningLabel()->text().isEmpty(),
			"a composition without a selected unit shows no warning");
		panel.unitsListWidget()->setCurrentRow(2);
		ok &= expect(panel.unitWarningLabel()->text() == QString::fromStdString(tractionAssociationWarning(false, false))
				&& !panel.unitWarningLabel()->isHidden() && role(panel.unitWarningLabel()) == "error",
			"a unit that the scene does not define is reported as an error");
		panel.unitsListWidget()->setCurrentRow(0);
		ok &= expect(panel.unitWarningLabel()->isHidden() && role(panel.unitWarningLabel()) != "inline style",
			"selecting a unit with a curve hides the warning again");
	}

	if (!ok)
		return 1;
	std::cout << "all composition panel tests passed\n";
	return 0;
}
