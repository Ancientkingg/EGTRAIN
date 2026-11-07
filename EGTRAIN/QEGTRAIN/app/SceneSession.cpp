#include "app/SceneSession.h"
// This header brings in SceneModel.h in the way that the member named signals needs, before the other scene headers do.
#include "editors/CountText.h"
#include "editors/EditorHelpers.h"
#include "editors/SceneDocument.h"
#include "app/CannotOpenMessage.h"
#include "app/DialogFolders.h"
#include "app/SceneSavePolicy.h"
#include "app/UpdateTelemetryController.h"
#include "diagrams/RunResults.h"
#include "scene/SceneBundle.h"
#include "scene/SceneCompatibility.h"
#include "scene/SceneImporter.h"
#include "scene/SceneMigration.h"
#include "scene/SceneValidator.h"
#include "scene/SceneWriter.h"
#include "widgets/CaseChooserDialog.h"
#include "widgets/DisplayPath.h"
#include "widgets/SaveSuffix.h"
#include "widgets/SettingsDialog.h"
#include "widgets/UserPrompts.h"
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QStatusBar>
#include <QUrl>
#include <algorithm>
#include <exception>
#include <iterator>

namespace {
const int kMaxRecentScenes = 8;
// The property of a recent scene action that holds its label without the Unavailable prefix.
const char* kRecentLabelProperty = "recentLabel";

bool sceneDropCandidate(const QMimeData* mime, QString* path, QString* rejection) {
	if (!mime || !mime->hasUrls() || mime->urls().isEmpty()) {
		*rejection = QStringLiteral("the drop holds no file");
		return false;
	}
	if (mime->urls().size() != 1) {
		*rejection = QStringLiteral("more than one file was dropped");
		return false;
	}
	const QUrl url = mime->urls().front();
	if (!url.isLocalFile()) {
		*rejection = QStringLiteral("it is not on this computer");
		return false;
	}
	const QFileInfo info(url.toLocalFile());
	if (!info.exists()) {
		*rejection = QStringLiteral("it does not exist");
		return false;
	}
	if (!info.isFile()) {
		*rejection = QStringLiteral("it is a folder, not an .egscene file");
		return false;
	}
	if (info.suffix().compare(QStringLiteral("egscene"), Qt::CaseInsensitive) != 0) {
		*rejection = QStringLiteral("it is not an .egscene file");
		return false;
	}
	*path = info.absoluteFilePath();
	return true;
}

// Whether the recent scene at `path` can still be opened: a bundle file, or a folder that holds a scene.json.
bool recentSceneExists(const QString& path) {
	const QFileInfo info(path);
	return info.isFile()
		? info.exists() && info.suffix().compare("egscene", Qt::CaseInsensitive) == 0
		: QFileInfo(QDir(path).filePath("scene.json")).exists();
}

QString unavailableLabel(const QString& label) {
	return QString("Unavailable: %1").arg(label);
}

int errorDiagnosticCount(const std::vector<SceneDiagnostic>& diagnostics) {
	int count = 0;
	for (const auto& d : diagnostics) {
		if (d.severity == SceneSeverity::Error)
			++count;
	}
	return count;
}

// Whether the folder `path` holds anything that an import, which replaces the folder with everything in it, would
// delete. Hidden files count. A .DS_Store, which Finder leaves in a folder that it has shown, holds no work.
bool holdsFiles(const QString& path) {
	const QStringList entries = QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
	return std::any_of(entries.begin(), entries.end(), [](const QString& entry) { return entry != QLatin1String(".DS_Store"); });
}

// The canonical form of `path`, or its clean absolute form when it does not exist.
QString resolvedFolder(const QString& path) {
	const QString canonical = QFileInfo(path).canonicalFilePath();
	return canonical.isEmpty() ? QDir::cleanPath(QFileInfo(path).absoluteFilePath()) : canonical;
}

} // namespace

QString firstDiagnosticMessage(const std::vector<SceneDiagnostic>& diagnostics) {
	const SceneDiagnostic* diagnostic = firstDiagnostic(diagnostics);
	return diagnostic ? QString::fromStdString(diagnostic->message) : QString();
}

SceneSession::SceneSession(QMainWindow* window, SceneDocument* document, Host& host)
	: QObject(window), m_window(window), m_document(document), m_host(host),
	  m_recentScenesMenu(new QMenu("Recent Cases", window)), m_ownPrompts([this] { return m_host.dialogsSuppressed(); }) {
	// A case can be deleted while the menu is closed, so the entries are checked again as the menu opens.
	connect(m_recentScenesMenu, &QMenu::aboutToShow, this, &SceneSession::refreshRecentSceneAvailability);
}

