#include "editors/CaseSettingsPanel.h"
#include "scene/SceneValidator.h"
#include "scene/SceneWriter.h"
#include "support/EditHistory.h"
#include "widgets/FocusWheel.h"

#include <QAccessible>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QString>
#include <QStringList>
#include <QTest>

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

static SceneModel caseScene(const char* name, const char* baseTime) {
	SceneModel scene;
	scene.name = name;
	scene.description = std::string("About ") + name;
	scene.baseTime = baseTime;
	scene.settings.hasDuration = true;
	scene.settings.durationSeconds = 3600.0;
	return scene;
}

// The order in which a panel and its document report an edit.
struct Events {
	std::vector<std::string> log;
	QString baseTime;

	Events(CaseSettingsPanel& panel, SceneDocument& document) {
		QObject::connect(&panel, &CaseSettingsPanel::baseTimeChanged, &panel, [this](const QString& text) {
			log.push_back("baseTimeChanged");
			baseTime = text;
		});
		QObject::connect(&document, &SceneDocument::changed, &panel, [this] { log.push_back("documentChanged"); });
		QObject::connect(&panel, &CaseSettingsPanel::changed, &panel, [this] { log.push_back("panelChanged"); });
	}
};

using Log = std::vector<std::string>;

// What the user does to a text field: type, then leave the field.
static void typeAndLeave(QLineEdit* edit, const QString& text) {
	edit->setText(text);
	emit edit->editingFinished();
}

// The text field of a number field.
static QLineEdit* spinText(QDoubleSpinBox* edit) {
	return edit->findChild<QLineEdit*>();
}

// Gives a field of a shown panel the focus, as a click does.
static bool giveFocus(QWidget* edit) {
	edit->setFocus();
	QApplication::processEvents();
	if (!edit->hasFocus())
		std::cerr << "failed: " << edit->objectName().toStdString() << " did not get the focus\n";
	return edit->hasFocus();
}

static bool hasCode(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code) {
	return std::any_of(diagnostics.begin(), diagnostics.end(), [&code](const SceneDiagnostic& diagnostic) { return diagnostic.code == code; });
}

// The value of a field as an accessibility client such as VoiceOver reads and sets it, or null.
static QAccessibleValueInterface* accessibleValue(QWidget* field) {
	QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(field);
	return accessible ? accessible->valueInterface() : nullptr;
}

// A panel on a document that shows `scene`, and the events of both.
struct CaseFixture {
	SceneDocument document;
	CaseSettingsPanel panel;
	Events events{panel, document};

	explicit CaseFixture(const SceneModel& scene) {
		panel.setDocument(&document);
		document.replace(scene, false);
	}

	// The panel is hidden while the fixture is whole. A panel destroyed while shown loses the focus in its QWidget
	// destructor, and a field that had the focus then calls a slot of the panel whose own part is already gone.
	~CaseFixture() { panel.hide(); }
};

class TestCaseSettingsPanel : public QObject {
	Q_OBJECT

private slots:
	void keepsUntouchedNumbers();
	void namesFields();
	void showsNothingWithoutScene();
	void showsNewScene();
	void leavingUnchangedFieldEditsNothing();
	void editsFieldsAndDropsUnfinishedText();
	void editableDuringRun();
	void detachedPanelIgnoresDocument();
	void forgetsDeletedDocument();
	void longDescriptionShowsStartAfterFocus();
	void accessibilityStepsBySingleStep();
	void namesCommandLineOverrides();
	void editsPassengerTransfers();
	void editsPassengerWalkingTime();
	void editsPassengerLateThreshold();
	void editsVirtualCouplingNumbers();
	void editsVirtualCouplingServices();
};

