#include "editors/RollingStockLibraryDialog.h"
#include "editors/TrainUnitPanel.h"

#include "scene/SceneFields.h"
#include "scene/SceneHistory.h"
#include "scene/SceneReferences.h"
#include "scene/SceneWriter.h"
#include "support/FakePrompts.h"

#include <QApplication>
#include <QCheckBox>
#include <QHeaderView>
#include <QPushButton>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Whether two units are the same, id included, value for value.
static bool sameUnit(const SceneTrainUnit& a, const SceneTrainUnit& b) {
	return a.id == b.id && a.hasPhysical == b.hasPhysical && a.tractionCurve == b.tractionCurve
		&& a.sourceDataFile == b.sourceDataFile && a.sourceTractionFile == b.sourceTractionFile
		&& std::all_of(kSceneTrainPhysicalFields.begin(), kSceneTrainPhysicalFields.end(),
			[&a, &b](const SceneTrainPhysicalField& field) { return a.physical.*field.member == b.physical.*field.member; });
}

// The bundled case in the folder `name`, as it loads.
static SceneModel bundledCase(const std::string& name) {
	return loadScene((fs::path(EGTRAIN_SCENES_DIR) / name).u8string()).scene;
}

// The rows of the dialog: a heading, which cannot be selected, as "# text" and a unit as its id.
static QStringList rows(const QTreeWidget* tree) {
	QStringList result;
	for (int row = 0; row < tree->topLevelItemCount(); ++row) {
		const QTreeWidgetItem* item = tree->topLevelItem(row);
		result << (item->flags() == Qt::NoItemFlags ? "# " + item->text(0) : item->text(0));
	}
	return result;
}

// The row of the unit `id` in the dialog, or null.
static QTreeWidgetItem* unitRow(const QTreeWidget* tree, const QString& id) {
	const QList<QTreeWidgetItem*> found = tree->findItems(id, Qt::MatchExactly);
	return found.isEmpty() ? nullptr : found.front();
}

class TestRollingStockLibrary : public QObject {
	Q_OBJECT

private slots:
	void readsBundledLibrary();
	void reportsUnreadableLibrary();
	void listsUnitsByCase();
	void addsUnitToNewCase();
	void savedCopyReopensWithCaseValues();
};

// The library holds the units of the bundled cases, each as its cases load it.
void TestRollingStockLibrary::readsBundledLibrary() {
	const RollingStockLibrary library = bundledRollingStockLibrary();
	QVERIFY2(library.diagnostics.empty(), qPrintable(QString::fromStdString(library.diagnostics.empty() ? std::string() : toDisplayText(library.diagnostics.front()))));
	// The six cases hold 20 different units: SLT06 is in two of them.
	QCOMPARE(library.units.size(), std::size_t(20));
	std::map<std::string, SceneModel> cases;
	for (const RollingStockLibraryUnit& entry : library.units) {
		QVERIFY2(!entry.cases.empty(), entry.unit.id.c_str());
		for (const std::string& name : entry.cases) {
			if (cases.count(name) == 0)
				cases[name] = bundledCase(name);
			const SceneTrainUnit* unit = sceneTrainUnitById(cases[name], entry.unit.id);
			QVERIFY2(unit != nullptr && sameUnit(*unit, entry.unit),
				qPrintable(QString("%1 is as in %2; run python3 tools/rolling_stock_library.py")
						.arg(QString::fromStdString(entry.unit.id), QString::fromStdString(name))));
		}
	}
	std::vector<std::vector<std::string>> slt06;
	for (const RollingStockLibraryUnit& entry : library.units) {
		if (entry.unit.id == "SLT06")
			slt06.push_back(entry.cases);
	}
	// A unit that two cases hold with the same values is listed once, with both cases.
	QVERIFY(slt06 == std::vector<std::vector<std::string>>({{"Amsterdam_Hilversum_Student", "Netherlands"}}));
}