void SceneSession::showError(const QString& title, const QString& message, bool warningIcon) {
	m_prompts->showError(m_window, title, message, warningIcon);
}

std::vector<SceneSession::DiskFile> SceneSession::diskFiles(const QString& path, bool bundle) {
	const auto file = [](const QString& name, const QString& filePath) {
		const QFileInfo info(filePath);
		DiskFile disk;
		disk.name = name;
		disk.exists = info.exists();
		if (disk.exists) {
			disk.size = info.size();
			disk.modifiedMs = info.lastModified().toMSecsSinceEpoch();
		}
		return disk;
	};
	std::vector<DiskFile> files;
	if (bundle) {
		files.push_back(file(QFileInfo(path).fileName(), path));
		return files;
	}
	const QDir folder(path);
	for (const SceneFile& sceneFile : kSceneFiles)
		files.push_back(file(QString::fromLatin1(sceneFile.name), folder.filePath(QString::fromLatin1(sceneFile.name))));
	return files;
}

void SceneSession::dragEnter(QDragEnterEvent* event) {
	QString path;
	QString rejection;
	if (sceneDropCandidate(event->mimeData(), &path, &rejection)
		&& event->possibleActions().testFlag(Qt::CopyAction)) {
		event->setDropAction(Qt::CopyAction);
		event->accept();
	} else {
		if (rejection.isEmpty())
			rejection = QStringLiteral("the source does not allow copying");
		m_window->statusBar()->showMessage(QStringLiteral("Cannot open the dropped file: %1").arg(rejection), 8000);
		event->ignore();
	}
}

void SceneSession::drop(QDropEvent* event) {
	QString path;
	QString rejection;
	if (!sceneDropCandidate(event->mimeData(), &path, &rejection)
		|| !event->possibleActions().testFlag(Qt::CopyAction)) {
		if (rejection.isEmpty())
			rejection = QStringLiteral("the source does not allow copying");
		m_window->statusBar()->showMessage(QStringLiteral("Cannot open the dropped file: %1").arg(rejection), 8000);
		event->ignore();
		return;
	}

	// The question about unsaved changes, or any other that the open asks, must not open behind the file manager.
	m_host.bringToFront();
	if (!requestOpenScene(path)) {
		event->ignore();
		return;
	}
	event->setDropAction(Qt::CopyAction);
	event->accept();
}

void SceneSession::newScene() {
	const auto operation = m_host.captureTelemetryOperation();
	if (!maybeSaveScene())
		return;
	if (m_host.afterRunStopped([this, operation] { createNewScene(operation); }))
		return;
	createNewScene(operation);
}

void SceneSession::createNewScene(const telemetry::OperationObservation& operation) {
	// The window ends the run and the canvas of the scene that goes before the document replaces it.
	emit sceneWillBeReplaced();
	m_sceneDir.clear();
	m_savedFiles.clear();
	m_savedSceneSha256.clear();
	m_document->replace(makeNewSceneModel(), true);
	m_sceneIsBundle = false;
	m_sceneBundleVersion.reset();
	m_sceneDiagnostics.clear();
	emit sceneCreated(operation);
}

void SceneSession::openSceneDialog() {
	const auto operation = m_host.captureTelemetryOperation();
	const QString startDir = dialog_folders::openStartFolder(m_sceneDir);
	const QString path = QFileDialog::getOpenFileName(m_window, "Open Case", startDir,
		"EGTRAIN Case (*.egscene)");
	if (path.isEmpty())
		return;

	requestOpenScene(path, operation);
}

void SceneSession::openSceneFolderDialog() {
	const auto operation = m_host.captureTelemetryOperation();
	const QString startDir = dialog_folders::openStartFolder(m_sceneDir);
	const QString dir = QFileDialog::getExistingDirectory(m_window, "Open Case Folder", startDir);
	if (!dir.isEmpty())
		requestOpenScene(dir, operation);
}

bool SceneSession::requestOpenScene(const QString& path) {
	return requestOpenScene(path, m_host.captureTelemetryOperation());
}

bool SceneSession::requestOpenScene(const QString& path, const telemetry::OperationObservation& operation) {
	if (path.isEmpty())
		return false;
	if (!maybeSaveScene())
		return false;
	// The request is accepted here. The scene opens when the running simulation has stopped.
	if (m_host.afterRunStopped([this, path, operation] { openRequestedScene(path, operation); }))
		return true;
	return openRequestedScene(path, operation);
}

