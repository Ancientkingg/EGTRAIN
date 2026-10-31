#include "editors/SceneDocument.h"

#include <utility>

void assignAuthoredScene(SceneModel& live, const SceneModel& snapshot) {
	SceneModel next = snapshot;
	next.sourceFiles = std::move(live.sourceFiles);
	next.savedWithAppVersion = std::move(live.savedWithAppVersion);
	live = std::move(next);
}

SceneDocument::SceneDocument(QObject* parent) : QObject(parent) {}

void SceneDocument::replace(SceneModel newModel, bool dirty) {
	m_model = std::move(newModel);
	m_loaded = true;
	++m_revision;
	setDirty(dirty);
	emit replaced();
}

void SceneDocument::restore(const SceneModel& snapshot, bool dirty) {
	assignAuthoredScene(m_model, snapshot);
	++m_revision;
	setDirty(dirty);
}

void SceneDocument::markChanged(ChangeKinds kinds, const QString& label) {
	++m_revision;
	m_lastChangeLabel = label;
	setDirty(true);
	emit changed(kinds);
}

bool SceneDocument::edit(ChangeKinds kinds, const QString& label, const std::function<void(SceneModel&)>& apply) {
	if (!m_editable && !(kinds & OpenDuringRun))
		return false;
	apply(m_model);
	markChanged(kinds, label);
	return true;
}

void SceneDocument::setDirty(bool dirty) {
	if (m_dirty == dirty)
		return;
	m_dirty = dirty;
	emit dirtyChanged(m_dirty);
}

void SceneDocument::setEditable(bool editable) {
	if (m_editable == editable)
		return;
	m_editable = editable;
	emit editableChanged(m_editable);
}
