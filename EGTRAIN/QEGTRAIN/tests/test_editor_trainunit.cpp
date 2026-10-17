#include "editors/TrainUnitPanel.h"

#include "widgets/DisplayPath.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>

#include <array>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static SceneTrainUnit trainUnit(const char* id, double lengthMeters, bool withCurve) {
	SceneTrainUnit unit;
	unit.id = id;
	unit.hasPhysical = true;
	unit.physical.mass_of_traction_unit_kg = 80000.0;
	unit.physical.max_speed_ms = 40.0;
	unit.physical.length_m = lengthMeters;
	if (withCurve) {
		unit.tractionCurve.push_back({0.0, 10.0, 1000.0, 0.0, 0.0});
		unit.tractionCurve.push_back({10.0, 20.0, 900.0, 1.0, 0.0});
	}
	unit.sourceDataFile = "u.data";
	unit.sourceTractionFile = "u.traction";
	return unit;
}

// u1 is used by the composition c1 and u2 by none.
static SceneModel unitScene() {
	SceneModel scene;
	scene.name = "Units";
	scene.trainUnits = {trainUnit("u1", 100.0, true), trainUnit("u2", 50.0, false)};
	SceneComposition composition;
	composition.id = "c1";
	composition.units = {"u1", "u2", "u1"};
	scene.compositions = {composition};
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
};

// The order in which a panel and its document report an edit.
struct Events {
	std::vector<std::string> log;
	SceneDocument::ChangeKinds kinds;
	QStringList renamed;
	QStringList deleted;
	QStringList plotted;
	std::vector<std::pair<std::string, bool>> sourceRequests;

	Events(TrainUnitPanel& panel, SceneDocument& document) {
		QObject::connect(&document, &SceneDocument::changed, &panel, [this](SceneDocument::ChangeKinds changedKinds) {
			log.push_back("documentChanged");
			kinds = changedKinds;
		});
		QObject::connect(&panel, &TrainUnitPanel::trainUnitsChanged, &panel, [this] { log.push_back("unitsChanged"); });
		QObject::connect(&panel, &TrainUnitPanel::sourceReferencesChanged, &panel, [this] { log.push_back("sourcesChanged"); });
		QObject::connect(&panel, &TrainUnitPanel::changed, &panel, [this] { log.push_back("panelChanged"); });
		QObject::connect(&panel, &TrainUnitPanel::unitRenamed, &panel, [this](const QString& oldId, const QString& newId) {
			log.push_back("unitRenamed");
			renamed << oldId << newId;
		});
		QObject::connect(&panel, &TrainUnitPanel::unitDeleted, &panel, [this](const QString& id) {
			log.push_back("unitDeleted");
			deleted << id;
		});
		QObject::connect(&panel, &TrainUnitPanel::tractionPlotRequested, &panel, [this](const QString& id) { plotted << id; });
		QObject::connect(&panel, &TrainUnitPanel::sourceLinkRequested, &panel,
			[this](bool traction) { sourceRequests.emplace_back("link", traction); });
		QObject::connect(&panel, &TrainUnitPanel::sourceUnlinkRequested, &panel,
			[this](bool traction) { sourceRequests.emplace_back("unlink", traction); });
		QObject::connect(&panel, &TrainUnitPanel::sourceRetryRequested, &panel,
			[this](bool traction) { sourceRequests.emplace_back("retry", traction); });
	}
};

using Log = std::vector<std::string>;

static QStringList items(const QListWidget* list) {
	QStringList result;
	for (int row = 0; row < list->count(); ++row)
		result << list->item(row)->text();
	return result;
}

static QDoubleSpinBox* cell(TrainUnitPanel& panel, int row, int column) {
	return qobject_cast<QDoubleSpinBox*>(panel.tractionTable()->cellWidget(row, column));
}

// The text field of a number field.
static QLineEdit* spinText(QDoubleSpinBox* edit) {
	return edit->findChild<QLineEdit*>();
}

