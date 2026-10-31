#include "editors/CompositionPanel.h"
#include "editors/TrainUnitPanel.h"

#include "diagrams/TractionCurve.h"
#include "support/FakePrompts.h"

#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMessageBox>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QScopeGuard>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

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
		QObject::connect(&panel, &CompositionPanel::changed, &panel, [this] { log.push_back("panelChanged"); });
		QObject::connect(&panel, &CompositionPanel::tractionPlotRequested, &panel,
			[this](const QString& unitId) { plotted << unitId; });
	}
};

using Log = std::vector<std::string>;

static const Log edit({"documentChanged", "panelChanged"});

// Closes each message box that opens while `action` runs, and returns how many did.
static int messageBoxesDuring(const std::function<void()>& action) {
	int shown = 0;
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, [&shown] {
		if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
			++shown;
			box->accept();
		}
	});
	timer.start(0);
	action();
	return shown;
}

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

// A panel with prompts of its own on a document that shows compositionScene(), and the events of both.
struct CompositionFixture {
	SceneDocument document;
	CompositionPanel panel;
	FakePrompts prompts;
	Events events{panel, document};

	CompositionFixture() {
		panel.setPrompts(&prompts);
		panel.setDocument(&document);
		document.replace(compositionScene(), false);
	}
};

class TestCompositionPanel : public QObject {
	Q_OBJECT

private slots:
	void showsNothingWithoutScene();
	void showsNewScene();
	void showsSelectedUnit();
	void renamesAddsAndDeletesCompositions();
	void editsUnits();
	void editableDuringRun();
	void followsRollingStock();
	void showsAnotherScene();
	void detachedPanelIgnoresDocument();
	void keepsSelectionOfReorderedScene();
	void keepsTypedTextWhenUnitIsAdded();
	void forgetsDeletedDocument();
	void emptyStateFollowsSelection();
	void unitsEmptyStateFollowsUnits();
	void unitButtonsFitNarrowPanel_data();
	void unitButtonsFitNarrowPanel();
	void reportsUndefinedUnitAsError();
	void showsMessagesThroughPrompts();
};

// The units of a composition follow the rolling stock, with the same unit selected.
void TestCompositionPanel::followsRollingStock() {
	CompositionFixture f;
	CompositionPanel& panel = f.panel;
	panel.listWidget()->setCurrentRow(0);
	panel.unitsListWidget()->setCurrentRow(1);
	QVERIFY(f.document.edit(SceneDocument::TrainUnits | SceneDocument::Compositions, "Rename rolling stock unit", [](SceneModel& model) {
		model.trainUnits[1].id = "renamed";
		model.compositions[0].units[1] = "renamed";
	}));
	QCOMPARE(panel.unitsListWidget()->currentItem()->text(), "renamed");
	QVERIFY(f.document.edit(SceneDocument::TrainUnits, "Edit rolling stock unit sources",
		[](SceneModel& model) { model.trainUnits[1].sourceDataFile = "u2.xml"; }));
	QCOMPARE(panel.unitsListWidget()->currentRow(), 1);
	QCOMPARE(panel.unitSourceDataLabel()->text(), "u2.xml");
}

void TestCompositionPanel::showsNothingWithoutScene() {
	CompositionPanel panel;
	// A panel without a document shows nothing and offers nothing.
	QCOMPARE(panel.listWidget()->count(), 0);
	QVERIFY(!panel.listWidget()->isEnabled());
	QVERIFY(!panel.addButton()->isEnabled());
	QVERIFY(!panel.idEdit()->isEnabled());
	QVERIFY(!panel.addUnitButton()->isEnabled());
	QVERIFY2(panel.emptyStateLabel()->isHidden(), "a panel without a scene does not point to Add Composition, which is disabled");
	// The source labels show their paths as wrapped plain text.
	QVERIFY(panel.unitSourceDataLabel()->wordWrap());
	QCOMPARE(panel.unitSourceDataLabel()->textFormat(), Qt::PlainText);
	QVERIFY(panel.unitSourceTractionLabel()->wordWrap());
	QCOMPARE(panel.unitSourceTractionLabel()->textFormat(), Qt::PlainText);
	SceneDocument document;
	panel.setDocument(&document);
	panel.addComposition();
	panel.duplicateComposition();
	panel.deleteComposition();
	panel.addUnit();
	panel.commitIdEdit();
	// A panel stays disabled while its document holds no scene, and its actions change nothing.
	QVERIFY(!panel.addButton()->isEnabled());
	QCOMPARE(document.revision(), quint64(0));
	QVERIFY(!document.isDirty());
	QVERIFY(document.model().compositions.empty());
}

