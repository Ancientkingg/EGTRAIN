#include "editors/TimetableStopsPanel.h"

#include "editors/StopEditorDialog.h"
#include "scene/StopInsertion.h"
#include "support/FakePrompts.h"
#include "util/TimeFormat.h"
#include "widgets/ChoiceComboBox.h"

#include <QAbstractEventDispatcher>
#include <QAbstractItemDelegate>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QEventLoop>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPoint>
#include <QProxyStyle>
#include <QPushButton>
#include <QRect>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QString>
#include <QStringList>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionViewItem>
#include <QTableWidget>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// The macOS style, where a form keeps each field at its size hint.
class MacFormStyle : public QProxyStyle {
public:
	MacFormStyle() : QProxyStyle(QStringLiteral("Fusion")) {}
	int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const override {
		if (hint == SH_FormLayoutFieldGrowthPolicy)
			return QFormLayout::FieldsStayAtSizeHint;
		return QProxyStyle::styleHint(hint, option, widget, returnData);
	}
};

static SceneStop stop(const std::string& stationId, const std::string& platformId, double arrival, double departure) {
	SceneStop result;
	result.stationId = stationId;
	result.platformId = platformId;
	result.hasPlannedArrival = true;
	result.plannedArrivalSeconds = arrival;
	result.hasPlannedDeparture = true;
	result.plannedDepartureSeconds = departure;
	result.dwellSeconds = departure - arrival;
	return result;
}

// Four platform stations on one track, visited in the order A, B, C and D by one route, and two services on it.
static SceneModel lineScene() {
	SceneModel scene;
	scene.schemaVersion = 1;
	scene.name = "Timetable stops";
	scene.baseTime = "08:00:00";
	scene.tracks.push_back({"track-1"});
	for (int index = 0; index < 4; ++index)
		scene.nodes.push_back({"node-" + std::to_string(index + 1), "track-1", static_cast<double>(index), 0.0});
	for (int index = 1; index < 4; ++index) {
		scene.arcs.push_back({"arc-" + std::to_string(index), "track-1", "node-" + std::to_string(index), "node-" + std::to_string(index + 1), 0.0, 0.0, 40.0});
		scene.blocks.push_back({"block-" + std::to_string(index), "track-1", 1.0});
	}
	const char letters[] = {'a', 'b', 'c', 'd'};
	for (int index = 0; index < 4; ++index) {
		SceneStation station;
		station.id = std::string("station-") + letters[index];
		station.name = std::string(1, static_cast<char>(letters[index] - 'a' + 'A'));
		station.platforms = {{std::string("platform-") + letters[index], {"node-" + std::to_string(index + 1)}}};
		scene.stations.push_back(station);
	}
	SceneRoute route;
	route.id = "route-1";
	route.blocks = {"block-1", "block-2", "block-3"};
	scene.routes.push_back(route);
	SceneService first;
	first.id = "service-1";
	first.route = "route-1";
	first.stops = {stop("station-a", "platform-a", 0.0, 30.0), stop("station-b", "platform-b", 60.0, 90.0), stop("station-c", "platform-c", 120.0, 150.0)};
	SceneService second;
	second.id = "service-2";
	second.route = "route-1";
	second.stops = {stop("station-a", "platform-a", 0.0, 30.0), stop("station-d", "platform-d", 200.0, 230.0)};
	scene.services = {first, second};
	return scene;
}

static QString longName(int length) {
	return QStringLiteral("Stop_abcdefghijklmnopqrstuvwxyz_0123456789_abcdefghijklmnopqrstuvwxyz_0123456789_abcdefghijklmnopqrstuvwxyz").left(length);
}

using Log = std::vector<std::string>;

// The order in which a panel and its document report an edit.
struct Events {
	Log log;
	SceneDocument::ChangeKinds kinds;

	Events(TimetableStopsPanel& panel, SceneDocument& document) {
		QObject::connect(&document, &SceneDocument::changed, &panel, [this](SceneDocument::ChangeKinds changedKinds) {
			log.push_back("documentChanged");
			kinds = changedKinds;
		});
		QObject::connect(&panel, &TimetableStopsPanel::timetableRefreshed, &panel, [this] { log.push_back("refreshed"); });
		QObject::connect(&panel, &TimetableStopsPanel::changed, &panel, [this] { log.push_back("panelChanged"); });
	}
};

static std::vector<std::string> stationsOf(const SceneService& service) {
	std::vector<std::string> result;
	for (const SceneStop& entry : service.stops)
		result.push_back(entry.stationId);
	return result;
}

static QStringList column(const QTableWidget* table, int index) {
	QStringList result;
	for (int row = 0; row < table->rowCount(); ++row)
		result << table->item(row, index)->text();
	return result;
}

static QStringList itemTexts(const QComboBox* combo) {
	QStringList result;
	for (int index = 0; index < combo->count(); ++index)
		result << combo->itemText(index);
	return result;
}

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

// What the test did to the stop dialog.
struct DialogRun {
	bool opened = false;
	bool accepted = false;
};

// Opens the stop dialog with `opener`, runs `driver` on it as soon as it is shown, and closes it with Cancel when `driver`
// has not accepted it. A watchdog closes a dialog that `driver` never reaches, so a failed test cannot hang.
static DialogRun runDialog(TimetableStopsPanel& panel, const std::function<void(QDialog&)>& driver, const std::function<void()>& opener) {
	DialogRun run;
	QTimer shown;
	shown.setSingleShot(true);
	QObject::connect(&shown, &QTimer::timeout, &panel, [&] {
		auto* dialog = panel.findChild<QDialog*>("stopEditorDialog");
		if (!dialog)
			return;
		run.opened = true;
		QApplication::processEvents();
		driver(*dialog);
		run.accepted = dialog->result() == QDialog::Accepted;
		if (dialog->isVisible())
			dialog->reject();
	});
	QTimer watchdog;
	watchdog.setSingleShot(true);
	QObject::connect(&watchdog, &QTimer::timeout, &panel, [&panel] {
		if (auto* dialog = panel.findChild<QDialog*>("stopEditorDialog"))
			dialog->reject();
	});
	shown.start(0);
	watchdog.start(5000);
	opener();
	return run;
}

static void click(QDialog& dialog, const char* name) {
	dialog.findChild<QPushButton*>(name)->click();
}

static QComboBox* combo(QDialog& dialog, const char* name) {
	return dialog.findChild<QComboBox*>(name);
}

static QLineEdit* edit(QDialog& dialog, const char* name) {
	return dialog.findChild<QLineEdit*>(name);
}

static QLabel* label(QDialog& dialog, const char* name) {
	return dialog.findChild<QLabel*>(name);
}

// The role that a dialog label has: "help", or the value of its status property, which is "neutral", "warning" or "error".
static QString role(const QLabel* message) {
	return message->property("dialogHelp").toBool() ? QStringLiteral("help") : message->property("dialogStatus").toString();
}

// A combo box that adjusts to its contents resizes itself to its size hint from a 20 ms timer of its popup after its
// items change, and its layout gives it back its width in the next turn of the event loop. Waits until no combo box of
// the dialog has that timer running, then lets the layout requests that it queued run.
static void settleCombos(const QDialog& dialog) {
	const auto adjusting = [&dialog] {
		for (const QComboBox* box : dialog.findChildren<QComboBox*>())
			if (!QAbstractEventDispatcher::instance()->registeredTimers(box->view()->parentWidget()).isEmpty())
				return true;
		return false;
	};
	while (adjusting())
		QApplication::processEvents(QEventLoop::WaitForMoreEvents);
	QApplication::processEvents();
}

// Lets the events that a change has posted take effect, including the timers that fire at once.
static void settle() {
	QEventLoop loop;
	QTimer::singleShot(30, &loop, &QEventLoop::quit);
	loop.exec();
}

// Whether the message is whole inside the part of the dialog body that is in view.
static bool inView(QDialog& dialog, const QLabel* message) {
	const auto* scroll = dialog.findChild<QScrollArea*>("dialogBodyScroll");
	return scroll && scroll->viewport()->rect().contains(QRect(message->mapTo(scroll->viewport(), QPoint(0, 0)), message->size()));
}

static const Log editLog({"documentChanged", "refreshed", "panelChanged"});

// Whether the log names this event.
static bool logged(const Log& log, const std::string& event) {
	return std::find(log.begin(), log.end(), event) != log.end();
}

// lineScene() with a second platform at station B on the node of station C, so that a stop at B without a platform is
// ambiguous.
static SceneModel twoPlatformScene() {
	SceneModel scene = lineScene();
	scene.stations[1].platforms.push_back({"platform-b2", {"node-3"}});
	return scene;
}

// What the fields of the stop dialog hold when it opens on `entry`, with the planned times as seconds after the base time
// of lineScene().
static StopDraftFields fieldsOf(const SceneStop& entry) {
	StopDraftFields fields;
	fields.hasArrival = entry.hasPlannedArrival;
	fields.arrivalText = entry.hasPlannedArrival ? QString::number(entry.plannedArrivalSeconds) : QString();
	fields.hasDeparture = entry.hasPlannedDeparture;
	fields.departureText = entry.hasPlannedDeparture ? QString::number(entry.plannedDepartureSeconds) : QString();
	fields.dwellText = QString::number(entry.dwellSeconds);
	fields.baseOffsetSeconds = 8 * 3600;
	return fields;
}

// A panel on a document that shows lineScene(), and the events of both from the opening of the scene on.
struct StopsFixture {
	SceneDocument document;
	TimetableStopsPanel panel;
	Events events{panel, document};

	StopsFixture() {
		panel.setDocument(&document);
		events.log.clear();
		document.replace(lineScene(), false);
	}
};

