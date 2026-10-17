#ifndef SCENESESSION_H
#define SCENESESSION_H

#include "scene/SceneDiagnostic.h"
#include "telemetry/TelemetryOperation.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class QDragEnterEvent;
class QDropEvent;
class QMainWindow;
class QMenu;
class SceneDocument;
struct SceneLoadResult;
struct SceneSaveResult;

// The message of the first error among the diagnostics, else of the first diagnostic, else an empty string.
QString firstDiagnosticMessage(const std::vector<SceneDiagnostic>& diagnostics);

// The scene that the window holds as a file or folder. The session opens, creates and saves scenes, asks what to do
// with unsaved changes, keeps the list of recent scenes, takes a scene file that is dropped on the window and imports
// legacy cases. It knows where the scene came from or was last saved, and the diagnostics of its last validation.
// What replacing or saving a scene changes in the rest of the window, such as the editors, the canvas and the undo
// history, is done by the window, which the session tells through its Host.
class SceneSession : public QObject {
	Q_OBJECT

public:
	// A recovered scene is the copy of another scene: it opens untitled and unsaved, and counts as no opened scene.
	enum class OpenOrigin {
		User,
		Recovered,
	};

	// A scene that the session has just read and put in the document.
	struct OpenedScene {
		// The absolute path of the file or folder that was read.
		QString path;
		// The scene was read from a bundle.
		bool bundle = false;
		bool recovered = false;
		// The window already held this scene, which is read again.
		bool reloadingSameScene = false;
		// Startup timing found the scene that it expects. True when startup timing is off.
		bool timingIdentityOk = true;
	};

	// What the session asks of the window, and the points at which it tells the window that the scene changed.
	class Host {
	public:
		virtual ~Host() = default;

		// The operation that the action which starts now belongs to, for telemetry.
		virtual telemetry::OperationObservation captureTelemetryOperation() const noexcept = 0;
		// A scripted run has no user to answer a dialog.
		virtual bool dialogsSuppressed() const = 0;
		// Takes the text of the fields that are being edited into the document, as if the user had left the fields.
		virtual void commitPendingEdits() = 0;
		// With a simulation running: asks it to stop, keeps `action` for the moment it has stopped and returns true. The
		// last action replaces an earlier one. Without a simulation it returns false and the caller acts at once.
		virtual bool afterRunStopped(std::function<void()> action) = 0;
		// Starts a check for a newer version at the request of the user.
		virtual void offerUpdateCheck() = 0;
		// What the actions of the window enable depends on the scene or on the recent scenes, and one of them changed.
		virtual void sceneActionsChanged() = 0;
		// The path of the file that a recovered scene is a copy of, or an empty string.
		virtual QString recoveredSceneSource() const = 0;
		// Reads the scene at `path`. `identityOk` tells whether startup timing, when it is on, found the scene that it
		// expects; it is true when startup timing is off.
		virtual SceneLoadResult readScene(const QString& path, bool& identityOk) = 0;

		// The scene in the document is about to be replaced by another. The run and what the canvas shows end.
		virtual void sceneWillBeReplaced() = 0;
		// The document holds a new scene that has no file. The session has no scene folder and no diagnostics.
		virtual void sceneCreated(const telemetry::OperationObservation& operation) = 0;
		// The document holds the scene that was read. Returns false when startup timing found that the scene changed,
		// which makes the open fail.
		virtual bool sceneOpened(const OpenedScene& opened) = 0;
		// The scene was written. The session holds its new location and the record of what was written.
		virtual void sceneSaved() = 0;
	};

	SceneSession(QMainWindow* window, SceneDocument* document, Host& host);