bool SceneSession::openRequestedScene(const QString& path, const telemetry::OperationObservation& operation) {
	if (openSceneDirectory(path, operation))
		return true;
	m_window->statusBar()->showMessage(QString("Could not open %1").arg(QFileInfo(path).fileName()), 8000);
	return false;
}

bool SceneSession::openSceneDirectory(const QString& dir) {
	return openSceneDirectory(dir, m_host.captureTelemetryOperation());
}

bool SceneSession::openSceneDirectory(const QString& dir, const telemetry::OperationObservation& operation, OpenOrigin origin) {
	const QString scenePath = QFileInfo(dir).absoluteFilePath();
	const SceneCompatibilityProbeResult compatibility = probeSceneCompatibility(scenePath.toStdString());
	const auto compatibilityMessage = [&compatibility]() {
		if (!compatibility.diagnostics.empty())
			return QString::fromStdString(toDialogText(compatibility.diagnostics.front()));
		return QStringLiteral("The scene format could not be identified.");
	};
	// Scene compatibility prompts are suppressed only for unattended app/E2E
	// runs. The update-only disable setting must not hide scene safety prompts.
	const bool noCompatibilityDialogs = m_host.dialogsSuppressed();
	if (compatibility.classification == SceneCompatibilityClass::OlderMigratable) {
		if (noCompatibilityDialogs) {
			m_window->statusBar()->showMessage("Older case requires an interactive upgrade copy");
			return false;
		}
		QMessageBox dialog(m_window);
		dialog.setIcon(QMessageBox::Question);
		dialog.setWindowTitle("Older Case");
		dialog.setText(QString("%1 was saved by an older version of EGTRAIN.").arg(QFileInfo(scenePath).fileName()));
		dialog.setInformativeText("Upgrade a copy to open it. The original stays as it is.");
		QString versions = QString("Scene schema %1, this version uses %2")
							   .arg(compatibility.schemaVersion)
							   .arg(kCurrentSceneSchemaVersion);
		if (compatibility.sourceKind == SceneSourceKind::Bundle && compatibility.bundleVersion)
			versions += QString("\nBundle layout %1, this version uses %2")
							.arg(*compatibility.bundleVersion)
							.arg(kCurrentSceneBundleVersion);
		dialog.setDetailedText(versions);
		QPushButton* upgrade = dialog.addButton("Upgrade a Copy...", QMessageBox::AcceptRole);
		dialog.addButton("Cancel", QMessageBox::RejectRole);
		dialog.exec();
		if (dialog.clickedButton() != upgrade)
			return false;
		const QFileInfo sourceInfo(scenePath);
		QString destination;
		if (compatibility.sourceKind == SceneSourceKind::Bundle) {
			const QString base = sourceInfo.completeBaseName() + "-upgraded.egscene";
			destination = QFileDialog::getSaveFileName(m_window, "Upgrade Case Copy",
				QDir(sourceInfo.absolutePath()).filePath(base), "EGTRAIN Case (*.egscene)");
		} else {
			// A migration destination must not already exist. A save-style chooser
			// lets the user name a sibling directory without selecting an existing one.
			destination = QFileDialog::getSaveFileName(m_window, "Upgrade Case Copy",
				QDir(sourceInfo.absolutePath()).filePath(sourceInfo.fileName() + "-upgraded"),
				"EGTRAIN Case Folder (*)");
		}
		if (destination.isEmpty())
			return false;
		const SceneMigrationResult migrated = migrateSceneCopy(scenePath.toStdString(),
			destination.toStdString());
		if (!migrated.success()) {
			operation.failure(telemetry::Operation::SceneOpen, telemetryDiagnosticError(migrated.diagnostics));
			QString message = firstDiagnosticMessage(migrated.diagnostics);
			if (message.isEmpty())
				message = compatibilityMessage();
			showError("Cannot Upgrade Case", message);
			return false;
		}
		return openSceneDirectory(destination, operation);
	}
	if (compatibility.classification == SceneCompatibilityClass::OlderUnsupported) {
		operation.failure(telemetry::Operation::SceneOpen, telemetry::Error::UnsupportedFormat);
		if (!noCompatibilityDialogs)
			showError("Older Case Not Supported",
				"This case is too old for this version of EGTRAIN to upgrade.\n\n" + compatibilityMessage());
		return false;
	}
	if (compatibility.classification == SceneCompatibilityClass::Newer) {
		if (noCompatibilityDialogs) {
			operation.failure(telemetry::Operation::SceneOpen, telemetry::Error::UnsupportedFormat);
			m_window->statusBar()->showMessage("Newer scene format requires a newer EGTRAIN version");
			return false;
		}
		QMessageBox dialog(m_window);
		dialog.setIcon(QMessageBox::Question);
		dialog.setWindowTitle("Newer Case");
		dialog.setText("This case was saved by a newer version of EGTRAIN.");
		dialog.setInformativeText("Update EGTRAIN to open it.");
		QString versions = QString("Scene schema %1, this version uses %2")
							   .arg(compatibility.schemaVersion)
							   .arg(kCurrentSceneSchemaVersion);
		if (compatibility.bundleVersion)
			versions += QString("\nBundle layout %1, this version uses %2")
							.arg(*compatibility.bundleVersion)
							.arg(kCurrentSceneBundleVersion);
		dialog.setDetailedText(versions);
		QPushButton* updates = dialog.addButton("Check for Updates...", QMessageBox::AcceptRole);
		dialog.addButton("Cancel", QMessageBox::RejectRole);
		dialog.exec();
		if (dialog.clickedButton() == updates) {
			operation.failure(telemetry::Operation::SceneOpen, telemetry::Error::UnsupportedFormat);
			m_host.offerUpdateCheck();
		}
		return false;
	}
	if (compatibility.classification == SceneCompatibilityClass::Malformed) {
		operation.failure(telemetry::Operation::SceneOpen, telemetryDiagnosticError(compatibility.diagnostics));
		if (!noCompatibilityDialogs) {
			const QString message = diagnosticsDialogText(compatibility.diagnostics);
			showError(cannot_open::title(), cannot_open::text(scenePath, message.isEmpty() ? compatibilityMessage() : message));
		}
		return false;
	}
	const bool sceneIsBundle = compatibility.sourceKind == SceneSourceKind::Bundle;
	const bool recovered = origin == OpenOrigin::Recovered;
	const bool reloadingSameScene = !recovered && m_document->isLoaded()
		&& QFileInfo(m_sceneDir).absoluteFilePath() == scenePath;
	bool timingIdentityOk = true;
	// Taken before the read, so that a change during the read counts as a change since the open.
	std::vector<DiskFile> files = recovered ? std::vector<DiskFile>() : diskFiles(scenePath, sceneIsBundle);
	SceneLoadResult result;
	try {
		result = m_host.readScene(scenePath, timingIdentityOk);
	} catch (const std::exception& error) {
		SceneDiagnostic diagnostic;
		diagnostic.code = "scene.read.exception";
		diagnostic.message = std::string("Cannot read the case: ") + error.what();
		result.diagnostics.push_back(std::move(diagnostic));
	}
	if (errorDiagnosticCount(result.diagnostics) > 0) {
		operation.failure(telemetry::Operation::SceneOpen, telemetryDiagnosticError(result.diagnostics));
		showError(cannot_open::title(), cannot_open::text(scenePath, diagnosticsDialogText(result.diagnostics)));
		return false;
	}

	// As in createNewScene(), the window has ended the run of the scene that goes when this returns.
	emit sceneWillBeReplaced();
	m_sceneDir = recovered ? QString() : scenePath;
	m_savedFiles = std::move(files);
	m_document->replace(result.scene, recovered);
	m_sceneIsBundle = sceneIsBundle && !recovered;
	m_sceneBundleVersion = recovered ? std::nullopt : result.bundleVersion;
	m_savedSceneSha256 = recovered ? std::string() : hashSceneInputSnapshot(result.inputSnapshot);
	m_sceneDiagnostics.clear();
	OpenedScene opened;
	opened.path = scenePath;
	opened.bundle = sceneIsBundle;
	opened.recovered = recovered;
	opened.reloadingSameScene = reloadingSameScene;
	opened.timingIdentityOk = timingIdentityOk;
	if (!m_host.sceneOpened(opened))
		return false;
	if (!recovered)
		operation.sceneOpened(sceneIsBundle ? telemetry::SceneKind::Bundled : telemetry::SceneKind::Local);
	return true;
}