// Save, Undo and validation commit the field that is being edited. A stored number that a field shows at a limit of its
// range reaches the saved file and the validation as it is while the user does not edit it, even when the field has the
// focus or another field is edited, and a number that the user types is stored as typed.
void TestCaseSettingsPanel::keepsUntouchedNumbers() {
	SceneModel scene = caseScene("Case F", "08:00:00");
	scene.settings.durationSeconds = -60.0;
	scene.settings.hasBufferTime = true;
	scene.settings.bufferTimeSeconds = -5.0;
	scene.settings.hasRecoveryTime = true;
	scene.settings.recoveryTimePercent = -2.5;
	CaseFixture f(scene);
	SceneDocument& document = f.document;
	CaseSettingsPanel& panel = f.panel;
	// A negative duration, buffer and recovery are shown at 0.
	QCOMPARE_EQ(panel.durationSecondsEdit()->value(), 0.0);
	QCOMPARE_EQ(panel.bufferSecondsEdit()->value(), 0.0);
	QCOMPARE_EQ(panel.recoveryPercentEdit()->value(), 0.0);

	const nlohmann::json saved = encodeSceneMetadata(document.model());
	const quint64 revision = document.revision();
	panel.commitPendingEditors();
	panel.show();
	panel.activateWindow();
	for (QDoubleSpinBox* field : {panel.durationSecondsEdit(), panel.bufferSecondsEdit(), panel.recoveryPercentEdit()}) {
		QVERIFY(giveFocus(field));
		panel.commitPendingEditors();
	}
	QVERIFY(giveFocus(panel.nameEdit()));
	panel.commit();
	const SceneSimulationSettings& kept = document.model().settings;
	// A duration of -60 s, a buffer of -5 s and a recovery of -2.5 % are saved as stored.
	QCOMPARE_EQ(kept.durationSeconds, -60.0);
	QCOMPARE_EQ(kept.bufferTimeSeconds, -5.0);
	QCOMPARE_EQ(kept.recoveryTimePercent, -2.5);
	QVERIFY(encodeSceneMetadata(document.model()) == saved);
	// Visiting the number fields and committing them adds no Undo step.
	QVERIFY(f.events.log.empty());
	QCOMPARE(document.revision(), revision);
	QVERIFY(!document.isDirty());
	const std::vector<SceneDiagnostic> diagnostics = validateScene(document.model());
	// The validation still reports the stored values.
	QVERIFY(hasCode(diagnostics, "scene.duration.invalid"));
	QVERIFY(hasCode(diagnostics, "scene.buffer.invalid"));
	QVERIFY(hasCode(diagnostics, "scene.recovery.invalid"));

	typeAndLeave(panel.nameEdit(), "Renamed");
	// A name edit leaves the number fields as they are stored.
	QCOMPARE_EQ(kept.durationSeconds, -60.0);
	QCOMPARE_EQ(kept.bufferTimeSeconds, -5.0);
	QCOMPARE_EQ(kept.recoveryTimePercent, -2.5);
	QCOMPARE(document.model().name, "Renamed");
	QCOMPARE(document.revision(), revision + 1);
	QVERIFY(giveFocus(panel.bufferSecondsEdit()));
	spinText(panel.bufferSecondsEdit())->setText("12.5");
	panel.commitPendingEditors();
	// A buffer that is being typed is stored, and the other fields keep their values.
	QCOMPARE_EQ(kept.bufferTimeSeconds, 12.5);
	QCOMPARE_EQ(kept.durationSeconds, -60.0);
	QCOMPARE_EQ(kept.recoveryTimePercent, -2.5);
	QCOMPARE(document.revision(), revision + 2);
	panel.recoveryPercentEdit()->setValue(3.0);
	// A recovery edit is stored, and the duration keeps its value.
	QCOMPARE_EQ(kept.recoveryTimePercent, 3.0);
	QCOMPARE_EQ(kept.durationSeconds, -60.0);
	QCOMPARE(document.revision(), revision + 3);
}