void TestCompositionPanel::showsNewScene() {
	CompositionFixture f;
	CompositionPanel& panel = f.panel;
	// A new scene is listed and its first composition is selected.
	QCOMPARE(items(panel.listWidget()), QStringList({"c1", "c2"}));
	QCOMPARE(panel.listWidget()->currentRow(), 0);
	QVERIFY(panel.listWidget()->isEnabled());
	QVERIFY(panel.addButton()->isEnabled());
	// The selected composition shows its id and its units, with no unit selected.
	QCOMPARE(panel.idEdit()->text(), "c1");
	QVERIFY(panel.idEdit()->isEnabled());
	QCOMPARE(items(panel.unitsListWidget()), QStringList({"u1", "u2"}));
	QCOMPARE(panel.unitsListWidget()->currentRow(), -1);
	// A selected composition has no empty-state message.
	QVERIFY(panel.emptyStateLabel()->isHidden());
	QVERIFY(panel.emptyStateLabel()->text().isEmpty());
	// The unit buttons wait for a unit to be selected.
	QVERIFY(panel.duplicateButton()->isEnabled());
	QVERIFY(panel.deleteButton()->isEnabled());
	QVERIFY(panel.addUnitButton()->isEnabled());
	QVERIFY(!panel.removeUnitButton()->isEnabled());
	QVERIFY(!panel.moveUnitUpButton()->isEnabled());
	QVERIFY(!panel.moveUnitDownButton()->isEnabled());
	QVERIFY(!panel.plotTractionButton()->isEnabled());
	// Showing a scene edits nothing.
	QVERIFY(f.events.log.empty());
	QVERIFY(!f.document.isDirty());
	QCOMPARE(f.document.revision(), quint64(1));
}

void TestCompositionPanel::showsSelectedUnit() {
	CompositionFixture f;
	CompositionPanel& panel = f.panel;
	panel.unitsListWidget()->setCurrentRow(0);
	// A unit with a traction curve shows its sources and can be plotted.
	QCOMPARE(panel.unitSourceDataLabel()->text(), "u1.xml");
	QCOMPARE(panel.unitSourceTractionLabel()->text(), "(none)");
	QVERIFY(panel.plotTractionButton()->isEnabled());
	QVERIFY(panel.unitWarningLabel()->text().isEmpty());
	QVERIFY(panel.unitWarningLabel()->isHidden());
	QVERIFY(!panel.moveUnitUpButton()->isEnabled());
	QVERIFY(panel.moveUnitDownButton()->isEnabled());
	QVERIFY(panel.removeUnitButton()->isEnabled());
	panel.unitsListWidget()->setCurrentRow(1);
	// A unit without a traction curve cannot be plotted and says why.
	QCOMPARE(panel.unitSourceDataLabel()->text(), "(none)");
	QVERIFY(!panel.plotTractionButton()->isEnabled());
	QCOMPARE(panel.unitWarningLabel()->text(), QString::fromStdString(tractionAssociationWarning(true, false)));
	QVERIFY(!panel.unitWarningLabel()->isHidden());
	QCOMPARE(role(panel.unitWarningLabel()), "warning");
	QVERIFY(panel.moveUnitUpButton()->isEnabled());
	QVERIFY(!panel.moveUnitDownButton()->isEnabled());
	f.document.mutableModel().trainUnits[1].sourceDataFile = "u2.xml";
	panel.refreshUnitDetails();
	// The sources of the selected unit are shown again on request.
	QCOMPARE(panel.unitSourceDataLabel()->text(), "u2.xml");
	QVERIFY(f.events.log.empty());
	panel.unitsListWidget()->setCurrentRow(0);
	panel.plotTractionButton()->click();
	// The plot of the selected unit is requested by id.
	QCOMPARE(f.events.plotted, QStringList({"u1"}));
	QVERIFY(f.events.log.empty());
}

