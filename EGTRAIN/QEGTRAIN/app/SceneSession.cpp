#include "app/SceneSession.h"
// This header brings in SceneModel.h in the way that the member named signals needs, before the other scene headers do.
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
#include "widgets/UserPrompts.h"
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
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
#include <iterator>

namespace {
const char* kRecentScenesKey = "recentScenes";
const int kMaxRecentScenes = 8;
// The property of a recent scene action that holds its label without the Unavailable prefix.
const char* kRecentLabelProperty = "recentLabel";

bool sceneDropCandidate(const QMimeData* mime, QString* path, QString* rejection) {
	if (!mime || !mime->hasUrls() || mime->urls().isEmpty()) {
		*rejection = QStringLiteral("no file was dropped");
		return false;
	}
	if (mime->urls().size() != 1) {
		*rejection = QStringLiteral("drop exactly one .egscene file");
		return false;
	}
	const QUrl url = mime->urls().front();
	if (!url.isLocalFile()) {
		*rejection = QStringLiteral("only local .egscene files can be opened");
		return false;
	}
	const QFileInfo info(url.toLocalFile());
	if (!info.exists()) {
		*rejection = QStringLiteral("the dropped file does not exist");
		return false;
	}
	if (!info.isFile()) {
		*rejection = QStringLiteral("drop an .egscene file, not a directory");
		return false;
	}
	if (info.suffix().compare(QStringLiteral("egscene"), Qt::CaseInsensitive) != 0) {
		*rejection = QStringLiteral("the dropped file must use the .egscene extension");
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

} // namespace

QString firstDiagnosticMessage(const std::vector<SceneDiagnostic>& diagnostics) {
	for (const auto& d : diagnostics) {
		if (d.severity == SceneSeverity::Error)
			return QString::fromStdString(d.message);
	}
	if (!diagnostics.empty())
		return QString::fromStdString(diagnostics.front().message);
	return QString();
}

SceneSession::SceneSession(QMainWindow* window, SceneDocument* document, Host& host)
	: QObject(window), m_window(window), m_document(document), m_host(host),
	  m_recentScenesMenu(new QMenu("Recent Scenes", window)) {
	// A case can be deleted while the menu is closed, so the entries are checked again as the menu opens.
	connect(m_recentScenesMenu, &QMenu::aboutToShow, this, &SceneSession::refreshRecentSceneAvailability);
}

void SceneSession::showError(const QString& title, const QString& message, bool warningIcon) {
	UserPrompts([this] { return m_host.dialogsSuppressed(); }).showError(m_window, title, message, warningIcon);
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
			rejection = QStringLiteral("the source does not permit copying");
		m_window->statusBar()->showMessage(QStringLiteral("Drop rejected: %1. Scene unchanged.").arg(rejection), 8000);
		event->ignore();
	}
}

void SceneSession::drop(QDropEvent* event) {
	QString path;
	QString rejection;
	if (!sceneDropCandidate(event->mimeData(), &path, &rejection)
		|| !event->possibleActions().testFlag(Qt::CopyAction)) {
		if (rejection.isEmpty())
			rejection = QStringLiteral("the source does not permit copying");
		m_window->statusBar()->showMessage(QStringLiteral("Drop rejected: %1. Scene unchanged.").arg(rejection), 8000);
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
	m_host.sceneWillBeReplaced();
	m_sceneDir.clear();
	m_savedSceneSha256.clear();
	m_document->replace(makeNewSceneModel(), true);
	m_sceneIsBundle = false;
	m_sceneBundleVersion.reset();
	m_sceneDiagnostics.clear();
	m_host.sceneCreated(operation);
}

void SceneSession::openSceneDialog() {
	const auto operation = m_host.captureTelemetryOperation();
	const QString startDir = dialog_folders::openStartFolder(m_sceneDir);
	const QString path = QFileDialog::getOpenFileName(m_window, "Open Case Study", startDir,
		"EGTRAIN Case Study (*.egscene)");
	if (path.isEmpty())
		return;

	requestOpenScene(path, operation);
}

void SceneSession::openSceneFolderDialog() {
	const auto operation = m_host.captureTelemetryOperation();
	const QString startDir = dialog_folders::openStartFolder(m_sceneDir);
	const QString dir = QFileDialog::getExistingDirectory(m_window, "Open Scene Folder", startDir);
	if (!dir.isEmpty())
		requestOpenScene(dir, operation);
}

bool SceneSession::requestOpenScene(const QString& path) {
	return requestOpenScene(path, m_host.captureTelemetryOperation());
}

bool SceneSession::requestOpenScene(const QString& path, const telemetry::OperationObservation& operation) {
	if (path.isEmpty())
		return false;
	if (!maybeSaveScene()) {
		m_window->statusBar()->showMessage("Open canceled. Current scene retained.", 8000);
		return false;
	}
	// The request is accepted here. The scene opens when the running simulation has stopped.
	if (m_host.afterRunStopped([this, path, operation] { openRequestedScene(path, operation); }))
		return true;
	return openRequestedScene(path, operation);
}

bool SceneSession::openRequestedScene(const QString& path, const telemetry::OperationObservation& operation) {
	if (openSceneDirectory(path, operation))
		return true;
	m_window->statusBar()->showMessage(QString("Scene unchanged: %1 could not be opened.")
										   .arg(QFileInfo(path).fileName()),
		8000);
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
			return QString::fromStdString(toDisplayText(compatibility.diagnostics.front()));
		return QStringLiteral("The scene format could not be identified.");
	};
	// Scene compatibility prompts are suppressed only for unattended app/E2E
	// runs. The update-only disable setting must not hide scene safety prompts.
	const bool noCompatibilityDialogs = m_host.dialogsSuppressed();
	if (compatibility.classification == SceneCompatibilityClass::OlderMigratable) {
		if (noCompatibilityDialogs) {
			m_window->statusBar()->showMessage("Older scene requires an interactive upgrade copy");
			return false;
		}
		QMessageBox dialog(m_window);
		dialog.setIcon(QMessageBox::Question);
		dialog.setWindowTitle("Older Scene");
		QString ageDetails;
		if (compatibility.schemaVersion < kCurrentSceneSchemaVersion)
			ageDetails = QString("schema %1 (this application uses schema %2)")
							 .arg(compatibility.schemaVersion)
							 .arg(kCurrentSceneSchemaVersion);
		if (compatibility.sourceKind == SceneSourceKind::Bundle && compatibility.bundleVersion
			&& *compatibility.bundleVersion < kCurrentSceneBundleVersion) {
			if (!ageDetails.isEmpty())
				ageDetails += "; ";
			ageDetails += QString("bundle layout %1 (this application uses bundle layout %2)")
							  .arg(*compatibility.bundleVersion)
							  .arg(kCurrentSceneBundleVersion);
		}
		if (ageDetails.isEmpty())
			ageDetails = QStringLiteral("an older scene layout");
		dialog.setText(QString("%1 uses %2.")
				.arg(QFileInfo(scenePath).fileName(), ageDetails));
		dialog.setInformativeText("Upgrade a copy; the original scene will remain unchanged.");
		QPushButton* upgrade = dialog.addButton("Upgrade a Copy...", QMessageBox::AcceptRole);
		dialog.addButton("Cancel", QMessageBox::RejectRole);
		dialog.exec();
		if (dialog.clickedButton() != upgrade)
			return false;
		const QFileInfo sourceInfo(scenePath);
		QString destination;
		if (compatibility.sourceKind == SceneSourceKind::Bundle) {
			const QString base = sourceInfo.completeBaseName() + "-upgraded.egscene";
			destination = QFileDialog::getSaveFileName(m_window, "Upgrade Scene Copy",
				QDir(sourceInfo.absolutePath()).filePath(base), "EGTRAIN Case Study (*.egscene)");
		} else {
			// A migration destination must not already exist. A save-style chooser
			// lets the user name a sibling directory without selecting an existing one.
			destination = QFileDialog::getSaveFileName(m_window, "Upgrade Scene Copy",
				QDir(sourceInfo.absolutePath()).filePath(sourceInfo.fileName() + "-upgraded"),
				"EGTRAIN Scene Directory (*)");
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
			showError("Cannot Upgrade Scene", message);
			return false;
		}
		return openSceneDirectory(destination, operation);
	}
	if (compatibility.classification == SceneCompatibilityClass::OlderUnsupported) {
		operation.failure(telemetry::Operation::SceneOpen, telemetry::Error::UnsupportedFormat);
		if (!noCompatibilityDialogs)
			showError("Older Scene Not Supported",
				"This scene uses an older schema or bundle layout with no registered migration path.\n\n"
					+ compatibilityMessage());
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
		dialog.setWindowTitle("Newer Scene");
		dialog.setText("This scene was saved with a newer scene format.");
		QString details = QString("Supported schema: %1; scene schema: %2")
							  .arg(kCurrentSceneSchemaVersion)
							  .arg(compatibility.schemaVersion);
		if (compatibility.bundleVersion)
			details += QString("; supported bundle: %1; scene bundle: %2")
						   .arg(kCurrentSceneBundleVersion)
						   .arg(*compatibility.bundleVersion);
		dialog.setInformativeText(details);
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
		if (!noCompatibilityDialogs)
			showError(cannot_open::title(), cannot_open::text(scenePath, compatibilityMessage()));
		return false;
	}
	const bool sceneIsBundle = compatibility.sourceKind == SceneSourceKind::Bundle;
	const bool recovered = origin == OpenOrigin::Recovered;
	const bool reloadingSameScene = !recovered && m_document->isLoaded()
		&& QFileInfo(m_sceneDir).absoluteFilePath() == scenePath;
	bool timingIdentityOk = true;
	auto result = m_host.readScene(scenePath, timingIdentityOk);
	int errorCount = errorDiagnosticCount(result.diagnostics);
	if (errorCount > 0) {
		operation.failure(telemetry::Operation::SceneOpen, telemetryDiagnosticError(result.diagnostics));
		QString message = firstDiagnosticMessage(result.diagnostics);
		if (message.isEmpty())
			message = "Scene could not be opened.";
		message += QString("\n\nError count: %1").arg(errorCount);
		showError(cannot_open::title(), cannot_open::text(scenePath, message));
		return false;
	}

	m_host.sceneWillBeReplaced();
	m_sceneDir = recovered ? QString() : scenePath;
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
		QString message = firstDiagnosticMessage(result.diagnostics);
		if (message.isEmpty())
			message = "Scene could not be saved.";
		showError("Cannot Save Scene", message);
		return false;
	}