// The transfer limit of live passenger journeys is shown, edited as one Undo step, kept when another field is edited, and
// shown at the nearest limit while a stored value lies outside 0 to 10.
void TestCaseSettingsPanel::editsPassengerTransfers() {
	SceneModel scene = caseScene("Case T", "08:00:00");
	CaseFixture f(scene);
	SceneDocument& document = f.document;
	CaseSettingsPanel& panel = f.panel;
	// A case without the setting shows the default of 2.
	QCOMPARE(panel.maxTransfersEdit()->value(), kDefaultPassengerMaxTransfers);
	QVERIFY(panel.maxTransfersEdit()->isEnabled());
	const quint64 revision = document.revision();
	panel.maxTransfersEdit()->setValue(4);
	// An edit is stored as one change.
	QCOMPARE(document.model().settings.passengerMaxTransfers, 4);
	QCOMPARE(document.revision(), revision + 1);
	QCOMPARE(document.lastChangeLabel(), "Edit case settings");
	typeAndLeave(panel.nameEdit(), "Renamed");
	// A name edit keeps the transfer limit.
	QCOMPARE(document.model().settings.passengerMaxTransfers, 4);

	SceneModel outside = caseScene("Case U", "08:00:00");
	outside.settings.passengerMaxTransfers = 12;
	CaseFixture g(outside);
	// A stored value above the range is shown at 10 and stays stored until the user edits the field.
	QCOMPARE(g.panel.maxTransfersEdit()->value(), kMaxPassengerMaxTransfers);
	g.panel.commitPendingEditors();
	typeAndLeave(g.panel.nameEdit(), "Renamed");
	QCOMPARE(g.document.model().settings.passengerMaxTransfers, 12);
}

// The walking time per platform is shown with its default, edited as one Undo step and kept when another field is edited.
void TestCaseSettingsPanel::editsPassengerWalkingTime() {
	SceneModel scene = caseScene("Case W", "08:00:00");
	CaseFixture f(scene);
	QCOMPARE(f.panel.walkingSecondsEdit()->value(), kDefaultPassengerWalkingSeconds);
	QCOMPARE(f.panel.walkingSecondsEdit()->maximum(), kMaxPassengerWalkingSeconds);
	const quint64 revision = f.document.revision();
	f.panel.walkingSecondsEdit()->setValue(45);
	QCOMPARE(f.document.model().settings.passengerWalkingSecondsPerPlatform, 45);
	QCOMPARE(f.document.revision(), revision + 1);
	typeAndLeave(f.panel.nameEdit(), "Renamed");
	QCOMPARE(f.document.model().settings.passengerWalkingSecondsPerPlatform, 45);
}

// The late threshold is shown with its default, edited as one Undo step and kept when another field is edited.
void TestCaseSettingsPanel::editsPassengerLateThreshold() {
	SceneModel scene = caseScene("Case L", "08:00:00");
	CaseFixture f(scene);
	QCOMPARE(f.panel.lateThresholdEdit()->value(), kDefaultPassengerLateThresholdSeconds);
	QCOMPARE(f.panel.lateThresholdEdit()->maximum(), kMaxPassengerLateThresholdSeconds);
	const quint64 revision = f.document.revision();
	f.panel.lateThresholdEdit()->setValue(600);
	QCOMPARE(f.document.model().settings.passengerLateThresholdSeconds, 600);
	QCOMPARE(f.document.revision(), revision + 1);
	typeAndLeave(f.panel.nameEdit(), "Renamed");
	QCOMPARE(f.document.model().settings.passengerLateThresholdSeconds, 600);
}

// A case with the services F1, F2 and F3.
static SceneModel couplingScene() {
	SceneModel scene = caseScene("Case V", "08:00:00");
	for (const char* id : {"F1", "F2", "F3"}) {
		SceneService service;
		service.id = id;
		scene.services.push_back(service);
	}
	return scene;
}

// The ids of the checked services of the list.
static QStringList checkedServices(const QListWidget* list) {
	QStringList ids;
	for (int row = 0; row < list->count(); ++row) {
		if (list->item(row)->checkState() == Qt::Checked)
			ids << list->item(row)->text();
	}
	return ids;
}