// A rename that is dropped, a rename, pending and unfinished text, then Add, Duplicate and Delete, which see the id that
// the renames left.
void TestCompositionPanel::renamesAddsAndDeletesCompositions() {
	CompositionFixture f;
	SceneDocument& document = f.document;
	CompositionPanel& panel = f.panel;
	FakePrompts& prompts = f.prompts;
	Events& events = f.events;

	// Dropping a rename that is rejected: an empty id and an used id.
	const quint64 revisionBeforeRejected = document.revision();
	panel.idEdit()->setText("");
	panel.commitIdEdit();
	// An empty id is dropped and the field shows the old id.
	QCOMPARE(panel.idEdit()->text(), "c1");
	QCOMPARE(document.model().compositions[0].id, "c1");
	panel.idEdit()->setText("c2");
	panel.commitIdEdit();
	// An id that another composition has is dropped and the field shows the old id.
	QCOMPARE(panel.idEdit()->text(), "c1");
	QCOMPARE(document.model().compositions[0].id, "c1");
	QCOMPARE(document.model().compositions[1].id, "c2");
	panel.idEdit()->setText("  c1  ");
	panel.commitIdEdit();
	// Rejected and unchanged ids do not touch the document.
	QCOMPARE(document.revision(), revisionBeforeRejected);
	QVERIFY(events.log.empty());
	QVERIFY(!document.isDirty());

	// Rename.
	panel.idEdit()->setText("c1_renamed");
	panel.commitIdEdit();
	// A rename is written to the document and marks it changed.
	QCOMPARE(document.model().compositions[0].id, "c1_renamed");
	QVERIFY(document.isDirty());
	QCOMPARE(document.revision(), revisionBeforeRejected + 1);
	QCOMPARE(document.lastChangeLabel(), "Rename composition");
	QVERIFY2(serviceCompositions(document) == std::vector<std::string>({"c1_renamed", "c1_renamed", "c2"}),
		"a rename rewrites the services that use the composition and no others");
	// A rename is one change, which names the compositions and the services.
	QCOMPARE(events.log, edit);
	QVERIFY(events.kinds.testFlag(SceneDocument::Compositions));
	QVERIFY(events.kinds.testFlag(SceneDocument::Services));
	// A rename keeps the selection and shows the new id in the list.
	QCOMPARE(items(panel.listWidget()), QStringList({"c1_renamed", "c2"}));
	QCOMPARE(panel.listWidget()->currentRow(), 0);
	QCOMPARE(panel.idEdit()->text(), "c1_renamed");

	// Pending edits.
	events.log.clear();
	panel.idEdit()->setText("c1_pending");
	QVERIFY2(document.model().compositions[0].id == "c1_renamed", "text that is still being typed is not in the document");
	panel.commitPendingEditors();
	// Committing the pending editors takes the id that is being typed.
	QCOMPARE(document.model().compositions[0].id, "c1_pending");
	QCOMPARE(events.log, edit);
	QVERIFY(events.kinds.testFlag(SceneDocument::Compositions));
	QCOMPARE(serviceCompositions(document)[0], "c1_pending");

	// Cancel: text that was typed and not finished is dropped when the panel is refreshed.
	events.log.clear();
	document.markSaved();
	const quint64 revisionBeforeCancel = document.revision();
	panel.idEdit()->setText("abandoned");
	panel.refresh();
	// Refreshing drops unfinished text and leaves the document as it is.
	QCOMPARE(panel.idEdit()->text(), "c1_pending");
	QCOMPARE(document.model().compositions[0].id, "c1_pending");
	QVERIFY(!document.isDirty());
	QCOMPARE(document.revision(), revisionBeforeCancel);
	QVERIFY(events.log.empty());

	// Add and duplicate.
	events.log.clear();
	panel.addComposition();
	// A new composition is added at the end and selected.
	QCOMPARE(document.model().compositions.size(), std::size_t(3));
	QCOMPARE(document.model().compositions[2].id, "new_composition");
	QCOMPARE(panel.listWidget()->currentRow(), 2);
	QCOMPARE(panel.idEdit()->text(), "new_composition");
	QCOMPARE(events.log, edit);
	QCOMPARE(events.kinds, SceneDocument::ChangeKinds(SceneDocument::Compositions));
	panel.addComposition();
	QVERIFY2(document.model().compositions[3].id == "new_composition_2", "a new composition gets an unused id");
	events.log.clear();
	panel.listWidget()->setCurrentRow(1);
	panel.duplicateComposition();
	// A duplicate follows its original, has its units and is selected.
	QCOMPARE(items(panel.listWidget()), QStringList({"c1_pending", "c2", "c2_copy", "new_composition", "new_composition_2"}));
	QCOMPARE(document.model().compositions[2].units, document.model().compositions[1].units);
	QCOMPARE(panel.listWidget()->currentRow(), 2);
	QCOMPARE(events.log, edit);
	QVERIFY(events.kinds.testFlag(SceneDocument::Compositions));

	// Delete.
	events.log.clear();
	panel.listWidget()->setCurrentRow(0);
	panel.deleteComposition();
	// A composition that services use is refused, naming the services.
	QCOMPARE(prompts.errorCount, 1);
	QVERIFY(prompts.errorWarning);
	QCOMPARE(prompts.errorTitle, "Cannot Delete Composition");
	QVERIFY(prompts.errorMessage.contains("c1_pending"));
	QVERIFY(prompts.errorMessage.contains("s1"));
	QVERIFY(prompts.errorMessage.contains("s2"));
	QVERIFY(!prompts.errorMessage.contains("s3"));
	QCOMPARE(prompts.confirmCount, 0);
	// A refused delete changes nothing.
	QCOMPARE(document.model().compositions.size(), std::size_t(5));
	QVERIFY(events.log.empty());
	panel.listWidget()->setCurrentRow(3);
	prompts.confirmAnswer = false;
	panel.deleteComposition();
	// Answering No keeps the composition.
	QCOMPARE(prompts.confirmCount, 1);
	QCOMPARE(prompts.confirmQuestion, "Delete composition 'new_composition'?");
	QCOMPARE(document.model().compositions.size(), std::size_t(5));
	QVERIFY(events.log.empty());
	prompts.confirmAnswer = true;
	panel.deleteComposition();
	// Answering Yes deletes the composition and keeps a row selected.
	QCOMPARE(items(panel.listWidget()), QStringList({"c1_pending", "c2", "c2_copy", "new_composition_2"}));
	QCOMPARE(panel.listWidget()->currentRow(), 3);
	QCOMPARE(events.log, edit);
	QVERIFY(events.kinds.testFlag(SceneDocument::Compositions));
	events.log.clear();
	panel.deleteComposition();
	panel.deleteComposition();
	panel.deleteComposition();
	// Deleting from the end stops at the composition that a service uses.
	QCOMPARE(items(panel.listWidget()), QStringList({"c1_pending", "c2"}));
	QCOMPARE(prompts.errorCount, 2);
	QCOMPARE(panel.listWidget()->currentRow(), 1);
}