void SceneSession::saveScene() {
	saveSceneToCurrentDir();
}

bool SceneSession::finishSceneSave(const SceneSaveResult& result,
	const telemetry::OperationObservation& operation, std::optional<telemetry::ExportKind> kind) {
	auto error = telemetryDiagnosticError(result.diagnostics);
	// The bundle save boundary checks this typed value before any filesystem attempt.
	if (kind == telemetry::ExportKind::SceneBundle && !result.success() && !result.writeAttempted
		&& m_document->model().schemaVersion != kCurrentSceneSchemaVersion)
		error = telemetry::Error::UnsupportedFormat;
	operation.exportFinished(kind, result.success(), result.writeAttempted, error);
	if (!result.success()) {
		QString message = diagnosticsDialogText(result.diagnostics);
		if (message.isEmpty())
			message = "The case could not be saved.";
		showError("Cannot Save Case", message);
		return false;
	}

	refreshSavedSceneMetadata(m_document->mutableModel());
	m_document->mutableModel().savedWithAppVersion = EGTRAIN_APP_VERSION;
	m_savedSceneSha256 = hashSceneInputSnapshot(result.inputSnapshot);
	m_savedFiles = m_sceneDir.isEmpty() ? std::vector<DiskFile>() : diskFiles(m_sceneDir, m_sceneIsBundle);
	// A saved folder case may carry a new name, which Recent Cases shows.
	rebuildRecentScenesMenu();
	emit sceneSaved();
	return true;
}