// The three numbers of the group "Virtual coupling" show their defaults and their ranges, and each edit is one Undo step.
void TestCaseSettingsPanel::editsVirtualCouplingNumbers() {
	CaseFixture f(couplingScene());
	RunSelection selection;
	EditHistory history(f.document, selection, f.panel);
	const QGroupBox* group = f.panel.findChild<QGroupBox*>("caseVirtualCouplingGroup");
	if (group == nullptr)
		QFAIL("the panel has no group Virtual coupling");
	QCOMPARE(group->title(), "Virtual coupling");
	QVERIFY(group->isAncestorOf(f.panel.couplingDistanceEdit()));
	QVERIFY(group->isAncestorOf(f.panel.authorityMarginEdit()));
	QCOMPARE_EQ(f.panel.couplingDistanceEdit()->value(), kDefaultVirtualCouplingDistanceM);
	QCOMPARE_EQ(f.panel.couplingSpeedToleranceEdit()->value(), kDefaultVirtualCouplingSpeedToleranceMs);
	QCOMPARE_EQ(f.panel.authorityMarginEdit()->value(), kDefaultEndOfAuthorityMarginM);
	QCOMPARE_EQ(f.panel.couplingDistanceEdit()->minimum(), kMinVirtualCouplingDistanceM);
	QCOMPARE_EQ(f.panel.couplingSpeedToleranceEdit()->maximum(), kMaxVirtualCouplingSpeedToleranceMs);
	QCOMPARE_EQ(f.panel.authorityMarginEdit()->maximum(), kMaxEndOfAuthorityMarginM);

	const quint64 revision = f.document.revision();
	f.panel.couplingDistanceEdit()->setValue(60.0);
	QCOMPARE_EQ(f.document.model().settings.virtualCouplingDistanceM, 60.0);
	QCOMPARE(f.document.revision(), revision + 1);
	QCOMPARE(f.document.lastChangeLabel(), "Edit case settings");
	f.panel.couplingSpeedToleranceEdit()->setValue(0.5);
	f.panel.authorityMarginEdit()->setValue(40.0);
	QCOMPARE(f.document.revision(), revision + 3);
	// Another field keeps the numbers.
	typeAndLeave(f.panel.nameEdit(), "Renamed");
	QCOMPARE_EQ(f.document.model().settings.endOfAuthorityMarginM, 40.0);

	QVERIFY(history.undo());
	QVERIFY(history.undo());
	// Undo takes back one number at a time and shows the restored value.
	QCOMPARE_EQ(f.document.model().settings.endOfAuthorityMarginM, kDefaultEndOfAuthorityMarginM);
	QCOMPARE_EQ(f.panel.authorityMarginEdit()->value(), kDefaultEndOfAuthorityMarginM);
	QCOMPARE_EQ(f.document.model().settings.virtualCouplingSpeedToleranceMs, 0.5);
	QVERIFY(history.undo());
	QVERIFY(history.undo());
	QCOMPARE_EQ(f.document.model().settings.virtualCouplingDistanceM, kDefaultVirtualCouplingDistanceM);
	QCOMPARE_EQ(f.panel.couplingDistanceEdit()->value(), kDefaultVirtualCouplingDistanceM);
}

// The list shows the services of the case, a check is one Undo step, and with none checked every train may couple.
void TestCaseSettingsPanel::editsVirtualCouplingServices() {
	SceneModel scene = couplingScene();
	// A listed id that names no service of the case is shown checked after the services, so that it can be unchecked.
	scene.settings.virtualCouplingServices = {"F9"};
	CaseFixture f(scene);
	RunSelection selection;
	EditHistory history(f.document, selection, f.panel);
	QListWidget* list = f.panel.couplingServiceList();
	QCOMPARE(list->count(), 4);
	QCOMPARE(list->item(3)->text(), "F9");
	QCOMPARE(checkedServices(list), QStringList({"F9"}));
	const QString only = f.panel.couplingServicesNote()->text();

	const quint64 revision = f.document.revision();
	list->item(3)->setCheckState(Qt::Unchecked);
	QVERIFY(f.document.model().settings.virtualCouplingServices.empty());
	QCOMPARE(f.document.revision(), revision + 1);
	QVERIFY2(f.panel.couplingServicesNote()->text() != only, "the note says that every train may couple");
	list->item(1)->setCheckState(Qt::Checked);
	list->item(0)->setCheckState(Qt::Checked);
	QCOMPARE(f.document.revision(), revision + 3);
	QCOMPARE(f.document.lastChangeLabel(), "Edit case settings");
	QVERIFY(f.document.model().settings.virtualCouplingServices == std::vector<std::string>({"F1", "F2"}));
	QCOMPARE(f.panel.couplingServicesNote()->text(), only);

	// Leaving another field unchanged, or listing the same services in another order, edits nothing.
	QVERIFY(f.document.edit(SceneDocument::CaseSettings, "Reorder", [](SceneModel& edited) { edited.settings.virtualCouplingServices = {"F2", "F1"}; }));
	const quint64 reordered = f.document.revision();
	f.panel.commitPendingEditors();
	QCOMPARE(f.document.revision(), reordered);

	QVERIFY(history.undo());
	QVERIFY(history.undo());
	// Undo takes back the last check and shows the list as it was.
	QVERIFY(f.document.model().settings.virtualCouplingServices == std::vector<std::string>({"F2"}));
	QCOMPARE(checkedServices(list), QStringList({"F2"}));
	QCOMPARE(list->count(), 3);

	// A service that another panel adds joins the list.
	QVERIFY(f.document.edit(SceneDocument::Services, "Add service", [](SceneModel& edited) {
		SceneService added;
		added.id = "F4";
		edited.services.push_back(added);
	}));
	QCOMPARE(list->count(), 4);
	QCOMPARE(list->item(3)->text(), "F4");
	QCOMPARE(list->item(3)->checkState(), Qt::Unchecked);
}