class TestTimetableStopsPanel : public QObject {
	Q_OBJECT

private slots:
	void showsNothingWithoutScene();
	void serviceWithoutStopsPointsToAddStop();
	void sceneWithoutServicesPointsToAddService();
	void columnsFillTable_data();
	void columnsFillTable();
	void stationHeaderStaysWholeInWindowsStyle();
	void clockTimesStayWhole_data();
	void clockTimesStayWhole();
	void showsStopsOfService();
	void showsClockTimes();
	void followsCaseSettings();
	void movesStops();
	void removesStopsAndFollowsService();
	void closesWhileRunUsesScene();
	void detachedPanelIgnoresDocument();
	void addStopWithoutReachableStationSaysSo();
	void editsStopInDialog();
	void cancelsStopDialog();
	void addsStopAtChosenPosition();
	void insertsStopsAfterSelected();
	void messageRoleFollowsResolution();
	void showsDashForMissingValues();
	void showsLongNames();
	void checksDraftInOrder();
	void refusesPlannedTimesThatDoNotParse();
	void refusesDwellThatIsNotZeroOrMore();
	void refusesAmbiguousPlatformOnlyWhenAssigned();
	void refusesStopThatInsertionRefuses();
	void takesDraftWithAbsentPlannedTime();
	void dialogGivesStopWithoutExec();
	void editAppliesNothingToReplacedScene();
	void editAppliesNothingWhileClosedForEdits();
	void addAppliesNothingToReplacedScene();
	void editAppliesNothingWhenServiceChanged();
	void editAppliesAfterChangeOutsideServices();
	void takesPendingTextBeforeCopyingService();
	void addTakesPendingTextBeforeCopyingService();
	void addAppliesNothingWhenStopNoLongerFits();
	void leavesNoEmptyBandInFormOfMacOs();
};

// The planned times follow the base time of the case.
void TestTimetableStopsPanel::followsCaseSettings() {
	StopsFixture f;
	f.panel.setServiceId("service-1");
	QCOMPARE(f.panel.timeBaseLabel()->text(), "Seconds after 08:00:00");
	QVERIFY(f.document.edit(SceneDocument::CaseSettings, "Edit case settings", [](SceneModel& model) { model.baseTime = "09:00:00"; }));
	QCOMPARE(f.panel.timeBaseLabel()->text(), "Seconds after 09:00:00");
}

void TestTimetableStopsPanel::showsNothingWithoutScene() {
	TimetableStopsPanel panel;
	// A panel without a document shows nothing and offers nothing.
	QCOMPARE(panel.table()->rowCount(), 0);
	QVERIFY(!panel.table()->isEnabled());
	QVERIFY(!panel.addButton()->isEnabled());
	QVERIFY(!panel.timeModeCombo()->isEnabled());
	QVERIFY(panel.timeBaseLabel()->text().isEmpty());
	// A panel without a document asks to open a case.
	QCOMPARE(panel.emptyStateLabel()->text(), "Open a case to edit services.");
	QVERIFY(panel.emptyStateLabel()->isVisibleTo(&panel));
	SceneDocument document;
	panel.setDocument(&document);
	QVERIFY2(panel.emptyStateLabel()->text() == "Open a case to edit services.", "a document without a scene asks to open a case");
	panel.setServiceId("service-1");
	panel.addStop();
	panel.removeStop();
	panel.moveStopUp();
	panel.editStop(0);
	// A panel stays disabled while its document holds no scene, and its actions change nothing.
	QVERIFY(!panel.addButton()->isEnabled());
	QCOMPARE(document.revision(), quint64(0));
	QVERIFY(!document.isDirty());
}

// A service that has no stops.
void TestTimetableStopsPanel::serviceWithoutStopsPointsToAddStop() {
	SceneDocument document;
	TimetableStopsPanel panel;
	panel.setDocument(&document);
	SceneModel scene = lineScene();
	scene.services[0].stops.clear();
	document.replace(scene, false);
	panel.setServiceId("service-1");
	// A service without stops points to Add Stop.
	QCOMPARE(panel.table()->rowCount(), 0);
	QVERIFY(panel.addButton()->isEnabled());
	QCOMPARE(panel.emptyStateLabel()->text(), "No stops. Use Add Stop.");
	QVERIFY(panel.emptyStateLabel()->isVisibleTo(&panel));
	document.setEditable(false);
	panel.refresh();
	// A service without stops does not point to Add Stop while a run uses the scene.
	QVERIFY(!panel.addButton()->isEnabled());
	QCOMPARE(panel.emptyStateLabel()->text(), "No stops.");
	document.setEditable(true);
	panel.refresh();
	panel.setServiceId("service-2");
	QVERIFY2(panel.emptyStateLabel()->isHidden(), "the message goes away when the service has stops");
}

// A scene that has no services.
void TestTimetableStopsPanel::sceneWithoutServicesPointsToAddService() {
	SceneDocument document;
	TimetableStopsPanel panel;
	panel.setDocument(&document);
	SceneModel scene = lineScene();
	scene.services.clear();
	document.replace(scene, false);
	// A scene without services points to Add Service while it can be edited.
	QCOMPARE(panel.emptyStateLabel()->text(), "No services. Use Add Service.");
	QVERIFY(panel.emptyStateLabel()->isVisibleTo(&panel));
	document.setEditable(false);
	panel.refresh();
	QVERIFY2(panel.emptyStateLabel()->text() == "No services.", "a scene without services does not point to Add Service while a run uses it");
}

void TestTimetableStopsPanel::columnsFillTable_data() {
	QTest::addColumn<double>("scale");
	QTest::newRow("1 times the font") << 1.0;
	QTest::newRow("1.25 times the font") << 1.25;
	QTest::newRow("1.5 times the font") << 1.5;
}

// The five columns fill the table without a horizontal scroll bar whatever the font and the width, show their whole
// headers when the table is wide enough for them, and elide a long station or platform name.
void TestTimetableStopsPanel::columnsFillTable() {
	QFETCH(double, scale);
	SceneModel scene = lineScene();
	scene.stations[0].name = longName(95).toStdString();
	scene.services[0].stops[0].platformId = longName(60).toStdString();
	SceneDocument document;
	TimetableStopsPanel panel;
	QFont font = panel.font();
	font.setPointSizeF(font.pointSizeF() * scale);
	panel.setFont(font);
	panel.setDocument(&document);
	document.replace(scene, false);
	panel.setServiceId("service-1");
	const QTableWidget* table = panel.table();
	const QHeaderView* header = table->horizontalHeader();
	const QFontMetrics body(table->font());
	int headers = 0;
	for (int index = 1; index < table->columnCount(); ++index)
		headers += header->sectionSizeHint(index);
	// the headers of the four narrow columns, a station column of twelve digits, the frame and the margins of the panel
	const int wide = headers + body.horizontalAdvance(QString(12, QLatin1Char('0'))) + 2 * table->frameWidth() + 40;
	panel.setMinimumWidth(wide);
	panel.resize(wide, 500);
	panel.show();
	// The panel is hidden before it is destroyed, also when a check fails.
	const auto hide = qScopeGuard([&panel] { panel.hide(); });
	QApplication::processEvents();
	const QString label = QString(" at %1 times the font").arg(scale);
	bool wholeHeaders = true;
	for (int index = 0; index < table->columnCount(); ++index)
		wholeHeaders = wholeHeaders && table->columnWidth(index) >= header->sectionSizeHint(index);
	// The five columns fill the table without a scroll bar.
	QVERIFY(!table->horizontalScrollBar()->isVisible());
	QCOMPARE(header->length(), table->viewport()->width());
	QVERIFY2(wholeHeaders, qPrintable("the five columns show their whole headers when the table fits them" + label));
	QVERIFY2(table->item(0, 0)->text() == longName(95), "the cell of a long station name holds the whole name");
	QVERIFY2(table->item(0, 0)->toolTip().contains(longName(95)), "the tooltip of a long station name holds the whole name");
	// The cell and the tooltip of a long platform name hold the whole name.
	QCOMPARE(table->item(0, 1)->text(), longName(60));
	QVERIFY(table->item(0, 1)->toolTip().endsWith(longName(60)));
	// A long station or platform name does not widen its column.
	QCOMPARE_LT(table->columnWidth(1), body.horizontalAdvance(longName(60)));
	QCOMPARE_LT(table->columnWidth(0), body.horizontalAdvance(longName(95)));

	// a table narrower than the headers shortens the columns instead of scrolling
	panel.setMinimumWidth(0);
	panel.table()->setFixedWidth(headers * 2 / 3 + body.horizontalAdvance(QString(8, QLatin1Char('0'))));
	QApplication::processEvents();
	// A narrow table shortens its columns instead of scrolling.
	QVERIFY(!table->horizontalScrollBar()->isVisible());
	QCOMPARE(header->length(), table->viewport()->width());
	// a cut header is read from its tooltip, which holds the whole text on one line
	const QStringList headerTips = {"Station", "Stop platform", "Minimum dwell (s)", "Planned arrival", "Planned departure"};
	bool wholeTips = table->columnCount() == headerTips.size();
	for (int index = 0; wholeTips && index < table->columnCount(); ++index)
		wholeTips = table->model()->headerData(index, Qt::Horizontal, Qt::ToolTipRole).toString() == headerTips.at(index);
	QVERIFY2(wholeTips, qPrintable("every column header has its whole text as a tooltip" + label));
}

// The Windows style keeps more room around the text of a header than around the text of a cell: where the other columns
// leave the station less than its header, they give up room, so that the Station header stays whole.
void TestTimetableStopsPanel::stationHeaderStaysWholeInWindowsStyle() {
	const std::unique_ptr<QStyle> windowsStyle(QStyleFactory::create("Windows"));
	if (windowsStyle == nullptr)
		QFAIL("Qt has no Windows style");
	SceneDocument document;
	TimetableStopsPanel panel;
	QTableWidget* table = panel.table();
	QHeaderView* header = table->horizontalHeader();
	table->setStyle(windowsStyle.get());
	header->setStyle(windowsStyle.get());
	panel.setDocument(&document);
	document.replace(lineScene(), false);
	panel.setServiceId("service-1");
	table->setFixedWidth(2000);
	panel.show();
	// The panel is hidden before it is destroyed, also when a check fails.
	const auto hide = qScopeGuard([&panel] { panel.hide(); });
	QApplication::processEvents();
	int others = 0;
	for (int index = 1; index < table->columnCount(); ++index)
		others += table->columnWidth(index);
	table->setFixedWidth(others + header->sectionSizeHint(0) - 1 + table->width() - table->viewport()->width());
	QApplication::processEvents();
	// The Station header stays whole where the other columns would leave it one pixel short.
	QVERIFY(!table->horizontalScrollBar()->isVisible());
	QCOMPARE_GE(table->columnWidth(0), header->sectionSizeHint(0));
}

