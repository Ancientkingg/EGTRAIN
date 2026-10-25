#include "update/Distribution.h"

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

static bool makeFolder(const QString& path) {
	return QDir().mkpath(path);
}

static bool writeEmptyFile(const QString& path) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly);
}

static const QString kMarker = QStringLiteral("installed-by-setup.ini");

class TestDistribution : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void namesTheMarker();
	void folderWithoutMarkerIsPortable();
	void folderWithMarkerIsInstalled();
	void directoryNamedLikeMarkerDoesNotCount();
	void markerInSubfolderDoesNotCount();
	void markerInParentDoesNotCount();
	void missingFolderIsPortable();
	void emptyOrRelativePathIsPortable();
	void currentDistributionIsThatOfPlatform();

private:
	QTemporaryDir m_temp;
};

void TestDistribution::initTestCase() {
	QVERIFY(m_temp.isValid());
}

void TestDistribution::namesTheMarker() {
	// The marker is installed-by-setup.ini.
	QCOMPARE(installerMarkerName(), kMarker);
}

void TestDistribution::folderWithoutMarkerIsPortable() {
	const QString empty = m_temp.filePath(QStringLiteral("empty"));
	QVERIFY2(makeFolder(empty), "the empty folder is created");
	// A folder without the marker is portable.
	QCOMPARE(windowsDistributionOf(empty), Distribution::WindowsPortable);
}

void TestDistribution::folderWithMarkerIsInstalled() {
	const QString marked = m_temp.filePath(QStringLiteral("marked"));
	// The folder with the marker is created.
	QVERIFY(makeFolder(marked));
	QVERIFY(writeEmptyFile(QDir(marked).filePath(kMarker)));
	// A folder with an empty marker file is installed.
	QCOMPARE(windowsDistributionOf(marked), Distribution::WindowsInstalled);
}

void TestDistribution::directoryNamedLikeMarkerDoesNotCount() {
	const QString markerFolder = m_temp.filePath(QStringLiteral("marker-folder"));
	QVERIFY2(makeFolder(QDir(markerFolder).filePath(kMarker)), "the folder that holds a directory named like the marker is created");
	// A directory named like the marker does not count.
	QCOMPARE(windowsDistributionOf(markerFolder), Distribution::WindowsPortable);
}

void TestDistribution::markerInSubfolderDoesNotCount() {
	const QString inner = m_temp.filePath(QStringLiteral("with-subfolder"));
	// The folder with a marker in a subfolder is created.
	QVERIFY(makeFolder(QDir(inner).filePath(QStringLiteral("sub"))));
	QVERIFY(writeEmptyFile(QDir(inner).filePath(QStringLiteral("sub/") + kMarker)));
	// A marker in a subfolder does not count.
	QCOMPARE(windowsDistributionOf(inner), Distribution::WindowsPortable);
}

void TestDistribution::markerInParentDoesNotCount() {
	const QString parent = m_temp.filePath(QStringLiteral("parent"));
	const QString child = QDir(parent).filePath(QStringLiteral("child"));
	// The folder with a marker in its parent is created.
	QVERIFY(makeFolder(child));
	QVERIFY(writeEmptyFile(QDir(parent).filePath(kMarker)));
	// A marker in the parent folder does not count.
	QCOMPARE(windowsDistributionOf(child), Distribution::WindowsPortable);
}

void TestDistribution::missingFolderIsPortable() {
	// A folder that does not exist is portable.
	QCOMPARE(windowsDistributionOf(m_temp.filePath(QStringLiteral("missing"))), Distribution::WindowsPortable);
}

// A relative path would be read against the current directory, which here holds the marker.
void TestDistribution::emptyOrRelativePathIsPortable() {
	const QString marked = m_temp.filePath(QStringLiteral("relative"));
	// The folder with the marker is created.
	QVERIFY(makeFolder(marked));
	QVERIFY(writeEmptyFile(QDir(marked).filePath(kMarker)));
	const QString previous = QDir::currentPath();
	QVERIFY2(QDir::setCurrent(marked), "the current directory can be changed");
	// Windows cannot delete the current directory, so a failed check still restores it.
	auto restore = qScopeGuard([&previous]() { QDir::setCurrent(previous); });
	// An empty path is portable.
	QCOMPARE(windowsDistributionOf(QString()), Distribution::WindowsPortable);
	// A relative path is portable.
	QCOMPARE(windowsDistributionOf(QStringLiteral(".")), Distribution::WindowsPortable);
	restore.dismiss();
	QVERIFY2(QDir::setCurrent(previous), "the current directory is restored");
}

void TestDistribution::currentDistributionIsThatOfPlatform() {
#if defined(Q_OS_WIN)
	const Distribution expectedCurrent = Distribution::WindowsPortable;
#elif defined(Q_OS_MACOS)
	const Distribution expectedCurrent = Distribution::MacApp;
#elif defined(Q_OS_LINUX)
	const Distribution expectedCurrent = Distribution::LinuxAppImage;
#else
	const Distribution expectedCurrent = Distribution::Unsupported;
#endif
	// The current distribution is the one of the platform.
	QCOMPARE(currentDistribution(), expectedCurrent);
}

QTEST_GUILESS_MAIN(TestDistribution)
#include "test_distribution.moc"