bool SceneSession::saveSceneToCurrentDir() {
	const auto operation = m_host.captureTelemetryOperation();
	if (!m_document->isLoaded())
		return false;
	m_host.commitPendingEdits();
	if (m_sceneDir.isEmpty() || isInstalledScenePath(m_sceneDir, QCoreApplication::applicationDirPath()))
		return saveSceneAsToBundle(operation);

	// A hand edit, a sync client or another window may have changed the case since it was opened or saved.
	if (!m_savedFiles.empty() && diskFiles(m_sceneDir, m_sceneIsBundle) != m_savedFiles
		&& !m_prompts->confirm(m_window, "Case Changed on Disk",
			QString("%1 has changed on disk since it was opened or last saved.\n\nReplace the case on disk with this "
					"version? To keep both, choose No and use Save As.")
				.arg(display_path::format(m_sceneDir))))
		return false;

	auto result = m_sceneIsBundle
		? saveSceneBundle(m_document->model(), m_sceneDir.toStdString())
		: ::saveScene(m_document->model(), m_sceneDir.toStdString());
	return finishSceneSave(result, operation, m_sceneIsBundle ? std::optional<telemetry::ExportKind>(telemetry::ExportKind::SceneBundle) : std::nullopt);
}

void SceneSession::saveSceneAs() {
	if (!m_document->isLoaded())
		return;
	// A save dialog chooses either a file or a folder, so the format comes first.
	const bool folder = !m_sceneIsBundle && !m_sceneDir.isEmpty();
	const int format = m_prompts->chooseButton(m_window, "Save As",
		"Save the case as one case file (.egscene) or as a case folder of JSON files?", {"Case File...", "Case Folder..."},
		folder ? 1 : 0);
	if (format == 0)
		saveSceneAsToBundle();
	else if (format == 1)
		saveSceneAsToDirectory();
}

bool SceneSession::saveSceneAsToBundle() {
	return saveSceneAsToBundle(m_host.captureTelemetryOperation());
}

bool SceneSession::saveSceneAsToBundle(const telemetry::OperationObservation& operation) {
	if (!m_document->isLoaded())
		return false;
	m_host.commitPendingEdits();

	const QString startPath = saveSceneAsStartPath();
	QString targetPath = QFileDialog::getSaveFileName(m_window, "Save Case As", startPath,
		"EGTRAIN Case (*.egscene)");
	if (targetPath.isEmpty())
		return false;
	targetPath = savePathWithSuffix(m_window, *m_prompts, "Save Case As", targetPath, "egscene");
	if (targetPath.isEmpty())
		return false;
	targetPath = QDir::cleanPath(QFileInfo(targetPath).absoluteFilePath());
	if (isInstalledScenePath(targetPath, QCoreApplication::applicationDirPath())) {
		operation.exportFinished(telemetry::ExportKind::SceneBundle, false, false, telemetry::Error::InvalidInput);
		showError("Cannot Save Case", "Bundled cases cannot be overwritten. Choose a location outside the EGTRAIN installation.");
		return false;
	}

	auto result = saveSceneBundle(m_document->model(), targetPath.toStdString());
	if (!result.success())
		return finishSceneSave(result, operation, telemetry::ExportKind::SceneBundle);

	m_sceneDir = targetPath;
	m_sceneIsBundle = true;
	m_sceneBundleVersion = kCurrentSceneBundleVersion;
	addRecentScene(targetPath);
	return finishSceneSave(result, operation, telemetry::ExportKind::SceneBundle);
}