void TestTimetableStopsPanel::clockTimesStayWhole_data() {
	QTest::addColumn<double>("scale");
	QTest::addColumn<bool>("dayPrefix");
	QTest::newRow("1 times the font with a day prefix") << 1.0 << true;
	QTest::newRow("1.5 times the font with a day prefix") << 1.5 << true;
	QTest::newRow("1 times the font without a day prefix") << 1.0 << false;
	QTest::newRow("1.5 times the font without a day prefix") << 1.5 << false;
}

// Clock times, also with a day prefix, are not elided at the width of a docked editor, whatever the platform or the
// station is; without a day prefix, the widest time sets the width of the column.
void TestTimetableStopsPanel::clockTimesStayWhole() {
	QFETCH(double, scale);
	QFETCH(bool, dayPrefix);
	SceneModel scene = lineScene();
	scene.stations[0].name = longName(95).toStdString();
	scene.services[0].stops[0].platformId = longName(60).toStdString();
	if (dayPrefix)
		scene.services[0].stops[2] = stop("station-c", "platform-c", 100000.0, 100030.0);
	SceneDocument document;
	TimetableStopsPanel panel;
	QFont font = panel.font();
	font.setPointSizeF(font.pointSizeF() * scale);
	panel.setFont(font);
	panel.setDocument(&document);
	document.replace(scene, false);
	panel.setServiceId("service-1");
	panel.timeModeCombo()->setCurrentIndex(1);
	panel.table()->setFixedWidth(static_cast<int>(400 * scale));
	panel.show();
	// The panel is hidden before it is destroyed, also when a check fails.
	const auto hide = qScopeGuard([&panel] { panel.hide(); });
	QApplication::processEvents();
	const QTableWidget* table = panel.table();
	const QFontMetrics body(table->font());
	const int margin = 2 * (table->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, table) + 1);
	const QString label = QString(" at %1 times the font%2").arg(scale).arg(dayPrefix ? QString() : QStringLiteral(" without a day prefix"));
	bool whole = true;
	for (int row = 0; row < table->rowCount(); ++row) {
		for (int index = 3; index < 5; ++index)
			whole = whole && body.horizontalAdvance(table->item(row, index)->text()) + margin <= table->columnWidth(index);
	}
	// the width that the delegate itself needs for the widest cell, which a style may round up
	QStyleOptionViewItem option;
	option.initFrom(table);
	option.widget = table;
	option.font = table->font();
	option.fontMetrics = body;
	for (int row = 0; row < table->rowCount(); ++row) {
		for (int index = 3; index < 5; ++index) {
			const QModelIndex cell = table->model()->index(row, index);
			whole = whole && table->itemDelegate()->sizeHint(option, cell).width() <= table->columnWidth(index);
		}
	}
	// The clock times with and without a day prefix are whole in the planned columns.
	QVERIFY(!dayPrefix || column(table, 3).back().startsWith("+1d "));
	QVERIFY(whole);
	// The columns still fill the table without a scroll bar when the clock times keep their width.
	QVERIFY(!table->horizontalScrollBar()->isVisible());
	QCOMPARE(table->horizontalHeader()->length(), table->viewport()->width());
	QVERIFY2(table->columnWidth(0) >= table->horizontalHeader()->minimumSectionSize(), qPrintable("the station column keeps a width" + label));
}

void TestTimetableStopsPanel::showsStopsOfService() {
	StopsFixture f;
	TimetableStopsPanel& panel = f.panel;
	// A scene shows no stops until a service is given.
	QCOMPARE(panel.table()->rowCount(), 0);
	QVERIFY(!panel.table()->isEnabled());
	QVERIFY(!panel.addButton()->isEnabled());
	QVERIFY2(panel.emptyStateLabel()->text() == "Select a service to edit its timetable.", "a scene without a given service asks for a service");
	panel.setServiceId("service-1");
	QVERIFY2(panel.emptyStateLabel()->isHidden(), "the message is hidden while the service has stops");
	// The stops of the service are shown in order with their station, platform, dwell and planned times.
	QCOMPARE(panel.table()->rowCount(), 3);
	QCOMPARE(column(panel.table(), 0), QStringList({"A", "B", "C"}));
	QCOMPARE(column(panel.table(), 1), QStringList({"platform-a", "platform-b", "platform-c"}));
	QCOMPARE(column(panel.table(), 2), QStringList({"30", "30", "30"}));
	QCOMPARE(column(panel.table(), 3), QStringList({"0", "60", "120"}));
	QCOMPARE(column(panel.table(), 4), QStringList({"30", "90", "150"}));
	// The first stop is selected and the buttons follow the selection.
	QVERIFY(panel.table()->isEnabled());
	QVERIFY(panel.timeModeCombo()->isEnabled());
	QCOMPARE(panel.table()->currentRow(), 0);
	QVERIFY(panel.addButton()->isEnabled());
	QVERIFY(panel.insertAfterButton()->isEnabled());
	QVERIFY(panel.removeButton()->isEnabled());
	QVERIFY(!panel.moveUpButton()->isEnabled());
	QVERIFY(panel.moveDownButton()->isEnabled());
	// The table says how a stop resolves on the route and keeps the ids of its stop.
	QCOMPARE(panel.timeBaseLabel()->text(), "Seconds after 08:00:00");
	QVERIFY(!panel.timeBaseLabel()->isHidden());
	QCOMPARE(panel.table()->item(0, 0)->toolTip(), "Station A [station-a]");
	QCOMPARE(panel.table()->item(1, 2)->data(Qt::UserRole).toString(), "station-b");
	QCOMPARE(panel.table()->item(1, 2)->data(Qt::UserRole + 1).toString(), "platform-b");
	QCOMPARE(panel.table()->item(1, 2)->data(Qt::UserRole + 2).toInt(), 1);
	QVERIFY2(f.events.log == Log({"refreshed", "refreshed"}), "showing a service only shows it");
	// Showing a service does not change the document.
	QVERIFY(!f.document.isDirty());
	QCOMPARE(f.document.revision(), quint64(1));
	panel.table()->setCurrentCell(2, 0);
	// The last stop cannot move down.
	QVERIFY(!panel.moveDownButton()->isEnabled());
	QVERIFY(panel.moveUpButton()->isEnabled());
}

// The time display.
void TestTimetableStopsPanel::showsClockTimes() {
	StopsFixture f;
	TimetableStopsPanel& panel = f.panel;
	panel.setServiceId("service-1");
	panel.table()->setCurrentCell(2, 0);
	panel.timeModeCombo()->setCurrentIndex(1);
	// The planned times can be shown as clock times and the selection stays.
	QCOMPARE(column(panel.table(), 3), QStringList({"08:00:00", "08:01:00", "08:02:00"}));
	QCOMPARE(column(panel.table(), 4).front(), "08:00:30");
	QVERIFY(panel.timeBaseLabel()->text().isEmpty());
	QVERIFY(panel.timeBaseLabel()->isHidden());
	QCOMPARE(panel.table()->currentRow(), 2);
	panel.timeModeCombo()->setCurrentIndex(0);
	QVERIFY2(column(panel.table(), 3) == QStringList({"0", "60", "120"}), "the planned times can be shown as elapsed seconds again");
}

// The stop order.
void TestTimetableStopsPanel::movesStops() {
	StopsFixture f;
	SceneDocument& document = f.document;
	TimetableStopsPanel& panel = f.panel;
	Events& events = f.events;
	panel.setServiceId("service-1");
	events.log.clear();
	panel.table()->setCurrentCell(1, 0);
	panel.moveStopUp();
	// A stop moves up and stays selected.
	QCOMPARE(stationsOf(document.model().services[0]), std::vector<std::string>({"station-b", "station-a", "station-c"}));
	QCOMPARE(panel.table()->currentRow(), 0);
	QCOMPARE(events.log, editLog);
	QCOMPARE(events.kinds, SceneDocument::ChangeKinds(SceneDocument::Services));
	QCOMPARE(document.lastChangeLabel(), "Move timetable stop up");
	QVERIFY(document.isDirty());
	QVERIFY2(panel.table()->item(1, 0)->toolTip().startsWith("The route passes this station before the previous stop.\nStation "),
		"a stop that no longer fits the route order says so");
	events.log.clear();
	panel.moveStopUp();
	// The first stop cannot move up.
	QVERIFY(events.log.empty());
	QCOMPARE(panel.table()->currentRow(), 0);
	panel.moveStopDown();
	// A stop moves down and stays selected.
	QCOMPARE(stationsOf(document.model().services[0]), std::vector<std::string>({"station-a", "station-b", "station-c"}));
	QCOMPARE(panel.table()->currentRow(), 1);
	QCOMPARE(events.log, editLog);
	QCOMPARE(document.lastChangeLabel(), "Move timetable stop down");
	panel.table()->setCurrentCell(2, 0);
	events.log.clear();
	panel.moveStopDown();
	// The last stop cannot move down.
	QVERIFY(events.log.empty());
	QCOMPARE(panel.table()->currentRow(), 2);
}

// Removing every stop of a service, then other services, which see the empty timetable, and another scene, which the
// removals left changed.
void TestTimetableStopsPanel::removesStopsAndFollowsService() {
	StopsFixture f;
	SceneDocument& document = f.document;
	TimetableStopsPanel& panel = f.panel;
	Events& events = f.events;
	panel.setServiceId("service-1");

	// Remove.
	events.log.clear();
	panel.table()->setCurrentCell(1, 0);
	panel.removeStop();
	// A removed stop leaves the next stop selected.
	QCOMPARE(stationsOf(document.model().services[0]), std::vector<std::string>({"station-a", "station-c"}));
	QCOMPARE(panel.table()->currentRow(), 1);
	QCOMPARE(events.log, editLog);
	QCOMPARE(document.lastChangeLabel(), "Remove timetable stop");
	events.log.clear();
	panel.removeStop();
	// Removing the last stop selects the new last stop.
	QCOMPARE(stationsOf(document.model().services[0]), std::vector<std::string>({"station-a"}));
	QCOMPARE(panel.table()->currentRow(), 0);
	QCOMPARE(events.log, editLog);
	panel.removeStop();
	// Removing the only stop leaves an empty timetable that stops can be added to.
	QVERIFY(document.model().services[0].stops.empty());
	QCOMPARE(panel.table()->rowCount(), 0);
	QVERIFY(!panel.removeButton()->isEnabled());
	QVERIFY(panel.addButton()->isEnabled());
	events.log.clear();
	panel.removeStop();
	QVERIFY2(events.log.empty(), "removing without a selected stop changes nothing");

	// The service follows the id.
	panel.setServiceId("service-2");
	// Another service shows its own stops.
	QCOMPARE(column(panel.table(), 0), QStringList({"A", "D"}));
	QVERIFY(stationsOf(document.model().services[0]).empty());
	panel.setServiceId("missing");
	// An unknown service shows nothing.
	QCOMPARE(panel.table()->rowCount(), 0);
	QVERIFY(!panel.table()->isEnabled());
	QVERIFY(!panel.addButton()->isEnabled());
	QVERIFY(!panel.timeModeCombo()->isEnabled());
	QVERIFY(panel.timeBaseLabel()->text().isEmpty());
	// An unknown service asks for a service.
	QCOMPARE(panel.emptyStateLabel()->text(), "Select a service to edit its timetable.");
	QVERIFY(panel.emptyStateLabel()->isVisibleTo(&panel));
	panel.setServiceId("");
	// An empty id shows nothing.
	QCOMPARE(panel.table()->rowCount(), 0);
	QVERIFY(!panel.addButton()->isEnabled());
	panel.setServiceId("service-2");

	// The scene is replaced.
	document.replace(lineScene(), false);
	// A new scene shows the stops of the service that is given, without an edit.
	QCOMPARE(column(panel.table(), 0), QStringList({"A", "D"}));
	QVERIFY(!document.isDirty());
}

