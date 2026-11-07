#pragma once

// After the scene document, which includes the scene model the way the scene history needs it.
#include "editors/SceneEditorPanel.h"
#include "editors/TrainUnitSourceLinker.h"
#include "scene/SceneHistory.h"

#include <QObject>

#include <memory>
#include <set>
#include <string>

/// The edit history of a document as the window keeps it (MainWindow::sceneDocumentChanged and stepSceneHistory), for the
/// tests of an editor panel: the run selection and the links of `linker`, when there is one, kept before each edit, a step
/// after each change of the document, and Undo and Redo, which put a step back, let the run selection and the links follow
/// it and show the result in the panel. Occurrences are counted over the duration of the scene.
class EditHistory {
public:
	/// A history that starts at the scene that `document` shows now.
	EditHistory(SceneDocument& document, RunSelection& selection, SceneEditorPanel& panel, TrainUnitSourceLinker* linker = nullptr)
		: m_document(document), m_selection(selection), m_panel(panel), m_linker(linker) {
		m_history.reset(document.model(), !document.isDirty());
		m_aboutToEdit = QObject::connect(&document, &SceneDocument::aboutToEdit, &panel, [this] { keepSessionState(); });
		m_changed = QObject::connect(&document, &SceneDocument::changed, &panel,
			[this] { m_history.record(m_document.model(), modifiedScenarioIds()); });
	}
	// The document may outlive the history, so the history stops listening when it goes.
	~EditHistory() {
		QObject::disconnect(m_aboutToEdit);
		QObject::disconnect(m_changed);
	}
	EditHistory(const EditHistory&) = delete;
	EditHistory& operator=(const EditHistory&) = delete;

	/// Puts back the state before the latest step; false when there is none.
	bool undo() { return step(false); }
	/// Puts back the state after the next step; false when there is none.
	bool redo() { return step(true); }

private:
	void keepSessionState() {
		m_history.keepRunSelection(m_selection.scenarioId(), m_selection.excluded());
		if (m_linker)
			m_history.keepTrainUnitSourceLinks(m_linker->links());
	}

	std::set<std::string> modifiedScenarioIds() const {
		std::set<std::string> ids;
		for (const SceneScenario& scenario : m_document.model().scenarios) {
			if (m_selection.isScenarioModified(scenario.id))
				ids.insert(scenario.id);
		}
		return ids;
	}

	bool step(bool redo) {
		if (!(redo ? m_history.canRedo() : m_history.canUndo()))
			return false;
		keepSessionState();
		const std::shared_ptr<const SceneModel> previous = m_history.head();
		const SceneHistoryEntry* entry = redo ? m_history.redo() : m_history.undo();
		if (entry == nullptr)
			return false;
		m_document.restore(*entry->scene, !m_history.isAtSavedPoint());
		m_selection.historyRestored(*previous, *entry, m_document.model().settings.durationSeconds);
		if (m_linker)
			m_linker->historyRestored(*previous, *entry);
		m_panel.refresh();
		return true;
	}

	SceneDocument& m_document;
	RunSelection& m_selection;
	SceneEditorPanel& m_panel;
	TrainUnitSourceLinker* m_linker;
	SceneHistory m_history;
	QMetaObject::Connection m_aboutToEdit;
	QMetaObject::Connection m_changed;
};
