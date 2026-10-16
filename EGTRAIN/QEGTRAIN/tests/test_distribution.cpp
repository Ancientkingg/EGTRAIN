#include "update/Distribution.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static bool makeFolder(const QString& path) {
	return QDir().mkpath(path);
}

static bool writeEmptyFile(const QString& path) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly);
}

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid())
		return 1;

	const QString marker = QStringLiteral("installed-by-setup.ini");
	const QDir root(temp.path());
	bool ok = true;

	ok &= expect(installerMarkerName() == marker, "the marker is installed-by-setup.ini");

	const QString empty = root.filePath(QStringLiteral("empty"));
	ok &= expect(makeFolder(empty), "the empty folder is created");
	ok &= expect(windowsDistributionOf(empty) == Distribution::WindowsPortable,
		"a folder without the marker is portable");

	const QString marked = root.filePath(QStringLiteral("marked"));
	ok &= expect(makeFolder(marked) && writeEmptyFile(QDir(marked).filePath(marker)),
		"the folder with the marker is created");
	ok &= expect(windowsDistributionOf(marked) == Distribution::WindowsInstalled,
		"a folder with an empty marker file is installed");

	const QString markerFolder = root.filePath(QStringLiteral("marker-folder"));
	ok &= expect(makeFolder(QDir(markerFolder).filePath(marker)),
		"the folder that holds a directory named like the marker is created");
	ok &= expect(windowsDistributionOf(markerFolder) == Distribution::WindowsPortable,
		"a directory named like the marker does not count");

	const QString inner = root.filePath(QStringLiteral("with-subfolder"));
	ok &= expect(makeFolder(QDir(inner).filePath(QStringLiteral("sub")))
			&& writeEmptyFile(QDir(inner).filePath(QStringLiteral("sub/") + marker)),
		"the folder with a marker in a subfolder is created");
	ok &= expect(windowsDistributionOf(inner) == Distribution::WindowsPortable,
		"a marker in a subfolder does not count");

	const QString parent = root.filePath(QStringLiteral("parent"));
	const QString child = QDir(parent).filePath(QStringLiteral("child"));
	ok &= expect(makeFolder(child) && writeEmptyFile(QDir(parent).filePath(marker)),
		"the folder with a marker in its parent is created");
	ok &= expect(windowsDistributionOf(child) == Distribution::WindowsPortable,
		"a marker in the parent folder does not count");

	ok &= expect(windowsDistributionOf(root.filePath(QStringLiteral("missing")))
			== Distribution::WindowsPortable,
		"a folder that does not exist is portable");

	// A relative path would be read against the current directory, which here holds the marker.
	const QString previous = QDir::currentPath();
	ok &= expect(QDir::setCurrent(marked), "the current directory can be changed");
	ok &= expect(windowsDistributionOf(QString()) == Distribution::WindowsPortable,
		"an empty path is portable");
	ok &= expect(windowsDistributionOf(QStringLiteral(".")) == Distribution::WindowsPortable,
		"a relative path is portable");
	ok &= expect(QDir::setCurrent(previous), "the current directory is restored");

#if defined(Q_OS_WIN)
	const Distribution expectedCurrent = Distribution::WindowsPortable;
#elif defined(Q_OS_MACOS)
	const Distribution expectedCurrent = Distribution::MacApp;
#elif defined(Q_OS_LINUX)
	const Distribution expectedCurrent = Distribution::LinuxAppImage;
#else
	const Distribution expectedCurrent = Distribution::Unsupported;
#endif
	ok &= expect(currentDistribution() == expectedCurrent, "the current distribution is the one of the platform");

	const QString reason = selfReplacementBlockedReason(Distribution::WindowsInstalled);
	ok &= expect(!reason.isEmpty() && reason.contains(QStringLiteral("installer"))
			&& reason.endsWith(QLatin1Char('.')),
		"an installed copy is blocked with a sentence that names the installer");
	ok &= expect(selfReplacementBlockedReason(Distribution::WindowsPortable).isEmpty(),
		"a portable copy is not blocked");
	ok &= expect(selfReplacementBlockedReason(Distribution::MacApp).isEmpty(),
		"a macOS app is not blocked");
	ok &= expect(selfReplacementBlockedReason(Distribution::LinuxAppImage).isEmpty(),
		"a Linux AppImage is not blocked");
	ok &= expect(selfReplacementBlockedReason(Distribution::Unsupported).isEmpty(),
		"an unsupported platform is not blocked");

	return ok ? 0 : 1;
}