// The units of a composition: Add, then Move and Remove.
void TestCompositionPanel::editsUnits() {
	CompositionFixture f;
	SceneDocument& document = f.document;
	CompositionPanel& panel = f.panel;
	FakePrompts& prompts = f.prompts;
	Events& events = f.events;

	prompts.chosenItem = "";
	panel.addUnit();
	// Cancelling the choice of a unit adds nothing.
	QCOMPARE(prompts.offeredItems, QStringList({"u1", "u2", "u3"}));
	QCOMPARE(document.model().compositions[0].units.size(), std::size_t(2));
	QVERIFY(events.log.empty());
	prompts.chosenItem = "u3";
	panel.addUnit();
	// A chosen unit is added at the end and selected.
	QCOMPARE(document.model().compositions[0].units, std::vector<std::string>({"u1", "u2", "u3"}));
	QCOMPARE(items(panel.unitsListWidget()), QStringList({"u1", "u2", "u3"}));
	QCOMPARE(panel.unitsListWidget()->currentRow(), 2);
	QCOMPARE(events.log, edit);
	QCOMPARE(events.kinds, SceneDocument::ChangeKinds(SceneDocument::Compositions));

	events.log.clear();
	panel.unitsListWidget()->setCurrentRow(2);
	panel.moveUnitUp();
	// A unit moves up and stays selected.
	QCOMPARE(document.model().compositions[0].units, std::vector<std::string>({"u1", "u3", "u2"}));
	QCOMPARE(panel.unitsListWidget()->currentRow(), 1);
	QCOMPARE(events.log, edit);
	panel.moveUnitDown();
	panel.moveUnitDown();
	// A unit moves down until it is last.
	QCOMPARE(document.model().compositions[0].units, std::vector<std::string>({"u1", "u2", "u3"}));
	QCOMPARE(panel.unitsListWidget()->currentRow(), 2);
	QVERIFY(!panel.moveUnitDownButton()->isEnabled());
	events.log.clear();
	panel.moveUnitDown();
	panel.unitsListWidget()->setCurrentRow(0);
	panel.moveUnitUp();
	// A unit cannot move past either end.
	QCOMPARE(document.model().compositions[0].units, std::vector<std::string>({"u1", "u2", "u3"}));
	QVERIFY(events.log.empty());
	panel.unitsListWidget()->setCurrentRow(1);
	panel.removeUnit();
	// A removed unit leaves the next unit selected.
	QCOMPARE(document.model().compositions[0].units, std::vector<std::string>({"u1", "u3"}));
	QCOMPARE(panel.unitsListWidget()->currentRow(), 1);
	QCOMPARE(events.log, edit);
	panel.removeUnit();
	// Removing the last unit selects the new last unit.
	QCOMPARE(document.model().compositions[0].units, std::vector<std::string>({"u1"}));
	QCOMPARE(panel.unitsListWidget()->currentRow(), 0);
	panel.removeUnit();
	// Removing the only unit leaves none selected.
	QVERIFY(document.model().compositions[0].units.empty());
	QCOMPARE(panel.unitsListWidget()->currentRow(), -1);
	QVERIFY(!panel.removeUnitButton()->isEnabled());
	events.log.clear();
	panel.removeUnit();
	QVERIFY2(events.log.empty(), "removing without a selected unit changes nothing");
}

