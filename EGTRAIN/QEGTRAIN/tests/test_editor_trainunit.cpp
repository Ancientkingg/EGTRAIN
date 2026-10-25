#include "editors/TrainUnitPanel.h"

#include "scene/SceneFields.h"
#include "support/FakePrompts.h"
#include "widgets/CompactDoubleSpinBox.h"
#include "widgets/DisplayPath.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

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

static const Log edit({"documentChanged", "panelChanged"});

static QStringList items(const QListWidget* list) {
	QStringList result;
	for (int row = 0; row < list->count(); ++row)
		result << list->item(row)->text();
	return result;
}

// The number in a cell of the traction table.
static double cellValue(TrainUnitPanel& panel, int row, int column) {
	return panel.tractionTable()->item(row, column)->data(Qt::EditRole).toDouble();
}

// Opens the editor of a cell of the traction table, as a double click on it does, and returns the editor.
static QDoubleSpinBox* openCell(TrainUnitPanel& panel, int row, int column) {
	QTableWidget* table = panel.tractionTable();
	table->setCurrentCell(row, column);
	table->editItem(table->currentItem());
	return qobject_cast<QDoubleSpinBox*>(table->indexWidget(table->currentIndex()));
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

// A panel with prompts of its own on a document that shows unitScene(), the events of both, and how often the panel
// showed the details of a unit since the scene was opened.
struct UnitFixture {
	SceneDocument document;
	TrainUnitPanel panel;
	FakePrompts prompts;
	Events events{panel, document};
	int detailsShown = 0;

	UnitFixture() {
		panel.setPrompts(&prompts);
		panel.setDocument(&document);
		QObject::connect(&panel, &TrainUnitPanel::detailsShown, &panel, [this] { ++detailsShown; });
		document.replace(unitScene(), false);
	}
};

class TestTrainUnitPanel : public QObject {
	Q_OBJECT

private slots:
	void showsNothingWithoutScene();
	void emptyStateFollowsSelection();
	void showsTractionAndPhysicalPages();
	void tractionTableKeys();
	void showsNewScene();
	void buttonsOnlyAsk();
	void keepsUnitWithoutPhysicalValues();
	void editsTractionRows();
	void editsRenamesAddsAndDeletesUnits();
	void showsAnotherScene();
	void detachedPanelIgnoresDocument();
	void showsSourceLinks();
	void buttonsFitNarrowPanel_data();
	void buttonsFitNarrowPanel();
	void focusDecidesPendingEdits();
	void numberFieldsStopAtRanges();
	void keepsSelectionOfReorderedScene();
	void forgetsDeletedDocument();
};

void TestTrainUnitPanel::showsNothingWithoutScene() {
	TrainUnitPanel panel;
	// A panel without a document shows nothing and offers nothing.
	QCOMPARE(panel.listWidget()->count(), 0);
	QVERIFY(!panel.listWidget()->isEnabled());
	QVERIFY(!panel.addButton()->isEnabled());
	QVERIFY(!panel.idEdit()->isEnabled());
	QVERIFY(!panel.tractionTable()->isEnabled());
	QVERIFY(panel.selectedUnit() == nullptr);
	SceneDocument document;
	panel.setDocument(&document);
	panel.addUnit();
	panel.duplicateUnit();
	panel.deleteUnit();
	panel.addTractionRow();
	panel.commitIdEdit();
	panel.commitPendingEditors();
	// A panel stays disabled while its document holds no scene, and its actions change nothing.
	QVERIFY(!panel.addButton()->isEnabled());
	QCOMPARE(document.revision(), quint64(0));
	QVERIFY(!document.isDirty());
	QVERIFY(document.model().trainUnits.empty());
	// A panel without a scene shows no message and no values.
	QVERIFY(panel.emptyStateLabel()->isHidden());
	QVERIFY(panel.tabs()->isHidden());
}

// The empty state follows the selection.
void TestTrainUnitPanel::emptyStateFollowsSelection() {
	SceneDocument document;
	TrainUnitPanel panel;
	FakePrompts prompts;
	prompts.confirmAnswer = true;
	panel.setPrompts(&prompts);
	panel.setDocument(&document);
	document.replace(SceneModel(), false);
	// A scene without units says what to select, and the Add button it points to is enabled.
	QCOMPARE(panel.emptyStateLabel()->text(), "Select a rolling stock unit or use Add Unit.");
	QVERIFY(!panel.emptyStateLabel()->isHidden());
	QVERIFY(panel.emptyStateLabel()->property("dialogHelp").toBool());
	QVERIFY(!panel.emptyStateLabel()->property("dialogStatus").isValid());
	QVERIFY(panel.tabs()->isHidden());
	QVERIFY(panel.addButton()->isEnabled());
	QVERIFY(!panel.idEdit()->isEnabled());
	panel.addUnit();
	// Adding a unit removes the message and shows its values.
	QVERIFY(panel.emptyStateLabel()->isHidden());
	QVERIFY(!panel.tabs()->isHidden());
	QVERIFY(panel.idEdit()->isEnabled());
	panel.deleteUnit();
	// Deleting the only unit brings the message back and hides the values.
	QVERIFY(!panel.emptyStateLabel()->isHidden());
	QVERIFY(panel.tabs()->isHidden());
	panel.setDocument(nullptr);
	QVERIFY2(panel.emptyStateLabel()->isHidden(), "a detached panel does not point to the disabled Add button");
}

// The detail area has a page for the traction and a page for the physical values, and no scroll area of its own.
void TestTrainUnitPanel::showsTractionAndPhysicalPages() {
	SceneDocument document;
	TrainUnitPanel panel;
	panel.setDocument(&document);
	document.replace(unitScene(), false);
	QTabWidget* tabs = panel.tabs();
	// A selected unit shows the Traction and the Physical page, starting with the Traction page.
	QVERIFY(!tabs->isHidden());
	QVERIFY(panel.emptyStateLabel()->isHidden());
	QCOMPARE(tabs->count(), 2);
	QCOMPARE(tabs->tabText(0), "Traction");
	QCOMPARE(tabs->tabText(1), "Physical");
	QCOMPARE(tabs->currentIndex(), 0);
	const QWidget* traction = tabs->widget(0);
	// The Traction page has the source reference, the curve and the Plot button.
	QVERIFY(traction->isAncestorOf(panel.sourceTractionEdit()));
	QVERIFY(traction->isAncestorOf(panel.linkButton(true)));
	QVERIFY(traction->isAncestorOf(panel.sourceStatusLabel(true)));
	QVERIFY(traction->isAncestorOf(panel.tractionTable()));
	QVERIFY(traction->isAncestorOf(panel.addTractionRowButton()));
	QVERIFY(traction->isAncestorOf(panel.removeTractionRowButton()));
	QVERIFY(traction->isAncestorOf(panel.plotButton()));
	const QWidget* physical = tabs->widget(1);
	bool physicalFieldsOnPage = true;
	for (const QDoubleSpinBox* physicalEdit : panel.physicalEdits())
		physicalFieldsOnPage = physicalFieldsOnPage && physical->isAncestorOf(physicalEdit);
	// The Physical page has the nine fields and the parameter source.
	QVERIFY(physicalFieldsOnPage);
	QVERIFY(physical->isAncestorOf(panel.sourceDataEdit()));
	QVERIFY(physical->isAncestorOf(panel.linkButton(false)));
	QVERIFY(physical->isAncestorOf(panel.sourceStatusLabel(false)));
	QVERIFY2(panel.findChildren<QScrollArea*>().isEmpty(), "the curve and the fields do not scroll inside a scroll area of the panel");
	panel.physicalEdits()[8]->setValue(123.0);
	QVERIFY2(document.model().trainUnits[0].physical.length_m == 123.0, "a field on a page that is not shown can be set");
}

// The keys of the traction table: without an open editor the arrow keys move between the cells and change no number, a
// number typed into a cell is taken with Return and dropped with Esc, and Tab leaves the table.
void TestTrainUnitPanel::tractionTableKeys() {
	SceneDocument document;
	TrainUnitPanel panel;
	panel.setDocument(&document);
	document.replace(unitScene(), false);
	panel.show();
	panel.activateWindow();
	QApplication::processEvents();
	QTableWidget* table = panel.tractionTable();
	const auto curve = [&document] { return document.model().trainUnits[0].tractionCurve; };
	const auto before = curve();
	const auto press = [](Qt::Key key, const QString& text) {
		QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier, text);
		QApplication::sendEvent(QApplication::focusWidget(), &event);
		QApplication::processEvents();
	};
	const QModelIndex firstC0 = table->model()->index(0, 2);
	table->scrollTo(firstC0);
	const QPoint c0 = table->viewport()->mapTo(&panel, table->visualRect(firstC0).center());
	// qt_handleMouseEvent, which <QTest> declares, delivers a mouse event as the window system does, so that a click gives
	// the focus as a click of the user does.
	int clickTime = 1000000;
	for (const QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease})
		qt_handleMouseEvent(panel.windowHandle(), c0, panel.mapToGlobal(c0), type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
			Qt::LeftButton, type, Qt::NoModifier, ++clickTime);
	QApplication::processEvents();
	press(Qt::Key_Down, QString());
	// Down on a clicked cell moves to the cell below and changes no number.
	QCOMPARE(table->currentRow(), 1);
	QCOMPARE(table->currentColumn(), 2);
	QVERIFY(curve() == before);
	QVERIFY(!document.isDirty());
	QCOMPARE(document.revision(), quint64(1));
	press(Qt::Key_5, "5");
	press(Qt::Key_Return, "\r");
	// A number typed into a cell replaces its number with Return.
	QCOMPARE_EQ(curve()[1][2], 5.0);
	QCOMPARE(document.revision(), quint64(2));
	press(Qt::Key_7, "7");
	press(Qt::Key_Escape, QString());
	const QTableWidgetItem* shown = table->item(1, 2);
	// Esc drops a number typed into a cell.
	QCOMPARE_EQ(curve()[1][2], 5.0);
	QCOMPARE(document.revision(), quint64(2));
	if (shown == nullptr)
		QFAIL("the cell has no item");
	QCOMPARE_EQ(shown->data(Qt::EditRole).toDouble(), 5.0);
	press(Qt::Key_Tab, "\t");
	const QWidget* focus = QApplication::focusWidget();
	// Tab leaves the traction table.
	if (focus == nullptr)
		QFAIL("no widget has the focus");
	QVERIFY(focus != table);
	QVERIFY(!table->isAncestorOf(focus));
}