void TestCaseSettingsPanel::namesFields() {
	CaseSettingsPanel panel;
	QStringList labels;
	for (const QLabel* label : panel.findChildren<QLabel*>())
		labels << label->text();
	// The panel has no note about when changes apply, and the duration is named with its unit.
	QVERIFY(!panel.findChild<QLabel*>("caseSettingsNote"));
	QVERIFY(labels.contains("Duration (s)"));
	QVERIFY(!labels.contains("Duration / horizon (s)"));
	QVERIFY2(panel.baseTimeEdit()->placeholderText() == "HH:MM:SS", "the base time field shows the format it expects");
}

void TestCaseSettingsPanel::showsNothingWithoutScene() {
	CaseSettingsPanel panel;
	// A panel without a document shows empty, disabled fields.
	QVERIFY(panel.nameEdit()->text().isEmpty());
	QVERIFY(!panel.nameEdit()->isEnabled());
	QVERIFY(!panel.baseTimeEdit()->isEnabled());
	QVERIFY(!panel.durationSecondsEdit()->isEnabled());
	SceneDocument document;
	panel.setDocument(&document);
	// A panel stays disabled while its document holds no scene.
	QVERIFY(!panel.nameEdit()->isEnabled());
	QVERIFY(!panel.recoveryPercentEdit()->isEnabled());
	typeAndLeave(panel.nameEdit(), "typed");
	// A field edited while the document holds no scene changes nothing.
	QVERIFY(document.model().name.empty());
	QCOMPARE(document.revision(), quint64(0));
	QVERIFY(!document.isDirty());
}

void TestCaseSettingsPanel::showsNewScene() {
	CaseFixture f(caseScene("Case A", "08:00:00"));
	CaseSettingsPanel& panel = f.panel;
	// A new scene is shown in the fields.
	QCOMPARE(panel.nameEdit()->text(), "Case A");
	QCOMPARE(panel.descriptionEdit()->text(), "About Case A");
	QCOMPARE(panel.baseTimeEdit()->text(), "08:00:00");
	QCOMPARE_EQ(panel.durationSecondsEdit()->value(), 3600.0);
	QCOMPARE_EQ(panel.bufferSecondsEdit()->value(), 0.0);
	QCOMPARE_EQ(panel.recoveryPercentEdit()->value(), 0.0);
	// The name and the description show the start of their text.
	QCOMPARE(panel.nameEdit()->cursorPosition(), 0);
	QCOMPARE(panel.descriptionEdit()->cursorPosition(), 0);
	// The fields are enabled once a scene is loaded.
	QVERIFY(panel.nameEdit()->isEnabled());
	QVERIFY(panel.baseTimeEdit()->isEnabled());
	QVERIFY(panel.durationSecondsEdit()->isEnabled());
	QVERIFY(panel.recoveryPercentEdit()->isEnabled());
	// Showing a scene edits nothing.
	QVERIFY(f.events.log.empty());
	QVERIFY(!f.document.isDirty());
	QCOMPARE(f.document.revision(), quint64(1));
}

