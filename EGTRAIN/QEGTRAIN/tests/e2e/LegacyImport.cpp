#include "tests/e2e/MainWindowTestAccess.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <cstdio>
#include <string>
#include <vector>
#include "graphics/NetworkScene.h"

// Imports one legacy case into a new scene folder, then tries an import that must fail and checks
// that the open scene is untouched and that the status bar no longer shows the progress message. It also
// checks that the counts of the summary of an import match the lines that it lists, and that an import with nothing to
// report has no summary, so that no box opens for it. The folders come from
// QEGTRAIN_E2E_LEGACY_SOURCE and _DEST for the import that works, and from _BAD_SOURCE and _BAD_DEST for the
// one that fails.
void MainWindowTestAccess::runLegacyImport(MainWindow& w) {
	if (scenarioStarted)
		return;
	scenarioStarted = true;

	bool ok = true;
	QStringList failures;
	auto marker = [](const char* name) {
		std::fprintf(stdout, "%s\n", name);
		std::fflush(stdout);
	};
	auto fail = [&](const QString& message) {
		ok = false;
		failures << message;
	};

	const QString destinationInput = qEnvironmentVariable("QEGTRAIN_E2E_LEGACY_DEST");
	const QString destinationCanonical = QFileInfo(destinationInput).canonicalFilePath();
	const QString destination = destinationCanonical.isEmpty() ? QDir(destinationInput).absolutePath() : destinationCanonical;
	w.m_session->importLegacyCase(qEnvironmentVariable("QEGTRAIN_E2E_LEGACY_SOURCE"), destinationInput, w.captureTelemetryOperation());
	if (w.m_document->isLoaded() && QDir(w.m_session->m_sceneDir).absolutePath() == destination && !w.m_document->mutableModel().stations.empty()
		&& w.scene && !w.scene->items().isEmpty())
		marker("E2E_LEGACY_IMPORT_SUCCESS");
	else
		fail("successful import did not open the destination scene");

	const QString progress = QStringLiteral("Importing legacy case...");
	if (w.statusBar()->currentMessage() == progress)
		fail("the status bar kept the progress message after the import");

	const QString previousDir = w.m_session->m_sceneDir;
	const std::string previousName = w.m_document->mutableModel().name;
	const int previousItemCount = w.scene ? w.scene->items().size() : -1;
	const QString badSource = qEnvironmentVariable("QEGTRAIN_E2E_LEGACY_BAD_SOURCE");
	const QString badDestination = qEnvironmentVariable("QEGTRAIN_E2E_LEGACY_BAD_DEST");
	w.m_session->importLegacyCase(badSource, badDestination, w.captureTelemetryOperation());

	if (w.statusBar()->currentMessage() != QStringLiteral("Legacy import failed"))
		fail("the status bar after the failed import reads: " + w.statusBar()->currentMessage());

	const auto diagnostic = [](SceneSeverity severity, const char* message) {
		SceneDiagnostic d;
		d.severity = severity;
		d.message = message;
		return d;
	};
	const QString shortSummary = SceneSession::legacyImportSummary(
		{diagnostic(SceneSeverity::Info, "note"), diagnostic(SceneSeverity::Warning, "warning"), diagnostic(SceneSeverity::Error, "error")});
	if (shortSummary != QStringLiteral("Import: 1 error, 1 warning, 1 note\nerror\nwarning\nnote"))
		fail("short summary: " + shortSummary);
	std::vector<SceneDiagnostic> many(6, diagnostic(SceneSeverity::Warning, "warning"));
	many.push_back(diagnostic(SceneSeverity::Error, "error"));
	const QString longSummary = SceneSession::legacyImportSummary(many);
	if (longSummary != QStringLiteral("Import: 1 error, 6 warnings\nerror\nwarning\nwarning\nwarning\nand 3 more"))
		fail("long summary: " + longSummary);
	// A count of zero is left out, and an import with nothing to report has no summary, so no box opens.
	const QString notesOnly = SceneSession::legacyImportSummary(
		{diagnostic(SceneSeverity::Info, "first note"), diagnostic(SceneSeverity::Info, "second note")});
	if (notesOnly != QStringLiteral("Import: 2 notes\nfirst note\nsecond note"))
		fail("summary of notes only: " + notesOnly);
	if (!SceneSession::legacyImportSummary({}).isEmpty())
		fail("an import without diagnostics has a summary: " + SceneSession::legacyImportSummary({}));

	const bool failedImport = !QFileInfo(QDir(badDestination).filePath("scene.json")).exists();
	if (failedImport)
		marker("E2E_LEGACY_IMPORT_FAILURE");
	else
		fail("malformed import unexpectedly wrote a scene");
	const bool preserved = w.m_session->m_sceneDir == previousDir && w.m_document->mutableModel().name == previousName
		&& (!w.scene || w.scene->items().size() == previousItemCount);
	if (preserved)
		marker("E2E_LEGACY_IMPORT_STATE_PRESERVED");
	else
		fail("failed import replaced the current scene or preview");

	if (ok) {
		marker("E2E_LEGACY_IMPORT_OK");
		QCoreApplication::exit(0);
		return;
	}

	std::fprintf(stderr, "E2E_LEGACY_IMPORT_FAIL: %s\n", failures.join(", ").toStdString().c_str());
	std::fflush(stderr);
	QCoreApplication::exit(2);
}