void TestTrainUnitPanel::showsNewScene() {
	UnitFixture f;
	SceneDocument& document = f.document;
	TrainUnitPanel& panel = f.panel;
	QVERIFY2(f.detailsShown == 1, "the details are shown again for a new scene");
	// A new scene is listed and its first unit is selected.
	QCOMPARE(items(panel.listWidget()), QStringList({"u1", "u2"}));
	QCOMPARE(panel.listWidget()->currentRow(), 0);
	QCOMPARE(panel.selectedUnit(), &document.model().trainUnits[0]);
	// The selected unit shows its id and its physical values.
	QCOMPARE(panel.idEdit()->text(), "u1");
	QVERIFY(panel.idEdit()->isEnabled());
	QCOMPARE_EQ(panel.physicalEdits()[0]->value(), 80000.0);
	QCOMPARE_EQ(panel.physicalEdits()[3]->value(), 40.0);
	QCOMPARE_EQ(panel.physicalEdits()[8]->value(), 100.0);
	QVERIFY(panel.physicalEdits()[8]->isEnabled());
	// The selected unit shows its source references.
	QCOMPARE(panel.sourceDataEdit()->text(), "u.data");
	QCOMPARE(panel.sourceTractionEdit()->text(), "u.traction");
	// The traction curve is shown in the table.
	QCOMPARE(panel.tractionTable()->rowCount(), 2);
	QVERIFY(panel.tractionTable()->isEnabled());
	QCOMPARE_EQ(cellValue(panel, 1, 0), 10.0);
	QCOMPARE_EQ(cellValue(panel, 1, 2), 900.0);
	QCOMPARE_EQ(cellValue(panel, 1, 3), 1.0);
	const auto* cellDelegate = qobject_cast<const QStyledItemDelegate*>(panel.tractionTable()->itemDelegate());
	const CompactDoubleSpinBox field;
	bool fieldText = cellDelegate != nullptr;
	for (const double value : {900.0, -0.000123456789, 1234567.25, 1e20})
		fieldText = fieldText && cellDelegate->displayText(value, QLocale()) == field.textFromValue(value);
	QVERIFY2(fieldText, "a cell shows its number as a number field without the focus shows it");
	// The buttons of a unit with a curve are enabled.
	QVERIFY(panel.duplicateButton()->isEnabled());
	QVERIFY(panel.deleteButton()->isEnabled());
	QVERIFY(panel.addTractionRowButton()->isEnabled());
	QVERIFY(panel.removeTractionRowButton()->isEnabled());
	QVERIFY(panel.plotButton()->isEnabled());
	// Showing a scene edits nothing.
	QVERIFY(f.events.log.empty());
	QVERIFY(!document.isDirty());
	QCOMPARE(document.revision(), quint64(1));
	panel.listWidget()->setCurrentRow(1);
	// A unit without a curve shows an empty table and cannot be plotted.
	QCOMPARE(panel.idEdit()->text(), "u2");
	QCOMPARE(panel.tractionTable()->rowCount(), 0);
	QVERIFY(!panel.plotButton()->isEnabled());
	QVERIFY(panel.addTractionRowButton()->isEnabled());
	QVERIFY(!panel.removeTractionRowButton()->isEnabled());
}