	refreshSavedSceneMetadata(m_document->mutableModel());
	m_document->mutableModel().savedWithAppVersion = EGTRAIN_APP_VERSION;
	m_savedSceneSha256 = hashSceneInputSnapshot(result.inputSnapshot);
	m_host.sceneSaved();
	return true;
}

bool SceneSession::saveSceneToCurrentDir() {
	const auto operation = m_host.captureTelemetryOperation();
	if (!m_document->isLoaded())
		return false;
	m_host.commitPendingEdits();
	if (m_sceneDir.isEmpty() || isInstalledScenePath(m_sceneDir, QCoreApplication::applicationDirPath()))
		return saveSceneAsToBundle(operation);

	auto result = m_sceneIsBundle
		? saveSceneBundle(m_document->model(), m_sceneDir.toStdString())
		: ::saveScene(m_document->model(), m_sceneDir.toStdString());
	return finishSceneSave(result, operation, m_sceneIsBundle ? std::optional<telemetry::ExportKind>(telemetry::ExportKind::SceneBundle) : std::nullopt);
}

void SceneSession::saveSceneAs() {
	saveSceneAsToBundle();
}

bool SceneSession::saveSceneAsToBundle() {
	return saveSceneAsToBundle(m_host.captureTelemetryOperation());
}