// A run does not close the compositions.
void TestCompositionPanel::editableDuringRun() {
	CompositionFixture f;
	f.document.setEditable(false);
	f.panel.addComposition();
	// Compositions can be edited while the document is closed for edits.
	QCOMPARE(f.document.model().compositions.size(), std::size_t(3));
	QCOMPARE(f.events.log, edit);
	QVERIFY(f.events.kinds.testFlag(SceneDocument::Compositions));
}

// Another scene replaces unfinished text without an edit.
void TestCompositionPanel::showsAnotherScene() {
	CompositionFixture f;
	CompositionPanel& panel = f.panel;
	panel.idEdit()->setText("abandoned");
	panel.unitsListWidget()->setCurrentRow(-1);
	SceneModel other;
	other.compositions = {composition("other", {})};
	f.document.replace(other, false);
	// Opening another scene replaces unfinished text without an edit, and adding units needs rolling stock units.
	QCOMPARE(items(panel.listWidget()), QStringList({"other"}));
	QCOMPARE(panel.idEdit()->text(), "other");
	QCOMPARE(panel.unitsListWidget()->count(), 0);
	QVERIFY(!panel.addUnitButton()->isEnabled());
	QVERIFY(f.events.log.empty());
}

void TestCompositionPanel::detachedPanelIgnoresDocument() {
	CompositionFixture f;
	f.panel.setDocument(nullptr);
	// A detached panel shows nothing.
	QCOMPARE(f.panel.listWidget()->count(), 0);
	QVERIFY(!f.panel.listWidget()->isEnabled());
	const quint64 revisionDetached = f.document.revision();
	f.panel.addComposition();
	f.document.replace(compositionScene(), false);
	// A detached panel neither edits nor follows the document.
	QCOMPARE(f.panel.listWidget()->count(), 0);
	QCOMPARE(f.document.revision(), revisionDetached + 1);
}