// The plot and the source link buttons only ask.
void TestTrainUnitPanel::buttonsOnlyAsk() {
	UnitFixture f;
	TrainUnitPanel& panel = f.panel;
	const Events& events = f.events;
	panel.plotButton()->click();
	panel.linkButton(false)->click();
	panel.unlinkButton(true)->click();
	panel.retryButton(false)->click();
	// The plot and the source buttons are reported with the source they mean and change nothing.
	QCOMPARE(events.plotted, QStringList({"u1"}));
	QCOMPARE(events.sourceRequests.size(), std::size_t(3));
	QVERIFY(events.sourceRequests[0] == std::make_pair(std::string("link"), false));
	QVERIFY(events.sourceRequests[1] == std::make_pair(std::string("unlink"), true));
	QVERIFY(events.sourceRequests[2] == std::make_pair(std::string("retry"), false));
	QVERIFY(events.log.empty());
}

// A unit without physical values is not given them by looking at it.
void TestTrainUnitPanel::keepsUnitWithoutPhysicalValues() {
	UnitFixture f;
	f.document.mutableModel().trainUnits[1].hasPhysical = false;
	f.panel.listWidget()->setCurrentRow(1);
	f.panel.commitPendingEditors();
	// Committing the pending editors does not touch number fields that nobody is editing.
	QVERIFY(!f.document.model().trainUnits[1].hasPhysical);
	QVERIFY(f.events.log.empty());
}

