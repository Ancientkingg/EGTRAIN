#include "diagrams/RouteReferenceChoice.h"
#include "scene/SceneModel.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;
using Used = std::vector<std::pair<int, std::string>>;

namespace {

bool check(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << '\n';
	return condition;
}

bool checkText(const QString& actual, const QString& expected, const char* message) {
	if (actual != expected)
		std::cerr << "failed: " << message << ": expected \"" << expected.toStdString()
				  << "\" got \"" << actual.toStdString() << "\"\n";
	return actual == expected;
}

SceneModel loadCommitted(const fs::path& scenes, const char* name) {
	return loadScene((scenes / name).string()).scene;
}

// One track with four nodes at 0, 1, 2 and 3 and one block per kilometre.
SceneModel lineScene() {
	SceneModel scene;
	scene.schemaVersion = 1;
	scene.tracks.push_back({"track-1"});
	for (int i = 1; i <= 4; ++i)
		scene.nodes.push_back({"node-" + std::to_string(i), "track-1", static_cast<double>(i - 1), 0.0});
	scene.arcs.push_back({"arc-1", "track-1", "node-1", "node-2", 0.0, 0.0, 40.0});
	scene.arcs.push_back({"arc-2", "track-1", "node-2", "node-3", 1000.0, 0.0, 40.0});
	scene.arcs.push_back({"arc-3", "track-1", "node-3", "node-4", 2000.0, 0.0, 40.0});
	for (int i = 1; i <= 3; ++i)
		scene.blocks.push_back({"block-" + std::to_string(i), "track-1", 1.0});
	scene.routes.push_back({"route-1", {"block-1", "block-2", "block-3"}, false, "", false});
	return scene;
}

SceneStation station(const std::string& id, const std::string& nodeId) {
	SceneStation result;
	result.id = id;
	result.name = id;
	result.platforms.push_back({id + "-platform", {nodeId}});
	return result;
}

QListWidget* listOf(QDialog& dialog) {
	return dialog.findChild<QListWidget*>(QStringLiteral("routeReferenceList"));
}

QPushButton* button(QDialog& dialog, QDialogButtonBox::StandardButton which) {
	return dialog.findChild<QDialogButtonBox*>()->button(which);
}

// Runs the dialog the way the diagram commands do: the runtime index on OK, -1 on Cancel.
int runDialog(RouteReferenceDialog& dialog, void (*act)(RouteReferenceDialog&)) {
	QTimer::singleShot(0, &dialog, [&dialog, act] { act(dialog); });
	return dialog.exec() == QDialog::Accepted ? dialog.selectedRuntimeIndex() : -1;
}

void pressReturn(RouteReferenceDialog& dialog) {
	QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
	QApplication::sendEvent(listOf(dialog), &press);
}

void clickOk(RouteReferenceDialog& dialog) { button(dialog, QDialogButtonBox::Ok)->click(); }
void clickCancel(RouteReferenceDialog& dialog) { button(dialog, QDialogButtonBox::Cancel)->click(); }

void doubleClickSecondRow(RouteReferenceDialog& dialog) {
	QListWidget* list = listOf(dialog);
	list->setCurrentRow(1);
	emit list->itemDoubleClicked(list->item(1));
}

} // namespace

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	if (argc != 2) {
		std::cerr << "usage: test_routereferencechoice <scenes directory>\n";
		return 2;
	}
	const fs::path scenes = argv[1];
	bool ok = true;

	const SceneModel assignment = loadCommitted(scenes, "Assignment_Gvc_Gdg_Ut");
	const auto assignmentChoices = buildRouteReferenceChoices(assignment, Used{{0, "route0"}, {1, "route1"}});
	ok &= check(assignmentChoices.size() == 2, "one choice per used route");
	if (assignmentChoices.size() == 2) {
		ok &= checkText(assignmentChoices[0].label, "route0 --> Gvc - Gdg - Ut", "assignment route0 label");
		ok &= checkText(assignmentChoices[1].label, "route1 --> Ut - Gdg - Gvc", "assignment route1 label");
		ok &= check(assignmentChoices[1].runtimeIndex == 1 && assignmentChoices[1].routeId == "route1",
			"choice keeps its runtime index and route id");
		ok &= checkText(assignmentChoices[0].toolTip, assignmentChoices[0].label, "tooltip holds the station list");
	}

	const SceneModel netherlands = loadCommitted(scenes, "Netherlands");
	const auto netherlandsChoices = buildRouteReferenceChoices(netherlands, Used{{6, "route6"}});
	ok &= check(netherlandsChoices.size() == 1, "netherlands choice exists");
	if (netherlandsChoices.size() == 1)
		ok &= checkText(netherlandsChoices[0].label,
			"route6 --> Ut - Uto - Bhv - Dld - Stz - St - Sd - Brn", "netherlands route6 label");

	SceneModel line = lineScene();
	line.stations = {station("A", "node-1"), station("B-1", "node-4")};
	line.routes.push_back({"route-2", {"block-3", "block-2", "block-1"}, false, "", false});
	line.routes.push_back({"route-3", {"block-1", "block-2", "block-3"}, false, "", false});
	const auto lineChoices = buildRouteReferenceChoices(line, Used{{2, "route-1"}, {7, "route-2"}, {4, "route-3"}});
	ok &= check(lineChoices.size() == 3, "line choices exist");
	if (lineChoices.size() == 3) {
		ok &= checkText(lineChoices[0].label, "route-1 --> A - B-1", "station ids keep their own hyphen");
		ok &= checkText(lineChoices[1].label, "route-2 --> B-1 - A", "reversed route lists the stations in travel order");
		ok &= check(lineChoices[0].label.mid(12) == "A - B-1" && lineChoices[2].label.mid(12) == "A - B-1"
				&& lineChoices[0].runtimeIndex == 2 && lineChoices[2].runtimeIndex == 4,
			"routes with the same stations keep their own id and index");
	}

	SceneModel bare = lineScene();
	SceneRoute unresolved{"route-x", {"no-such-block"}, false, "", false};
	bare.routes.push_back(unresolved);
	const auto bareChoices = buildRouteReferenceChoices(bare, Used{{0, "route-1"}, {1, "route-x"}, {2, "route-missing"}});
	ok &= check(bareChoices.size() == 3, "bare choices exist");
	if (bareChoices.size() == 3) {
		ok &= checkText(bareChoices[0].label, "route-1 (no stations on this route)", "route without stations");
		ok &= checkText(bareChoices[1].label, "route-x (station order unavailable)", "unresolved route");
		ok &= check(bareChoices[1].toolTip.contains("station order unavailable"), "unresolved route tooltip says so");
		ok &= checkText(bareChoices[2].label, "route-missing (station order unavailable)", "route missing from the scene");
		ok &= check(bareChoices[2].runtimeIndex == 2 && bareChoices[2].routeId == "route-missing",
			"unavailable choices stay selectable by index");
	}

	SceneModel longScene = lineScene();
	const std::string longA(80, 'a');
	const std::string longB(80, 'b');
	longScene.stations = {station(longA, "node-1"), station(longB, "node-4")};
	const auto longChoices = buildRouteReferenceChoices(longScene, Used{{0, "route-1"}});
	ok &= check(longChoices.size() == 1, "long choice exists");
	if (longChoices.size() == 1) {
		const QString full = QString("route-1 --> %1 - %2").arg(QString::fromStdString(longA), QString::fromStdString(longB));
		const QString& label = longChoices[0].label;
		ok &= checkText(longChoices[0].toolTip, full, "long label keeps the full text as tooltip");
		ok &= check(label.size() < full.size() && label.contains(QChar(0x2026)), "long label is shortened in the middle");
		ok &= check(label.startsWith("route-1 --> aaa") && label.endsWith("bbb"), "shortened label keeps both ends");
	}

	// Two routes with the same stations stay apart through the dialog.
	const auto twins = buildRouteReferenceChoices(line, Used{{5, "route-1"}, {9, "route-3"}});
	{
		RouteReferenceDialog dialog(twins, "train paths");
		QListWidget* list = listOf(dialog);
		ok &= check(list && dialog.objectName() == "routeReferenceDialog", "dialog and list have their object names");
		if (list) {
			ok &= check(list->selectionMode() == QAbstractItemView::SingleSelection, "single selection");
			ok &= check(list->count() == 2 && list->currentRow() == 0, "first row is selected by default");
			ok &= check(dialog.selectedRuntimeIndex() == 5, "default selection returns the first runtime index");
			ok &= checkText(list->item(1)->toolTip(), twins[1].toolTip, "rows carry their tooltip");
			list->setCurrentRow(1);
			ok &= check(dialog.selectedRuntimeIndex() == 9, "second identical row returns its own runtime index");
		}
		ok &= checkText(dialog.windowTitle(), "Reference route", "dialog title");
		const auto* context = dialog.findChild<QLabel*>(QStringLiteral("dialogContext"));
		ok &= check(context && context->text() == "Reference route for train paths:", "dialog context line names the purpose");
	}
	{
		RouteReferenceDialog dialog(twins, "timetable");
		ok &= check(runDialog(dialog, pressReturn) == 5, "Return accepts the selected row");
	}
	{
		RouteReferenceDialog dialog(twins, "timetable");
		ok &= check(runDialog(dialog, clickOk) == 5, "OK accepts the selected row");
	}
	{
		RouteReferenceDialog dialog(twins, "timetable");
		ok &= check(runDialog(dialog, doubleClickSecondRow) == 9, "double click accepts the clicked row");
	}
	{
		RouteReferenceDialog dialog(twins, "timetable");
		ok &= check(runDialog(dialog, clickCancel) == -1, "Cancel gives -1");
	}

	if (!ok)
		return 1;
	std::cout << "route reference choice tests passed\n";
	return 0;
}