bool SceneSession::saveSceneAsToDirectory() {
	const auto operation = m_host.captureTelemetryOperation();
	if (!m_document->isLoaded())
		return false;
	m_host.commitPendingEdits();

	const QString startDir = dialog_folders::startFolder(m_sceneDir);
	QString dir = QFileDialog::getExistingDirectory(m_window, "Save Case As Folder", startDir);
	if (dir.isEmpty())
		return false;

	QString targetPath = QDir(dir).absolutePath();
	if (isInstalledScenePath(targetPath, QCoreApplication::applicationDirPath())) {
		operation.exportFinished(std::nullopt, false, false, telemetry::Error::InvalidInput);
		showError("Cannot Save Case", "Bundled cases cannot be overwritten. Choose a location outside the EGTRAIN installation.");
		return false;
	}
	QDir targetDir(targetPath);
	if (targetDir.exists()) {
		bool isNonEmpty = !targetDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty();
		bool hasSceneJson = QFileInfo(targetDir.filePath("scene.json")).exists();
		const bool currentFolder = !m_sceneIsBundle && !m_sceneDir.isEmpty() && resolvedFolder(m_sceneDir) == resolvedFolder(targetPath);
		if (hasSceneJson && !currentFolder) {
			if (!m_prompts->confirm(m_window, "Save Case As Folder",
					QString("%1 holds another case. Replace that case with this one?").arg(display_path::format(targetPath))))
				return false;
		} else if (isNonEmpty && !hasSceneJson) {
			if (!m_prompts->confirm(m_window, "Save Case As Folder", "The folder is not empty. Save the case there anyway?"))
				return false;
		}
	}

	if (!prepareSaveFolder(targetPath, operation))
		return false;

	auto result = ::saveScene(m_document->model(), targetPath.toStdString());
	if (!result.success())
		return finishSceneSave(result, operation, std::nullopt);

	m_sceneDir = targetPath;
	m_sceneIsBundle = false;
	m_sceneBundleVersion.reset();
	addRecentScene(targetPath);
	return finishSceneSave(result, operation, std::nullopt);
}

// Creates the folder `targetDir` for a scene that came from a folder, and refuses a folder inside that one. The save
// writes every scene file, views.json included, from the model.
bool SceneSession::prepareSaveFolder(const QString& targetDir, const telemetry::OperationObservation& operation) {
	if (m_sceneDir.isEmpty() || m_sceneIsBundle || !QFileInfo(m_sceneDir).isDir())
		return true;

	QString sourcePath = QDir(m_sceneDir).absolutePath();
	QString targetPath = QDir(targetDir).absolutePath();

	QDir target(targetPath);
	if (!target.exists() && !QDir().mkpath(targetPath)) {
		operation.failure(telemetry::Operation::Export, telemetry::Error::IoFailure);
		showError("Cannot Save Case", QString("Could not create the folder %1.").arg(display_path::format(targetPath)));
		return false;
	}

	QString sourceCanonical = QDir(sourcePath).canonicalPath();
	QString targetCanonical = QDir(targetPath).canonicalPath();
	if (!sourceCanonical.isEmpty() && targetCanonical.startsWith(sourceCanonical + "/")) {
		showError("Cannot Save Case", "Choose a folder outside the current case folder.");
		return false;
	}
	return true;
}

bool SceneSession::maybeSaveScene() {
	m_host.commitPendingEdits();
	if (!m_document->isDirty())
		return true;

	const QString name = QString::fromStdString(m_document->model().name).trimmed();
	auto response = QMessageBox::warning(m_window,
		"Unsaved Case",
		QString("Save the changes to %1?").arg(name.isEmpty() ? QStringLiteral("this case") : name),
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
		QMessageBox::Save);
	if (response == QMessageBox::Save)
		return saveSceneToCurrentDir();
	if (response == QMessageBox::Cancel)
		return false;
	return true;
}

QString SceneSession::saveSceneAsStartPath() const {
	// A recovered scene has no file of its own, so the dialog starts at the case that it is a copy of.
	const QString recoveredSource = m_host.recoveredSceneSource();
	const QString casePath = m_sceneDir.isEmpty() && !recoveredSource.isEmpty() ? recoveredSource : m_sceneDir;
	// A case file is suggested under its own name, and a case folder as a case file of its name beside it.
	const QFileInfo info(casePath);
	const QString name = info.isDir() ? QDir(casePath).dirName() + ".egscene" : info.fileName();
	return dialog_folders::suggestedPath(name, casePath);
}

QMenu* SceneSession::recentScenesMenu() const {
	return m_recentScenesMenu;
}

QStringList SceneSession::recentScenes() {
	// Several spellings of one path, kept by an earlier version, list once, with the newest.
	QStringList recent;
	QSet<QString> seen;
	for (const QString& stored : QSettings().value(kRecentScenesKey).toStringList()) {
		const QString key = sceneListKey(stored);
		if (seen.contains(key))
			continue;
		seen.insert(key);
		recent << stored;
	}
	return recent;
}

