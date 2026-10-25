#include "diagrams/RouteReferenceChoice.h"
#include "scene/SceneModel.h"
#include "widgets/PlainToolTip.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTest>
#include <QTimer>

#include <filesystem>

namespace fs = std::filesystem;
using Used = std::vector<std::pair<int, std::string>>;

namespace {

SceneModel loadCommitted(const char* name) {
	return loadScene((fs::path(EGTRAIN_SCENES_DIR) / name).string()).scene;
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

// The line with the stations A and B-1 at its ends, and two more routes: route-2 the other way and route-3 over the
// same blocks as route-1.
SceneModel twoStationLine() {
	SceneModel line = lineScene();
	line.stations = {station("A", "node-1"), station("B-1", "node-4")};
	line.routes.push_back({"route-2", {"block-3", "block-2", "block-1"}, false, "", false});
	line.routes.push_back({"route-3", {"block-1", "block-2", "block-3"}, false, "", false});
	return line;
}

// Two routes with the same stations.
QVector<RouteReferenceChoice> twins() {
	return buildRouteReferenceChoices(twoStationLine(), Used{{5, "route-1"}, {9, "route-3"}});
}

} // namespace

// A train path or timetable diagram asks for its reference route with a list of the used routes, each named by its
// stations in travel order.
class TestRouteReferenceChoice : public QObject {
	Q_OBJECT

private slots:
	void assignmentRoutes();
	void netherlandsRoute();
	void lineStations();
	void routesWithoutStations();
	void longLabels();
	void dialogKeepsTwinsApart();
	void returnAcceptsSelectedRow();
	void okAcceptsSelectedRow();
	void doubleClickAcceptsClickedRow();
	void cancelGivesMinusOne();
};

void TestRouteReferenceChoice::assignmentRoutes() {
	const SceneModel assignment = loadCommitted("Assignment_Gvc_Gdg_Ut");
	const auto assignmentChoices = buildRouteReferenceChoices(assignment, Used{{0, "route0"}, {1, "route1"}});
	// One choice per used route.
	QCOMPARE(assignmentChoices.size(), qsizetype(2));
	// Assignment route0 label.
	QCOMPARE(assignmentChoices[0].label, QStringLiteral("route0 --> Gvc - Gdg - Ut"));
	// Assignment route1 label.
	QCOMPARE(assignmentChoices[1].label, QStringLiteral("route1 --> Ut - Gdg - Gvc"));
	// The choice keeps its runtime index and route id.
	QCOMPARE(assignmentChoices[1].runtimeIndex, 1);
	QCOMPARE(assignmentChoices[1].routeId, QStringLiteral("route1"));
	// The tooltip holds the station list.
	QCOMPARE(assignmentChoices[0].toolTip, assignmentChoices[0].label);
}

void TestRouteReferenceChoice::netherlandsRoute() {
	const SceneModel netherlands = loadCommitted("Netherlands");
	const auto netherlandsChoices = buildRouteReferenceChoices(netherlands, Used{{6, "route6"}});
	// The Netherlands choice exists.
	QCOMPARE(netherlandsChoices.size(), qsizetype(1));
	// Netherlands route6 label.
	QCOMPARE(netherlandsChoices[0].label, QStringLiteral("route6 --> Ut - Uto - Bhv - Dld - Stz - St - Sd - Brn"));
}

void TestRouteReferenceChoice::lineStations() {
	const auto lineChoices = buildRouteReferenceChoices(twoStationLine(), Used{{2, "route-1"}, {7, "route-2"}, {4, "route-3"}});
	// The line choices exist.
	QCOMPARE(lineChoices.size(), qsizetype(3));
	// Station ids keep their own hyphen.
	QCOMPARE(lineChoices[0].label, QStringLiteral("route-1 --> A - B-1"));
	// A reversed route lists the stations in travel order.
	QCOMPARE(lineChoices[1].label, QStringLiteral("route-2 --> B-1 - A"));
	// Routes with the same stations keep their own id and index.
	QCOMPARE(lineChoices[0].label.mid(12), QStringLiteral("A - B-1"));
	QCOMPARE(lineChoices[2].label.mid(12), QStringLiteral("A - B-1"));
	QCOMPARE(lineChoices[0].runtimeIndex, 2);
	QCOMPARE(lineChoices[2].runtimeIndex, 4);
}

void TestRouteReferenceChoice::routesWithoutStations() {
	SceneModel bare = lineScene();
	SceneRoute unresolved{"route-x", {"no-such-block"}, false, "", false};
	bare.routes.push_back(unresolved);
	const auto bareChoices = buildRouteReferenceChoices(bare, Used{{0, "route-1"}, {1, "route-x"}, {2, "route-missing"}});
	// The bare choices exist.
	QCOMPARE(bareChoices.size(), qsizetype(3));
	// Route without stations.
	QCOMPARE(bareChoices[0].label, QStringLiteral("route-1 (no stations on this route)"));
	// Unresolved route.
	QCOMPARE(bareChoices[1].label, QStringLiteral("route-x (station order unavailable)"));
	QVERIFY2(bareChoices[1].toolTip.contains("station order unavailable"), "unresolved route tooltip says so");
	// Route missing from the scene.
	QCOMPARE(bareChoices[2].label, QStringLiteral("route-missing (station order unavailable)"));
	// Unavailable choices stay selectable by index.
	QCOMPARE(bareChoices[2].runtimeIndex, 2);
	QCOMPARE(bareChoices[2].routeId, QStringLiteral("route-missing"));
}

void TestRouteReferenceChoice::longLabels() {
	SceneModel longScene = lineScene();
	const std::string longA(80, 'a');
	const std::string longB(80, 'b');
	longScene.stations = {station(longA, "node-1"), station(longB, "node-4")};
	const auto longChoices = buildRouteReferenceChoices(longScene, Used{{0, "route-1"}});
	// The long choice exists.
	QCOMPARE(longChoices.size(), qsizetype(1));
	const QString full = QString("route-1 --> %1 - %2").arg(QString::fromStdString(longA), QString::fromStdString(longB));
	const QString& label = longChoices[0].label;
	// A long label keeps the full text as tooltip.
	QCOMPARE(longChoices[0].toolTip, full);
	// A long label is shortened in the middle.
	QCOMPARE_LT(label.size(), full.size());
	QVERIFY(label.contains(QChar(0x2026)));
	// The shortened label keeps both ends.
	QVERIFY(label.startsWith("route-1 --> aaa"));
	QVERIFY(label.endsWith("bbb"));
}

// Two routes with the same stations stay apart through the dialog.
void TestRouteReferenceChoice::dialogKeepsTwinsApart() {
	const auto choices = twins();
	RouteReferenceDialog dialog(choices, "train paths");
	QListWidget* list = listOf(dialog);
	// The dialog and the list have their object names.
	if (list == nullptr)
		QFAIL("dialog and list have their object names");
	QCOMPARE(dialog.objectName(), QStringLiteral("routeReferenceDialog"));
	// Single selection.
	QCOMPARE(list->selectionMode(), QAbstractItemView::SingleSelection);
	// The first row is selected by default.
	QCOMPARE(list->count(), 2);
	QCOMPARE(list->currentRow(), 0);
	// The default selection returns the first runtime index.
	QCOMPARE(dialog.selectedRuntimeIndex(), 5);
	// Rows carry their tooltip.
	QCOMPARE(list->item(1)->toolTip(), plainToolTip(choices[1].toolTip));
	// A row tooltip goes through the plain tooltip helper, so a long identifier can wrap.
	QCOMPARE_NE(list->item(1)->toolTip(), choices[1].toolTip);
	list->setCurrentRow(1);
	// The second identical row returns its own runtime index.
	QCOMPARE(dialog.selectedRuntimeIndex(), 9);
	// Dialog title.
	QCOMPARE(dialog.windowTitle(), QStringLiteral("Reference route"));
	const auto* context = dialog.findChild<QLabel*>(QStringLiteral("dialogContext"));
	// The dialog context line names the purpose.
	if (context == nullptr)
		QFAIL("dialog context line names the purpose");
	QCOMPARE(context->text(), QStringLiteral("Reference route for train paths:"));
}

void TestRouteReferenceChoice::returnAcceptsSelectedRow() {
	RouteReferenceDialog dialog(twins(), "timetable");
	// Return accepts the selected row.
	QCOMPARE(runDialog(dialog, pressReturn), 5);
}

void TestRouteReferenceChoice::okAcceptsSelectedRow() {
	RouteReferenceDialog dialog(twins(), "timetable");
	// OK accepts the selected row.
	QCOMPARE(runDialog(dialog, clickOk), 5);
}

void TestRouteReferenceChoice::doubleClickAcceptsClickedRow() {
	RouteReferenceDialog dialog(twins(), "timetable");
	// A double click accepts the clicked row.
	QCOMPARE(runDialog(dialog, doubleClickSecondRow), 9);
}

void TestRouteReferenceChoice::cancelGivesMinusOne() {
	RouteReferenceDialog dialog(twins(), "timetable");
	// Cancel gives -1.
	QCOMPARE(runDialog(dialog, clickCancel), -1);
}

QTEST_MAIN(TestRouteReferenceChoice)
#include "test_routereferencechoice.moc"