// A run closes the timetable.
void TestTimetableStopsPanel::closesWhileRunUsesScene() {
	StopsFixture f;
	SceneDocument& document = f.document;
	TimetableStopsPanel& panel = f.panel;
	panel.setServiceId("service-2");
	document.setEditable(false);
	panel.refresh();
	// The timetable is closed while a run uses the scene.
	QVERIFY(!panel.table()->isEnabled());
	QVERIFY(!panel.addButton()->isEnabled());
	QVERIFY(!panel.insertAfterButton()->isEnabled());
	QVERIFY(!panel.timeModeCombo()->isEnabled());
	f.events.log.clear();
	panel.table()->setCurrentCell(0, 0);
	panel.removeStop();
	panel.moveStopDown();
	panel.moveStopUp();
	panel.addStop();
	panel.editStop(0);
	// No stop can be edited while a run uses the scene.
	QVERIFY(f.events.log.empty());
	QCOMPARE(document.model().services[1].stops.size(), std::size_t(2));
	QVERIFY(!panel.findChild<QDialog*>("stopEditorDialog"));
}

void TestTimetableStopsPanel::detachedPanelIgnoresDocument() {
	StopsFixture f;
	f.panel.setServiceId("service-2");
	f.panel.setDocument(nullptr);
	// A detached panel shows nothing.
	QCOMPARE(f.panel.table()->rowCount(), 0);
	QVERIFY(!f.panel.table()->isEnabled());
	const quint64 revisionDetached = f.document.revision();
	f.document.replace(lineScene(), false);
	// A detached panel does not follow the document.
	QCOMPARE(f.panel.table()->rowCount(), 0);
	QCOMPARE(f.document.revision(), revisionDetached + 1);
}

// A service whose route cannot be followed has no position with a station to add.
void TestTimetableStopsPanel::addStopWithoutReachableStationSaysSo() {
	SceneDocument document;
	TimetableStopsPanel panel;
	FakePrompts prompts;
	panel.setPrompts(&prompts);
	panel.setDocument(&document);
	SceneModel scene = lineScene();
	scene.services[0].route = "route-missing";
	document.replace(scene, false);
	panel.setServiceId("service-1");
	const quint64 revision = document.revision();
	const int boxes = messageBoxesDuring([&] { panel.addStop(); });
	// Add Stop without a reachable station says so through the prompts of the panel.
	QCOMPARE(prompts.infoCount, 1);
	QCOMPARE(prompts.infoTitle, "No Reachable Stop");
	QCOMPARE(prompts.infoMessage, "No station on the route is left to stop at. Check the route and the stop order.");
	// Add Stop without a reachable station opens no box and no dialog and changes nothing.
	QCOMPARE(boxes, 0);
	QVERIFY(!panel.findChild<QDialog*>("stopEditorDialog"));
	QCOMPARE(document.revision(), revision);
}

// The stop dialog: an edit, then planned times as clock times, which see the arrival of that edit.
void TestTimetableStopsPanel::editsStopInDialog() {
	StopsFixture f;
	SceneDocument& document = f.document;
	TimetableStopsPanel& panel = f.panel;
	Events& events = f.events;
	panel.setServiceId("service-1");

	// Edit.
	events.log.clear();
	DialogRun run = runDialog(
		panel,
		[&](QDialog& dialog) {
			// Editing a stop has no position to choose.
			QCOMPARE(dialog.windowTitle(), "Edit Timetable Stop");
			QVERIFY(!combo(dialog, "stopEditorPositionCombo"));
			// The dialog shows the stop that is edited, with no line for a stop that is on the route and no context line.
			QCOMPARE(combo(dialog, "stopEditorStationCombo")->currentData().toString(), "station-b");
			QCOMPARE(combo(dialog, "stopEditorPlatformCombo")->currentData().toString(), "platform-b");
			QCOMPARE(edit(dialog, "stopEditorDwellEdit")->text(), "30");
			QCOMPARE(edit(dialog, "stopEditorArrivalEdit")->text(), "60");
			QCOMPARE(edit(dialog, "stopEditorDepartureEdit")->text(), "90");
			QVERIFY(label(dialog, "stopEditorEligibilityLabel")->text().isEmpty());
			QVERIFY(label(dialog, "stopEditorEligibilityLabel")->isHidden());
			QVERIFY(!dialog.findChild<QLabel*>("dialogContext"));
			const auto* form = dialog.findChild<QFormLayout*>();
			QWidget* arrivalEdit = edit(dialog, "stopEditorArrivalEdit");
			QWidget* departureEdit = edit(dialog, "stopEditorDepartureEdit");
			QWidget* dwellEdit = edit(dialog, "stopEditorDwellEdit");
			const auto* arrivalPresent = dialog.findChild<QCheckBox*>("stopEditorArrivalPresent");
			const auto* departurePresent = dialog.findChild<QCheckBox*>("stopEditorDeparturePresent");
			// The two boxes of the planned times line up, whichever side the labels of the form are aligned to.
			if (arrivalPresent == nullptr)
				QFAIL("the stop dialog has no box for the planned arrival");
			if (departurePresent == nullptr)
				QFAIL("the stop dialog has no box for the planned departure");
			QCOMPARE(arrivalPresent->width(), departurePresent->width());
			QCOMPARE(arrivalPresent->x(), departurePresent->x());
			// The box of a planned time is the label of its field, and the time fields line up with the other fields.
			if (form == nullptr)
				QFAIL("the stop dialog has no form");
			QVERIFY(form->labelForField(arrivalEdit) == dialog.findChild<QCheckBox*>("stopEditorArrivalPresent"));
			QVERIFY(form->labelForField(departureEdit) == dialog.findChild<QCheckBox*>("stopEditorDeparturePresent"));
			QCOMPARE(arrivalEdit->mapTo(&dialog, QPoint()).x(), dwellEdit->mapTo(&dialog, QPoint()).x());
			QCOMPARE(departureEdit->mapTo(&dialog, QPoint()).x(), dwellEdit->mapTo(&dialog, QPoint()).x());
			QLabel* error = label(dialog, "stopEditorErrorLabel");
			// A reachable stop and the base time are help, and the error label is hidden and takes its look from the
			// stylesheet.
			QCOMPARE(role(label(dialog, "stopEditorEligibilityLabel")), "help");
			QCOMPARE(role(label(dialog, "stopEditorBaseTimeLabel")), "help");
			QCOMPARE(label(dialog, "stopEditorBaseTimeLabel")->text(), "Seconds after 08:00:00");
			QVERIFY(!error->isVisible());
			QVERIFY(error->styleSheet().isEmpty());
			const QSize openSize = dialog.size();
			const auto* openScroll = dialog.findChild<QScrollArea*>("dialogBodyScroll");
			const QWidget* dwell = edit(dialog, "stopEditorDwellEdit");
			// A screen that is too small for the content caps the height, and the body then scrolls.
			// The dialog opens at the height its content needs, with the dwell field in view and no vertical scroll range.
			if (openScroll == nullptr)
				QFAIL("the body of the stop dialog does not scroll");
			QVERIFY(dialog.height() == dialog.maximumHeight() || openScroll->verticalScrollBar()->maximum() == 0);
			QVERIFY(dialog.height() == dialog.maximumHeight()
				|| openScroll->viewport()->rect().contains(QRect(dwell->mapTo(openScroll->viewport(), QPoint(0, 0)), dwell->size())));
			edit(dialog, "stopEditorDwellEdit")->setText("-1");
			click(dialog, "stopEditorAcceptButton");
			settle();
			// A negative dwell is refused with a message and the dialog stays open.
			QVERIFY(dialog.isVisible());
			QVERIFY(error->text().startsWith("Minimum dwell must be"));
			// The message is an error callout in view, and the dialog does not get narrower or shorter when it appears.
			QVERIFY(error->isVisible());
			QCOMPARE(role(error), "error");
			QCOMPARE(dialog.width(), openSize.width());
			QCOMPARE_GE(dialog.height(), openSize.height());
			QVERIFY(inView(dialog, error));
			auto* bodyScroll = dialog.findChild<QScrollArea*>("dialogBodyScroll");
			// The dialog grows for the message up to its height limit; only a dialog at that limit scrolls the message into view.
			// The first rows stay in view when the message appears and the dialog has room to grow.
			if (bodyScroll == nullptr)
				QFAIL("the body of the stop dialog does not scroll");
			QVERIFY(bodyScroll->verticalScrollBar()->value() == 0 || dialog.height() == dialog.maximumHeight());
			edit(dialog, "stopEditorDwellEdit")->setText("45");
			edit(dialog, "stopEditorArrivalEdit")->setText("no time");
			click(dialog, "stopEditorAcceptButton");
			// A planned time that does not parse is refused with a message and the dialog stays open.
			QVERIFY(dialog.isVisible());
			QCOMPARE(label(dialog, "stopEditorErrorLabel")->text(), "Planned arrival must be seconds after the base time, such as 120.");
			edit(dialog, "stopEditorArrivalEdit")->setText("70");
			click(dialog, "stopEditorAcceptButton");
		},
		[&] { panel.editStop(1); });
	if (QTest::currentTestFailed())
		return;
	const SceneStop& changedStop = document.model().services[0].stops[1];
	// An accepted dialog writes the stop to the document.
	QVERIFY(run.opened);
	QVERIFY(run.accepted);
	QCOMPARE_EQ(changedStop.dwellSeconds, 45.0);
	QCOMPARE_EQ(changedStop.plannedArrivalSeconds, 70.0);
	QCOMPARE_EQ(changedStop.plannedDepartureSeconds, 90.0);
	QCOMPARE(changedStop.stationId, "station-b");
	QCOMPARE(document.model().services[0].stops.size(), std::size_t(3));
	// An edited stop is shown again and selected, and the document is marked changed once.
	QCOMPARE(events.log, editLog);
	QCOMPARE(document.lastChangeLabel(), "Edit timetable stop");
	QCOMPARE(panel.table()->currentRow(), 1);
	QCOMPARE(panel.table()->item(1, 3)->text(), "70");
	QCOMPARE(panel.table()->item(1, 2)->text(), "45");

	// The planned times in the dialog follow the time display.
	run = runDialog(
		panel,
		[&](QDialog& dialog) {
			combo(dialog, "stopEditorTimeModeCombo")->setCurrentIndex(1);
			// The dialog converts the planned times when it shows clock times.
			QCOMPARE(edit(dialog, "stopEditorArrivalEdit")->text(), "08:01:10");
			QCOMPARE(edit(dialog, "stopEditorDepartureEdit")->text(), "08:01:30");
			// Clock times need no line for the base time.
			QVERIFY(label(dialog, "stopEditorBaseTimeLabel")->text().isEmpty());
			QVERIFY(label(dialog, "stopEditorBaseTimeLabel")->isHidden());
			edit(dialog, "stopEditorArrivalEdit")->setText("eight");
			click(dialog, "stopEditorAcceptButton");
			// A clock time that does not parse is refused with an example of a clock time.
			QVERIFY(dialog.isVisible());
			QCOMPARE(label(dialog, "stopEditorErrorLabel")->text(), "Planned arrival must be a time such as 08:15:00, or +1d 00:10:00 for the next day.");
			edit(dialog, "stopEditorArrivalEdit")->setText("08:01:20");
			click(dialog, "stopEditorAcceptButton");
		},
		[&] { panel.editStop(1); });
	if (QTest::currentTestFailed())
		return;
	// A planned time typed as a clock time is stored as seconds from the case base.
	QVERIFY(run.accepted);
	QCOMPARE_EQ(document.model().services[0].stops[1].plannedArrivalSeconds, 80.0);
	QCOMPARE_EQ(document.model().services[0].stops[1].plannedDepartureSeconds, 90.0);
}

