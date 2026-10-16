#include "tests/e2e/MainWindowTestAccess.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QString>
#include <QStringList>
#include <cstdio>
#include <string>

// Imports one legacy case into a new scene folder, then tries an import that must fail and checks
// that the open scene is untouched. The folders come from QEGTRAIN_E2E_LEGACY_SOURCE and _DEST for
// the import that works, and from _BAD_SOURCE and _BAD_DEST for the one that fails.
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
	w.importLegacyCase(qEnvironmentVariable("QEGTRAIN_E2E_LEGACY_SOURCE"), destinationInput, w.captureTelemetryOperation());
	if (w.m_sceneLoaded && QDir(w.m_sceneDir).absolutePath() == destination && !w.m_sceneModel.stations.empty()
		&& w.scene && !w.scene->items().isEmpty())
		marker("E2E_LEGACY_IMPORT_SUCCESS");
	else
		fail("successful import did not open the destination scene");

	const QString previousDir = w.m_sceneDir;
	const std::string previousName = w.m_sceneModel.name;
	const int previousItemCount = w.scene ? w.scene->items().size() : -1;
	const QString badSource = qEnvironmentVariable("QEGTRAIN_E2E_LEGACY_BAD_SOURCE");
	const QString badDestination = qEnvironmentVariable("QEGTRAIN_E2E_LEGACY_BAD_DEST");
	w.importLegacyCase(badSource, badDestination, w.captureTelemetryOperation());

	const bool failedImport = !QFileInfo(QDir(badDestination).filePath("scene.json")).exists();
	if (failedImport)
		marker("E2E_LEGACY_IMPORT_FAILURE");
	else
		fail("malformed import unexpectedly wrote a scene");
	const bool preserved = w.m_sceneDir == previousDir && w.m_sceneModel.name == previousName
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