// A library that cannot be read says where.
void TestRollingStockLibrary::reportsUnreadableLibrary() {
	const RollingStockLibrary empty = parseRollingStockLibrary("");
	// Text that is not a JSON object is an error.
	QCOMPARE(empty.diagnostics.size(), std::size_t(1));
	QCOMPARE(empty.diagnostics[0].code, "scene.json.parse");
	QCOMPARE(empty.diagnostics[0].file, "rolling_stock_library.json");
	QVERIFY(empty.units.empty());
	const RollingStockLibrary noUnits = parseRollingStockLibrary("{}");
	// A library without units is an error.
	QVERIFY(hasErrors(noUnits.diagnostics));
	QCOMPARE(noUnits.diagnostics[0].code, "scene.section.missing");
	const RollingStockLibrary noCases = parseRollingStockLibrary(
		R"({"cases": [["Case"], [], ["Case", 1]], "compositions": [], "train_units": [{"id": "a"}, {"id": "b"}, {"id": "c"}]})");
	// A unit without a list of case names is an error at its cases, and the other units are read.
	QCOMPARE(noCases.diagnostics.size(), std::size_t(2));
	QCOMPARE(noCases.diagnostics[0].path, "cases[1]");
	QCOMPARE(noCases.diagnostics[1].path, "cases[2]");
	QCOMPARE(noCases.units.size(), std::size_t(3));
	QCOMPARE(noCases.units[0].unit.id, "a");
	QVERIFY(noCases.units[0].cases == std::vector<std::string>({"Case"}));
	const RollingStockLibrary shortCases =
		parseRollingStockLibrary(R"({"cases": [["Case"]], "compositions": [], "train_units": [{"id": "a"}, {"id": "b"}]})");
	// Cases must hold a list for each unit.
	QCOMPARE(shortCases.diagnostics.size(), std::size_t(1));
	QCOMPARE(shortCases.diagnostics[0].path, "cases");
	QCOMPARE(shortCases.units.size(), std::size_t(2));
}

// The dialog lists the units under the cases that hold them, with their id, maximum speed, length and number of traction
// rows, and adds a composition unless the tick box is cleared.
void TestRollingStockLibrary::listsUnitsByCase() {
	RollingStockLibraryDialog dialog(bundledRollingStockLibrary().units);
	const QTreeWidget* tree = dialog.unitTree();
	const QStringList shown = rows(tree);
	// Each list of cases is a heading, in the order of the case folders, and SLT06 is listed once.
	QCOMPARE(shown.filter("# ").join(" | "),
		"# Amsterdam_Hilversum_Student, Netherlands | # Assignment_Gvc_Gdg_Ut | # Copenhagen | # Milano_Brescia | # Paimpol");
	QCOMPARE(shown.mid(0, 2), QStringList({"# Amsterdam_Hilversum_Student, Netherlands", "SLT06"}));
	QCOMPARE(shown.count("SLT06"), 1);
	QCOMPARE(shown.size(), 25);
	const QTreeWidgetItem* header = tree->headerItem();
	// The columns name the id, the maximum speed, the length and the number of traction rows.
	QCOMPARE(QStringList({header->text(0), header->text(1), header->text(2), header->text(3)}),
		QStringList({"ID", "Maximum speed", "Length", "Traction rows"}));
	const QTreeWidgetItem* litra = unitRow(tree, "LITRA_SA");
	if (litra == nullptr)
		QFAIL("LITRA_SA is not listed");
	QCOMPARE(shown.mid(shown.indexOf("LITRA_SA") - 1, 2), QStringList({"# Copenhagen", "LITRA_SA"}));
	QCOMPARE(QStringList({litra->text(1), litra->text(2), litra->text(3)}), QStringList({"120 km/h", "84 m", "2"}));
	// The tick box adds a composition until it is cleared.
	QCOMPARE(dialog.compositionCheck()->text(), "Also add a composition with only this unit");
	QVERIFY(dialog.compositionCheck()->isChecked());
	QVERIFY(dialog.addsComposition());
	// The first unit is selected, so Add adds it.
	QVERIFY(dialog.addButton()->isEnabled());
	QVERIFY(dialog.selectedUnit() != nullptr);
	QCOMPARE(dialog.selectedUnit()->unit.id, "SLT06");
	QTest::keyClick(dialog.unitTree(), Qt::Key_Down);
	// The arrow keys pass over a heading.
	QCOMPARE(dialog.selectedUnit()->unit.id, "SLT_Sprinter");
	dialog.unitTree()->setCurrentItem(nullptr);
	// Without a unit there is nothing to add.
	QVERIFY(dialog.selectedUnit() == nullptr);
	QVERIFY(!dialog.addButton()->isEnabled());
	dialog.compositionCheck()->setChecked(false);
	QVERIFY(!dialog.addsComposition());
}