// Cancel, and the dialog of a clicked row.
void TestTimetableStopsPanel::cancelsStopDialog() {
	StopsFixture f;
	SceneDocument& document = f.document;
	TimetableStopsPanel& panel = f.panel;
	panel.setServiceId("service-1");
	f.events.log.clear();
	const SceneStop beforeCancel = document.model().services[0].stops[1];
	DialogRun run = runDialog(
		panel,
		[&](QDialog& dialog) {
			edit(dialog, "stopEditorDwellEdit")->setText("99");
			click(dialog, "stopEditorCancelButton");
		},
		[&] { panel.editStop(1); });
	// Cancelling the dialog changes nothing.
	QVERIFY(run.opened);
	QVERIFY(!run.accepted);
	QCOMPARE_EQ(document.model().services[0].stops[1].dwellSeconds, beforeCancel.dwellSeconds);
	QVERIFY(f.events.log.empty());
	run = runDialog(
		panel,
		[&](QDialog& dialog) {
			QComboBox* station = combo(dialog, "stopEditorStationCombo");
			QVERIFY2(station->currentData().toString() == "station-c", "clicking a row edits that stop");
			QVERIFY2(dialog.focusWidget() == station, "the stop dialog opened from a row does not focus its first field");
			// The second click of a double click on the row lands on the dialog that the first click opened, here on the
			// label of its station field.
			const QLabel* stationLabel = nullptr;
			for (const QLabel* candidate : dialog.findChildren<QLabel*>())
				if (candidate->buddy() == station)
					stationLabel = candidate;
			if (stationLabel == nullptr)
				QFAIL("the station field has a label");
			const QPoint point = stationLabel->mapTo(&dialog, stationLabel->rect().center());
			// qt_handleMouseEvent, which <QTest> declares, delivers a mouse event as the window system does, so that a click gives
			// the focus as a click of the user does.
			int clickTime = 1000000;
			for (const QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease})
				qt_handleMouseEvent(dialog.windowHandle(), point, dialog.mapToGlobal(point),
					type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::LeftButton, type, Qt::NoModifier, ++clickTime);
			QVERIFY2(dialog.focusWidget() == station, "a click on the body of the stop dialog takes the focus from its first field");
			click(dialog, "stopEditorCancelButton");
		},
		[&] { emit panel.table()->cellClicked(2, 0); });
	if (QTest::currentTestFailed())
		return;
	// Clicking a row of the table opens the dialog of the stop.
	QVERIFY(run.opened);
	QVERIFY(!run.accepted);
}

// Add: the position defaults to the last place that offers a station.
void TestTimetableStopsPanel::addsStopAtChosenPosition() {
	StopsFixture f;
	SceneDocument& document = f.document;
	TimetableStopsPanel& panel = f.panel;
	panel.setServiceId("service-1");
	f.events.log.clear();
	const DialogRun run = runDialog(
		panel,
		[&](QDialog& dialog) {
			// Adding a stop offers every position and starts at the last one that has a station.
			QCOMPARE(dialog.windowTitle(), "Add Timetable Stop");
			QCOMPARE(itemTexts(combo(dialog, "stopEditorPositionCombo")),
				QStringList({"At the start of the timetable", "After stop 1: A [platform-a]", "After stop 2: B [platform-b]",
					"After stop 3: C [platform-c]"}));
			QCOMPARE(combo(dialog, "stopEditorPositionCombo")->currentIndex(), 3);
			// Only the stations that the route reaches after the neighbouring stop are offered, and a single platform is chosen.
			QCOMPARE(itemTexts(combo(dialog, "stopEditorStationCombo")), QStringList({"D"}));
			QCOMPARE(combo(dialog, "stopEditorPlatformCombo")->currentData().toString(), "platform-d");
			QVERIFY(label(dialog, "stopEditorEligibilityLabel")->text().isEmpty());
			combo(dialog, "stopEditorPositionCombo")->setCurrentIndex(1);
			// A position between stops with no station between them offers none and says why.
			QVERIFY(!combo(dialog, "stopEditorStationCombo")->isEnabled());
			QCOMPARE(combo(dialog, "stopEditorStationCombo")->currentText(), "(no reachable station here)");
			QCOMPARE(label(dialog, "stopEditorEligibilityLabel")->text(), "The route has no station here between the stops before and after.");
			click(dialog, "stopEditorAcceptButton");
			// A position with no station cannot be accepted.
			QVERIFY(dialog.isVisible());
			QCOMPARE(label(dialog, "stopEditorErrorLabel")->text(), "The route has no station here between the stops before and after.");
			// A position with no station is shown as an error.
			QCOMPARE(role(label(dialog, "stopEditorEligibilityLabel")), "error");
			QCOMPARE(role(label(dialog, "stopEditorErrorLabel")), "error");
			combo(dialog, "stopEditorPositionCombo")->setCurrentIndex(3);
			// Choosing a position again offers its stations and clears and hides the message.
			QVERIFY(label(dialog, "stopEditorErrorLabel")->text().isEmpty());
			QVERIFY(!label(dialog, "stopEditorErrorLabel")->isVisible());
			QCOMPARE(role(label(dialog, "stopEditorEligibilityLabel")), "help");
			QCOMPARE(combo(dialog, "stopEditorStationCombo")->currentText(), "D");
			QVERIFY2(combo(dialog, "stopEditorPlatformCombo")->currentData().toString().isEmpty(),
				"a platform that was chosen before the position changed is not kept");
			QComboBox* platform = combo(dialog, "stopEditorPlatformCombo");
			platform->setCurrentIndex(platform->findData("platform-d"));
			edit(dialog, "stopEditorDwellEdit")->setText("20");
			click(dialog, "stopEditorAcceptButton");
		},
		[&] { panel.addStop(); });
	if (QTest::currentTestFailed())
		return;
	// An added stop enters the timetable at the chosen position and is selected.
	QVERIFY(run.accepted);
	QCOMPARE(stationsOf(document.model().services[0]), std::vector<std::string>({"station-a", "station-b", "station-c", "station-d"}));
	QCOMPARE(document.model().services[0].stops[3].platformId, "platform-d");
	QCOMPARE_EQ(document.model().services[0].stops[3].dwellSeconds, 20.0);
	QVERIFY(!document.model().services[0].stops[3].hasPlannedArrival);
	QCOMPARE(panel.table()->currentRow(), 3);
	QCOMPARE(f.events.log, editLog);
	QCOMPARE(document.lastChangeLabel(), "Add timetable stop");
}