void TestCaseSettingsPanel::leavingUnchangedFieldEditsNothing() {
	CaseFixture f(caseScene("Case A", "08:00:00"));
	emit f.panel.nameEdit()->editingFinished();
	f.panel.commitPendingEditors();
	// Leaving a field without changing it does not touch the document.
	QVERIFY(f.events.log.empty());
	QCOMPARE(f.document.revision(), quint64(1));
	QVERIFY(!f.document.isDirty());
}

// The edits of the fields, then the pending edits, which see the duration that the edits left, and a refresh, which
// sees the name.
void TestCaseSettingsPanel::editsFieldsAndDropsUnfinishedText() {
	CaseFixture f(caseScene("Case A", "08:00:00"));
	SceneDocument& document = f.document;
	CaseSettingsPanel& panel = f.panel;
	Events& events = f.events;

	typeAndLeave(panel.nameEdit(), "Renamed");
	// A finished name edit is written to the document and marks it changed.
	QCOMPARE(document.model().name, "Renamed");
	QVERIFY(document.isDirty());
	QCOMPARE(document.revision(), quint64(2));
	QCOMPARE(document.lastChangeLabel(), "Edit case settings");
	QVERIFY2(events.log == Log({"documentChanged", "panelChanged"}),
		"a name edit reports the document change and then the panel change, without a base time change");
	// The other fields keep their values.
	QCOMPARE(document.model().description, "About Case A");
	QCOMPARE(document.model().baseTime, "08:00:00");
	// A name edit leaves the optional settings as they were.
	QVERIFY(document.model().settings.hasDuration);
	QVERIFY(!document.model().settings.hasBufferTime);
	QVERIFY(!document.model().settings.hasRecoveryTime);

	// The base time.
	events.log.clear();
	typeAndLeave(panel.baseTimeEdit(), "09:30:00");
	// A base time edit is written to the document and reported with its text.
	QCOMPARE(document.model().baseTime, "09:30:00");
	QCOMPARE(events.baseTime, "09:30:00");
	QVERIFY2(events.log == Log({"baseTimeChanged", "documentChanged", "panelChanged"}),
		"the base time is reported before the document is marked changed");

	// The flags of the optional settings.
	events.log.clear();
	panel.bufferSecondsEdit()->setValue(45.0);
	// A buffer edit sets only the buffer flag.
	QVERIFY(document.model().settings.hasBufferTime);
	QCOMPARE_EQ(document.model().settings.bufferTimeSeconds, 45.0);
	QVERIFY(!document.model().settings.hasRecoveryTime);
	QVERIFY(document.model().settings.hasDuration);
	panel.durationSecondsEdit()->setValue(7200.0);
	// A duration edit is written to the document.
	QCOMPARE_EQ(document.model().settings.durationSeconds, 7200.0);
	QVERIFY(document.model().settings.hasDuration);
	panel.recoveryPercentEdit()->setValue(5.0);
	// A recovery edit is written to the document.
	QVERIFY(document.model().settings.hasRecoveryTime);
	QCOMPARE_EQ(document.model().settings.recoveryTimePercent, 5.0);
	QVERIFY2(events.log == Log({"documentChanged", "panelChanged", "documentChanged", "panelChanged", "documentChanged", "panelChanged"}),
		"each number edit is one document change");

	// Pending edits: a number field commits the text in it only while it has the focus.
	const quint64 revisionBeforePending = document.revision();
	events.log.clear();
	panel.show();
	panel.activateWindow();
	panel.descriptionEdit()->setText("typed, not finished");
	spinText(panel.durationSecondsEdit())->setText("90");
	QVERIFY(giveFocus(panel.bufferSecondsEdit()));
	spinText(panel.bufferSecondsEdit())->setText("60");
	// Text that is still being typed is not in the document.
	QCOMPARE(document.model().description, "About Case A");
	QCOMPARE(document.revision(), revisionBeforePending);
	panel.commitPendingEditors();
	// Committing the pending editors writes the text of text fields and of the number field with the focus.
	QCOMPARE(document.model().description, "typed, not finished");
	QCOMPARE_EQ(document.model().settings.bufferTimeSeconds, 60.0);
	QVERIFY2(document.model().settings.durationSeconds == 7200.0,
		"committing the pending editors does not take a number in a field without the focus");
	// The pending values of all fields are one change.
	QCOMPARE(document.revision(), revisionBeforePending + 1);
	QCOMPARE(events.log, Log({"documentChanged", "panelChanged"}));
	panel.hide();

	// Cancel: text that was typed and not finished is dropped when the fields are refreshed.
	document.markSaved();
	const quint64 revisionBeforeCancel = document.revision();
	events.log.clear();
	panel.nameEdit()->setText("abandoned");
	panel.refresh();
	// Refreshing drops unfinished text and leaves the document as it is.
	QCOMPARE(panel.nameEdit()->text(), "Renamed");
	QVERIFY(!document.isDirty());
	QCOMPARE(document.revision(), revisionBeforeCancel);
	QVERIFY(events.log.empty());
	panel.nameEdit()->setText("abandoned");
	document.replace(caseScene("Case B", "10:00:00"), false);
	// Opening another scene replaces unfinished text without an edit.
	QCOMPARE(panel.nameEdit()->text(), "Case B");
	QCOMPARE(panel.baseTimeEdit()->text(), "10:00:00");
	QCOMPARE_EQ(panel.durationSecondsEdit()->value(), 3600.0);
	QCOMPARE_EQ(panel.bufferSecondsEdit()->value(), 0.0);
	QVERIFY(!document.isDirty());
	QVERIFY(events.log.empty());
}