// The rows of the traction curve.
void TestTrainUnitPanel::editsTractionRows() {
	UnitFixture f;
	SceneDocument& document = f.document;
	TrainUnitPanel& panel = f.panel;
	Events& events = f.events;
	panel.addTractionRow();
	// A new traction row continues the curve and is selected.
	QCOMPARE(document.model().trainUnits[0].tractionCurve.size(), std::size_t(3));
	QVERIFY((document.model().trainUnits[0].tractionCurve[2] == std::array<double, 5>{{20.0, 21.0, 0.0, 0.0, 0.0}}));
	QCOMPARE(panel.tractionTable()->rowCount(), 3);
	QCOMPARE(panel.tractionTable()->currentRow(), 2);
	QCOMPARE(events.log, edit);
	panel.tractionTable()->item(2, 2)->setData(Qt::EditRole, 500.0);
	// A number in the table is written to the curve.
	QCOMPARE_EQ(document.model().trainUnits[0].tractionCurve[2][2], 500.0);
	QCOMPARE(events.log, Log({"documentChanged", "panelChanged", "documentChanged", "panelChanged"}));
	events.log.clear();
	typeNumber(openCell(panel, 2, 2), "abc");
	panel.commitPendingEditors();
	// Text that is not a number stays out of the curve.
	QCOMPARE_EQ(document.model().trainUnits[0].tractionCurve[2][2], 500.0);
	QVERIFY(events.log.empty());
	panel.commitTractionCell(0, 5, 1.0);
	panel.commitTractionCell(7, 0, 1.0);
	panel.commitTractionCell(0, 0, 0.0);
	QVERIFY2(events.log.empty(), "a cell that does not exist, or a value that is already there, changes nothing");
	panel.tractionTable()->setCurrentCell(1, 0);
	panel.removeTractionRow();
	// The selected traction row is removed.
	QCOMPARE(document.model().trainUnits[0].tractionCurve.size(), std::size_t(2));
	QCOMPARE_EQ(document.model().trainUnits[0].tractionCurve[1][2], 500.0);
	QCOMPARE(panel.tractionTable()->rowCount(), 2);
	QCOMPARE(events.log, edit);
	events.log.clear();
	panel.tractionTable()->setCurrentCell(-1, -1);
	panel.removeTractionRow();
	// Removing without a selected row changes nothing.
	QCOMPARE(document.model().trainUnits[0].tractionCurve.size(), std::size_t(2));
	QVERIFY(events.log.empty());
}