// Insert after the selected stop of another service.
void TestTimetableStopsPanel::insertsStopsAfterSelected() {
	StopsFixture f;
	SceneDocument& document = f.document;
	TimetableStopsPanel& panel = f.panel;
	panel.setServiceId("service-2");
	panel.table()->setCurrentCell(0, 0);
	DialogRun run = runDialog(
		panel,
		[&](QDialog& dialog) {
			// Inserting after a stop starts after it and offers the stations between it and the next stop.
			QCOMPARE(combo(dialog, "stopEditorPositionCombo")->currentIndex(), 1);
			QCOMPARE(itemTexts(combo(dialog, "stopEditorStationCombo")), QStringList({"B", "C"}));
			combo(dialog, "stopEditorStationCombo")->setCurrentIndex(1);
			// Choosing another station offers its platforms and drops the platform of the first.
			QVERIFY(combo(dialog, "stopEditorPlatformCombo")->currentData().toString().isEmpty());
			QCOMPARE(itemTexts(combo(dialog, "stopEditorPlatformCombo")), QStringList({"(no platform)", "platform-c"}));
			combo(dialog, "stopEditorPlatformCombo")->setCurrentIndex(1);
			click(dialog, "stopEditorAcceptButton");
		},
		[&] { panel.insertStopAfterSelected(); });
	if (QTest::currentTestFailed())
		return;
	// A stop inserted after the selected one lands right behind it.
	QVERIFY(run.accepted);
	QCOMPARE(stationsOf(document.model().services[1]), std::vector<std::string>({"station-a", "station-c", "station-d"}));
	QCOMPARE(document.model().services[1].stops[1].platformId, "platform-c");
	QCOMPARE(panel.table()->currentRow(), 1);
	panel.table()->setCurrentCell(0, 0);
	run = runDialog(
		panel,
		[&](QDialog& dialog) {
			QVERIFY2(itemTexts(combo(dialog, "stopEditorStationCombo")) == QStringList({"B"}), "the stations between the new neighbours are offered");
			click(dialog, "stopEditorAcceptButton");
		},
		[&] { panel.insertStopAfterSelected(); });
	if (QTest::currentTestFailed())
		return;
	// Stops inserted one after the other keep the order of the route.
	QVERIFY(run.accepted);
	QCOMPARE(stationsOf(document.model().services[1]), std::vector<std::string>({"station-a", "station-b", "station-c", "station-d"}));
	QCOMPARE(panel.table()->currentRow(), 1);
}

// How a stop resolves decides the role of its message.
void TestTimetableStopsPanel::messageRoleFollowsResolution() {
	SceneModel scene = lineScene();
	// Station B gets a second platform on the route, so a stop there without a platform is ambiguous. Station E has no platform node on the route.
	scene.stations[1].platforms.push_back({"platform-b2", {"node-3"}});
	SceneStation off;
	off.id = "station-e";
	off.name = "E";
	off.platforms = {{"platform-e", {}}};
	scene.stations.push_back(off);
	const std::vector<std::pair<std::string, std::vector<SceneStop>>> cases = {
		{"resolved", {stop("station-a", "platform-a", 0.0, 30.0)}},
		{"ambiguous", {stop("station-b", "", 0.0, 30.0)}},
		{"off-route", {stop("station-e", "", 0.0, 30.0)}},
		{"invalid-platform", {stop("station-a", "platform-x", 0.0, 30.0)}},
		{"unknown-station", {stop("station-z", "", 0.0, 30.0)}},
		{"out-of-order", {stop("station-c", "platform-c", 0.0, 30.0), stop("station-b", "platform-b", 60.0, 90.0)}},
	};
	for (const auto& entry : cases) {
		SceneService service;
		service.id = entry.first;
		service.route = "route-1";
		service.stops = entry.second;
		scene.services.push_back(service);
	}
	SceneDocument document;
	TimetableStopsPanel panel;
	panel.setDocument(&document);
	document.replace(scene, false);
	QStringList roles;
	for (const auto& entry : cases) {
		panel.setServiceId(QString::fromStdString(entry.first));
		runDialog(
			panel,
			[&](QDialog& dialog) { roles << role(label(dialog, "stopEditorEligibilityLabel")); },
			[&] { panel.editStop(static_cast<int>(entry.second.size()) - 1); });
	}
	// A stop on the route is help, an ambiguous or off-route stop or an invalid platform is a warning, and a missing
	// station or a stop out of order is an error.
	QCOMPARE(roles, QStringList({"help", "warning", "warning", "warning", "error", "error"}));
}

// A stop without a platform or planned times.
void TestTimetableStopsPanel::showsDashForMissingValues() {
	SceneModel scene = lineScene();
	scene.services[0].stops[0].platformId.clear();
	scene.services[0].stops[0].hasPlannedArrival = false;
	scene.services[0].stops[0].hasPlannedDeparture = false;
	SceneDocument document;
	TimetableStopsPanel panel;
	panel.setDocument(&document);
	document.replace(scene, false);
	panel.setServiceId("service-1");
	// A platform or a planned time that a stop does not have is shown as a dash.
	QCOMPARE(panel.table()->item(0, 1)->text(), "-");
	QCOMPARE(panel.table()->item(0, 3)->text(), "-");
	QCOMPARE(panel.table()->item(0, 4)->text(), "-");
	QCOMPARE(panel.table()->item(1, 3)->text(), "60");
}

// Long names of stations and platforms.
void TestTimetableStopsPanel::showsLongNames() {
	SceneModel scene = lineScene();
	scene.stations[0].name = longName(95).toStdString();
	scene.stations[1].name = longName(31).toStdString();
	scene.stations[1].platforms[0].id = longName(95).toStdString();
	scene.services[0].stops[1].platformId = scene.stations[1].platforms[0].id;
	scene.services[0].stops.pop_back();
	SceneDocument document;
	TimetableStopsPanel panel;
	panel.setDocument(&document);
	document.replace(scene, false);
	panel.setServiceId("service-1");
	// The table holds the whole name of a long station and platform.
	QCOMPARE(panel.table()->item(0, 0)->text(), longName(95));
	QCOMPARE(panel.table()->item(1, 1)->text(), longName(95));

	DialogRun run = runDialog(
		panel,
		[&](QDialog& dialog) {
			settleCombos(dialog);
			auto* station = static_cast<ChoiceComboBox*>(combo(dialog, "stopEditorStationCombo"));
			auto* platform = static_cast<ChoiceComboBox*>(combo(dialog, "stopEditorPlatformCombo"));
			const QRect screen = dialog.screen()->availableGeometry();
			// The dialog selects the station and the platform of the stop.
			QCOMPARE(station->currentText(), longName(31));
			QCOMPARE(platform->currentText(), longName(95));
			// A platform name wider than its field is drawn elided and the field can shrink.
			QVERIFY(platform->displayText() != platform->currentText());
			QCOMPARE_LT(platform->minimumSizeHint().width(), platform->sizeHint().width());
			QVERIFY2(dialog.width() <= screen.width() * 9 / 10, "the dialog stays within the width of the screen");
			bool scrolls = false;
			for (QScrollArea* area : dialog.findChildren<QScrollArea*>())
				scrolls = scrolls || area->horizontalScrollBar()->isVisible();
			QVERIFY2(!scrolls, "the form of the dialog needs no horizontal scroll bar");
			click(dialog, "stopEditorCancelButton");
		},
		[&] { panel.editStop(1); });
	if (QTest::currentTestFailed())
		return;
	QVERIFY2(run.opened, "the dialog opens for a stop with long names");

	run = runDialog(
		panel,
		[&](QDialog& dialog) {
			QApplication::processEvents();
			auto* position = static_cast<ChoiceComboBox*>(combo(dialog, "stopEditorPositionCombo"));
			// The positions name the stops with their whole station name.
			QVERIFY(position->itemText(1).startsWith("After stop 1: "));
			QVERIFY(position->itemText(1).endsWith(" [platform-a]"));
			QVERIFY(position->itemText(1).contains(longName(95)));
			QVERIFY2(dialog.width() <= dialog.screen()->availableGeometry().width() * 9 / 10,
				"the dialog of an added stop stays within the width of the screen");
			click(dialog, "stopEditorCancelButton");
		},
		[&] { panel.addStop(); });
	if (QTest::currentTestFailed())
		return;
	QVERIFY2(run.opened, "the dialog opens to add a stop after long names");
}

// The checks of a stop come in a fixed order, and the first problem is reported with the field that it belongs to.
void TestTimetableStopsPanel::checksDraftInOrder() {
	const SceneModel scene = twoPlatformScene();
	const SceneService& service = scene.services[0];
	SceneStop draft = service.stops[1];
	draft.stationId.clear();
	draft.platformId.clear();
	StopDraftFields fields = fieldsOf(service.stops[1]);
	fields.arrivalText = "soon";
	fields.departureText = "later";
	fields.dwellText = "-5";
	StopDraftCheck check = validateStopDraft(scene, service, 1, false, draft, fields);
	QVERIFY(!check.ok);
	QCOMPARE(check.message, "Planned arrival must be seconds after the base time, such as 120.");
	QVERIFY(check.field == StopDraftField::PlannedArrival);
	fields.arrivalText = "60";
	check = validateStopDraft(scene, service, 1, false, draft, fields);
	QCOMPARE(check.message, "Planned departure must be seconds after the base time, such as 120.");
	QVERIFY(check.field == StopDraftField::PlannedDeparture);
	fields.departureText = "90";
	check = validateStopDraft(scene, service, 1, false, draft, fields);
	QCOMPARE(check.message, "Minimum dwell must be 0 or more seconds.");
	QVERIFY(check.field == StopDraftField::Dwell);
	fields.dwellText = "30";
	check = validateStopDraft(scene, service, 1, false, draft, fields);
	QCOMPARE(check.message, "Choose a station for this timetable stop.");
	QVERIFY(check.field == StopDraftField::Station);
	draft.stationId = "station-b";
	check = validateStopDraft(scene, service, 1, false, draft, fields);
	QCOMPARE(check.message, "Choose a platform: the route reaches more than one here.");
	QVERIFY(check.field == StopDraftField::Platform);
	draft.platformId = "platform-b";
	check = validateStopDraft(scene, service, 1, false, draft, fields);
	QVERIFY(check.ok);
	QVERIFY(check.message.isEmpty());

	// A position that offers no station comes before every field.
	StopDraftFields bad;
	bad.hasArrival = true;
	bad.arrivalText = "soon";
	bad.dwellText = "-1";
	check = validateStopDraft(scene, service, 1, true, SceneStop(), bad);
	QVERIFY(!check.ok);
	QCOMPARE(check.message, "The route has no station here between the stops before and after.");
	QVERIFY(check.field == StopDraftField::Position);
}