// A run does not close the case settings.
void TestCaseSettingsPanel::editableDuringRun() {
	CaseFixture f(caseScene("Case A", "08:00:00"));
	f.document.setEditable(false);
	// The fields stay enabled while the document is closed for edits.
	QVERIFY(f.panel.nameEdit()->isEnabled());
	QVERIFY(f.panel.durationSecondsEdit()->isEnabled());
	typeAndLeave(f.panel.nameEdit(), "Edited during a run");
	QVERIFY2(f.document.model().name == "Edited during a run", "the case settings can be edited during a run");
}

void TestCaseSettingsPanel::detachedPanelIgnoresDocument() {
	CaseFixture f(caseScene("Case A", "08:00:00"));
	f.panel.setDocument(nullptr);
	// A detached panel shows nothing.
	QVERIFY(f.panel.nameEdit()->text().isEmpty());
	QVERIFY(!f.panel.nameEdit()->isEnabled());
	const quint64 revisionDetached = f.document.revision();
	typeAndLeave(f.panel.nameEdit(), "ignored");
	f.document.replace(caseScene("Case C", "11:00:00"), false);
	// A detached panel neither edits nor follows the document.
	QCOMPARE(f.panel.nameEdit()->text(), "ignored");
	QCOMPARE(f.document.model().name, "Case C");
	QCOMPARE(f.document.revision(), revisionDetached + 1);
}

void TestCaseSettingsPanel::forgetsDeletedDocument() {
	auto* document = new SceneDocument;
	CaseSettingsPanel panel;
	panel.setDocument(document);
	document->replace(caseScene("Case D", "07:00:00"), false);
	delete document;
	panel.nameEdit()->setText("after the document");
	panel.commit();
	panel.commitPendingEditors();
	panel.refresh();
	// A panel whose document is gone shows nothing and edits nothing.
	QVERIFY(panel.nameEdit()->text().isEmpty());
	QVERIFY(!panel.nameEdit()->isEnabled());
}

