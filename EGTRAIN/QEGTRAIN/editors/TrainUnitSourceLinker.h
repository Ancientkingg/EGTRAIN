#ifndef TRAINUNITSOURCELINKER_H
#define TRAINUNITSOURCELINKER_H

#include "editors/SceneDocument.h"

#include <QFileSystemWatcher>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

/// Links the parameter file and the traction file of a rolling stock unit to the files they came from, and takes a
/// changed file into the unit. The files are watched. A change waits until the file has been quiet for 150 ms, and
/// until no run uses the model. A value that was edited by hand and differs from the file is only replaced when the
/// user agrees. The links belong to the session, not to the scene: they are cleared when the document is replaced.
/// Units are named by their id.
class TrainUnitSourceLinker : public QObject {
	Q_OBJECT

public:
	/// The state of the links of one unit. `data` is the parameter file and `traction` the traction file.
	struct Link {
		QString dataPath;	  ///< The absolute path of the linked parameter file; empty when it is not linked.
		QString tractionPath; ///< The absolute path of the linked traction file; empty when it is not linked.
		/// The physical values of the unit when the parameter file was last taken or linked; empty while it is not linked.
		/// A hand edit is a difference from these.
		std::optional<SceneTrainPhysical> acceptedPhysical;
		/// The traction curve of the unit when the traction file was last taken or linked, in the units of
		/// SceneTrainUnit::tractionCurve.
		std::vector<std::array<double, 5>> acceptedTraction;
		/// What the parameter file held when it was last handled, as a digest of its content, or "<missing>" when it
		/// could not be read. Empty when the file is to be taken again whatever it holds.
		QString dataSignature;
		/// The same for the traction file.
		QString tractionSignature;
		/// The state of the parameter file link that the panel shows, such as "Reloaded" or "Up to date"; empty when
		/// the link has no status of its own.
		QString dataStatus;
		/// The same for the traction file link.
		QString tractionStatus;
		/// The parameter file changed while a run used the model, so the user is asked before it is taken.
		bool dataDeferred = false;
		/// The same for the traction file.
		bool tractionDeferred = false;
		/// Grows when the link is made or removed, so that an answer to a question about an older link is dropped.
		quint64 generation = 0;
	};

	/// A linker without a document and without links.
	explicit TrainUnitSourceLinker(QObject* parent = nullptr);

	/// The linker works on the model of this document and drops its links when the document is replaced.
	void setDocument(SceneDocument* document);
	/// The questions of the linker are put in front of this window.
	void setDialogParent(QWidget* parent) { m_dialogParent = parent; }

	/// The links of a unit, or null. The pointer is valid until the links change.
	const Link* linkOf(const std::string& unitId) const;
	/// The linked files that are watched.
	QStringList watchedFiles() const { return m_watcher.files(); }
	/// The folders of the linked files, which are watched as well.
	QStringList watchedDirectories() const { return m_watcher.directories(); }
	/// True while a change waits to be taken.
	bool hasPendingChanges() const { return !m_pendingPaths.isEmpty(); }

	/// Asks for a file for the parameters (`traction` false) or the traction of a unit and links it. The caller commits
	/// pending edits first, so that the unit is in the model as it is shown.
	void link(std::string unitId, bool traction);
	/// Links this file, which takes the current values of the unit as the ones that came from it.
	void linkFile(const std::string& unitId, bool traction, const QString& path);
	/// Removes the link of the parameter file (`traction` false) or the traction file of the unit.
	void unlink(const std::string& unitId, bool traction);
	/// Takes the linked file again, whether or not it has changed.
	void retry(const std::string& unitId, bool traction);
	/// The unit has another id. Its links go with it.
	void renameUnit(const std::string& oldId, const std::string& newId);
	/// The unit is gone.
	void forgetUnit(const std::string& unitId);
	/// Drops all links and stops watching.
	void clear();

	/// A watched path has changed: a linked file or the folder that holds it.
	void scheduleChange(const QString& changedPath);
	/// Takes the files that changed. Nothing happens while a run uses the model.
	void processChanges();
	/// Takes the values of this file into the units that are linked to it.
	void processFile(const QString& path, bool traction);
	/// Watches exactly the linked files and the folders that hold them.
	void refreshWatches();

Q_SIGNALS:
	/// The status of a link, or a link itself, has changed.
	void statusChanged();
	/// The links have been dropped.
	void linksCleared();
	/// A file has been handled. The traction curves of the units in `tractionUnitIds` have changed. Emitted after the
	/// document is marked changed.
	void reloaded(const QStringList& tractionUnitIds);
	/// Pending edits have to be taken into the model before the linker compares the model with a file.
	void commitPendingEditsRequested();
	/// A message for the status line, shown for `milliseconds`.
	void statusMessage(const QString& message, int milliseconds);

private:
	// MainWindowTestAccess sets up and reads the state in the editor smoke.
	friend class MainWindowTestAccess;

	bool loaded() const;
	// True while a run uses the model.
	bool runActive() const;

	QPointer<SceneDocument> m_document;
	QPointer<QWidget> m_dialogParent;
	QMetaObject::Connection m_replacedConnection;
	std::map<std::string, Link> m_links;
	QFileSystemWatcher m_watcher;
	QTimer m_debounceTimer;
	QSet<QString> m_pendingPaths;
	bool m_processing = false;
};

#endif // TRAINUNITSOURCELINKER_H