// A planned time whose box is checked must parse as seconds after the base time or as a clock time, as the display shows.
void TestTimetableStopsPanel::refusesPlannedTimesThatDoNotParse() {
	const SceneModel scene = lineScene();
	const SceneService& service = scene.services[0];
	StopDraftFields fields = fieldsOf(service.stops[1]);
	fields.arrivalText = "no time";
	StopDraftCheck check = validateStopDraft(scene, service, 1, false, service.stops[1], fields);
	QVERIFY(!check.ok);
	QCOMPARE(check.message, "Planned arrival must be seconds after the base time, such as 120.");
	QVERIFY(check.field == StopDraftField::PlannedArrival);
	fields.clockMode = true;
	fields.arrivalText = "08:01:00";
	fields.departureText = "eight";
	check = validateStopDraft(scene, service, 1, false, service.stops[1], fields);
	QVERIFY(!check.ok);
	QCOMPARE(check.message, "Planned departure must be a time such as 08:15:00, or +1d 00:10:00 for the next day.");
	QVERIFY(check.field == StopDraftField::PlannedDeparture);
	fields.departureText = "08:01:30";
	check = validateStopDraft(scene, service, 1, false, service.stops[1], fields);
	QVERIFY(check.ok);
	QCOMPARE_EQ(check.stop.plannedArrivalSeconds, 60.0);
	QCOMPARE_EQ(check.stop.plannedDepartureSeconds, 90.0);
}

// The minimum dwell is a finite number of 0 or more.
void TestTimetableStopsPanel::refusesDwellThatIsNotZeroOrMore() {
	const SceneModel scene = lineScene();
	const SceneService& service = scene.services[0];
	StopDraftFields fields = fieldsOf(service.stops[1]);
	for (const char* dwell : {"-1", "inf", "1e999", "thirty", ""}) {
		fields.dwellText = dwell;
		const StopDraftCheck check = validateStopDraft(scene, service, 1, false, service.stops[1], fields);
		QVERIFY2(!check.ok, dwell);
		QCOMPARE(check.message, "Minimum dwell must be 0 or more seconds.");
		QVERIFY(check.field == StopDraftField::Dwell);
	}
	fields.dwellText = " 0 ";
	const StopDraftCheck check = validateStopDraft(scene, service, 1, false, service.stops[1], fields);
	QVERIFY(check.ok);
	QCOMPARE_EQ(check.stop.dwellSeconds, 0.0);
}

// A platform that the route reaches more than once is refused for a new stop and for a stop whose station or platform
// changed, and kept for a stop that the dialog does not move.
void TestTimetableStopsPanel::refusesAmbiguousPlatformOnlyWhenAssigned() {
	SceneModel scene = twoPlatformScene();
	SceneStop atB = scene.services[0].stops[1];
	atB.platformId.clear();
	StopDraftCheck check = validateStopDraft(scene, scene.services[1], 1, true, atB, fieldsOf(atB));
	QVERIFY(!check.ok);
	QCOMPARE(check.message, "Choose a platform: the route reaches more than one here.");
	QVERIFY(check.field == StopDraftField::Platform);
	check = validateStopDraft(scene, scene.services[0], 1, false, atB, fieldsOf(atB));
	QVERIFY(!check.ok);
	QCOMPARE(check.message, "Choose a platform: the route reaches more than one here.");
	QVERIFY(check.field == StopDraftField::Platform);
	scene.services[0].stops[1].platformId.clear();
	check = validateStopDraft(scene, scene.services[0], 1, false, atB, fieldsOf(atB));
	QVERIFY2(check.ok, qPrintable(check.message));
	QVERIFY(check.stop.platformId.empty());
}

// An added stop that the timetable cannot take is refused with the reason of insertSceneStop, and the service stays as it
// was.
void TestTimetableStopsPanel::refusesStopThatInsertionRefuses() {
	const SceneModel scene = lineScene();
	const SceneService& service = scene.services[0];
	const SceneStop atA = stop("station-a", "platform-a", 200.0, 230.0);
	SceneService copy = service;
	const SceneStopInsertionResult refusal = insertSceneStop(scene, copy, 3, atA);
	QVERIFY(!refusal.inserted);
	const StopDraftCheck check = validateStopDraft(scene, service, 3, true, atA, fieldsOf(atA));
	QVERIFY(!check.ok);
	QVERIFY(!check.message.isEmpty());
	QCOMPARE(check.message, QString::fromStdString(refusal.error));
	QVERIFY(check.field == StopDraftField::None);
	QCOMPARE(stationsOf(service), std::vector<std::string>({"station-a", "station-b", "station-c"}));
}

// A planned time without a checked box or without text is absent, and a time whose text still shows the stored value
// keeps that value exactly. The clock time read back from that text would differ from it in the last digits.
void TestTimetableStopsPanel::takesDraftWithAbsentPlannedTime() {
	SceneModel scene = lineScene();
	scene.services[0].stops[1].plannedDepartureSeconds = 90.123456789;
	const SceneService& service = scene.services[0];
	StopDraftFields fields = fieldsOf(service.stops[1]);
	fields.clockMode = true;
	fields.hasArrival = false;
	fields.departureText = QString::fromStdString(formatPlannedTime(90.123456789, true, 8 * 3600));
	fields.dwellText = " 35 ";
	StopDraftCheck check = validateStopDraft(scene, service, 1, false, service.stops[1], fields);
	QVERIFY(check.ok);
	QVERIFY(check.message.isEmpty());
	QVERIFY(check.field == StopDraftField::None);
	QVERIFY(!check.stop.hasPlannedArrival);
	QVERIFY(check.stop.hasPlannedDeparture);
	QCOMPARE_EQ(check.stop.plannedDepartureSeconds, 90.123456789);
	QCOMPARE_EQ(check.stop.dwellSeconds, 35.0);
	QCOMPARE(check.stop.stationId, "station-b");
	QCOMPARE(check.stop.platformId, "platform-b");
	fields.hasArrival = true;
	fields.arrivalText = "  ";
	check = validateStopDraft(scene, service, 1, false, service.stops[1], fields);
	QVERIFY(check.ok);
	QVERIFY(!check.stop.hasPlannedArrival);
}

// The dialog can be driven without being shown: it gives the stop and the position that OK accepted.
void TestTimetableStopsPanel::dialogGivesStopWithoutExec() {
	const SceneModel scene = lineScene();
	StopEditorDialog editDialog(scene, scene.services[0], 1, false, false);
	edit(editDialog, "stopEditorDwellEdit")->setText("-1");
	click(editDialog, "stopEditorAcceptButton");
	// A refused stop leaves the dialog open with the message.
	QCOMPARE(editDialog.result(), int(QDialog::Rejected));
	QCOMPARE(label(editDialog, "stopEditorErrorLabel")->text(), "Minimum dwell must be 0 or more seconds.");
	edit(editDialog, "stopEditorDwellEdit")->setText("45");
	edit(editDialog, "stopEditorArrivalEdit")->setText("70");
	click(editDialog, "stopEditorAcceptButton");
	QCOMPARE(editDialog.result(), int(QDialog::Accepted));
	QCOMPARE(editDialog.position(), std::size_t(1));
	QCOMPARE(editDialog.stop().stationId, "station-b");
	QCOMPARE_EQ(editDialog.stop().dwellSeconds, 45.0);
	QCOMPARE_EQ(editDialog.stop().plannedArrivalSeconds, 70.0);
	QCOMPARE_EQ(editDialog.stop().plannedDepartureSeconds, 90.0);

	// An added stop takes the position chosen in the dialog.
	StopEditorDialog addDialog(scene, scene.services[0], 1, true, false);
	QVERIFY(!combo(addDialog, "stopEditorStationCombo")->isEnabled());
	combo(addDialog, "stopEditorPositionCombo")->setCurrentIndex(3);
	QCOMPARE(combo(addDialog, "stopEditorStationCombo")->currentData().toString(), "station-d");
	QComboBox* platform = combo(addDialog, "stopEditorPlatformCombo");
	platform->setCurrentIndex(platform->findData("platform-d"));
	edit(addDialog, "stopEditorDwellEdit")->setText("20");
	click(addDialog, "stopEditorAcceptButton");
	QCOMPARE(addDialog.result(), int(QDialog::Accepted));
	QCOMPARE(addDialog.position(), std::size_t(3));
	QCOMPARE(addDialog.stop().stationId, "station-d");
	QCOMPARE(addDialog.stop().platformId, "platform-d");
	QCOMPARE_EQ(addDialog.stop().dwellSeconds, 20.0);
}

// A scene that replaces the open one while the dialog is open, with a service of that id that differs from the copy that
// the dialog started from, keeps its stops when the dialog is accepted. A service equal to the copy would take the stop.
void TestTimetableStopsPanel::editAppliesNothingToReplacedScene() {
	StopsFixture f;
	f.panel.setServiceId("service-1");
	SceneModel replacement = lineScene();
	replacement.services[0].stops[0].dwellSeconds = 50.0;
	const DialogRun run = runDialog(
		f.panel,
		[&](QDialog& dialog) {
			edit(dialog, "stopEditorDwellEdit")->setText("45");
			f.document.replace(replacement, false);
			f.events.log.clear();
			click(dialog, "stopEditorAcceptButton");
		},
		[&] { f.panel.editStop(1); });
	QVERIFY(run.opened);
	QVERIFY(run.accepted);
	QCOMPARE_EQ(f.document.model().services[0].stops[0].dwellSeconds, 50.0);
	QCOMPARE_EQ(f.document.model().services[0].stops[1].dwellSeconds, 30.0);
	QVERIFY(!f.document.isDirty());
	QVERIFY(!logged(f.events.log, "documentChanged"));
	QVERIFY(!logged(f.events.log, "panelChanged"));
}

// A scene that a run closes for edits while the dialog is open takes nothing from it.
void TestTimetableStopsPanel::editAppliesNothingWhileClosedForEdits() {
	StopsFixture f;
	f.panel.setServiceId("service-1");
	f.events.log.clear();
	const quint64 revision = f.document.revision();
	const DialogRun run = runDialog(
		f.panel,
		[&](QDialog& dialog) {
			edit(dialog, "stopEditorDwellEdit")->setText("45");
			f.document.setEditable(false);
			click(dialog, "stopEditorAcceptButton");
		},
		[&] { f.panel.editStop(1); });
	QVERIFY(run.opened);
	QVERIFY(run.accepted);
	QCOMPARE_EQ(f.document.model().services[0].stops[1].dwellSeconds, 30.0);
	QCOMPARE(f.document.revision(), revision);
	QVERIFY(!f.document.isDirty());
	QVERIFY(f.events.log.empty());
}