// In a new case, Add from Library adds LITRA_SA with the values, the traction curve and the source references that it has
// in Copenhagen, and a composition LITRA_SA, as one step that one Undo takes back.
void TestRollingStockLibrary::addsUnitToNewCase() {
	SceneDocument document;
	TrainUnitPanel panel;
	FakePrompts prompts;
	panel.setPrompts(&prompts);
	panel.setDocument(&document);
	document.replace(makeNewSceneModel(), false);
	// The history of the window records the scene after each change of the document.
	SceneHistory history;
	history.reset(document.model(), true);
	QObject::connect(&document, &SceneDocument::changed, &panel, [&history, &document] { history.record(document.model()); });
	QSignalSpy messages(&panel, &TrainUnitPanel::statusMessage);
	QPushButton* button = panel.libraryButton();
	QCOMPARE(button->text(), "Add from Library...");
	QVERIFY(button->isEnabled());
	bool listed = false;
	QTimer::singleShot(0, &panel, [&listed] {
		auto* dialog = qobject_cast<RollingStockLibraryDialog*>(QApplication::activeModalWidget());
		if (dialog == nullptr)
			return;
		QTreeWidgetItem* row = unitRow(dialog->unitTree(), "LITRA_SA");
		listed = row != nullptr;
		if (row == nullptr) {
			dialog->reject();
			return;
		}
		dialog->unitTree()->setCurrentItem(row);
		dialog->addButton()->click();
		// A dialog that did not take the choice is closed, so that the test goes on.
		if (dialog->isVisible())
			dialog->reject();
	});
	button->click();
	QVERIFY2(listed, "Add from Library... opens the dialog, which lists LITRA_SA");

	const SceneModel copenhagen = bundledCase("Copenhagen");
	const SceneTrainUnit* original = sceneTrainUnitById(copenhagen, "LITRA_SA");
	if (original == nullptr)
		QFAIL("Copenhagen has no LITRA_SA");
	const SceneModel& model = document.model();
	// The unit has the values, the traction curve and the source references of Copenhagen, and is selected.
	QCOMPARE(model.trainUnits.size(), std::size_t(1));
	QVERIFY(sameUnit(model.trainUnits[0], *original));
	QCOMPARE(panel.selectedUnit(), &model.trainUnits[0]);
	// A composition with only the unit is added with it, in one change.
	QCOMPARE(model.compositions.size(), std::size_t(1));
	QCOMPARE(model.compositions[0].id, "LITRA_SA");
	QVERIFY(model.compositions[0].units == std::vector<std::string>({"LITRA_SA"}));
	QCOMPARE(history.undoCount(), std::size_t(1));
	QCOMPARE(messages.size(), 1);
	QCOMPARE(messages[0][0].toString(), "Added rolling stock unit LITRA_SA and composition LITRA_SA.");
	QCOMPARE(prompts.infoCount, 0);
	const SceneHistoryEntry* before = history.undo();
	if (before == nullptr)
		QFAIL("nothing to undo");
	document.restore(*before->scene, !history.isAtSavedPoint());
	// One Undo takes back the unit and the composition.
	QVERIFY(document.model().trainUnits.empty());
	QVERIFY(document.model().compositions.empty());
	QVERIFY(!document.isDirty());
}

// A copied unit that is saved and opened again has the values of Copenhagen, number for number.
void TestRollingStockLibrary::savedCopyReopensWithCaseValues() {
	SceneDocument document;
	TrainUnitPanel panel;
	panel.setDocument(&document);
	document.replace(makeNewSceneModel(), false);
	const std::vector<RollingStockLibraryUnit> units = bundledRollingStockLibrary().units;
	const auto litra = std::find_if(units.begin(), units.end(),
		[](const RollingStockLibraryUnit& entry) { return entry.unit.id == "LITRA_SA"; });
	if (litra == units.end())
		QFAIL("the library has no LITRA_SA");
	panel.addLibraryUnit(litra->unit, true);
	QTemporaryDir directory;
	QVERIFY2(directory.isValid(), "a folder for the case could be made");
	const std::string folder = directory.filePath("copied").toStdString();
	const SceneSaveResult saved = saveScene(document.model(), folder);
	QVERIFY2(saved.success(), "the case with the copied unit is saved");
	const SceneLoadResult reopened = loadScene(folder);
	QVERIFY2(!hasErrors(reopened.diagnostics), "the saved case opens");
	const SceneModel copenhagen = bundledCase("Copenhagen");
	const SceneTrainUnit* original = sceneTrainUnitById(copenhagen, "LITRA_SA");
	const SceneTrainUnit* copy = sceneTrainUnitById(reopened.scene, "LITRA_SA");
	if (original == nullptr || copy == nullptr)
		QFAIL("LITRA_SA is missing from Copenhagen or from the saved case");
	// Saving and opening again keeps every value of the copy as Copenhagen has it.
	QVERIFY(sameUnit(*copy, *original));
	QCOMPARE(reopened.scene.compositions.size(), std::size_t(1));
	QVERIFY(reopened.scene.compositions[0].units == std::vector<std::string>({"LITRA_SA"}));
}

QTEST_MAIN(TestRollingStockLibrary)
#include "test_rollingstocklibrary.moc"