bool SceneSession::saveSceneAsToBundle(const telemetry::OperationObservation& operation) {
	if (!m_document->isLoaded())
		return false;
	m_host.commitPendingEdits();

	const QString startPath = saveSceneAsStartPath();
	QString targetPath = QFileDialog::getSaveFileName(m_window, "Save Case Study As", startPath,
		"EGTRAIN Case Study (*.egscene)");
	if (targetPath.isEmpty())
		return false;
	if (!targetPath.endsWith(".egscene", Qt::CaseInsensitive))
		targetPath += ".egscene";
	targetPath = QDir::cleanPath(QFileInfo(targetPath).absoluteFilePath());
	if (isInstalledScenePath(targetPath, QCoreApplication::applicationDirPath())) {
		operation.exportFinished(telemetry::ExportKind::SceneBundle, false, false, telemetry::Error::InvalidInput);
		showError("Cannot Save Scene", "Installed example scenes cannot be overwritten. Choose a location outside the EGTRAIN installation.");
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
	QString dir = QFileDialog::getExistingDirectory(m_window, "Save Scene As Folder", startDir);
	if (dir.isEmpty())
		return false;

	QString targetPath = QDir(dir).absolutePath();
	if (isInstalledScenePath(targetPath, QCoreApplication::applicationDirPath())) {
		operation.exportFinished(std::nullopt, false, false, telemetry::Error::InvalidInput);
		showError("Cannot Save Scene", "Installed example scenes cannot be overwritten. Choose a location outside the EGTRAIN installation.");
		return false;
	}
	QDir targetDir(targetPath);
	if (targetDir.exists()) {
		bool isNonEmpty = !targetDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty();
		bool hasSceneJson = QFileInfo(targetDir.filePath("scene.json")).exists();
		if (isNonEmpty && !hasSceneJson) {
			auto response = QMessageBox::question(m_window,
				"Save Scene As Folder",
				"The selected folder is not empty and does not contain scene.json. Save scene files there?",
				QMessageBox::Yes | QMessageBox::No,
				QMessageBox::No);
			if (response != QMessageBox::Yes)
				return false;
		}
	}

	if (!copyScenePassthroughFiles(targetPath, operation))
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

bool SceneSession::copyScenePassthroughFiles(const QString& targetDir, const telemetry::OperationObservation& operation) {
	if (m_sceneDir.isEmpty() || m_sceneIsBundle || !QFileInfo(m_sceneDir).isDir())
		return true;

	QString sourcePath = QDir(m_sceneDir).absolutePath();
	QString targetPath = QDir(targetDir).absolutePath();

	QDir target(targetPath);
	if (!target.exists() && !QDir().mkpath(targetPath)) {
		operation.failure(telemetry::Operation::Export, telemetry::Error::IoFailure);
		QMessageBox::critical(m_window, "Cannot Save Scene", "Cannot create target scene directory.");
		return false;
	}

	QString sourceCanonical = QDir(sourcePath).canonicalPath();
	QString targetCanonical = QDir(targetPath).canonicalPath();
	if (!sourceCanonical.isEmpty() && sourceCanonical == targetCanonical)
		return true;
	if (!sourceCanonical.isEmpty() && targetCanonical.startsWith(sourceCanonical + "/")) {
		QMessageBox::critical(m_window, "Cannot Save Scene", "Cannot save a scene inside the current scene folder.");
		return false;
	}

	QDir sourceDir(sourcePath);

	QString sourceViews = sourceDir.filePath("views.json");
	if (QFileInfo(sourceViews).exists()) {
		QString targetViews = target.filePath("views.json");
		QFile::remove(targetViews);
		if (!QFile::copy(sourceViews, targetViews)) {
			operation.failure(telemetry::Operation::Export, telemetry::Error::IoFailure);
			QMessageBox::critical(m_window, "Cannot Save Scene", "Cannot copy views.json.");
			return false;
		}
	}

	return true;
}

bool SceneSession::maybeSaveScene() {
	m_host.commitPendingEdits();
	if (!m_document->isDirty())
		return true;

	auto response = QMessageBox::warning(m_window,
		"Unsaved Scene",
		"The current scene has unsaved changes.",
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
		QMessageBox::Save);
	if (response == QMessageBox::Save)
		return saveSceneToCurrentDir();
	if (response == QMessageBox::Cancel)
		return false;
	return true;
}

QString SceneSession::saveSceneAsStartPath() const {
	// A recovered scene has no file of its own, so the dialog starts at the file that it is a copy of.
	const QString recoveredSource = m_host.recoveredSceneSource();
	if (m_sceneDir.isEmpty() && !recoveredSource.isEmpty())
		return dialog_folders::suggestedPath(QFileInfo(recoveredSource).fileName(), recoveredSource);
	return dialog_folders::suggestedPath(m_sceneIsBundle ? QFileInfo(m_sceneDir).fileName() : QString(), m_sceneDir);
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
	m_host.sceneActionsChanged();
}

void SceneSession::rebuildRecentScenesMenu() {
	if (!m_recentScenesMenu)
		return;

	m_recentScenesMenu->clear();
	const QStringList recent = recentScenes();
	// Label entries as the chooser does; the full path stays in the status tip. When two entries
	// share a label, a shortened parent path tells them apart.
	QHash<QString, int> nameCount;
	QStringList names;
	for (const QString& path : recent) {
		names << sceneDisplayName(path);
		++nameCount[names.last()];
	}
	for (int i = 0; i < recent.size(); ++i) {
		const QString& path = recent.at(i);
		const QString& name = names.at(i);
		QString label = name;
		if (nameCount.value(name) > 1) {
			QString parent = display_path::format(QFileInfo(path).dir().path());
			if (parent.length() > 44)
				parent = parent.left(20) + "..." + parent.right(20);
			label = QString("%1 (%2)").arg(name, parent);
		}
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
		QAction* emptyAction = m_recentScenesMenu->addAction("No Recent Scenes");
		emptyAction->setEnabled(false);
	}
	m_host.sceneActionsChanged();
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
	const QString sourceDir = QFileDialog::getExistingDirectory(m_window, "Select Legacy Case Folder", QDir::homePath());
	if (sourceDir.isEmpty())
		return;
	const QString destinationDir = QFileDialog::getExistingDirectory(m_window, "Select Scene Destination", QDir::homePath());
	importLegacyCase(sourceDir, destinationDir, operation);
}

QString SceneSession::legacyImportSummary(const std::vector<SceneDiagnostic>& diagnostics) {
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

	QString summary = QString("Import: %1 error(s), %2 warning(s)").arg(errors).arg(warnings);
	if (notes > 0)
		summary += QString(", %1 note(s)").arg(notes);
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
		showError("Cannot Import Legacy Case",
			"Choose a scene destination that is separate from and outside the legacy source folder.");
		return;
	}

	if (!maybeSaveScene())
		return;

	m_window->statusBar()->showMessage("Importing legacy case...");
	QApplication::processEvents();
	// Replaces the progress message, which would otherwise stay in the status bar.
	const auto importFailed = [this] {
		m_window->statusBar()->showMessage("Legacy import failed. Scene unchanged.", 8000);
	};

	const QString sceneName = QFileInfo(sourcePath).fileName().isEmpty()
		? QString("Imported Legacy Scene")
		: QFileInfo(sourcePath).fileName();
	SceneImportResult importResult = importLegacyScene(sourcePath.toStdString(), destinationPath.toStdString(),
		sceneName.toStdString());
	if (!importResult.success()) {
		importFailed();
		showError("Cannot Import Legacy Case", legacyImportSummary(importResult.diagnostics));
		return;
	}

	SceneLoadResult loadResult = loadScene(destinationPath.toStdString());
	std::vector<SceneDiagnostic> diagnostics = importResult.diagnostics;
	diagnostics.insert(diagnostics.end(), loadResult.diagnostics.begin(), loadResult.diagnostics.end());
	if (errorDiagnosticCount(loadResult.diagnostics) > 0) {
		importFailed();
		showError("Cannot Open Imported Scene", legacyImportSummary(diagnostics));
		return;
	}
	const auto semanticDiagnostics = validateRunnableScene(loadResult.scene);
	diagnostics.insert(diagnostics.end(), semanticDiagnostics.begin(), semanticDiagnostics.end());
	m_window->statusBar()->clearMessage();
	showError("Legacy Import Diagnostics", legacyImportSummary(diagnostics), true);
	if (m_host.afterRunStopped([this, destinationPath, operation] { openSceneDirectory(destinationPath, operation); }))
		return;
	openSceneDirectory(destinationPath, operation);
}