// Opening a scene that lists the selected composition at another row keeps it selected.
void TestCompositionPanel::keepsSelectionOfReorderedScene() {
	SceneDocument document;
	CompositionPanel panel;
	panel.setDocument(&document);
	document.replace(compositionScene(), false);
	panel.listWidget()->setCurrentRow(1);
	SceneModel reordered = compositionScene();
	std::reverse(reordered.compositions.begin(), reordered.compositions.end());
	document.replace(reordered, false);
	QCOMPARE(items(panel.listWidget()), QStringList({"c2", "c1"}));
	QCOMPARE(panel.listWidget()->currentRow(), 0);
	QCOMPARE(panel.idEdit()->text(), "c2");
}

// An id that is still being typed stays when the rolling stock panel adds a unit, which shows the compositions again: the
// panel takes the text into the document first, as an edit of its own after the other one. The labels and kinds of the
// changes are kept in their order, by a connection made before those of the panels, as the window keeps its undo steps.
void TestCompositionPanel::keepsTypedTextWhenUnitIsAdded() {
	SceneDocument document;
	Log labels;
	std::vector<SceneDocument::ChangeKinds> kinds;
	QObject::connect(&document, &SceneDocument::changed, &document, [&](SceneDocument::ChangeKinds changedKinds) {
		labels.push_back(document.lastChangeLabel().toStdString());
		kinds.push_back(changedKinds);
	});
	FakePrompts prompts;
	CompositionPanel panel;
	TrainUnitPanel units;
	panel.setPrompts(&prompts);
	panel.setDocument(&document);
	units.setPrompts(&prompts);
	units.setDocument(&document);
	document.replace(compositionScene(), false);
	panel.listWidget()->setCurrentRow(0);
	panel.idEdit()->setText("c1_typed");
	units.addButton()->click();
	// The unit is added, and the typed id is taken in after it with the services that use the composition.
	QCOMPARE(document.model().trainUnits.size(), std::size_t(4));
	QCOMPARE(document.model().compositions[0].id, "c1_typed");
	QCOMPARE(serviceCompositions(document), std::vector<std::string>({"c1_typed", "c1_typed", "c2"}));
	QCOMPARE(labels, Log({"Add rolling stock unit", "Rename composition"}));
	QCOMPARE(kinds[0], SceneDocument::ChangeKinds(SceneDocument::TrainUnits));
	QCOMPARE(kinds[1], SceneDocument::ChangeKinds(SceneDocument::Compositions | SceneDocument::Services));
	// The field and the list show the typed id.
	QCOMPARE(panel.idEdit()->text(), "c1_typed");
	QCOMPARE(items(panel.listWidget()), QStringList({"c1_typed", "c2"}));
}

void TestCompositionPanel::forgetsDeletedDocument() {
	auto* document = new SceneDocument;
	CompositionPanel panel;
	panel.setDocument(document);
	document->replace(compositionScene(), false);
	delete document;
	panel.idEdit()->setText("after the document");
	panel.commitIdEdit();
	panel.addComposition();
	panel.refresh();
	// A panel whose document is gone shows nothing and edits nothing.
	QCOMPARE(panel.listWidget()->count(), 0);
	QVERIFY(!panel.addButton()->isEnabled());
}