// The number fields, the sources and the renames, then Add, Duplicate and Delete, which see the length, the source and
// the id that the edits left, and an Add while the document is closed for edits, which sees the units that Delete left.
void TestTrainUnitPanel::editsRenamesAddsAndDeletesUnits() {
	UnitFixture f;
	SceneDocument& document = f.document;
	TrainUnitPanel& panel = f.panel;
	FakePrompts& prompts = f.prompts;
	Events& events = f.events;

	// Number fields.
	panel.physicalEdits()[8]->setValue(105.5);
	// A number that is entered is written to the document and marks it changed.
	QCOMPARE_EQ(document.model().trainUnits[0].physical.length_m, 105.5);
	QVERIFY(document.isDirty());
	QCOMPARE(document.revision(), quint64(2));
	QCOMPARE(document.lastChangeLabel(), "Edit rolling stock unit");
	// A number edit is one document change.
	QCOMPARE(events.log, edit);
	QCOMPARE(events.kinds, SceneDocument::ChangeKinds(SceneDocument::TrainUnits));
	events.log.clear();
	typeNumber(panel.physicalEdits()[8], "abc");
	// Text that is not a number stays out of the model.
	QCOMPARE_EQ(document.model().trainUnits[0].physical.length_m, 105.5);
	QVERIFY(events.log.empty());
	QCOMPARE(document.revision(), quint64(2));
	typeNumber(panel.physicalEdits()[8], "");
	typeNumber(panel.physicalEdits()[8], "1e");
	// An empty or unfinished number stays out of the model.
	QCOMPARE_EQ(document.model().trainUnits[0].physical.length_m, 105.5);
	QVERIFY(events.log.empty());
	typeNumber(panel.physicalEdits()[8], "106");
	// A number that follows text that was not a number is taken.
	QCOMPARE_EQ(document.model().trainUnits[0].physical.length_m, 106.0);
	QCOMPARE(events.log, edit);
	events.log.clear();
	panel.commitPhysical(8);
	panel.commitPhysical(-1);
	panel.commitPhysical(TrainUnitPanel::PhysicalFieldCount);
	QVERIFY2(events.log.empty(), "a value that is already in the model, or a field that does not exist, changes nothing");

	// Sources.
	panel.sourceDataEdit()->setText("new.data");
	emit panel.sourceDataEdit()->editingFinished();
	// A source reference is written to the document and reported.
	QCOMPARE(document.model().trainUnits[0].sourceDataFile, "new.data");
	QCOMPARE(document.model().trainUnits[0].sourceTractionFile, "u.traction");
	QCOMPARE(events.log, Log({"documentChanged", "sourcesChanged", "panelChanged"}));
	events.log.clear();
	emit panel.sourceTractionEdit()->editingFinished();
	QVERIFY2(events.log.empty(), "leaving a source field without changing it does not touch the document");

	// A rename that is rejected, and one that is not.
	const quint64 revisionBeforeRejected = document.revision();
	panel.idEdit()->setText("   ");
	panel.commitIdEdit();
	// An empty id is dropped and the field shows the old id.
	QCOMPARE(panel.idEdit()->text(), "u1");
	QCOMPARE(document.model().trainUnits[0].id, "u1");
	panel.idEdit()->setText("u2");
	panel.commitIdEdit();
	// An id that another unit has is dropped and the field shows the old id.
	QCOMPARE(panel.idEdit()->text(), "u1");
	QCOMPARE(document.model().trainUnits[0].id, "u1");
	QCOMPARE(document.revision(), revisionBeforeRejected);
	QVERIFY(events.log.empty());
	panel.idEdit()->setText("u1_renamed");
	panel.commitIdEdit();
	// A rename is written to the document.
	QCOMPARE(document.model().trainUnits[0].id, "u1_renamed");
	QCOMPARE(document.lastChangeLabel(), "Rename rolling stock unit");
	QVERIFY2(document.model().compositions[0].units == std::vector<std::string>({"u1_renamed", "u2", "u1_renamed"}),
		"a rename rewrites the compositions that use the unit");
	// A rename reports the unit ids and the changed units after the document is marked changed.
	QCOMPARE(events.log, Log({"documentChanged", "unitRenamed", "unitsChanged", "panelChanged"}));
	QCOMPARE(events.renamed, QStringList({"u1", "u1_renamed"}));
	QVERIFY(events.kinds.testFlag(SceneDocument::TrainUnits));
	QVERIFY(events.kinds.testFlag(SceneDocument::Compositions));
	// A rename keeps the selection and shows the new id in the list.
	QCOMPARE(items(panel.listWidget()), QStringList({"u1_renamed", "u2"}));
	QCOMPARE(panel.listWidget()->currentRow(), 0);

	// Cancel: text that was typed and not finished is dropped when the panel is refreshed.
	events.log.clear();
	document.markSaved();
	const quint64 revisionBeforeCancel = document.revision();
	panel.idEdit()->setText("abandoned");
	panel.sourceDataEdit()->setText("abandoned.data");
	spinText(panel.physicalEdits()[0])->setText("12345");
	panel.refresh();
	// Refreshing drops unfinished text and leaves the document as it is.
	QCOMPARE(panel.idEdit()->text(), "u1_renamed");
	QCOMPARE(panel.sourceDataEdit()->text(), "new.data");
	QCOMPARE(spinText(panel.physicalEdits()[0])->text(), "80000");
	QVERIFY(!document.isDirty());
	QCOMPARE(document.revision(), revisionBeforeCancel);
	QVERIFY(events.log.empty());

	// Pending edits.
	panel.idEdit()->setText("u1_pending");
	panel.sourceTractionEdit()->setText("pending.traction");
	QVERIFY2(document.model().trainUnits[0].id == "u1_renamed", "text that is still being typed is not in the document");
	panel.commitPendingEditors();
	// Committing the pending editors takes the id and the source references that are being typed.
	QCOMPARE(document.model().trainUnits[0].id, "u1_pending");
	QCOMPARE(document.model().trainUnits[0].sourceTractionFile, "pending.traction");
	QCOMPARE(document.model().compositions[0].units[0], "u1_pending");

	// Add, duplicate, select.
	events.log.clear();
	panel.addUnit();
	// A new unit is added at the end and selected.
	QCOMPARE(items(panel.listWidget()), QStringList({"u1_pending", "u2", "new_train_unit"}));
	QCOMPARE(panel.listWidget()->currentRow(), 2);
	QCOMPARE(panel.idEdit()->text(), "new_train_unit");
	QCOMPARE(events.log, Log({"documentChanged", "unitsChanged", "panelChanged"}));
	QCOMPARE(events.kinds, SceneDocument::ChangeKinds(SceneDocument::TrainUnits));
	events.log.clear();
	panel.listWidget()->setCurrentRow(0);
	panel.duplicateUnit();
	// A duplicate follows its original, has its values and is selected.
	QCOMPARE(items(panel.listWidget()), QStringList({"u1_pending", "u1_pending_copy", "u2", "new_train_unit"}));
	QCOMPARE(panel.listWidget()->currentRow(), 1);
	QCOMPARE_EQ(document.model().trainUnits[1].physical.length_m, 106.0);
	QCOMPARE(events.log, Log({"documentChanged", "unitsChanged", "panelChanged"}));
	// A unit is selected by its id.
	QVERIFY(panel.selectUnit("u2"));
	QCOMPARE(panel.selectedUnit(), &document.model().trainUnits[2]);
	QVERIFY(!panel.selectUnit("nothing"));
	QCOMPARE(panel.selectedUnit(), &document.model().trainUnits[2]);

	// Delete.
	events.log.clear();
	panel.selectUnit("u1_pending");
	panel.deleteUnit();
	// A unit that a composition uses is refused, naming the composition.
	QCOMPARE(prompts.errorCount, 1);
	QVERIFY(prompts.errorWarning);
	QCOMPARE(prompts.errorTitle, "Cannot Delete Rolling Stock Unit");
	QVERIFY(prompts.errorMessage.contains("c1"));
	QCOMPARE(prompts.confirmCount, 0);
	QVERIFY(events.log.empty());
	QCOMPARE(document.model().trainUnits.size(), std::size_t(4));
	panel.selectUnit("new_train_unit");
	prompts.confirmAnswer = false;
	panel.deleteUnit();
	// Answering No keeps the unit.
	QCOMPARE(prompts.confirmCount, 1);
	QCOMPARE(prompts.confirmQuestion, "Delete rolling stock unit 'new_train_unit'?");
	QCOMPARE(document.model().trainUnits.size(), std::size_t(4));
	QVERIFY(events.log.empty());
	prompts.confirmAnswer = true;
	panel.deleteUnit();
	// Answering Yes deletes the unit and reports it.
	QCOMPARE(items(panel.listWidget()), QStringList({"u1_pending", "u1_pending_copy", "u2"}));
	QCOMPARE(panel.listWidget()->currentRow(), 2);
	QCOMPARE(events.log, Log({"documentChanged", "unitDeleted", "unitsChanged", "panelChanged"}));
	QCOMPARE(events.deleted, QStringList({"new_train_unit"}));

	// The units stay editable while the document is closed for edits.
	document.setEditable(false);
	events.log.clear();
	panel.addUnit();
	// Units can be edited while the document is closed for edits.
	QCOMPARE(document.model().trainUnits.size(), std::size_t(4));
	QCOMPARE(events.log.size(), std::size_t(3));
}