// What the user does to a number field: type text, then leave the field.
static void typeNumber(QDoubleSpinBox* edit, const QString& text) {
	spinText(edit)->setText(text);
	edit->interpretText();
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = true;
	const Log edit({"documentChanged", "panelChanged"});

	{
		TrainUnitPanel panel;
		ok &= expect(panel.listWidget()->count() == 0 && !panel.listWidget()->isEnabled() && !panel.addButton()->isEnabled()
				&& !panel.idEdit()->isEnabled() && !panel.tractionTable()->isEnabled() && panel.selectedUnit() == nullptr,
			"a panel without a document shows nothing and offers nothing");
		SceneDocument document;
		panel.setDocument(&document);
		panel.addUnit();
		panel.duplicateUnit();
		panel.deleteUnit();
		panel.addTractionRow();
		panel.commitIdEdit();
		panel.commitPendingEditors();
		ok &= expect(!panel.addButton()->isEnabled() && document.revision() == 0 && !document.isDirty()
				&& document.model().trainUnits.empty(),
			"a panel stays disabled while its document holds no scene, and its actions change nothing");
		ok &= expect(panel.emptyStateLabel()->isHidden() && panel.tabs()->isHidden(),
			"a panel without a scene shows no message and no values");
	}

	{
		// the empty state follows the selection
		SceneDocument document;
		TrainUnitPanel panel;
		FakePrompts prompts;
		prompts.confirmAnswer = true;
		panel.setPrompts(&prompts);
		panel.setDocument(&document);
		document.replace(SceneModel(), false);
		ok &= expect(panel.emptyStateLabel()->text() == "Select a rolling stock unit or use Add Unit."
				&& !panel.emptyStateLabel()->isHidden() && panel.emptyStateLabel()->property("dialogHelp").toBool()
				&& !panel.emptyStateLabel()->property("dialogStatus").isValid() && panel.tabs()->isHidden()
				&& panel.addButton()->isEnabled() && !panel.idEdit()->isEnabled(),
			"a scene without units says what to select, and the Add button it points to is enabled");
		panel.addUnit();
		ok &= expect(panel.emptyStateLabel()->isHidden() && !panel.tabs()->isHidden() && panel.idEdit()->isEnabled(),
			"adding a unit removes the message and shows its values");
		panel.deleteUnit();
		ok &= expect(!panel.emptyStateLabel()->isHidden() && panel.tabs()->isHidden(),
			"deleting the only unit brings the message back and hides the values");
		panel.setDocument(nullptr);
		ok &= expect(panel.emptyStateLabel()->isHidden(), "a detached panel does not point to the disabled Add button");
	}

	{
		// the detail area has a page for the traction and a page for the physical values, and no scroll area of its own
		SceneDocument document;
		TrainUnitPanel panel;
		panel.setDocument(&document);
		document.replace(unitScene(), false);
		QTabWidget* tabs = panel.tabs();
		ok &= expect(!tabs->isHidden() && panel.emptyStateLabel()->isHidden() && tabs->count() == 2
				&& tabs->tabText(0) == "Traction" && tabs->tabText(1) == "Physical" && tabs->currentIndex() == 0,
			"a selected unit shows the Traction and the Physical page, starting with the Traction page");
		const QWidget* traction = tabs->widget(0);
		ok &= expect(traction->isAncestorOf(panel.sourceTractionEdit()) && traction->isAncestorOf(panel.linkButton(true))
				&& traction->isAncestorOf(panel.sourceStatusLabel(true)) && traction->isAncestorOf(panel.tractionTable())
				&& traction->isAncestorOf(panel.addTractionRowButton()) && traction->isAncestorOf(panel.removeTractionRowButton())
				&& traction->isAncestorOf(panel.plotButton()),
			"the Traction page has the source reference, the curve and the Plot button");
		const QWidget* physical = tabs->widget(1);
		bool physicalFieldsOnPage = true;
		for (const QDoubleSpinBox* physicalEdit : panel.physicalEdits())
			physicalFieldsOnPage = physicalFieldsOnPage && physical->isAncestorOf(physicalEdit);
		ok &= expect(physicalFieldsOnPage && physical->isAncestorOf(panel.sourceDataEdit()) && physical->isAncestorOf(panel.linkButton(false))
				&& physical->isAncestorOf(panel.sourceStatusLabel(false)),
			"the Physical page has the nine fields and the parameter source");
		ok &= expect(panel.findChildren<QScrollArea*>().isEmpty(), "the curve and the fields do not scroll inside a scroll area of the panel");
		panel.physicalEdits()[8]->setValue(123.0);
		ok &= expect(document.model().trainUnits[0].physical.length_m == 123.0, "a field on a page that is not shown can be set");
	}

	{
		SceneDocument document;
		TrainUnitPanel panel;
		FakePrompts prompts;
		panel.setPrompts(&prompts);
		panel.setDocument(&document);
		Events events(panel, document);
		int detailsShown = 0;
		QObject::connect(&panel, &TrainUnitPanel::detailsShown, &panel, [&detailsShown] { ++detailsShown; });

		// refresh
		document.replace(unitScene(), false);
		ok &= expect(detailsShown == 1, "the details are shown again for a new scene");
		ok &= expect(items(panel.listWidget()) == QStringList({"u1", "u2"}) && panel.listWidget()->currentRow() == 0
				&& panel.selectedUnit() == &document.model().trainUnits[0],
			"a new scene is listed and its first unit is selected");
		ok &= expect(panel.idEdit()->text() == "u1" && panel.idEdit()->isEnabled()
				&& panel.physicalEdits()[0]->value() == 80000.0 && panel.physicalEdits()[3]->value() == 40.0
				&& panel.physicalEdits()[8]->value() == 100.0 && panel.physicalEdits()[8]->isEnabled(),
			"the selected unit shows its id and its physical values");
		ok &= expect(panel.sourceDataEdit()->text() == "u.data" && panel.sourceTractionEdit()->text() == "u.traction",
			"the selected unit shows its source references");
		ok &= expect(panel.tractionTable()->rowCount() == 2 && panel.tractionTable()->isEnabled()
				&& cell(panel, 1, 0)->value() == 10.0 && cell(panel, 1, 2)->value() == 900.0 && cell(panel, 1, 3)->value() == 1.0,
			"the traction curve is shown in the table");
		ok &= expect(panel.duplicateButton()->isEnabled() && panel.deleteButton()->isEnabled()
				&& panel.addTractionRowButton()->isEnabled() && panel.removeTractionRowButton()->isEnabled()
				&& panel.plotButton()->isEnabled(),
			"the buttons of a unit with a curve are enabled");
		ok &= expect(events.log.empty() && !document.isDirty() && document.revision() == 1, "showing a scene edits nothing");
		panel.listWidget()->setCurrentRow(1);
		ok &= expect(panel.idEdit()->text() == "u2" && panel.tractionTable()->rowCount() == 0 && !panel.plotButton()->isEnabled()
				&& panel.addTractionRowButton()->isEnabled() && !panel.removeTractionRowButton()->isEnabled(),
			"a unit without a curve shows an empty table and cannot be plotted");
		panel.listWidget()->setCurrentRow(0);

		// the plot and the source link buttons only ask
		panel.plotButton()->click();
		panel.linkButton(false)->click();
		panel.unlinkButton(true)->click();
		panel.retryButton(false)->click();
		ok &= expect(events.plotted == QStringList({"u1"}) && events.sourceRequests.size() == 3
				&& events.sourceRequests[0] == std::make_pair(std::string("link"), false)
				&& events.sourceRequests[1] == std::make_pair(std::string("unlink"), true)
				&& events.sourceRequests[2] == std::make_pair(std::string("retry"), false) && events.log.empty(),
			"the plot and the source buttons are reported with the source they mean and change nothing");

		// number fields
		panel.physicalEdits()[8]->setValue(105.5);
		ok &= expect(document.model().trainUnits[0].physical.length_m == 105.5 && document.isDirty() && document.revision() == 2
				&& document.lastChangeLabel() == "Edit rolling stock unit",
			"a number that is entered is written to the document and marks it changed");
		ok &= expect(events.log == edit && events.kinds == SceneDocument::TrainUnits, "a number edit is one document change");
		events.log.clear();
		typeNumber(panel.physicalEdits()[8], "abc");
		ok &= expect(document.model().trainUnits[0].physical.length_m == 105.5 && events.log.empty()
				&& document.revision() == 2,
			"text that is not a number stays out of the model");
		typeNumber(panel.physicalEdits()[8], "");
		typeNumber(panel.physicalEdits()[8], "1e");
		ok &= expect(document.model().trainUnits[0].physical.length_m == 105.5 && events.log.empty(),
			"an empty or unfinished number stays out of the model");
		typeNumber(panel.physicalEdits()[8], "106");
		ok &= expect(document.model().trainUnits[0].physical.length_m == 106.0 && events.log == edit,
			"a number that follows text that was not a number is taken");
		events.log.clear();
		panel.commitPhysical(8);
		panel.commitPhysical(-1);
		panel.commitPhysical(TrainUnitPanel::PhysicalFieldCount);
		ok &= expect(events.log.empty(), "a value that is already in the model, or a field that does not exist, changes nothing");

		// a unit without physical values is not given them by looking at it
		document.mutableModel().trainUnits[1].hasPhysical = false;
		panel.listWidget()->setCurrentRow(1);
		panel.commitPendingEditors();
		ok &= expect(!document.model().trainUnits[1].hasPhysical && events.log.empty(),
			"committing the pending editors does not touch number fields that nobody is editing");
		panel.listWidget()->setCurrentRow(0);

		// traction rows
		panel.addTractionRow();
		ok &= expect(document.model().trainUnits[0].tractionCurve.size() == 3
				&& document.model().trainUnits[0].tractionCurve[2] == std::array<double, 5>{{20.0, 21.0, 0.0, 0.0, 0.0}}
				&& panel.tractionTable()->rowCount() == 3 && panel.tractionTable()->currentRow() == 2 && events.log == edit,
			"a new traction row continues the curve and is selected");
		cell(panel, 2, 2)->setValue(500.0);
		ok &= expect(document.model().trainUnits[0].tractionCurve[2][2] == 500.0 && events.log == Log({"documentChanged", "panelChanged", "documentChanged", "panelChanged"}),
			"a number in the table is written to the curve");
		events.log.clear();
		typeNumber(cell(panel, 2, 2), "abc");
		ok &= expect(document.model().trainUnits[0].tractionCurve[2][2] == 500.0 && events.log.empty(),
			"text that is not a number stays out of the curve");
		panel.commitTractionCell(0, 5, 1.0);
		panel.commitTractionCell(7, 0, 1.0);
		panel.commitTractionCell(0, 0, 0.0);
		ok &= expect(events.log.empty(), "a cell that does not exist, or a value that is already there, changes nothing");
		panel.tractionTable()->setCurrentCell(1, 0);
		panel.removeTractionRow();
		ok &= expect(document.model().trainUnits[0].tractionCurve.size() == 2
				&& document.model().trainUnits[0].tractionCurve[1][2] == 500.0 && panel.tractionTable()->rowCount() == 2
				&& events.log == edit,
			"the selected traction row is removed");
		events.log.clear();
		panel.tractionTable()->setCurrentCell(-1, -1);
		panel.removeTractionRow();
		ok &= expect(document.model().trainUnits[0].tractionCurve.size() == 2 && events.log.empty(),
			"removing without a selected row changes nothing");

		// sources
		panel.sourceDataEdit()->setText("new.data");
		emit panel.sourceDataEdit()->editingFinished();
		ok &= expect(document.model().trainUnits[0].sourceDataFile == "new.data"
				&& document.model().trainUnits[0].sourceTractionFile == "u.traction"
				&& events.log == Log({"documentChanged", "sourcesChanged", "panelChanged"}),
			"a source reference is written to the document and reported");
		events.log.clear();
		emit panel.sourceTractionEdit()->editingFinished();
		ok &= expect(events.log.empty(), "leaving a source field without changing it does not touch the document");

		// a rename that is rejected, and one that is not
		const quint64 revisionBeforeRejected = document.revision();
		panel.idEdit()->setText("   ");
		panel.commitIdEdit();
		ok &= expect(panel.idEdit()->text() == "u1" && document.model().trainUnits[0].id == "u1",
			"an empty id is dropped and the field shows the old id");
		panel.idEdit()->setText("u2");
		panel.commitIdEdit();
		ok &= expect(panel.idEdit()->text() == "u1" && document.model().trainUnits[0].id == "u1"
				&& document.revision() == revisionBeforeRejected && events.log.empty(),
			"an id that another unit has is dropped and the field shows the old id");
		panel.idEdit()->setText("u1_renamed");
		panel.commitIdEdit();
		ok &= expect(document.model().trainUnits[0].id == "u1_renamed" && document.lastChangeLabel() == "Rename rolling stock unit",
			"a rename is written to the document");
		ok &= expect(document.model().compositions[0].units == std::vector<std::string>({"u1_renamed", "u2", "u1_renamed"}),
			"a rename rewrites the compositions that use the unit");
		ok &= expect(events.log == Log({"documentChanged", "unitRenamed", "unitsChanged", "panelChanged"})
				&& events.renamed == QStringList({"u1", "u1_renamed"}) && events.kinds.testFlag(SceneDocument::TrainUnits)
				&& events.kinds.testFlag(SceneDocument::Compositions),
			"a rename reports the unit ids and the changed units after the document is marked changed");
		ok &= expect(items(panel.listWidget()) == QStringList({"u1_renamed", "u2"}) && panel.listWidget()->currentRow() == 0,
			"a rename keeps the selection and shows the new id in the list");

		// cancel: text that was typed and not finished is dropped when the panel is refreshed
		events.log.clear();
		document.markSaved();
		const quint64 revisionBeforeCancel = document.revision();
		panel.idEdit()->setText("abandoned");
		panel.sourceDataEdit()->setText("abandoned.data");
		spinText(panel.physicalEdits()[0])->setText("12345");
		panel.refresh();
		ok &= expect(panel.idEdit()->text() == "u1_renamed" && panel.sourceDataEdit()->text() == "new.data"
				&& spinText(panel.physicalEdits()[0])->text() == "80000" && !document.isDirty()
				&& document.revision() == revisionBeforeCancel && events.log.empty(),
			"refreshing drops unfinished text and leaves the document as it is");

		// pending edits
		panel.idEdit()->setText("u1_pending");
		panel.sourceTractionEdit()->setText("pending.traction");
		ok &= expect(document.model().trainUnits[0].id == "u1_renamed", "text that is still being typed is not in the document");
		panel.commitPendingEditors();
		ok &= expect(document.model().trainUnits[0].id == "u1_pending"
				&& document.model().trainUnits[0].sourceTractionFile == "pending.traction"
				&& document.model().compositions[0].units[0] == "u1_pending",
			"committing the pending editors takes the id and the source references that are being typed");

		// add, duplicate, select
		events.log.clear();
		panel.addUnit();
		ok &= expect(items(panel.listWidget()) == QStringList({"u1_pending", "u2", "new_train_unit"})
				&& panel.listWidget()->currentRow() == 2 && panel.idEdit()->text() == "new_train_unit"
				&& events.log == Log({"documentChanged", "unitsChanged", "panelChanged"}) && events.kinds == SceneDocument::TrainUnits,
			"a new unit is added at the end and selected");
		events.log.clear();
		panel.listWidget()->setCurrentRow(0);
		panel.duplicateUnit();
		ok &= expect(items(panel.listWidget()) == QStringList({"u1_pending", "u1_pending_copy", "u2", "new_train_unit"})
				&& panel.listWidget()->currentRow() == 1 && document.model().trainUnits[1].physical.length_m == 106.0
				&& events.log == Log({"documentChanged", "unitsChanged", "panelChanged"}),
			"a duplicate follows its original, has its values and is selected");
		ok &= expect(panel.selectUnit("u2") && panel.selectedUnit() == &document.model().trainUnits[2] && !panel.selectUnit("nothing")
				&& panel.selectedUnit() == &document.model().trainUnits[2],
			"a unit is selected by its id");

		// delete
		events.log.clear();
		panel.selectUnit("u1_pending");
		panel.deleteUnit();
		ok &= expect(prompts.errorCount == 1 && prompts.errorWarning && prompts.errorTitle == "Cannot Delete Rolling Stock Unit"
				&& prompts.errorMessage.contains("c1") && prompts.confirmCount == 0 && events.log.empty()
				&& document.model().trainUnits.size() == 4,
			"a unit that a composition uses is refused, naming the composition");
		panel.selectUnit("new_train_unit");
		prompts.confirmAnswer = false;
		panel.deleteUnit();
		ok &= expect(prompts.confirmCount == 1 && prompts.confirmQuestion == "Delete rolling stock unit 'new_train_unit'?"
				&& document.model().trainUnits.size() == 4 && events.log.empty(),
			"answering No keeps the unit");
		prompts.confirmAnswer = true;
		panel.deleteUnit();
		ok &= expect(items(panel.listWidget()) == QStringList({"u1_pending", "u1_pending_copy", "u2"})
				&& panel.listWidget()->currentRow() == 2
				&& events.log == Log({"documentChanged", "unitDeleted", "unitsChanged", "panelChanged"})
				&& events.deleted == QStringList({"new_train_unit"}),
			"answering Yes deletes the unit and reports it");

		// the units stay editable while the document is closed for edits
		document.setEditable(false);
		events.log.clear();
		panel.addUnit();
		ok &= expect(document.model().trainUnits.size() == 4 && events.log.size() == 3,
			"units can be edited while the document is closed for edits");
		document.setEditable(true);

		// another scene replaces unfinished text without an edit
		events.log.clear();
		panel.idEdit()->setText("abandoned");
		SceneModel other;
		other.trainUnits = {trainUnit("other", 10.0, false)};
		document.replace(other, false);
		ok &= expect(items(panel.listWidget()) == QStringList({"other"}) && panel.idEdit()->text() == "other" && events.log.empty(),
			"opening another scene replaces unfinished text without an edit");

		// detach
		panel.setDocument(nullptr);
		ok &= expect(panel.listWidget()->count() == 0 && !panel.listWidget()->isEnabled() && panel.selectedUnit() == nullptr,
			"a detached panel shows nothing");
		const quint64 revisionDetached = document.revision();
		panel.addUnit();
		document.replace(unitScene(), false);
		ok &= expect(panel.listWidget()->count() == 0 && document.revision() == revisionDetached + 1,
			"a detached panel neither edits nor follows the document");
	}

	{
		// the state of the source links
		SceneDocument document;
		TrainUnitPanel panel;
		panel.setDocument(&document);
		panel.setSourceState(false, "/some/file", "Linked");
		ok &= expect(panel.sourceStatusLabel(false)->text() == "No unit selected" && !panel.linkButton(false)->isEnabled()
				&& !panel.unlinkButton(false)->isEnabled() && !panel.retryButton(false)->isEnabled(),
			"the source links wait for a unit to be selected");
		document.replace(unitScene(), false);
		panel.setSourceState(false, QString(), QString());
		ok &= expect(panel.sourceStatusLabel(false)->text() == "Unlinked" && panel.linkButton(false)->isEnabled()
				&& !panel.unlinkButton(false)->isEnabled() && !panel.retryButton(false)->isEnabled()
				&& panel.sourceStatusLabel(false)->toolTip().isEmpty(),
			"an unlinked source can be linked and nothing else");
		QTemporaryDir directory;
		const QString existing = directory.filePath("linked.txt");
		QFile file(existing);
		ok &= expect(file.open(QIODevice::WriteOnly), "a file for the link could be made");
		file.close();
		panel.setSourceState(true, existing, QString());
		ok &= expect(panel.sourceStatusLabel(true)->text() == "Linked: linked.txt" && panel.sourceStatusLabel(true)->toolTip() == display_path::format(existing)
				&& panel.unlinkButton(true)->isEnabled() && panel.retryButton(true)->isEnabled()
				&& panel.sourceStatusLabel(false)->text() == "Unlinked",
			"a linked source shows its file, with the path as tooltip, and the other source is not touched");
		const QString missing = directory.filePath("missing.txt");
		const auto shownText = [&panel](bool traction) { return QString(panel.sourceStatusLabel(traction)->text()).remove(QChar(0x200B)); };
		const QString missingText = "Missing: " + display_path::format(missing) + "; Retry available";
		panel.setSourceState(true, missing, QString());
		ok &= expect(shownText(true) == missingText && panel.sourceStatusLabel(true)->toolTip() == display_path::format(missing)
				&& panel.unlinkButton(true)->isEnabled() && panel.retryButton(true)->isEnabled(),
			"a linked file that is gone is missing, with its path, and can be retried or unlinked");
		for (const char* status : {"Rejected: The traction source could not be read; Retry available", "Kept local; Retry available",
				 "Up to date", "Linked; waiting for reload"}) {
			panel.setSourceState(true, missing, status);
			ok &= expect(shownText(true) == missingText, "a linked file that is gone is missing whatever the last status was");
		}
		panel.setSourceState(false, missing, "Kept local; Retry available");
		ok &= expect(shownText(false) == missingText && shownText(true) == missingText,
			"the parameter source and the traction source each show their own missing file");
		panel.setSourceState(false, existing, "Rejected: The parameter source could not be read; Retry available");
		ok &= expect(panel.sourceStatusLabel(false)->text() == "Rejected: The parameter source could not be read; Retry available",
			"a file that exists and cannot be read is rejected, not missing");
		panel.setSourceState(false, existing, "Kept local; Retry available");
		ok &= expect(panel.sourceStatusLabel(false)->text() == "Kept local; Retry available",
			"a file that exists keeps the status of its link");
		panel.setSourceState(true, existing, "Reloaded");
		ok &= expect(panel.sourceStatusLabel(true)->text() == "Reloaded", "the status of the link replaces the default text");
		panel.listWidget()->setCurrentRow(-1);
		ok &= expect(panel.sourceStatusLabel(true)->text() == "Reloaded", "the state stays until the owner shows it again");
		panel.setSourceState(true, existing, "Reloaded");
		ok &= expect(panel.sourceStatusLabel(true)->text() == "No unit selected" && !panel.linkButton(true)->isEnabled(),
			"without a selected unit the state is not shown");
	}

	for (const double scale : {1.0, 1.5}) {
		// the buttons of the links and of the traction curve are in two rows and fit the panel at its minimum width
		SceneDocument document;
		TrainUnitPanel panel;
		QFont font = panel.font();
		font.setPointSizeF(font.pointSizeF() * scale);
		panel.setFont(font);
		panel.setDocument(&document);
		document.replace(unitScene(), false);
		panel.show();
		panel.resize(panel.minimumSizeHint());
		QApplication::processEvents();
		const QString label = QString(" at %1 times the font").arg(scale);
		ok &= expect(panel.minimumSizeHint().width() <= 480 * scale, qPrintable("the panel is not wider than a dock of 480 pixels needs" + label));
		const auto inside = [&panel](const QList<QPushButton*>& buttons) {
			bool whole = true;
			for (const QPushButton* button : buttons) {
				const QRect placed(button->mapTo(&panel, QPoint(0, 0)), button->size());
				whole = whole && button->isVisible() && panel.rect().contains(placed) && button->width() >= button->minimumSizeHint().width();
			}
			return whole;
		};
		panel.tabs()->setCurrentIndex(0);
		QApplication::processEvents();
		QPushButton* link = panel.linkButton(true);
		QPushButton* unlink = panel.unlinkButton(true);
		QPushButton* retry = panel.retryButton(true);
		ok &= expect(inside({link, unlink, retry, panel.addTractionRowButton(), panel.removeTractionRowButton(), panel.plotButton()}),
			qPrintable("the buttons of the Traction tab are whole inside the panel at its minimum width" + label));
		ok &= expect(link->y() < unlink->y() && unlink->y() == retry->y() && unlink->x() < retry->x(),
			qPrintable("Link is above Unlink and Retry on the Traction tab" + label));
		ok &= expect(panel.addTractionRowButton()->y() == panel.removeTractionRowButton()->y()
				&& panel.plotButton()->y() > panel.addTractionRowButton()->y(),
			qPrintable("Plot is under the traction row buttons" + label));
		panel.tabs()->setCurrentIndex(1);
		QApplication::processEvents();
		ok &= expect(inside({panel.linkButton(false), panel.unlinkButton(false), panel.retryButton(false)}),
			qPrintable("the buttons of the Physical tab are whole inside the panel at its minimum width" + label));
		ok &= expect(panel.linkButton(false)->y() < panel.unlinkButton(false)->y()
				&& panel.unlinkButton(false)->y() == panel.retryButton(false)->y(),
			qPrintable("Link is above Unlink and Retry on the Physical tab" + label));
		bool fieldsInside = true;
		for (const QDoubleSpinBox* field : panel.physicalEdits())
			fieldsInside = fieldsInside && panel.rect().contains(QRect(field->mapTo(&panel, QPoint(0, 0)), field->size()));
		ok &= expect(fieldsInside, qPrintable("the physical fields are whole inside the panel at its minimum width" + label));
	}

	{
		// focus decides which number fields are pending edits
		SceneDocument document;
		TrainUnitPanel panel;
		panel.setDocument(&document);
		document.replace(unitScene(), false);
		panel.show();
		panel.activateWindow();
		QApplication::processEvents();
		QDoubleSpinBox* length = panel.physicalEdits()[8];
		panel.tabs()->setCurrentIndex(1);
		length->setFocus();
		QApplication::processEvents();
		if (!length->hasFocus()) {
			std::cerr << "failed: the length field could not be given focus\n";
			ok = false;
		} else {
			const quint64 revision = document.revision();
			spinText(length)->setText("77");
			ok &= expect(document.model().trainUnits[0].physical.length_m == 100.0, "a number that is being typed is not in the model");
			panel.commitPendingEditors();
			ok &= expect(document.model().trainUnits[0].physical.length_m == 77.0 && document.revision() == revision + 1,
				"committing the pending editors takes the number that has focus");
			spinText(length)->setText("not a number");
			panel.commitPendingEditors();
			ok &= expect(document.model().trainUnits[0].physical.length_m == 77.0 && document.revision() == revision + 1,
				"committing the pending editors does not take text that is not a number");
			panel.tabs()->setCurrentIndex(0);
			QDoubleSpinBox* curveCell = cell(panel, 0, 2);
			curveCell->setFocus();
			QApplication::processEvents();
			spinText(curveCell)->setText("1234");
			panel.commitPendingEditors();
			ok &= expect(document.model().trainUnits[0].tractionCurve[0][2] == 1234.0,
				"committing the pending editors takes the number of a table cell that has focus");
			panel.tabs()->setCurrentIndex(1);
			length->setFocus();
			QApplication::processEvents();
			if (!length->hasFocus()) {
				std::cerr << "failed: the length field could not be given focus on the Physical page\n";
				ok = false;
			} else {
				spinText(length)->setText("88");
				panel.tabs()->setCurrentIndex(0);
				QApplication::processEvents();
				ok &= expect(document.model().trainUnits[0].physical.length_m == 88.0,
					"leaving the Physical page takes the number that was typed in a field");
			}
		}
	}

	{
		auto* document = new SceneDocument;
		TrainUnitPanel panel;
		panel.setDocument(document);
		document->replace(unitScene(), false);
		delete document;
		panel.idEdit()->setText("after the document");
		panel.commitIdEdit();
		panel.addUnit();
		panel.refresh();
		ok &= expect(panel.listWidget()->count() == 0 && !panel.addButton()->isEnabled() && panel.selectedUnit() == nullptr,
			"a panel whose document is gone shows nothing and edits nothing");
	}

	if (!ok)
		return 1;
	std::cout << "all train unit panel tests passed\n";
	return 0;
}