// The empty state follows the selection.
void TestCompositionPanel::emptyStateFollowsSelection() {
	SceneDocument document;
	CompositionPanel panel;
	FakePrompts prompts;
	prompts.confirmAnswer = true;
	panel.setPrompts(&prompts);
	panel.setDocument(&document);
	document.replace(SceneModel(), false);
	// A scene without compositions says what to select, and the Add button it points to is enabled.
	QCOMPARE(panel.emptyStateLabel()->text(), "Select a composition or use Add Composition.");
	QVERIFY(!panel.emptyStateLabel()->isHidden());
	QVERIFY(panel.emptyStateLabel()->property("dialogHelp").toBool());
	QVERIFY(panel.addButton()->isEnabled());
	QVERIFY(!panel.idEdit()->isEnabled());
	panel.addComposition();
	// Adding a composition removes the message.
	QVERIFY(panel.emptyStateLabel()->isHidden());
	QVERIFY(panel.idEdit()->isEnabled());
	panel.deleteComposition();
	// Deleting the only composition brings the message back.
	QVERIFY(!panel.emptyStateLabel()->isHidden());
	QVERIFY(!panel.idEdit()->isEnabled());
	panel.setDocument(nullptr);
	QVERIFY2(panel.emptyStateLabel()->isHidden(), "a detached panel does not point to the disabled Add button");
}

// The list of units says when it is empty.
void TestCompositionPanel::unitsEmptyStateFollowsUnits() {
	SceneDocument document;
	CompositionPanel panel;
	FakePrompts prompts;
	panel.setPrompts(&prompts);
	panel.setDocument(&document);
	QVERIFY2(panel.unitsEmptyStateLabel()->isHidden(), "a panel without a scene has no message about units");
	document.replace(compositionScene(), false);
	// A composition with units has no message about units.
	QVERIFY(panel.unitsEmptyStateLabel()->isHidden());
	QVERIFY(panel.unitsEmptyStateLabel()->text().isEmpty());
	panel.addComposition();
	// A composition without units points to Add Unit.
	QCOMPARE(panel.unitsListWidget()->count(), 0);
	QCOMPARE(panel.unitsEmptyStateLabel()->text(), "This composition has no units. Use Add Unit.");
	QVERIFY(!panel.unitsEmptyStateLabel()->isHidden());
	QVERIFY(panel.unitsEmptyStateLabel()->property("dialogHelp").toBool());
	prompts.chosenItem = "u1";
	panel.addUnit();
	QVERIFY2(panel.unitsEmptyStateLabel()->isHidden(), "adding a unit removes the message about units");
	panel.removeUnit();
	QVERIFY2(!panel.unitsEmptyStateLabel()->isHidden(), "removing the last unit brings the message back");
	SceneModel bare;
	bare.compositions = {composition("c1", {})};
	document.replace(bare, false);
	// A scene without rolling stock units says to add one first.
	QCOMPARE(panel.unitsEmptyStateLabel()->text(), "This composition has no units. Add a rolling stock unit first.");
	QVERIFY(!panel.addUnitButton()->isEnabled());
	document.replace(SceneModel(), false);
	QVERIFY2(panel.unitsEmptyStateLabel()->isHidden(), "a scene without compositions has no message about units");
}

void TestCompositionPanel::unitButtonsFitNarrowPanel_data() {
	QTest::addColumn<double>("scale");
	QTest::newRow("1 times the font") << 1.0;
	QTest::newRow("1.5 times the font") << 1.5;
}