	// The folder or bundle that the scene came from or was last saved to. Empty for a scene that has none.
	const QString& sceneDir() const { return m_sceneDir; }
	bool sceneIsBundle() const { return m_sceneIsBundle; }
	// The layout version that the bundle on disk records.
	const std::optional<int>& sceneBundleVersion() const { return m_sceneBundleVersion; }
	// The record of the scene input as it was last read or written, which the provenance of a run keeps.
	const std::string& savedSceneSha256() const { return m_savedSceneSha256; }
	// The messages of the last validation of the scene.
	const std::vector<SceneDiagnostic>& diagnostics() const { return m_sceneDiagnostics; }
	void setDiagnostics(std::vector<SceneDiagnostic> diagnostics) { m_sceneDiagnostics = std::move(diagnostics); }
	// The vector itself, which the validation panel writes to when it validates.
	std::vector<SceneDiagnostic>* mutableDiagnostics() { return &m_sceneDiagnostics; }

	// The Recent Scenes menu, which the window puts in its File menu.
	QMenu* recentScenesMenu() const;
	// The paths of the recent scenes, newest first.
	static QStringList recentScenes();
	void addRecentScene(const QString& path);
	void rebuildRecentScenesMenu();
	// Marks the entries of the menu whose scene is gone as unavailable, and the others as available.
	void refreshRecentSceneAvailability();

	// Asks about unsaved changes, then opens the scene at `path`, after a running simulation has stopped. Returns
	// whether the request was accepted; the open can still fail later.
	bool requestOpenScene(const QString& path);
	bool requestOpenScene(const QString& path, const telemetry::OperationObservation& operation);
	// Opens the scene folder or bundle at `dir` now. The scene stays as it was when this returns false.
	bool openSceneDirectory(const QString& dir);
	bool openSceneDirectory(const QString& dir, const telemetry::OperationObservation& operation,
		OpenOrigin origin = OpenOrigin::User);
	void openSceneDialog();
	void openSceneFolderDialog();
	// Asks about unsaved changes, then replaces the scene with a new one.
	void newScene();
	// Imports a legacy case, which the user chooses, as a scene and opens it.
	void actionLoad_Network();
	// The text of the box that reports an import: the counts of the diagnostics by severity, then the first four
	// lines, the most serious first, then the number of lines left out.
	static QString legacyImportSummary(const std::vector<SceneDiagnostic>& diagnostics);
	void importLegacyCase(const QString& sourceDir, const QString& destinationDir,
		const telemetry::OperationObservation& operation);

	void saveScene();
	void saveSceneAs();
	bool saveSceneToCurrentDir();
	bool saveSceneAsToDirectory();
	// Asks whether to save unsaved changes. Returns false when the user cancels.
	bool maybeSaveScene();

	void updateSceneWindowTitle();

	// A drag of one scene file is accepted, and the drop opens it. Any other drag is refused with a message.
	void dragEnter(QDragEnterEvent* event);
	void drop(QDropEvent* event);

private:
	// The scripted runs in tests/e2e set the state of the session directly.
	friend class MainWindowTestAccess;

	void createNewScene(const telemetry::OperationObservation& operation);
	bool openRequestedScene(const QString& path, const telemetry::OperationObservation& operation);
	bool finishSceneSave(const SceneSaveResult& result, const telemetry::OperationObservation& operation,
		std::optional<telemetry::ExportKind> kind);
	bool saveSceneAsToBundle();
	bool saveSceneAsToBundle(const telemetry::OperationObservation& operation);
	QString saveSceneAsStartPath() const;
	bool copyScenePassthroughFiles(const QString& targetDir, const telemetry::OperationObservation& operation);
	void showError(const QString& title, const QString& message, bool warningIcon = false);

	QMainWindow* m_window;
	SceneDocument* m_document;
	Host& m_host;
	QPointer<QMenu> m_recentScenesMenu;
	QString m_sceneDir;
	std::string m_savedSceneSha256;
	bool m_sceneIsBundle = false;
	std::optional<int> m_sceneBundleVersion;
	std::vector<SceneDiagnostic> m_sceneDiagnostics;
};

#endif // SCENESESSION_H
