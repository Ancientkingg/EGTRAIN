#pragma once

// The scene model has a member named "signals", so this header includes no Qt header that
// defines that macro. Include scene/SceneModel.h before any Qt header that includes QObject.
#include <QDateTime>
#include <QString>
#include <QVector>
#include <memory>

struct SceneModel;
class QLockFile;

/// Where a scene came from. A recovery copy never changes the file or folder named here.
enum class RecoverySourceKind {
	Untitled,  ///< A new scene that was never saved.
	Bundle,	   ///< A scene bundle file.
	Directory, ///< A scene directory with a scene.json.
};

/// The source of a scene at the time its copy is written. A directory source is judged by its
/// scene.json alone.
struct RecoverySource {
	RecoverySourceKind kind = RecoverySourceKind::Untitled; ///< Kind of file or folder that path names.
	QString path;											///< Absolute path the scene was loaded from or last saved to; empty for Untitled.
	QString displayName;									///< Name of the scene, shown to the user; empty shows as an untitled case.
	qint64 sizeBytes = -1;									///< Size in bytes of the bundle file, or of scene.json in a directory; -1 when unknown.
	QDateTime modified;										///< Modification time (UTC) of the same file; invalid when unknown.
};

/// How the source compares with the copy.
enum class RecoverySourceStatus {
	NotApplicable, ///< Untitled scene, or no path recorded.
	Unknown,	   ///< The recorded size or time is missing.
	Unchanged,	   ///< Size and modification time equal the recorded values.
	Changed,	   ///< Size or modification time differ from the recorded values.
	NewerThanCopy, ///< Modified after the copy was written.
	Missing,	   ///< The bundle file, or scene.json of the directory, is not there.
};

/// Whether this version can open a recovery copy.
enum class RecoveryCandidateState {
	Openable,			///< The bundle is of the current scene version.
	NewerRecordVersion, ///< The record has a layout this version does not know. Nothing is changed.
	OtherSceneVersion,	///< The bundle is valid but not of the current scene version.
};

/// A recovery copy that an application left behind.
struct RecoveryCandidate {
	/// Id of the application that wrote the copy, which is also the name of its folder.
	QString instanceId;
	RecoveryCandidateState state = RecoveryCandidateState::Openable; ///< Whether this version can open the copy.
	/// The scene bundle of the copy. Empty only for NewerRecordVersion when no readable bundle is found.
	QString bundlePath;
	RecoverySource source; ///< The source as recorded; Untitled when the record is missing or not usable.
	/// Time (UTC) the copy was written. Without a usable record it is the modification time of the bundle, or of the
	/// record for NewerRecordVersion.
	QDateTime writtenAt;
	RecoverySourceStatus sourceStatus = RecoverySourceStatus::NotApplicable; ///< The source compared with the copy at scan time.
};

/// The outcome of a store operation.
struct RecoveryResult {
	bool ok = false; ///< True when the operation succeeded.
	/// Empty when ok. One sentence for the user, without a path, a diagnostic code or scene content.
	/// A copy that cannot be written because an entry of the case is incomplete says which entry.
	QString error;
	QString bundlePath; ///< write and adopt: the bundle that now holds the copy. Empty otherwise.
};

/// The result of RecoveryStore::scan.
struct RecoveryScan {
	QVector<RecoveryCandidate> candidates; ///< Newest first.
	int removedOverCap = 0;				   ///< Candidates removed because more than kMaxKeptInstances were left.
};

/// Keeps recovery copies of a scene below a root folder that the caller names. Each running
/// application (an instance) owns one folder below the root, named by its instance id. It holds a
/// lock on it while the store is open and keeps at most one copy of the scene in it, as an ordinary
/// scene bundle. A clean close removes the folder. A folder whose owner is gone is a candidate for
/// recovery.
///
/// The store writes only below its root, apart from the private copy that saveSceneBundle makes
/// in the system temporary directory while it runs. It never touches the file or folder a scene
/// came from. No function throws, prints or logs, and no error text holds a path.
///
/// A store is used by one thread at a time and has no static state.
class RecoveryStore {
public:
	/// At most this many candidates are kept; scan removes the oldest beyond it.
	static constexpr int kMaxKeptInstances = 20;

	/// root is the folder that holds the instance folders. Nothing is created until open().
	explicit RecoveryStore(const QString& root);
	/// Releases the lock and deletes nothing, so a missing close() never destroys a copy.
	~RecoveryStore();
	RecoveryStore(const RecoveryStore&) = delete;
	RecoveryStore& operator=(const RecoveryStore&) = delete;

	/// Creates the instance folder with a new random id and locks it. Ok when already open.
	RecoveryResult open();
	/// True while the store holds the lock of its instance folder.
	bool isOpen() const;
	/// The id of the instance folder; empty while the store is not open.
	QString instanceId() const;

	/// Writes the scene as a new generation next to the previous one, then records it and removes
	/// the previous one. A failed write leaves the previous copy.
	RecoveryResult write(const SceneModel& scene, const RecoverySource& source);
	/// Removes the copy and the record. The folder and the lock stay.
	RecoveryResult clear();
	/// Removes the instance folder and releases the lock. Ok when already closed.
	RecoveryResult close();

	/// Lists the copies that other applications left behind, newest first. A folder whose lock is
	/// held belongs to a running application and is skipped. The scan removes what is not usable:
	/// stray files of an interrupted write, older generations, folders without a complete bundle,
	/// and candidates beyond kMaxKeptInstances.
	RecoveryScan scan();
	/// Takes over the copy of an Openable candidate. It replaces the copy this store holds, as write
	/// does, and the candidate folder is removed. Fails when the candidate is in use.
	RecoveryResult adopt(const RecoveryCandidate& candidate);
	/// Removes the folder of a candidate in any state. Fails when the candidate is in use.
	RecoveryResult discard(const RecoveryCandidate& candidate);

private:
	QString m_root;
	QString m_id;
	std::unique_ptr<QLockFile> m_lock;
	int m_generation = 0; // Number of the newest generation this store wrote.
};

/// Compares the source with the values recorded when the copy was written. writtenAt is the time (UTC)
/// of the copy; when it is invalid, NewerThanCopy is never returned. Reads metadata only.
RecoverySourceStatus recoverySourceStatus(const RecoverySource& source, const QDateTime& writtenAt);
