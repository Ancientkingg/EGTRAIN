#include "app/SceneOutputDirectory.h"

#include "scene/SceneModel.h"
#include <QDir>
#include <QStandardPaths>

QString sceneOutputDirectory(const std::string& sceneName) {
	QString base = qEnvironmentVariable("QEGTRAIN_OUTPUT_DIR");
	if (base.isEmpty()) {
		base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
		if (base.isEmpty())
			base = QDir::homePath() + "/EGTRAIN";
	}
	const QString suffix = QString::fromStdString(sceneOutputDirectoryComponent(sceneName));
	return QDir(QDir(base).filePath("Output/" + suffix)).absolutePath();
}
