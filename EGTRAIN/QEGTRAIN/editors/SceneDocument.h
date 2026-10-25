#ifndef SCENEDOCUMENT_H
#define SCENEDOCUMENT_H

#include <QFlags>
#include <QObject>
#include <QString>
#include <QtGlobal>

// SceneModel has a member named signals, which the Qt keyword hides.
#ifdef signals
#define EGTRAIN_RESTORE_SIGNALS_KEYWORD
#undef signals
#endif
#include "scene/SceneModel.h"
#ifdef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#define signals Q_SIGNALS
#undef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#endif

/// Copies `snapshot` over `live`, except for the parts of the scene that the application derives from the files on disk,
/// which an edit does not author: the source files and the version that saved the scene. Those keep their values in `live`.
void assignAuthoredScene(SceneModel& live, const SceneModel& snapshot);

/// The scene that the editors work on: the model, whether it is loaded, edited since it was saved and open for edits, and
/// how often it has been changed. The document owns the model by value; the window creates the document as its child.
///
/// An editor changes the model in place through mutableModel() and then calls markChanged(), which counts the revision, marks
/// the document dirty and emits changed(). The window keeps the undo history: it records a copy of the model on every
/// changed(), and Undo and Redo put a recorded copy back with restore(), which emits no changed(). Opening or creating a
/// scene goes through replace(), which emits replaced().
class SceneDocument : public QObject {
	Q_OBJECT

public:
	/// The parts of the model that an edit can touch. markChanged() and changed() carry a combination of them.
	enum Change {
		CaseSettings = 1 << 0,	 ///< Name, description, base time, duration, buffer time and recovery time.
		Infrastructure = 1 << 1, ///< The infrastructure.
		TrainUnits = 1 << 2,	 ///< The rolling stock units.
		Compositions = 1 << 3,	 ///< The compositions.
		Services = 1 << 4,		 ///< The services and their timetables.
		Scenarios = 1 << 5,		 ///< The scenarios with their incidents and entrance delays.
		Passengers = 1 << 6,	 ///< The passengers with their journeys and legs.
		/// Every part.
		AllChanges = CaseSettings | Infrastructure | TrainUnits | Compositions | Services | Scenarios | Passengers
	};
	Q_DECLARE_FLAGS(ChangeKinds, Change)

	/// An empty document: not loaded, not dirty and editable.
	explicit SceneDocument(QObject* parent = nullptr);

	/// The scene. It is an empty model until the first replace().
	const SceneModel& model() const { return m_model; }
	/// The scene for an edit. The caller calls markChanged() after it has changed the model.
	SceneModel& mutableModel() { return m_model; }

	/// False until the first replace().
	bool isLoaded() const { return m_loaded; }
	/// True when the model has changed since it was opened or saved.
	bool isDirty() const { return m_dirty; }
	/// False while a run uses the model.
	bool isEditable() const { return m_editable; }
	/// Grows by one with every replace(), restore() and markChanged(), never otherwise.
	quint64 revision() const { return m_revision; }
	/// The label of the latest markChanged(), such as "Edit case settings"; empty before the first one.
	const QString& lastChangeLabel() const { return m_lastChangeLabel; }

	/// Takes another model: a scene that is opened or created. `dirty` tells whether it differs from what is saved.
	void replace(SceneModel newModel, bool dirty);
	/// Puts back an earlier state of the scene, as Undo and Redo do. The authored parts of the model become those of
	/// `snapshot` (see assignAuthoredScene), and `dirty` tells whether that state differs from what is saved. It is not an
	/// edit: changed() is not emitted, so the editors have to be refreshed by the caller.
	void restore(const SceneModel& snapshot, bool dirty);
	/// Records that an editor has changed `kinds` of the model, and what the change was called.
	void markChanged(ChangeKinds kinds, const QString& label = {});
	/// The model is what is saved now: clears the dirty state.
	void markSaved() { setDirty(false); }
	/// Sets the dirty state, and emits dirtyChanged() when it changes.
	void setDirty(bool dirty);
	/// Opens or closes the model for edits, and emits editableChanged() when that changes.
	void setEditable(bool editable);

Q_SIGNALS:
	/// Emitted once for each markChanged(), after the revision and the dirty state are updated.
	void changed(SceneDocument::ChangeKinds kinds);
	/// Emitted by replace(), after the whole state is set.
	void replaced();
	/// The dirty state has changed to `dirty`.
	void dirtyChanged(bool dirty);
	/// The model has been opened (`editable` true) or closed for edits.
	void editableChanged(bool editable);

private:
	// The scripted runs in tests/e2e set the revision directly.
	friend class MainWindowTestAccess;

	SceneModel m_model;
	bool m_loaded = false;
	bool m_dirty = false;
	bool m_editable = true;
	quint64 m_revision = 0;
	QString m_lastChangeLabel;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(SceneDocument::ChangeKinds)

#endif // SCENEDOCUMENT_H