void SceneSession::addRecentScene(const QString& path) {
	// The case chooser lists the installed scenes under Bundled cases. Their folder can change at every start, as the
	// mount folder of an AppImage does, so a stored path would soon point nowhere.
	if (isInstalledScenePath(path, QCoreApplication::applicationDirPath()))
		return;
	const QString cleanPath = QFileInfo(path).absoluteFilePath();
	QSettings settings;
	QStringList recent = recentScenes();
	const QString key = sceneListKey(cleanPath);
	recent.erase(std::remove_if(recent.begin(), recent.end(), [&key](const QString& stored) { return sceneListKey(stored) == key; }),
		recent.end());
	recent.prepend(cleanPath);
	while (recent.size() > kMaxRecentScenes)
		recent.removeLast();
	settings.setValue(kRecentScenesKey, recent);
	rebuildRecentScenesMenu();
	emit sceneActionsChanged();
}

void SceneSession::rebuildRecentScenesMenu() {
	if (!m_recentScenesMenu)
		return;

	m_recentScenesMenu->clear();
	const QStringList recent = recentScenes();
	// Label entries as the chooser does; the full path stays in the status tip.
	const QStringList labels = sceneListLabels(recent);
	for (int i = 0; i < recent.size(); ++i) {
		const QString& path = recent.at(i);
		QString label = labels.at(i);
		// A scene name may hold '&', which a menu would read as a mnemonic.
		label.replace('&', "&&");
		const bool exists = recentSceneExists(path);
		QAction* action = m_recentScenesMenu->addAction(exists ? label : unavailableLabel(label));
		action->setEnabled(exists);
		action->setData(path);
		action->setProperty(kRecentLabelProperty, label);
		action->setStatusTip(display_path::format(path));
		action->setToolTip(display_path::format(path));
		connect(action, &QAction::triggered, this, [this, action]() {
			requestOpenScene(action->data().toString());
		});
	}
	if (recent.isEmpty()) {
		QAction* emptyAction = m_recentScenesMenu->addAction("No Recent Cases");
		emptyAction->setEnabled(false);
	}
	emit sceneActionsChanged();
}

void SceneSession::refreshRecentSceneAvailability() {
	for (QAction* action : m_recentScenesMenu->actions()) {
		const QString path = action->data().toString();
		const QString label = action->property(kRecentLabelProperty).toString();
		if (path.isEmpty() || label.isEmpty())
			continue;
		const bool exists = recentSceneExists(path);
		action->setEnabled(exists);
		action->setText(exists ? label : unavailableLabel(label));
	}
}

void SceneSession::updateSceneWindowTitle() {
	if (m_document->isLoaded()) {
		m_window->setWindowTitle(QString("EGTRAIN - %1[*]").arg(QString::fromStdString(m_document->model().name)));
	} else {
		m_window->setWindowTitle("EGTRAIN[*]");
	}
	m_window->setWindowModified(m_document->isDirty());
}

void SceneSession::actionLoad_Network() {
	const auto operation = m_host.captureTelemetryOperation();
	const QString startDir = dialog_folders::openStartFolder(m_sceneDir);
	const QString sourceDir = QFileDialog::getExistingDirectory(m_window, "Select Legacy Case Folder", startDir);
	if (sourceDir.isEmpty())
		return;
	const QString destinationDir =
		QFileDialog::getExistingDirectory(m_window, "Select Case Destination", QFileInfo(sourceDir).absolutePath());
	importLegacyCase(sourceDir, destinationDir, operation);
}

QString SceneSession::legacyImportSummary(const std::vector<SceneDiagnostic>& diagnostics, bool imported) {
	constexpr size_t kMaxListed = 4;
	int errors = 0;
	int warnings = 0;
	int notes = 0;
	for (const auto& d : diagnostics) {
		if (d.severity == SceneSeverity::Error)
			++errors;
		else if (d.severity == SceneSeverity::Warning)
			++warnings;
		else
			++notes;
	}
	// The errors come first, so that the lines listed are the ones that the counts call most serious.
	std::vector<const SceneDiagnostic*> ordered;
	for (const auto& d : diagnostics)
		ordered.push_back(&d);
	std::stable_sort(ordered.begin(), ordered.end(),
		[](const SceneDiagnostic* a, const SceneDiagnostic* b) { return a->severity > b->severity; });

	if (ordered.empty())
		return QString();
	QStringList counts;
	if (errors > 0)
		counts << countText(errors, "error", "errors");
	if (warnings > 0)
		counts << countText(warnings, "warning", "warnings");
	if (notes > 0)
		counts << countText(notes, "note", "notes");
	QString summary = (imported ? "Imported with " : "Not imported: ") + counts.join(", ");
	for (size_t i = 0; i < ordered.size() && i < kMaxListed; ++i) {
		QString detail = QString::fromStdString(ordered[i]->message);
		if (!ordered[i]->file.empty())
			detail += QString(" [%1]").arg(QString::fromStdString(ordered[i]->file));
		summary += "\n" + detail;
	}
	if (ordered.size() > kMaxListed)
		summary += QString("\nand %1 more").arg(static_cast<int>(ordered.size() - kMaxListed));
	return summary;
}