// Another scene replaces unfinished text without an edit.
void TestTrainUnitPanel::showsAnotherScene() {
	UnitFixture f;
	f.panel.idEdit()->setText("abandoned");
	SceneModel other;
	other.trainUnits = {trainUnit("other", 10.0, false)};
	f.document.replace(other, false);
	// Opening another scene replaces unfinished text without an edit.
	QCOMPARE(items(f.panel.listWidget()), QStringList({"other"}));
	QCOMPARE(f.panel.idEdit()->text(), "other");
	QVERIFY(f.events.log.empty());
}

void TestTrainUnitPanel::detachedPanelIgnoresDocument() {
	UnitFixture f;
	f.panel.setDocument(nullptr);
	// A detached panel shows nothing.
	QCOMPARE(f.panel.listWidget()->count(), 0);
	QVERIFY(!f.panel.listWidget()->isEnabled());
	QVERIFY(f.panel.selectedUnit() == nullptr);
	const quint64 revisionDetached = f.document.revision();
	f.panel.addUnit();
	f.document.replace(unitScene(), false);
	// A detached panel neither edits nor follows the document.
	QCOMPARE(f.panel.listWidget()->count(), 0);
	QCOMPARE(f.document.revision(), revisionDetached + 1);
}

// The state of the source links.
void TestTrainUnitPanel::showsSourceLinks() {
	SceneDocument document;
	TrainUnitPanel panel;
	panel.setDocument(&document);
	panel.setSourceState(false, "/some/file", "Linked");
	// The source links wait for a unit to be selected.
	QCOMPARE(panel.sourceStatusLabel(false)->text(), "No unit selected");
	QVERIFY(!panel.linkButton(false)->isEnabled());
	QVERIFY(!panel.unlinkButton(false)->isEnabled());
	QVERIFY(!panel.retryButton(false)->isEnabled());
	document.replace(unitScene(), false);
	panel.setSourceState(false, QString(), QString());
	// An unlinked source can be linked and nothing else.
	QCOMPARE(panel.sourceStatusLabel(false)->text(), "Unlinked");
	QVERIFY(panel.linkButton(false)->isEnabled());
	QVERIFY(!panel.unlinkButton(false)->isEnabled());
	QVERIFY(!panel.retryButton(false)->isEnabled());
	QVERIFY(panel.sourceStatusLabel(false)->toolTip().isEmpty());
	QTemporaryDir directory;
	const QString existing = directory.filePath("linked.txt");
	QFile file(existing);
	QVERIFY2(file.open(QIODevice::WriteOnly), "a file for the link could be made");
	file.close();
	panel.setSourceState(true, existing, QString());
	// A linked source shows its file, with the path as tooltip, and the other source is not touched.
	QCOMPARE(panel.sourceStatusLabel(true)->text(), "Linked: linked.txt");
	QCOMPARE(panel.sourceStatusLabel(true)->toolTip(), display_path::format(existing));
	QVERIFY(panel.unlinkButton(true)->isEnabled());
	QVERIFY(panel.retryButton(true)->isEnabled());
	QCOMPARE(panel.sourceStatusLabel(false)->text(), "Unlinked");
	const QString missing = directory.filePath("missing.txt");
	const auto shownText = [&panel](bool traction) { return QString(panel.sourceStatusLabel(traction)->text()).remove(QChar(0x200B)); };
	const QString missingText = "File not found: " + display_path::format(missing);
	panel.setSourceState(true, missing, QString());
	// A linked file that is gone is missing, with its path, and can be retried or unlinked.
	QCOMPARE(shownText(true), missingText);
	QCOMPARE(panel.sourceStatusLabel(true)->toolTip(), display_path::format(missing));
	QVERIFY(panel.unlinkButton(true)->isEnabled());
	QVERIFY(panel.retryButton(true)->isEnabled());
	for (const char* status : {"Not loaded: The traction file could not be read", "Kept your edits", "Up to date", "Linked"}) {
		panel.setSourceState(true, missing, status);
		QVERIFY2(shownText(true) == missingText,
			qPrintable(QString("a linked file that is gone is missing whatever the last status was (%1)").arg(status)));
	}
	panel.setSourceState(false, missing, "Kept your edits");
	// The parameter source and the traction source each show their own missing file.
	QCOMPARE(shownText(false), missingText);
	QCOMPARE(shownText(true), missingText);
	panel.setSourceState(false, existing, "Not loaded: The parameter file could not be read");
	QVERIFY2(panel.sourceStatusLabel(false)->text() == "Not loaded: The parameter file could not be read",
		"a file that exists and cannot be read is rejected, not missing");
	panel.setSourceState(false, existing, "Kept your edits");
	QVERIFY2(panel.sourceStatusLabel(false)->text() == "Kept your edits", "a file that exists keeps the status of its link");
	panel.setSourceState(true, existing, "Reloaded");
	QVERIFY2(panel.sourceStatusLabel(true)->text() == "Reloaded", "the status of the link replaces the default text");
	panel.listWidget()->setCurrentRow(-1);
	QVERIFY2(panel.sourceStatusLabel(true)->text() == "Reloaded", "the state stays until the owner shows it again");
	panel.setSourceState(true, existing, "Reloaded");
	// Without a selected unit the state is not shown.
	QCOMPARE(panel.sourceStatusLabel(true)->text(), "No unit selected");
	QVERIFY(!panel.linkButton(true)->isEnabled());
}