// An added stop does not enter a scene that replaced the open one while the dialog was open.
void TestTimetableStopsPanel::addAppliesNothingToReplacedScene() {
	StopsFixture f;
	f.panel.setServiceId("service-2");
	f.panel.table()->setCurrentCell(0, 0);
	SceneModel replacement = lineScene();
	replacement.services[1].stops[0].dwellSeconds = 50.0;
	const DialogRun run = runDialog(
		f.panel,
		[&](QDialog& dialog) {
			QCOMPARE(combo(dialog, "stopEditorStationCombo")->currentData().toString(), "station-b");
			f.document.replace(replacement, false);
			f.events.log.clear();
			click(dialog, "stopEditorAcceptButton");
		},
		[&] { f.panel.insertStopAfterSelected(); });
	QVERIFY(run.opened);
	QVERIFY(run.accepted);
	QCOMPARE(stationsOf(f.document.model().services[1]), std::vector<std::string>({"station-a", "station-d"}));
	QCOMPARE_EQ(f.document.model().services[1].stops[0].dwellSeconds, 50.0);
	QVERIFY(!f.document.isDirty());
	QVERIFY(!logged(f.events.log, "documentChanged"));
	QVERIFY(!logged(f.events.log, "panelChanged"));
}

// A service that changed while the dialog was open does not take the stop of the dialog, which was checked against the
// service as it was, and keeps the other change.
void TestTimetableStopsPanel::editAppliesNothingWhenServiceChanged() {
	StopsFixture f;
	f.panel.setServiceId("service-1");
	quint64 revision = 0;
	const DialogRun run = runDialog(
		f.panel,
		[&](QDialog& dialog) {
			edit(dialog, "stopEditorDwellEdit")->setText("45");
			QVERIFY(f.document.edit(SceneDocument::Services, "Edit timetable stop", [](SceneModel& model) { model.services[0].stops[0].dwellSeconds = 50.0; }));
			revision = f.document.revision();
			f.events.log.clear();
			click(dialog, "stopEditorAcceptButton");
		},
		[&] { f.panel.editStop(1); });
	QVERIFY(run.opened);
	QVERIFY(run.accepted);
	QCOMPARE_EQ(f.document.model().services[0].stops[0].dwellSeconds, 50.0);
	QCOMPARE_EQ(f.document.model().services[0].stops[1].dwellSeconds, 30.0);
	QCOMPARE(f.document.revision(), revision);
	QVERIFY(!logged(f.events.log, "documentChanged"));
	QVERIFY(!logged(f.events.log, "panelChanged"));
}

// A change outside the services while the dialog is open, as a reload of a linked rolling stock file makes, does not keep
// the stop of the dialog out.
void TestTimetableStopsPanel::editAppliesAfterChangeOutsideServices() {
	StopsFixture f;
	f.panel.setServiceId("service-1");
	const DialogRun run = runDialog(
		f.panel,
		[&](QDialog& dialog) {
			edit(dialog, "stopEditorDwellEdit")->setText("45");
			QVERIFY(f.document.edit(SceneDocument::TrainUnits, "Reload linked source", [](SceneModel& model) {
				SceneTrainUnit unit;
				unit.id = "unit-1";
				model.trainUnits.push_back(unit);
			}));
			f.events.log.clear();
			click(dialog, "stopEditorAcceptButton");
		},
		[&] { f.panel.editStop(1); });
	QVERIFY(run.opened);
	QVERIFY(run.accepted);
	QCOMPARE_EQ(f.document.model().services[0].stops[1].dwellSeconds, 45.0);
	QCOMPARE(f.document.model().trainUnits.size(), std::size_t(1));
	QCOMPARE(f.events.log, editLog);
	QCOMPARE(f.document.lastChangeLabel(), "Edit timetable stop");
	QCOMPARE(f.panel.table()->currentRow(), 1);
}

// The text that another panel still holds is taken before the dialog copies the service. Here it renames the
// composition of the service, as the composition panel does with a typed id. Without the request the field would give
// its text up only when the dialog takes the focus, and that change of the service would keep the stop of the dialog out.
void TestTimetableStopsPanel::takesPendingTextBeforeCopyingService() {
	StopsFixture f;
	f.panel.setServiceId("service-1");
	bool pending = true;
	const auto takePendingText = [&f, &pending] {
		if (!pending)
			return;
		pending = false;
		QVERIFY(f.document.edit(SceneDocument::Compositions | SceneDocument::Services, "Rename composition",
			[](SceneModel& model) { model.services[0].composition = "composition-2"; }));
	};
	int requests = 0;
	QObject::connect(&f.panel, &TimetableStopsPanel::commitPendingEditsRequested, &f.panel, [&] {
		++requests;
		takePendingText();
	});
	const DialogRun run = runDialog(
		f.panel,
		[&](QDialog& dialog) {
			// The field that kept the focus loses it to the dialog.
			takePendingText();
			edit(dialog, "stopEditorDwellEdit")->setText("45");
			f.events.log.clear();
			click(dialog, "stopEditorAcceptButton");
		},
		[&] { f.panel.editStop(1); });
	QVERIFY(run.opened);
	QVERIFY(run.accepted);
	QCOMPARE(f.document.model().services[0].composition, "composition-2");
	QCOMPARE_EQ(f.document.model().services[0].stops[1].dwellSeconds, 45.0);
	QCOMPARE(f.events.log, editLog);
	QCOMPARE(f.document.lastChangeLabel(), "Edit timetable stop");
	QCOMPARE(requests, 1);
}

// The same for Add Stop, which is where text is still pending in the window: a click on a row moves the focus to the
// table first, but on macOS a button does not take the focus. The added stop enters the service that the taken text
// changed.
void TestTimetableStopsPanel::addTakesPendingTextBeforeCopyingService() {
	StopsFixture f;
	f.panel.setServiceId("service-1");
	bool pending = true;
	const auto takePendingText = [&f, &pending] {
		if (!pending)
			return;
		pending = false;
		QVERIFY(f.document.edit(SceneDocument::Compositions | SceneDocument::Services, "Rename composition",
			[](SceneModel& model) { model.services[0].composition = "composition-2"; }));
	};
	int requests = 0;
	QObject::connect(&f.panel, &TimetableStopsPanel::commitPendingEditsRequested, &f.panel, [&] {
		++requests;
		takePendingText();
	});
	const DialogRun run = runDialog(
		f.panel,
		[&](QDialog& dialog) {
			// The field that kept the focus loses it to the dialog.
			takePendingText();
			QComboBox* platform = combo(dialog, "stopEditorPlatformCombo");
			platform->setCurrentIndex(platform->findData("platform-d"));
			edit(dialog, "stopEditorDwellEdit")->setText("20");
			f.events.log.clear();
			click(dialog, "stopEditorAcceptButton");
		},
		[&] { f.panel.addStop(); });
	QVERIFY(run.opened);
	QVERIFY(run.accepted);
	QCOMPARE(f.document.model().services[0].composition, "composition-2");
	QCOMPARE(stationsOf(f.document.model().services[0]), std::vector<std::string>({"station-a", "station-b", "station-c", "station-d"}));
	QCOMPARE_EQ(f.document.model().services[0].stops[3].dwellSeconds, 20.0);
	QCOMPARE(f.events.log, editLog);
	QCOMPARE(f.document.lastChangeLabel(), "Add timetable stop");
	QCOMPARE(requests, 1);
}

// An added stop that the infrastructure no longer takes after a change while the dialog was open stays out of the
// service, although the service is the one that the dialog opened on.
void TestTimetableStopsPanel::addAppliesNothingWhenStopNoLongerFits() {
	StopsFixture f;
	f.panel.setServiceId("service-1");
	quint64 revision = 0;
	const DialogRun run = runDialog(
		f.panel,
		[&](QDialog& dialog) {
			QCOMPARE(combo(dialog, "stopEditorStationCombo")->currentData().toString(), "station-d");
			QComboBox* platform = combo(dialog, "stopEditorPlatformCombo");
			platform->setCurrentIndex(platform->findData("platform-d"));
			edit(dialog, "stopEditorDwellEdit")->setText("20");
			QVERIFY(f.document.edit(SceneDocument::Infrastructure, "Delete station", [](SceneModel& model) { model.stations.pop_back(); }));
			f.document.markSaved();
			revision = f.document.revision();
			f.events.log.clear();
			click(dialog, "stopEditorAcceptButton");
		},
		[&] { f.panel.addStop(); });
	QVERIFY(run.opened);
	QVERIFY(run.accepted);
	QCOMPARE(stationsOf(f.document.model().services[0]), std::vector<std::string>({"station-a", "station-b", "station-c"}));
	QCOMPARE(f.document.revision(), revision);
	QVERIFY(!f.document.isDirty());
	QVERIFY(f.events.log.empty());
}

// With the form of macOS the messages of the stop dialog still take the width of their column, so that no row of the
// form is an empty band. The application style stays changed after this slot, so it comes last.
void TestTimetableStopsPanel::leavesNoEmptyBandInFormOfMacOs() {
	QApplication::setStyle(new MacFormStyle);
	SceneDocument document;
	TimetableStopsPanel panel;
	panel.setDocument(&document);
	document.replace(lineScene(), false);
	panel.setServiceId("service-1");
	const DialogRun run = runDialog(
		panel,
		[&](QDialog& dialog) {
			const auto* form = dialog.findChild<QFormLayout*>();
			// No empty band taller than the spacing sits between the rows of the stop dialog.
			if (form == nullptr)
				QFAIL("the stop dialog has no form");
			int previousBottom = -1;
			int widestGap = 0;
			for (int row = 0; row < form->rowCount(); ++row) {
				QRect drawn;
				for (const auto role : {QFormLayout::LabelRole, QFormLayout::FieldRole}) {
					const QLayoutItem* item = form->itemAt(row, role);
					if (item && !item->isEmpty() && !item->geometry().isEmpty())
						drawn |= item->geometry();
				}
				if (drawn.isEmpty())
					continue;
				if (previousBottom >= 0)
					widestGap = std::max(widestGap, drawn.top() - previousBottom - 1);
				previousBottom = drawn.bottom();
			}
			QCOMPARE_GE(previousBottom, 0);
			QCOMPARE_LE(widestGap, dialog.layout()->spacing());
		},
		[&] { panel.editStop(1); });
	if (QTest::currentTestFailed())
		return;
	QVERIFY2(run.opened, "the stop dialog opens with the form of macOS");
}

QTEST_MAIN(TestTimetableStopsPanel)
#include "test_editor_timetablestops.moc"
