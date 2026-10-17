#include "editors/CaseSettingsPanel.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QString>

#include <iostream>
#include <string>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = true;

	{
		CaseSettingsPanel panel;
		const QLabel* note = panel.findChild<QLabel*>("caseSettingsNote");
		ok &= expect(note && note->text() == "Changes apply when editing finishes." && note->wordWrap()
				&& note->property("dialogHelp").toBool() && !note->property("dialogStatus").isValid()
				&& !note->isHidden(),
			"the note about when changes apply is wrapped help text");
		ok &= expect(panel.baseTimeEdit()->placeholderText() == "HH:MM:SS", "the base time field shows the format it expects");
		ok &= expect(panel.nameEdit()->text().isEmpty() && !panel.nameEdit()->isEnabled()
				&& !panel.baseTimeEdit()->isEnabled() && !panel.durationSecondsEdit()->isEnabled(),
			"a panel without a document shows empty, disabled fields");
		SceneDocument document;
		panel.setDocument(&document);
		ok &= expect(!panel.nameEdit()->isEnabled() && !panel.recoveryPercentEdit()->isEnabled(),
			"a panel stays disabled while its document holds no scene");
		typeAndLeave(panel.nameEdit(), "typed");
		ok &= expect(document.model().name.empty() && document.revision() == 0 && !document.isDirty(),
			"a field edited while the document holds no scene changes nothing");
	}

	{
		SceneDocument document;
		CaseSettingsPanel panel;
		panel.setDocument(&document);
		Events events(panel, document);

		// refresh
		document.replace(caseScene("Case A", "08:00:00"), false);
		ok &= expect(panel.nameEdit()->text() == "Case A" && panel.descriptionEdit()->text() == "About Case A"
				&& panel.baseTimeEdit()->text() == "08:00:00" && panel.durationSecondsEdit()->value() == 3600.0
				&& panel.bufferSecondsEdit()->value() == 0.0 && panel.recoveryPercentEdit()->value() == 0.0,
			"a new scene is shown in the fields");
		ok &= expect(panel.nameEdit()->isEnabled() && panel.baseTimeEdit()->isEnabled()
				&& panel.durationSecondsEdit()->isEnabled() && panel.recoveryPercentEdit()->isEnabled(),
			"the fields are enabled once a scene is loaded");
		ok &= expect(events.log.empty() && !document.isDirty() && document.revision() == 1,
			"showing a scene edits nothing");

		// commit without a change
		emit panel.nameEdit()->editingFinished();
		panel.commitPendingEditors();
		ok &= expect(events.log.empty() && document.revision() == 1 && !document.isDirty(),
			"leaving a field without changing it does not touch the document");

		// edit
		typeAndLeave(panel.nameEdit(), "Renamed");
		ok &= expect(document.model().name == "Renamed" && document.isDirty() && document.revision() == 2
				&& document.lastChangeLabel() == "Edit case settings",
			"a finished name edit is written to the document and marks it changed");
		ok &= expect(events.log == Log({"documentChanged", "panelChanged"}),
			"a name edit reports the document change and then the panel change, without a base time change");
		ok &= expect(document.model().description == "About Case A" && document.model().baseTime == "08:00:00",
			"the other fields keep their values");
		ok &= expect(document.model().settings.hasDuration && !document.model().settings.hasBufferTime
				&& !document.model().settings.hasRecoveryTime,
			"a name edit leaves the optional settings as they were");

		// base time
		events.log.clear();
		typeAndLeave(panel.baseTimeEdit(), "09:30:00");
		ok &= expect(document.model().baseTime == "09:30:00" && events.baseTime == "09:30:00",
			"a base time edit is written to the document and reported with its text");
		ok &= expect(events.log == Log({"baseTimeChanged", "documentChanged", "panelChanged"}),
			"the base time is reported before the document is marked changed");

		// the flags of the optional settings
		events.log.clear();
		panel.bufferSecondsEdit()->setValue(45.0);
		ok &= expect(document.model().settings.hasBufferTime && document.model().settings.bufferTimeSeconds == 45.0
				&& !document.model().settings.hasRecoveryTime && document.model().settings.hasDuration,
			"a buffer edit sets only the buffer flag");
		panel.durationSecondsEdit()->setValue(7200.0);
		ok &= expect(document.model().settings.durationSeconds == 7200.0 && document.model().settings.hasDuration,
			"a duration edit is written to the document");
		panel.recoveryPercentEdit()->setValue(5.0);
		ok &= expect(document.model().settings.hasRecoveryTime && document.model().settings.recoveryTimePercent == 5.0,
			"a recovery edit is written to the document");
		ok &= expect(events.log == Log({"documentChanged", "panelChanged", "documentChanged", "panelChanged", "documentChanged", "panelChanged"}),
			"each number edit is one document change");

		// pending edits
		const quint64 revisionBeforePending = document.revision();
		events.log.clear();
		panel.descriptionEdit()->setText("typed, not finished");
		panel.bufferSecondsEdit()->findChild<QLineEdit*>()->setText("60");
		ok &= expect(document.model().description == "About Case A" && document.revision() == revisionBeforePending,
			"text that is still being typed is not in the document");
		panel.commitPendingEditors();
		ok &= expect(document.model().description == "typed, not finished" && document.model().settings.bufferTimeSeconds == 60.0,
			"committing the pending editors writes the text of text fields and of number fields");
		ok &= expect(document.revision() == revisionBeforePending + 1 && events.log == Log({"documentChanged", "panelChanged"}),
			"the pending values of all fields are one change");

		// cancel: text that was typed and not finished is dropped when the fields are refreshed
		document.markSaved();
		const quint64 revisionBeforeCancel = document.revision();
		events.log.clear();
		panel.nameEdit()->setText("abandoned");
		panel.refresh();
		ok &= expect(panel.nameEdit()->text() == "Renamed" && !document.isDirty()
				&& document.revision() == revisionBeforeCancel && events.log.empty(),
			"refreshing drops unfinished text and leaves the document as it is");
		panel.nameEdit()->setText("abandoned");
		document.replace(caseScene("Case B", "10:00:00"), false);
		ok &= expect(panel.nameEdit()->text() == "Case B" && panel.baseTimeEdit()->text() == "10:00:00"
				&& panel.durationSecondsEdit()->value() == 3600.0 && panel.bufferSecondsEdit()->value() == 0.0
				&& !document.isDirty() && events.log.empty(),
			"opening another scene replaces unfinished text without an edit");

		// a run does not close the case settings
		document.setEditable(false);
		ok &= expect(panel.nameEdit()->isEnabled() && panel.durationSecondsEdit()->isEnabled(),
			"the fields stay enabled while the document is closed for edits");
		typeAndLeave(panel.nameEdit(), "Edited during a run");
		ok &= expect(document.model().name == "Edited during a run", "the case settings can be edited during a run");
		document.setEditable(true);

		// detach
		panel.setDocument(nullptr);
		ok &= expect(panel.nameEdit()->text().isEmpty() && !panel.nameEdit()->isEnabled(), "a detached panel shows nothing");
		const quint64 revisionDetached = document.revision();
		typeAndLeave(panel.nameEdit(), "ignored");
		document.replace(caseScene("Case C", "11:00:00"), false);
		ok &= expect(panel.nameEdit()->text() == "ignored" && document.model().name == "Case C"
				&& document.revision() == revisionDetached + 1,
			"a detached panel neither edits nor follows the document");
	}

	{
		auto* document = new SceneDocument;
		CaseSettingsPanel panel;
		panel.setDocument(document);
		document->replace(caseScene("Case D", "07:00:00"), false);
		delete document;
		panel.nameEdit()->setText("after the document");
		panel.commit();
		panel.commitPendingEditors();
		panel.refresh();
		ok &= expect(panel.nameEdit()->text().isEmpty() && !panel.nameEdit()->isEnabled(),
			"a panel whose document is gone shows nothing and edits nothing");
	}

	if (!ok)
		return 1;
	std::cout << "all case settings panel tests passed\n";
	return 0;
}
