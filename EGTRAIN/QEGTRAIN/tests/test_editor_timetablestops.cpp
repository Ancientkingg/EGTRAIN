#include "editors/TimetableStopsPanel.h"

#include "widgets/ChoiceComboBox.h"

#include <QAbstractItemDelegate>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QEventLoop>
#include <QFont>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QString>
#include <QStringList>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTableWidget>
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

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = true;
	const Log editLog({"documentChanged", "refreshed", "panelChanged"});

	{
		TimetableStopsPanel panel;
		ok &= expect(panel.table()->rowCount() == 0 && !panel.table()->isEnabled() && !panel.addButton()->isEnabled()
				&& !panel.timeModeCombo()->isEnabled() && panel.timeBaseLabel()->text().isEmpty(),
			"a panel without a document shows nothing and offers nothing");
		ok &= expect(panel.emptyStateLabel()->text() == "Open a case to edit services." && panel.emptyStateLabel()->isVisibleTo(&panel),
			"a panel without a document asks to open a case");
		SceneDocument document;
		panel.setDocument(&document);
		ok &= expect(panel.emptyStateLabel()->text() == "Open a case to edit services.", "a document without a scene asks to open a case");
		panel.setServiceId("service-1");
		panel.addStop();
		panel.removeStop();
		panel.moveStopUp();
		panel.editStop(0);
		ok &= expect(!panel.addButton()->isEnabled() && document.revision() == 0 && !document.isDirty(),
			"a panel stays disabled while its document holds no scene, and its actions change nothing");
	}

	{
		// a service that has no stops
		SceneDocument document;
		TimetableStopsPanel panel;
		panel.setDocument(&document);
		SceneModel scene = lineScene();
		scene.services[0].stops.clear();
		document.replace(scene, false);
		panel.setServiceId("service-1");
		ok &= expect(panel.table()->rowCount() == 0 && panel.addButton()->isEnabled()
				&& panel.emptyStateLabel()->text() == "No stops. Use Add Stop." && panel.emptyStateLabel()->isVisibleTo(&panel),
			"a service without stops points to Add Stop");
		document.setEditable(false);
		panel.refresh();
		ok &= expect(!panel.addButton()->isEnabled() && panel.emptyStateLabel()->text() == "No stops.",
			"a service without stops does not point to Add Stop while a run uses the scene");
		document.setEditable(true);
		panel.refresh();
		panel.setServiceId("service-2");
		ok &= expect(panel.emptyStateLabel()->isHidden(), "the message goes away when the service has stops");
	}

	{
		// a scene that has no services
		SceneDocument document;
		TimetableStopsPanel panel;
		panel.setDocument(&document);
		SceneModel scene = lineScene();
		scene.services.clear();
		document.replace(scene, false);
		ok &= expect(panel.emptyStateLabel()->text() == "No services. Use Add Service." && panel.emptyStateLabel()->isVisibleTo(&panel),
			"a scene without services points to Add Service while it can be edited");
		document.setEditable(false);
		panel.refresh();
		ok &= expect(panel.emptyStateLabel()->text() == "No services.", "a scene without services does not point to Add Service while a run uses it");
	}

	for (const double scale : {1.0, 1.25, 1.5}) {
		// the five columns fill the table without a horizontal scroll bar whatever the font and the width, show their whole
		// headers when the table is wide enough for them, and elide a long station or platform name
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
		QApplication::processEvents();
		const QString label = QString(" at %1 times the font").arg(scale);
		bool wholeHeaders = true;
		for (int index = 0; index < table->columnCount(); ++index)
			wholeHeaders = wholeHeaders && table->columnWidth(index) >= header->sectionSizeHint(index);
		ok &= expect(!table->horizontalScrollBar()->isVisible() && header->length() == table->viewport()->width(),
			qPrintable("the five columns fill the table without a scroll bar" + label));
		ok &= expect(wholeHeaders, qPrintable("the five columns show their whole headers when the table fits them" + label));
		ok &= expect(table->item(0, 0)->text() == longName(95), "the cell of a long station name holds the whole name");
		ok &= expect(table->item(0, 0)->toolTip().contains(longName(95)), "the tooltip of a long station name holds the whole name");
		ok &= expect(table->item(0, 1)->text() == longName(60) && table->item(0, 1)->toolTip().endsWith(longName(60)),
			"the cell and the tooltip of a long platform name hold the whole name");
		ok &= expect(table->columnWidth(1) < body.horizontalAdvance(longName(60)) && table->columnWidth(0) < body.horizontalAdvance(longName(95)),
			qPrintable("a long station or platform name does not widen its column" + label));

		// a table narrower than the headers shortens the columns instead of scrolling
		panel.setMinimumWidth(0);
		panel.table()->setFixedWidth(headers * 2 / 3 + body.horizontalAdvance(QString(8, QLatin1Char('0'))));
		QApplication::processEvents();
		ok &= expect(!table->horizontalScrollBar()->isVisible() && header->length() == table->viewport()->width(),
			qPrintable("a narrow table shortens its columns instead of scrolling" + label));
		// a cut header is read from its tooltip, which holds the whole text on one line
		const QStringList headerTips = {"Station", "Stop platform", "Minimum dwell (s)", "Planned arrival", "Planned departure"};
		bool wholeTips = table->columnCount() == headerTips.size();
		for (int index = 0; wholeTips && index < table->columnCount(); ++index)
			wholeTips = table->model()->headerData(index, Qt::Horizontal, Qt::ToolTipRole).toString() == headerTips.at(index);
		ok &= expect(wholeTips, qPrintable("every column header has its whole text as a tooltip" + label));
	}

	for (int variant = 0; variant < 4; ++variant) {
		// clock times, also with a day prefix, are not elided at the width of a docked editor, whatever the platform or the station is;
		// without a day prefix, the widest time sets the width of the column
		const double scale = variant % 2 == 0 ? 1.0 : 1.5;
		const bool dayPrefix = variant < 2;
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
				whole = whole && table->itemDelegate(cell)->sizeHint(option, cell).width() <= table->columnWidth(index);
			}
		}
		ok &= expect((!dayPrefix || column(table, 3).back().startsWith("+1d ")) && whole,
			qPrintable("the clock times with and without a day prefix are whole in the planned columns" + label));
		ok &= expect(!table->horizontalScrollBar()->isVisible() && table->horizontalHeader()->length() == table->viewport()->width(),
			qPrintable("the columns still fill the table without a scroll bar when the clock times keep their width" + label));
		ok &= expect(table->columnWidth(0) >= table->horizontalHeader()->minimumSectionSize(),
			qPrintable("the station column keeps a width" + label));
	}

	{
		SceneDocument document;
		TimetableStopsPanel panel;
		panel.setDocument(&document);
		Events events(panel, document);

		// refresh
		document.replace(lineScene(), false);
		ok &= expect(panel.table()->rowCount() == 0 && !panel.table()->isEnabled() && !panel.addButton()->isEnabled(),
			"a scene shows no stops until a service is given");
		ok &= expect(panel.emptyStateLabel()->text() == "Select a service to edit its timetable.",
			"a scene without a given service asks for a service");
		panel.setServiceId("service-1");
		ok &= expect(panel.emptyStateLabel()->isHidden(), "the message is hidden while the service has stops");
		ok &= expect(panel.table()->rowCount() == 3 && column(panel.table(), 0) == QStringList({"A", "B", "C"})
				&& column(panel.table(), 1) == QStringList({"platform-a", "platform-b", "platform-c"})
				&& column(panel.table(), 2) == QStringList({"30", "30", "30"}) && column(panel.table(), 3) == QStringList({"0", "60", "120"})
				&& column(panel.table(), 4) == QStringList({"30", "90", "150"}),
			"the stops of the service are shown in order with their station, platform, dwell and planned times");
		ok &= expect(panel.table()->isEnabled() && panel.timeModeCombo()->isEnabled() && panel.table()->currentRow() == 0
				&& panel.addButton()->isEnabled() && panel.insertAfterButton()->isEnabled() && panel.removeButton()->isEnabled()
				&& !panel.moveUpButton()->isEnabled() && panel.moveDownButton()->isEnabled(),
			"the first stop is selected and the buttons follow the selection");
		ok &= expect(panel.timeBaseLabel()->text() == "Case base 08:00:00; planned times are elapsed seconds from this base"
				&& panel.table()->item(0, 0)->toolTip() == "Reachable ordered route visit\nStation A [station-a]"
				&& panel.table()->item(1, 2)->data(Qt::UserRole).toString() == "station-b"
				&& panel.table()->item(1, 2)->data(Qt::UserRole + 1).toString() == "platform-b"
				&& panel.table()->item(1, 2)->data(Qt::UserRole + 2).toInt() == 1,
			"the table says how a stop resolves on the route and keeps the ids of its stop");
		ok &= expect(events.log == Log({"refreshed", "refreshed"}), "showing a service only shows it");
		ok &= expect(!document.isDirty() && document.revision() == 1, "showing a service does not change the document");
		panel.table()->setCurrentCell(2, 0);
		ok &= expect(!panel.moveDownButton()->isEnabled() && panel.moveUpButton()->isEnabled(), "the last stop cannot move down");

		// the time display
		panel.timeModeCombo()->setCurrentIndex(1);
		ok &= expect(column(panel.table(), 3) == QStringList({"08:00:00", "08:01:00", "08:02:00"}) && column(panel.table(), 4).front() == "08:00:30"
				&& panel.timeBaseLabel()->text().contains("clock times include explicit day offsets") && panel.table()->currentRow() == 2,
			"the planned times can be shown as clock times and the selection stays");
		panel.timeModeCombo()->setCurrentIndex(0);
		ok &= expect(column(panel.table(), 3) == QStringList({"0", "60", "120"}), "the planned times can be shown as elapsed seconds again");

		// stop order
		events.log.clear();
		panel.table()->setCurrentCell(1, 0);
		panel.moveStopUp();
		ok &= expect(stationsOf(document.model().services[0]) == std::vector<std::string>({"station-b", "station-a", "station-c"})
				&& panel.table()->currentRow() == 0 && events.log == editLog && events.kinds == SceneDocument::Services
				&& document.lastChangeLabel() == "Move timetable stop up" && document.isDirty(),
			"a stop moves up and stays selected");
		ok &= expect(panel.table()->item(1, 0)->toolTip().startsWith("Invalid: stop is before or at an already used route visit\nStation "),
			"a stop that no longer fits the route order says so");
		events.log.clear();
		panel.moveStopUp();
		ok &= expect(events.log.empty() && panel.table()->currentRow() == 0, "the first stop cannot move up");
		panel.moveStopDown();
		ok &= expect(stationsOf(document.model().services[0]) == std::vector<std::string>({"station-a", "station-b", "station-c"})
				&& panel.table()->currentRow() == 1 && events.log == editLog && document.lastChangeLabel() == "Move timetable stop down",
			"a stop moves down and stays selected");
		panel.table()->setCurrentCell(2, 0);
		events.log.clear();
		panel.moveStopDown();
		ok &= expect(events.log.empty() && panel.table()->currentRow() == 2, "the last stop cannot move down");

		// remove
		panel.table()->setCurrentCell(1, 0);
		panel.removeStop();
		ok &= expect(stationsOf(document.model().services[0]) == std::vector<std::string>({"station-a", "station-c"})
				&& panel.table()->currentRow() == 1 && events.log == editLog && document.lastChangeLabel() == "Remove timetable stop",
			"a removed stop leaves the next stop selected");
		events.log.clear();
		panel.removeStop();
		ok &= expect(stationsOf(document.model().services[0]) == std::vector<std::string>({"station-a"}) && panel.table()->currentRow() == 0
				&& events.log == editLog,
			"removing the last stop selects the new last stop");
		panel.removeStop();
		ok &= expect(document.model().services[0].stops.empty() && panel.table()->rowCount() == 0 && !panel.removeButton()->isEnabled()
				&& panel.addButton()->isEnabled(),
			"removing the only stop leaves an empty timetable that stops can be added to");
		events.log.clear();
		panel.removeStop();
		ok &= expect(events.log.empty(), "removing without a selected stop changes nothing");

		// the service follows the id
		panel.setServiceId("service-2");
		ok &= expect(column(panel.table(), 0) == QStringList({"A", "D"}) && stationsOf(document.model().services[0]).empty(),
			"another service shows its own stops");
		panel.setServiceId("missing");
		ok &= expect(panel.table()->rowCount() == 0 && !panel.table()->isEnabled() && !panel.addButton()->isEnabled()
				&& !panel.timeModeCombo()->isEnabled() && panel.timeBaseLabel()->text().isEmpty(),
			"an unknown service shows nothing");
		ok &= expect(panel.emptyStateLabel()->text() == "Select a service to edit its timetable." && panel.emptyStateLabel()->isVisibleTo(&panel),
			"an unknown service asks for a service");
		panel.setServiceId("");
		ok &= expect(panel.table()->rowCount() == 0 && !panel.addButton()->isEnabled(), "an empty id shows nothing");
		panel.setServiceId("service-2");

		// the scene is replaced
		document.replace(lineScene(), false);
		ok &= expect(column(panel.table(), 0) == QStringList({"A", "D"}) && !document.isDirty(),
			"a new scene shows the stops of the service that is given, without an edit");

		// a run closes the timetable
		document.setEditable(false);
		panel.refresh();
		ok &= expect(!panel.table()->isEnabled() && !panel.addButton()->isEnabled() && !panel.insertAfterButton()->isEnabled()
				&& !panel.timeModeCombo()->isEnabled(),
			"the timetable is closed while a run uses the scene");
		events.log.clear();
		panel.table()->setCurrentCell(0, 0);
		panel.removeStop();
		panel.moveStopDown();
		panel.moveStopUp();
		panel.addStop();
		panel.editStop(0);
		ok &= expect(events.log.empty() && document.model().services[1].stops.size() == 2 && !panel.findChild<QDialog*>("stopEditorDialog"),
			"no stop can be edited while a run uses the scene");
		document.setEditable(true);
		panel.refresh();

		// detach
		panel.setDocument(nullptr);
		ok &= expect(panel.table()->rowCount() == 0 && !panel.table()->isEnabled(), "a detached panel shows nothing");
		const quint64 revisionDetached = document.revision();
		document.replace(lineScene(), false);
		ok &= expect(panel.table()->rowCount() == 0 && document.revision() == revisionDetached + 1,
			"a detached panel does not follow the document");
	}

	{
		// the stop dialog
		SceneDocument document;
		TimetableStopsPanel panel;
		panel.setDocument(&document);
		Events events(panel, document);
		document.replace(lineScene(), false);
		panel.setServiceId("service-1");

		// edit
		events.log.clear();
		DialogRun run = runDialog(
			panel,
			[&](QDialog& dialog) {
				ok &= expect(dialog.windowTitle() == "Edit timetable stop" && !combo(dialog, "stopEditorPositionCombo"),
					"editing a stop has no position to choose");
				ok &= expect(combo(dialog, "stopEditorStationCombo")->currentData().toString() == "station-b"
						&& combo(dialog, "stopEditorPlatformCombo")->currentData().toString() == "platform-b"
						&& edit(dialog, "stopEditorDwellEdit")->text() == "30" && edit(dialog, "stopEditorArrivalEdit")->text() == "60"
						&& edit(dialog, "stopEditorDepartureEdit")->text() == "90"
						&& label(dialog, "stopEditorEligibilityLabel")->text() == "Reachable ordered route visit",
					"the dialog shows the stop that is edited");
				QLabel* error = label(dialog, "stopEditorErrorLabel");
				ok &= expect(role(label(dialog, "stopEditorEligibilityLabel")) == "help" && role(label(dialog, "stopEditorBaseTimeLabel")) == "help"
						&& label(dialog, "stopEditorBaseTimeLabel")->text() == "Case base time: 08:00:00" && !error->isVisible()
						&& error->styleSheet().isEmpty(),
					"a reachable stop and the case base time are help, and the error label is hidden and takes its look from the stylesheet");
				const QSize openSize = dialog.size();
				const auto* openScroll = dialog.findChild<QScrollArea*>("dialogBodyScroll");
				const QWidget* dwell = edit(dialog, "stopEditorDwellEdit");
				// A screen that is too small for the content caps the height, and the body then scrolls.
				ok &= expect(openScroll && (dialog.height() == dialog.maximumHeight() || openScroll->verticalScrollBar()->maximum() == 0)
						&& (dialog.height() == dialog.maximumHeight()
							|| openScroll->viewport()->rect().contains(QRect(dwell->mapTo(openScroll->viewport(), QPoint(0, 0)), dwell->size()))),
					"the dialog opens at the height its content needs, with the dwell field in view and no vertical scroll range");
				edit(dialog, "stopEditorDwellEdit")->setText("-1");
				click(dialog, "stopEditorAcceptButton");
				settle();
				ok &= expect(dialog.isVisible() && error->text().startsWith("Minimum dwell must be"),
					"a negative dwell is refused with a message and the dialog stays open");
				ok &= expect(error->isVisible() && role(error) == "error" && dialog.width() == openSize.width() && dialog.height() >= openSize.height() && inView(dialog, error),
					"the message is an error callout in view, and the dialog does not get narrower or shorter when it appears");
				auto* bodyScroll = dialog.findChild<QScrollArea*>("dialogBodyScroll");
				// The dialog grows for the message up to its height limit; only a dialog at that limit scrolls the message into view.
				ok &= expect(bodyScroll && (bodyScroll->verticalScrollBar()->value() == 0 || dialog.height() == dialog.maximumHeight()),
					"the first rows stay in view when the message appears and the dialog has room to grow");
				edit(dialog, "stopEditorDwellEdit")->setText("45");
				edit(dialog, "stopEditorArrivalEdit")->setText("no time");
				click(dialog, "stopEditorAcceptButton");
				ok &= expect(dialog.isVisible() && label(dialog, "stopEditorErrorLabel")->text().startsWith("Planned arrival is invalid"),
					"a planned time that does not parse is refused with a message and the dialog stays open");
				edit(dialog, "stopEditorArrivalEdit")->setText("70");
				click(dialog, "stopEditorAcceptButton");
			},
			[&] { panel.editStop(1); });
		const SceneStop& changedStop = document.model().services[0].stops[1];
		ok &= expect(run.opened && run.accepted && changedStop.dwellSeconds == 45.0 && changedStop.plannedArrivalSeconds == 70.0
				&& changedStop.plannedDepartureSeconds == 90.0 && changedStop.stationId == "station-b"
				&& document.model().services[0].stops.size() == 3,
			"an accepted dialog writes the stop to the document");
		ok &= expect(events.log == editLog && document.lastChangeLabel() == "Edit timetable stop" && panel.table()->currentRow() == 1
				&& panel.table()->item(1, 3)->text() == "70" && panel.table()->item(1, 2)->text() == "45",
			"an edited stop is shown again and selected, and the document is marked changed once");

		// the planned times in the dialog follow the time display
		run = runDialog(
			panel,
			[&](QDialog& dialog) {
				combo(dialog, "stopEditorTimeModeCombo")->setCurrentIndex(1);
				ok &= expect(edit(dialog, "stopEditorArrivalEdit")->text() == "08:01:10" && edit(dialog, "stopEditorDepartureEdit")->text() == "08:01:30",
					"the dialog converts the planned times when it shows clock times");
				edit(dialog, "stopEditorArrivalEdit")->setText("08:01:20");
				click(dialog, "stopEditorAcceptButton");
			},
			[&] { panel.editStop(1); });
		ok &= expect(run.accepted && document.model().services[0].stops[1].plannedArrivalSeconds == 80.0
				&& document.model().services[0].stops[1].plannedDepartureSeconds == 90.0,
			"a planned time typed as a clock time is stored as seconds from the case base");

		// cancel
		events.log.clear();
		const SceneStop beforeCancel = document.model().services[0].stops[1];
		run = runDialog(
			panel,
			[&](QDialog& dialog) {
				edit(dialog, "stopEditorDwellEdit")->setText("99");
				click(dialog, "stopEditorCancelButton");
			},
			[&] { panel.editStop(1); });
		ok &= expect(run.opened && !run.accepted && document.model().services[0].stops[1].dwellSeconds == beforeCancel.dwellSeconds
				&& events.log.empty(),
			"cancelling the dialog changes nothing");
		run = runDialog(
			panel,
			[&](QDialog& dialog) {
				ok &= expect(combo(dialog, "stopEditorStationCombo")->currentData().toString() == "station-c", "clicking a row edits that stop");
				click(dialog, "stopEditorCancelButton");
			},
			[&] { emit panel.table()->cellClicked(2, 0); });
		ok &= expect(run.opened && !run.accepted, "clicking a row of the table opens the dialog of the stop");

		// add: the position defaults to the last place that offers a station
		events.log.clear();
		run = runDialog(
			panel,
			[&](QDialog& dialog) {
				ok &= expect(dialog.windowTitle() == "Add timetable stop"
						&& itemTexts(combo(dialog, "stopEditorPositionCombo"))
							== QStringList({"At the start of the timetable", "After stop 1: A [platform-a]", "After stop 2: B [platform-b]",
								"After stop 3: C [platform-c]"})
						&& combo(dialog, "stopEditorPositionCombo")->currentIndex() == 3,
					"adding a stop offers every position and starts at the last one that has a station");
				ok &= expect(itemTexts(combo(dialog, "stopEditorStationCombo")) == QStringList({"D"})
						&& combo(dialog, "stopEditorPlatformCombo")->currentData().toString() == "platform-d"
						&& label(dialog, "stopEditorEligibilityLabel")->text() == "Reachable ordered route visit",
					"only the stations that the route reaches after the neighbouring stop are offered, and a single platform is chosen");
				combo(dialog, "stopEditorPositionCombo")->setCurrentIndex(1);
				ok &= expect(!combo(dialog, "stopEditorStationCombo")->isEnabled()
						&& combo(dialog, "stopEditorStationCombo")->currentText() == "(no reachable station here)"
						&& label(dialog, "stopEditorEligibilityLabel")->text().startsWith("No station visit is available"),
					"a position between stops with no station between them offers none and says why");
				click(dialog, "stopEditorAcceptButton");
				ok &= expect(dialog.isVisible() && label(dialog, "stopEditorErrorLabel")->text().startsWith("No station visit is available"),
					"a position with no station cannot be accepted");
				ok &= expect(role(label(dialog, "stopEditorEligibilityLabel")) == "error" && role(label(dialog, "stopEditorErrorLabel")) == "error",
					"a position with no station is shown as an error");
				combo(dialog, "stopEditorPositionCombo")->setCurrentIndex(3);
				ok &= expect(label(dialog, "stopEditorErrorLabel")->text().isEmpty() && !label(dialog, "stopEditorErrorLabel")->isVisible()
						&& role(label(dialog, "stopEditorEligibilityLabel")) == "help" && combo(dialog, "stopEditorStationCombo")->currentText() == "D",
					"choosing a position again offers its stations and clears and hides the message");
				ok &= expect(combo(dialog, "stopEditorPlatformCombo")->currentData().toString().isEmpty(),
					"a platform that was chosen before the position changed is not kept");
				QComboBox* platform = combo(dialog, "stopEditorPlatformCombo");
				platform->setCurrentIndex(platform->findData("platform-d"));
				edit(dialog, "stopEditorDwellEdit")->setText("20");
				click(dialog, "stopEditorAcceptButton");
			},
			[&] { panel.addStop(); });
		ok &= expect(run.accepted && stationsOf(document.model().services[0]) == std::vector<std::string>({"station-a", "station-b", "station-c", "station-d"})
				&& document.model().services[0].stops[3].platformId == "platform-d" && document.model().services[0].stops[3].dwellSeconds == 20.0
				&& !document.model().services[0].stops[3].hasPlannedArrival && panel.table()->currentRow() == 3 && events.log == editLog
				&& document.lastChangeLabel() == "Add timetable stop",
			"an added stop enters the timetable at the chosen position and is selected");

		// insert after the selected stop of another service
		panel.setServiceId("service-2");
		panel.table()->setCurrentCell(0, 0);
		run = runDialog(
			panel,
			[&](QDialog& dialog) {
				ok &= expect(combo(dialog, "stopEditorPositionCombo")->currentIndex() == 1
						&& itemTexts(combo(dialog, "stopEditorStationCombo")) == QStringList({"B", "C"}),
					"inserting after a stop starts after it and offers the stations between it and the next stop");
				combo(dialog, "stopEditorStationCombo")->setCurrentIndex(1);
				ok &= expect(combo(dialog, "stopEditorPlatformCombo")->currentData().toString().isEmpty()
						&& itemTexts(combo(dialog, "stopEditorPlatformCombo")) == QStringList({"(no platform)", "platform-c"}),
					"choosing another station offers its platforms and drops the platform of the first");
				combo(dialog, "stopEditorPlatformCombo")->setCurrentIndex(1);
				click(dialog, "stopEditorAcceptButton");
			},
			[&] { panel.insertStopAfterSelected(); });
		ok &= expect(run.accepted && stationsOf(document.model().services[1]) == std::vector<std::string>({"station-a", "station-c", "station-d"})
				&& document.model().services[1].stops[1].platformId == "platform-c" && panel.table()->currentRow() == 1,
			"a stop inserted after the selected one lands right behind it");
		panel.table()->setCurrentCell(0, 0);
		run = runDialog(
			panel,
			[&](QDialog& dialog) {
				ok &= expect(itemTexts(combo(dialog, "stopEditorStationCombo")) == QStringList({"B"}),
					"the stations between the new neighbours are offered");
				click(dialog, "stopEditorAcceptButton");
			},
			[&] { panel.insertStopAfterSelected(); });
		ok &= expect(run.accepted && stationsOf(document.model().services[1]) == std::vector<std::string>({"station-a", "station-b", "station-c", "station-d"})
				&& panel.table()->currentRow() == 1,
			"stops inserted one after the other keep the order of the route");
	}

	{
		// how a stop resolves decides the role of its message
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
		ok &= expect(roles == QStringList({"help", "warning", "warning", "warning", "error", "error"}),
			"a stop on the route is help, an ambiguous or off-route stop or an invalid platform is a warning, and a missing station or a stop out of order is an error");
	}

	{
		// a stop without a platform or planned times
		SceneModel scene = lineScene();
		scene.services[0].stops[0].platformId.clear();
		scene.services[0].stops[0].hasPlannedArrival = false;
		scene.services[0].stops[0].hasPlannedDeparture = false;
		SceneDocument document;
		TimetableStopsPanel panel;
		panel.setDocument(&document);
		document.replace(scene, false);
		panel.setServiceId("service-1");
		const QString emDash(QChar(0x2014));
		ok &= expect(panel.table()->item(0, 1)->text() == emDash && panel.table()->item(0, 3)->text() == emDash
				&& panel.table()->item(0, 4)->text() == emDash && panel.table()->item(1, 3)->text() == "60",
			"a platform or a planned time that a stop does not have is shown as a dash");
	}

	{
		// long names of stations and platforms
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
		ok &= expect(panel.table()->item(0, 0)->text() == longName(95) && panel.table()->item(1, 1)->text() == longName(95),
			"the table holds the whole name of a long station and platform");

		DialogRun run = runDialog(
			panel,
			[&](QDialog& dialog) {
				QApplication::processEvents();
				auto* station = static_cast<ChoiceComboBox*>(combo(dialog, "stopEditorStationCombo"));
				auto* platform = static_cast<ChoiceComboBox*>(combo(dialog, "stopEditorPlatformCombo"));
				const QRect screen = dialog.screen()->availableGeometry();
				ok &= expect(station->currentText() == longName(31) && platform->currentText() == longName(95),
					"the dialog selects the station and the platform of the stop");
				ok &= expect(platform->displayText() != platform->currentText() && platform->minimumSizeHint().width() < platform->sizeHint().width(),
					"a platform name wider than its field is drawn elided and the field can shrink");
				ok &= expect(dialog.width() <= screen.width() * 9 / 10, "the dialog stays within the width of the screen");
				bool scrolls = false;
				for (QScrollArea* area : dialog.findChildren<QScrollArea*>())
					scrolls = scrolls || area->horizontalScrollBar()->isVisible();
				ok &= expect(!scrolls, "the form of the dialog needs no horizontal scroll bar");
				click(dialog, "stopEditorCancelButton");
			},
			[&] { panel.editStop(1); });
		ok &= expect(run.opened, "the dialog opens for a stop with long names");

		run = runDialog(
			panel,
			[&](QDialog& dialog) {
				QApplication::processEvents();
				auto* position = static_cast<ChoiceComboBox*>(combo(dialog, "stopEditorPositionCombo"));
				ok &= expect(position->itemText(1).startsWith("After stop 1: ") && position->itemText(1).endsWith(" [platform-a]")
						&& position->itemText(1).contains(longName(95)),
					"the positions name the stops with their whole station name");
				ok &= expect(dialog.width() <= dialog.screen()->availableGeometry().width() * 9 / 10, "the dialog of an added stop stays within the width of the screen");
				click(dialog, "stopEditorCancelButton");
			},
			[&] { panel.addStop(); });
		ok &= expect(run.opened, "the dialog opens to add a stop after long names");
	}

	if (!ok)
		return 1;
	std::cout << "all timetable stops panel tests passed\n";
	return 0;
}
