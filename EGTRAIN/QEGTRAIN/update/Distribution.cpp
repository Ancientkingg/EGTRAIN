#include "update/Distribution.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

QString installerMarkerName() {
	return QStringLiteral("installed-by-setup.ini");
}

Distribution windowsDistributionOf(const QString& applicationDirectory) {
	if (!QDir::isAbsolutePath(applicationDirectory))
		return Distribution::WindowsPortable;
	return QFileInfo(QDir(applicationDirectory).filePath(installerMarkerName())).isFile()
		? Distribution::WindowsInstalled
		: Distribution::WindowsPortable;
}

Distribution currentDistribution() {
#if defined(Q_OS_WIN)
	return windowsDistributionOf(QCoreApplication::applicationDirPath());
#elif defined(Q_OS_MACOS)
	return Distribution::MacApp;
#elif defined(Q_OS_LINUX)
	return Distribution::LinuxAppImage;
#else
	return Distribution::Unsupported;
#endif
}