void SceneSession::importLegacyCase(const QString& sourceDir, const QString& destinationDir,
	const telemetry::OperationObservation& operation) {
	if (sourceDir.isEmpty() || destinationDir.isEmpty())
		return;

	const QString sourceCanonical = QFileInfo(sourceDir).canonicalFilePath();
	const QString destinationCanonical = QFileInfo(destinationDir).canonicalFilePath();
	const QString sourcePath = sourceCanonical.isEmpty() ? QDir(sourceDir).absolutePath() : sourceCanonical;
	const QString destinationPath = destinationCanonical.isEmpty() ? QDir(destinationDir).absolutePath() : destinationCanonical;
	auto containsPath = [](const QString& parent, const QString& child) {
		return child == parent || child.startsWith(parent + QDir::separator());
	};
	if (containsPath(sourcePath, destinationPath) || containsPath(destinationPath, sourcePath)) {
		showError("Cannot Import Legacy Case", "Choose a destination outside the legacy case folder.");
		return;
	}

	const QString sceneName = QFileInfo(sourcePath).fileName().isEmpty()
		? QString("Imported Legacy Case")
		: QFileInfo(sourcePath).fileName();
	// The import replaces the folder that it writes with everything in it.
	QString sceneDir = destinationPath;
	if (QFileInfo(QDir(destinationPath).filePath("scene.json")).exists()) {
		if (!m_prompts->confirm(m_window, "Import Legacy Case",
				QString("%1 holds another case. Replace that case and everything else in the folder with the imported one?")
					.arg(display_path::format(destinationPath))))
			return;
	} else if (holdsFiles(destinationPath)) {
		if (!m_prompts->confirm(m_window, "Import Legacy Case",
				QString("%1 is not empty. Import the case into a new folder %2 inside it?")
					.arg(display_path::format(destinationPath), sceneName)))
			return;
		sceneDir = QDir(destinationPath).filePath(sceneName);
		const QFileInfo caseFolder(sceneDir);
		if (caseFolder.exists() && (!caseFolder.isDir() || holdsFiles(sceneDir))) {
			showError("Cannot Import Legacy Case",
				QString("%1 already exists and is not empty. Choose another destination.").arg(display_path::format(sceneDir)));
			return;
		}
	}

	if (!maybeSaveScene())
		return;

	m_window->statusBar()->showMessage("Importing legacy case...");
	QApplication::processEvents();
	// Replaces the progress message, which would otherwise stay in the status bar.
	const auto importFailed = [this] {
		m_window->statusBar()->showMessage("Legacy import failed", 8000);
	};

	SceneImportResult importResult = importLegacyScene(sourcePath.toStdString(), sceneDir.toStdString(),
		sceneName.toStdString());
	if (!importResult.success()) {
		importFailed();
		showError("Cannot Import Legacy Case", legacyImportSummary(importResult.diagnostics, false));
		return;
	}

	SceneLoadResult loadResult = loadScene(sceneDir.toStdString());
	std::vector<SceneDiagnostic> diagnostics = importResult.diagnostics;
	diagnostics.insert(diagnostics.end(), loadResult.diagnostics.begin(), loadResult.diagnostics.end());
	if (errorDiagnosticCount(loadResult.diagnostics) > 0) {
		importFailed();
		showError("Cannot Open Imported Case", legacyImportSummary(diagnostics, true));
		return;
	}
	const auto semanticDiagnostics = validateRunnableScene(loadResult.scene);
	diagnostics.insert(diagnostics.end(), semanticDiagnostics.begin(), semanticDiagnostics.end());
	m_window->statusBar()->clearMessage();
	// An import that found nothing to report opens the case without a box.
	const QString summary = legacyImportSummary(diagnostics, true);
	if (!summary.isEmpty())
		showError("Legacy Case Imported", summary, true);
	if (m_host.afterRunStopped([this, sceneDir, operation] { openSceneDirectory(sceneDir, operation); }))
		return;
	openSceneDirectory(sceneDir, operation);
}