// Tab selects the whole text of a field, which puts the cursor at its end. The main window watches each editor dock as
// below, so that a field shows the start of its text again once the focus has left it.
void TestCaseSettingsPanel::longDescriptionShowsStartAfterFocus() {
	SceneDocument document;
	SceneModel scene = caseScene("Case E", "08:00:00");
	scene.description = "A description that is much longer than the field, so that the field shows only a part of it";
	document.replace(scene, false);
	{
		CaseSettingsPanel panel;
		panel.setDocument(&document);
		FocusWheel::watch(&panel);
		// The panel is destroyed while shown, without a hide first: the checks after this block are about that.
		panel.show();
		panel.activateWindow();
		QApplication::processEvents();
		QLineEdit* description = panel.descriptionEdit();
		description->setFocus(Qt::TabFocusReason);
		QApplication::processEvents();
		// Tab into a long description puts the cursor at its end.
		QVERIFY(description->hasFocus());
		QCOMPARE(qsizetype(description->cursorPosition()), description->text().size());
		QCOMPARE_GT(description->fontMetrics().horizontalAdvance(description->text()), description->width());
		panel.baseTimeEdit()->setFocus(Qt::TabFocusReason);
		QApplication::processEvents();
		// A long description shows its start again once the focus has left it.
		QVERIFY(!description->hasFocus());
		QCOMPARE(description->cursorPosition(), 0);
		QCOMPARE(description->text(), QString::fromStdString(scene.description));
		panel.baseTimeEdit()->setText("09:00:00");
	}
	// A panel window that is closed with the focus in a field leaves the document as it is.
	QCOMPARE(document.model().baseTime, "08:00:00");
	QVERIFY(!document.isDirty());
	QCOMPARE(document.revision(), quint64(1));
}

// Qt's accessibility performs an Increment, such as VO+Up of VoiceOver, by adding the step that a number field reports
// to its value, and a tenth of the range when the field reports none: that took a Duration of 8000 s to 1.8e307 s.
void TestCaseSettingsPanel::accessibilityStepsBySingleStep() {
	SceneModel scene = caseScene("Case A", "08:00:00");
	scene.settings.durationSeconds = 8000.0;
	CaseFixture f(scene);
	CaseSettingsPanel& panel = f.panel;
	for (QDoubleSpinBox* field : {panel.durationSecondsEdit(), panel.bufferSecondsEdit(), panel.recoveryPercentEdit()}) {
		QAccessibleValueInterface* value = accessibleValue(field);
		if (value == nullptr)
			QFAIL("a number field has no accessible value");
		// Each number field reports its single step, 1, and an Increment adds it.
		QCOMPARE_EQ(value->minimumStepSize().toDouble(), 1.0);
		value->setCurrentValue(value->currentValue().toDouble() + value->minimumStepSize().toDouble());
	}
	// The Increments store a duration of 8001 s, a buffer of 1 s and a recovery of 1 %.
	const SceneSimulationSettings& settings = f.document.model().settings;
	QCOMPARE_EQ(settings.durationSeconds, 8001.0);
	QCOMPARE_EQ(settings.bufferTimeSeconds, 1.0);
	QCOMPARE_EQ(settings.recoveryTimePercent, 1.0);
}

// A value that the command line replaces is named above the fields, which keep the values of the case.
void TestCaseSettingsPanel::namesCommandLineOverrides() {
	CaseFixture f(caseScene("Case A", "08:00:00"));
	CaseSettingsPanel& panel = f.panel;
	f.panel.show();
	// Without an override no line shows.
	QVERIFY(!panel.overrideNotice()->isVisible());
	panel.setCommandLineOverrides(600.0, 30.0, 7.0);
	QVERIFY(panel.overrideNotice()->isVisible());
	QVERIFY(panel.overrideNotice()->text().contains("-h 600, -b 30 and -c 7 from the command line"));
	// The fields still show the case.
	QCOMPARE_EQ(panel.durationSecondsEdit()->value(), 3600.0);
	panel.setCommandLineOverrides(std::nullopt, 12.5, std::nullopt);
	QVERIFY(panel.overrideNotice()->text().contains("-b 12.5 from the command line"));
	QVERIFY(!panel.overrideNotice()->text().contains("-h"));
	QVERIFY(!panel.overrideNotice()->text().contains("-c"));
	panel.setCommandLineOverrides(600.0, std::nullopt, 7.0);
	QVERIFY(panel.overrideNotice()->text().contains("-h 600 and -c 7 from the command line"));
	panel.setCommandLineOverrides(std::nullopt, std::nullopt, std::nullopt);
	QVERIFY(!panel.overrideNotice()->isVisible());
	QVERIFY(panel.overrideNotice()->text().isEmpty());
}

QTEST_MAIN(TestCaseSettingsPanel)
#include "test_editor_casesettings.moc"