// The buttons of the unit list are in two rows and fit a narrow panel.
void TestCompositionPanel::unitButtonsFitNarrowPanel() {
	QFETCH(double, scale);
	SceneDocument document;
	CompositionPanel panel;
	QFont font = panel.font();
	font.setPointSizeF(font.pointSizeF() * scale);
	panel.setFont(font);
	panel.setDocument(&document);
	document.replace(compositionScene(), false);
	panel.resize(panel.minimumSizeHint());
	panel.show();
	// The panel is hidden before it is destroyed, also when a check fails.
	const auto hide = qScopeGuard([&panel] { panel.hide(); });
	QApplication::processEvents();
	const QList<QPushButton*> buttons = {panel.addUnitButton(), panel.removeUnitButton(), panel.moveUnitUpButton(), panel.moveUnitDownButton()};
	bool inside = true;
	for (const QPushButton* button : buttons) {
		const QRect placed(button->mapTo(&panel, QPoint(0, 0)), button->size());
		inside = inside && panel.rect().contains(placed) && button->width() >= button->minimumSizeHint().width();
	}
	QVERIFY2(inside, "the unit buttons are whole inside the panel at its minimum width");
	// The unit buttons are in two rows.
	QCOMPARE(buttons[0]->y(), buttons[1]->y());
	QCOMPARE(buttons[2]->y(), buttons[3]->y());
	QCOMPARE_GT(buttons[2]->y(), buttons[0]->y());
	QCOMPARE(buttons[0]->x(), buttons[2]->x());
	QCOMPARE(buttons[1]->x(), buttons[3]->x());
}

// A unit that the scene does not define is an error, not a note.
void TestCompositionPanel::reportsUndefinedUnitAsError() {
	SceneDocument document;
	CompositionPanel panel;
	panel.setDocument(&document);
	SceneModel scene = compositionScene();
	scene.compositions[0].units.push_back("missing");
	document.replace(scene, false);
	// A composition without a selected unit shows no warning.
	QVERIFY(panel.unitWarningLabel()->isHidden());
	QVERIFY(panel.unitWarningLabel()->text().isEmpty());
	panel.unitsListWidget()->setCurrentRow(2);
	// A unit that the scene does not define is reported as an error.
	QCOMPARE(panel.unitWarningLabel()->text(), QString::fromStdString(tractionAssociationWarning(false, false)));
	QVERIFY(!panel.unitWarningLabel()->isHidden());
	QCOMPARE(role(panel.unitWarningLabel()), "error");
	panel.unitsListWidget()->setCurrentRow(0);
	// Selecting a unit with a curve hides the warning again.
	QVERIFY(panel.unitWarningLabel()->isHidden());
	QVERIFY(role(panel.unitWarningLabel()) != "inline style");
}

// The messages go through the prompts, and a run without a user shows none.
void TestCompositionPanel::showsMessagesThroughPrompts() {
	CompositionFixture f;
	SceneDocument& document = f.document;
	CompositionPanel& panel = f.panel;
	FakePrompts& prompts = f.prompts;
	panel.unitsListWidget()->setCurrentRow(1);
	// A unit without a traction curve is reported through the prompts and plots nothing.
	QCOMPARE(messageBoxesDuring([&panel] { panel.plotSelectedUnitTraction(); }), 0);
	QCOMPARE(prompts.infoCount, 1);
	QCOMPARE(prompts.infoTitle, "No Traction Data");
	QCOMPARE(prompts.infoMessage, "The selected unit has no traction curve to plot.");
	QVERIFY(f.events.plotted.isEmpty());
	SceneModel bare;
	bare.compositions = {composition("c1", {})};
	document.replace(bare, false);
	// A case without rolling stock units is reported through the prompts and offers no unit.
	QCOMPARE(messageBoxesDuring([&panel] { panel.addUnit(); }), 0);
	QCOMPARE(prompts.infoCount, 2);
	QCOMPARE(prompts.infoTitle, "Add Rolling Stock Unit");
	QCOMPARE(prompts.infoMessage, "This case has no rolling stock units.");
	QVERIFY(prompts.offeredItems.isEmpty());
	UserPrompts suppressed([] { return true; });
	panel.setPrompts(&suppressed);
	const auto revision = document.revision();
	// A run without a user shows no message and adds no unit.
	QCOMPARE(messageBoxesDuring([&panel] { panel.addUnit(); }), 0);
	QCOMPARE(document.revision(), revision);
}

QTEST_MAIN(TestCompositionPanel)
#include "test_editor_composition.moc"