void TestTrainUnitPanel::buttonsFitNarrowPanel_data() {
	QTest::addColumn<double>("scale");
	QTest::newRow("1 times the font") << 1.0;
	QTest::newRow("1.5 times the font") << 1.5;
}

// The buttons of the links and of the traction curve are in two rows and fit the panel at its minimum width.
void TestTrainUnitPanel::buttonsFitNarrowPanel() {
	QFETCH(double, scale);
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
	QVERIFY2(panel.minimumSizeHint().width() <= 480 * scale, "the panel is not wider than a dock of 480 pixels needs");
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
	QVERIFY2(inside({link, unlink, retry, panel.addTractionRowButton(), panel.removeTractionRowButton(), panel.plotButton()}),
		"the buttons of the Traction tab are whole inside the panel at its minimum width");
	// Link is above Unlink and Retry on the Traction tab.
	QCOMPARE_LT(link->y(), unlink->y());
	QCOMPARE(unlink->y(), retry->y());
	QCOMPARE_LT(unlink->x(), retry->x());
	// Plot is under the traction row buttons.
	QCOMPARE(panel.addTractionRowButton()->y(), panel.removeTractionRowButton()->y());
	QCOMPARE_GT(panel.plotButton()->y(), panel.addTractionRowButton()->y());
	panel.tabs()->setCurrentIndex(1);
	QApplication::processEvents();
	QVERIFY2(inside({panel.linkButton(false), panel.unlinkButton(false), panel.retryButton(false)}),
		"the buttons of the Physical tab are whole inside the panel at its minimum width");
	// Link is above Unlink and Retry on the Physical tab.
	QCOMPARE_LT(panel.linkButton(false)->y(), panel.unlinkButton(false)->y());
	QCOMPARE(panel.unlinkButton(false)->y(), panel.retryButton(false)->y());
	bool fieldsInside = true;
	for (const QDoubleSpinBox* field : panel.physicalEdits())
		fieldsInside = fieldsInside && panel.rect().contains(QRect(field->mapTo(&panel, QPoint(0, 0)), field->size()));
	QVERIFY2(fieldsInside, "the physical fields are whole inside the panel at its minimum width");
}

// Focus decides which number fields are pending edits.
void TestTrainUnitPanel::focusDecidesPendingEdits() {
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
	QVERIFY2(length->hasFocus(), "the length field could not be given focus");
	const quint64 revision = document.revision();
	spinText(length)->setText("77");
	QVERIFY2(document.model().trainUnits[0].physical.length_m == 100.0, "a number that is being typed is not in the model");
	panel.commitPendingEditors();
	// Committing the pending editors takes the number that has focus.
	QCOMPARE_EQ(document.model().trainUnits[0].physical.length_m, 77.0);
	QCOMPARE(document.revision(), revision + 1);
	spinText(length)->setText("not a number");
	panel.commitPendingEditors();
	// Committing the pending editors does not take text that is not a number.
	QCOMPARE_EQ(document.model().trainUnits[0].physical.length_m, 77.0);
	QCOMPARE(document.revision(), revision + 1);
	panel.tabs()->setCurrentIndex(0);
	QDoubleSpinBox* curveCell = openCell(panel, 0, 2);
	spinText(curveCell)->setText("1234");
	panel.commitPendingEditors();
	// Committing the pending editors takes the number of the open editor of the table.
	QCOMPARE_EQ(document.model().trainUnits[0].tractionCurve[0][2], 1234.0);
	QCOMPARE_EQ(cellValue(panel, 0, 2), 1234.0);
	panel.tabs()->setCurrentIndex(1);
	length->setFocus();
	QApplication::processEvents();
	QVERIFY2(length->hasFocus(), "the length field could not be given focus on the Physical page");
	spinText(length)->setText("88");
	panel.tabs()->setCurrentIndex(0);
	QApplication::processEvents();
	QVERIFY2(document.model().trainUnits[0].physical.length_m == 88.0, "leaving the Physical page takes the number that was typed in a field");
}

// The number fields stop at their ranges, and a stored number outside its range is shown at the nearest limit but kept.
void TestTrainUnitPanel::numberFieldsStopAtRanges() {
	const SceneValueRange& lengthRange = kSceneTrainPhysicalFields[8].range;
	const SceneValueRange& c0Range = kSceneTractionColumns[2].range;
	SceneModel scene = unitScene();
	scene.trainUnits[0].physical.length_m = 1e300;
	scene.trainUnits[0].tractionCurve[0][2] = -1e300;
	scene.trainUnits[0].tractionCurve[0][3] = 1e-20;
	SceneDocument document;
	TrainUnitPanel panel;
	panel.setDocument(&document);
	document.replace(scene, false);
	panel.show();
	panel.activateWindow();
	QApplication::processEvents();
	QDoubleSpinBox* length = panel.physicalEdits()[8];
	panel.tabs()->setCurrentIndex(1);
	length->setFocus();
	QApplication::processEvents();
	const bool lengthFocused = length->hasFocus();
	panel.commitPendingEditors();
	panel.tabs()->setCurrentIndex(0);
	QDoubleSpinBox* c0 = openCell(panel, 0, 2);
	QApplication::processEvents();
	// The number fields have the shared ranges and show a number beyond them at the nearest limit.
	QCOMPARE_EQ(length->minimum(), lengthRange.min);
	QCOMPARE_EQ(length->maximum(), lengthRange.max);
	QCOMPARE_EQ(length->value(), lengthRange.max);
	QCOMPARE_EQ(cellValue(panel, 0, 2), c0Range.min);
	if (c0 == nullptr)
		QFAIL("the cell did not open a number editor");
	QCOMPARE_EQ(c0->minimum(), c0Range.min);
	QCOMPARE_EQ(c0->maximum(), c0Range.max);
	QCOMPARE_EQ(c0->value(), c0Range.min);
	// The editor of a narrow column is as wide as it asks to be.
	QCOMPARE_GE(c0->width(), std::min(c0->sizeHint().width(), panel.tractionTable()->viewport()->width()));
	const bool c0Focused = c0->hasFocus();
	panel.commitPendingEditors();
	// Focusing a number field or opening a cell that shows a limit does not replace the stored number.
	QVERIFY(lengthFocused);
	QVERIFY(c0Focused);
	QCOMPARE_EQ(document.model().trainUnits[0].physical.length_m, 1e300);
	QCOMPARE_EQ(document.model().trainUnits[0].tractionCurve[0][2], -1e300);
	QCOMPARE(document.revision(), quint64(1));
	length->setValue(100.0);
	length->setValue(1e300);
	c0->setValue(0.0);
	c0->setValue(1e300);
	panel.commitPendingEditors();
	// A number that is entered beyond the range stops at the limit.
	QCOMPARE_EQ(document.model().trainUnits[0].physical.length_m, lengthRange.max);
	QCOMPARE_EQ(document.model().trainUnits[0].tractionCurve[0][2], c0Range.max);
	openCell(panel, 0, 3);
	panel.commitPendingEditors();
	// A cell shows a number as its editor rounds it, and an editor that closes unchanged keeps the stored number.
	QCOMPARE_EQ(cellValue(panel, 0, 3), 0.0);
	QCOMPARE_EQ(document.model().trainUnits[0].tractionCurve[0][3], 1e-20);
}

// Opening a scene that lists the selected rolling stock unit at another row keeps it selected.
void TestTrainUnitPanel::keepsSelectionOfReorderedScene() {
	SceneDocument document;
	TrainUnitPanel panel;
	panel.setDocument(&document);
	document.replace(unitScene(), false);
	panel.listWidget()->setCurrentRow(1);
	SceneModel reordered = unitScene();
	std::reverse(reordered.trainUnits.begin(), reordered.trainUnits.end());
	document.replace(reordered, false);
	// Opening a scene that lists the selected unit at another row keeps it selected.
	QCOMPARE(items(panel.listWidget()), QStringList({"u2", "u1"}));
	QCOMPARE(panel.listWidget()->currentRow(), 0);
	QCOMPARE(panel.idEdit()->text(), "u2");
}

void TestTrainUnitPanel::forgetsDeletedDocument() {
	auto* document = new SceneDocument;
	TrainUnitPanel panel;
	panel.setDocument(document);
	document->replace(unitScene(), false);
	delete document;
	panel.idEdit()->setText("after the document");
	panel.commitIdEdit();
	panel.addUnit();
	panel.refresh();
	// A panel whose document is gone shows nothing and edits nothing.
	QCOMPARE(panel.listWidget()->count(), 0);
	QVERIFY(!panel.addButton()->isEnabled());
	QVERIFY(panel.selectedUnit() == nullptr);
}

QTEST_MAIN(TestTrainUnitPanel)
#include "test_editor_trainunit.moc"
