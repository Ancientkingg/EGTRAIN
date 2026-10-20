#include "update/UpdatePreparation.h"

#include "update/WindowsStaging.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryFile>
#include <QThread>
#include <QXmlStreamReader>

#if defined(Q_OS_UNIX)
#include <sys/stat.h>
#include <sys/statvfs.h>
#endif

namespace {

constexpr qint64 kHashChunkBytes = 1024 * 1024;
constexpr int kDetachAttempts = 4;
// Expand-Archive unpacks a package of tens of megabytes. An emulated or heavily loaded machine
// needs well over a minute for it.
constexpr int kUnpackTimeoutMs = 10 * 60 * 1000;

bool runProcess(const QString& program, const QStringList& arguments, int timeoutMs,
	QString* error = nullptr, const QProcessEnvironment* environment = nullptr) {
	QProcess process;
	if (environment)
		process.setProcessEnvironment(*environment);
	process.start(program, arguments);
	if (!process.waitForStarted(5000)) {
		if (error)
			*error = QStringLiteral("Could not start %1.").arg(program);
		return false;
	}
	if (!process.waitForFinished(timeoutMs)) {
		process.kill();
		process.waitForFinished(1000);
		if (error)
			*error = QStringLiteral("%1 timed out.").arg(program);
		return false;
	}
	if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
		if (error)
			*error = QStringLiteral("%1 could not prepare the update package.").arg(program);
		return false;
	}
	return true;
}

// The newest modification time of the entries in a directory and below it, or of the directory
// itself when it is empty. The time of the directory alone is not used: it changes whenever the
// update helper removes an entry, although the entries that remain are old.
QDateTime newestModification(const QString& directory) {
	QDateTime newest;
	QDirIterator entries(directory, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
		QDirIterator::Subdirectories);
	while (entries.hasNext()) {
		entries.next();
		const QDateTime modified = entries.fileInfo().lastModified();
		if (modified.isValid() && (!newest.isValid() || modified > newest))
			newest = modified;
	}
	return newest.isValid() ? newest : QFileInfo(directory).lastModified();
}

bool validElf(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return false;
	return file.read(4) == QByteArray::fromHex("7f454c46");
}

std::optional<QString> macBundleVersion(const QString& bundlePath) {
	QFile plist(QDir(bundlePath).filePath(QStringLiteral("Contents/Info.plist")));
	if (!plist.open(QIODevice::ReadOnly | QIODevice::Text))
		return std::nullopt;
	QXmlStreamReader xml(&plist);
	QString key;
	while (!xml.atEnd()) {
		xml.readNext();
		if (!xml.isStartElement())
			continue;
		if (xml.name() == QStringLiteral("key")) {
			key = xml.readElementText();
		} else if (xml.name() == QStringLiteral("string")
			&& key == QStringLiteral("CFBundleShortVersionString")) {
			return xml.readElementText();
		}
	}
	return std::nullopt;
}

// True when path is the root of a mounted volume: it is on another device than its parent folder.
bool isMountPoint(const QString& path) {
#if defined(Q_OS_UNIX)
	struct stat volume;
	struct stat parent;
	return ::stat(QFile::encodeName(path).constData(), &volume) == 0
		&& ::stat(QFile::encodeName(QFileInfo(path).absolutePath()).constData(), &parent) == 0
		&& volume.st_dev != parent.st_dev;
#else
	Q_UNUSED(path);
	return false;
#endif
}

// Mounts a disk image read-only at mountPoint, hidden from the Finder and without a window.
bool attachDiskImage(const QString& image, const QString& mountPoint, QString* error) {
	if (!runProcess(QStringLiteral("/usr/bin/hdiutil"),
			{QStringLiteral("attach"), QStringLiteral("-readonly"), QStringLiteral("-nobrowse"),
				QStringLiteral("-noautoopen"), QStringLiteral("-mountpoint"), mountPoint, image},
			120000, error))
		return false;
	if (!isMountPoint(mountPoint)) {
		if (error)
			*error = QStringLiteral("The macOS update disk image did not mount.");
		return false;
	}
	return true;
}

// Detaches the disk image mounted at mountPoint, if there is one. A volume that stays busy is
// detached by force on the last attempt.
void detachDiskImage(const QString& mountPoint) {
	for (int attempt = 1; attempt <= kDetachAttempts && isMountPoint(mountPoint); ++attempt) {
		if (attempt > 1)
			QThread::msleep(1000);
		QStringList arguments{QStringLiteral("detach")};
		if (attempt == kDetachAttempts)
			arguments << QStringLiteral("-force");
		arguments << mountPoint;
		runProcess(QStringLiteral("/usr/bin/hdiutil"), arguments, 30000);
	}
}

bool copyMacApp(const QString& source, const QString& stagedPath, QString* error) {
	const QFileInfo app(source);
	if (!app.isDir() || app.fileName() != QStringLiteral("QEGTRAIN.app")
		|| !QFileInfo(QDir(source).filePath("Contents/MacOS/QEGTRAIN")).isFile()
		|| !QFileInfo(QDir(source).filePath("Contents/Info.plist")).isFile()) {
		if (error)
			*error = QStringLiteral("The macOS update package has an invalid app bundle.");
		return false;
	}
	return runProcess(QStringLiteral("/usr/bin/ditto"), {source, stagedPath}, 60000, error);
}

bool copyMacAppFromZip(const UpdatePreparationInput& input, const QString& stagedPath, QString* error) {
	const QString extract = QDir(input.stagingRoot).filePath(QStringLiteral("extract"));
	if (!QDir().mkpath(extract))
		return false;
	if (!runProcess(QStringLiteral("/usr/bin/ditto"),
			{QStringLiteral("-x"), QStringLiteral("-k"), input.packagePath, extract}, 60000, error))
		return false;
	return copyMacApp(QDir(extract).filePath(QStringLiteral("QEGTRAIN-Lebanon/QEGTRAIN.app")), stagedPath, error);
}

// The image is detached before this returns, whether or not the app was copied.
bool copyMacAppFromDiskImage(const UpdatePreparationInput& input, const QString& stagedPath, QString* error) {
	const QString mountPoint = QDir(input.stagingRoot).filePath(QStringLiteral("mnt"));
	bool copied = false;
	if (QDir().mkpath(mountPoint) && attachDiskImage(input.packagePath, mountPoint, error))
		copied = copyMacApp(QDir(mountPoint).filePath(QStringLiteral("QEGTRAIN.app")), stagedPath, error);
	detachDiskImage(mountPoint);
	return copied;
}

[[maybe_unused]] QString stageMacPackage(const UpdatePreparationInput& input, QString* error) {
	QString stagedPath = QDir(input.stagingRoot).filePath(QStringLiteral("QEGTRAIN.app"));
	// Releases before the disk image publish a zip package.
	const bool diskImage = input.packagePath.endsWith(QStringLiteral(".dmg"), Qt::CaseInsensitive);
	if (!(diskImage ? copyMacAppFromDiskImage(input, stagedPath, error)
					: copyMacAppFromZip(input, stagedPath, error)))
		return {};
	if (!runProcess(QStringLiteral("/usr/bin/codesign"),
			{QStringLiteral("--verify"), QStringLiteral("--deep"), QStringLiteral("--strict"), stagedPath},
			60000, error))
		return {};
	const std::optional<QString> version = macBundleVersion(stagedPath);
	if (!version || *version != input.manifest.version) {
		if (error)
			*error = QStringLiteral("The macOS update app version does not match the release manifest.");
		return {};
	}
	return stagedPath;
}

[[maybe_unused]] QString stageWindowsPackage(const UpdatePreparationInput& input, QString* error) {
	const QString root = input.stagingRoot;
	const QString extract = QDir(root).filePath(QStringLiteral("extract"));
	if (!QDir().mkpath(extract))
		return {};
	QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	environment.insert(QStringLiteral("EGTRAIN_UPDATE_ZIP"), input.packagePath);
	environment.insert(QStringLiteral("EGTRAIN_UPDATE_DEST"), extract);
	const QString script = QStringLiteral(
		"$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath $env:EGTRAIN_UPDATE_ZIP "
		"-DestinationPath $env:EGTRAIN_UPDATE_DEST -Force");
	QString detail;
	if (!runProcess(QStringLiteral("powershell.exe"),
			{QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"), script},
			kUnpackTimeoutMs, &detail, &environment)) {
		if (error)
			*error = unpackFailureMessage(detail);
		return {};
	}
	QString stagedPath = QDir(root).filePath(QStringLiteral("QEGTRAIN"));
	if (!WindowsStaging::buildStage(extract, stagedPath, input.manifest.files, error))
		return {};
	return stagedPath;
}

[[maybe_unused]] QString stageLinuxPackage(const UpdatePreparationInput& input, QString* error) {
	const QString root = input.stagingRoot;
	QString stagedPath = QDir(root).filePath(QStringLiteral("ready.AppImage"));
	if (!QFile::copy(input.packagePath, stagedPath)) {
		if (error)
			*error = QStringLiteral("Could not stage the AppImage.");
		return {};
	}
	QFile::Permissions permissions = QFileInfo(input.currentPath).permissions();
	permissions |= QFile::ExeOwner;
	if (!QFile::setPermissions(stagedPath, permissions) || !validElf(stagedPath)) {
		if (error)
			*error = QStringLiteral("The downloaded AppImage is structurally invalid.");
		return {};
	}
	return stagedPath;
}

} // namespace

QString stageUpdatePackage(const UpdatePreparationInput& input, QString* error) {
	if (input.installer)
		return input.packagePath;
#if defined(Q_OS_MACOS)
	return stageMacPackage(input, error);
#elif defined(Q_OS_WIN)
	return stageWindowsPackage(input, error);
#elif defined(Q_OS_LINUX)
	return stageLinuxPackage(input, error);
#else
	if (error)
		*error = QStringLiteral("Self-update is not supported on this platform.");
	return {};
#endif
}

QString unpackFailureMessage(const QString& detail) {
	QString message = QStringLiteral(
		"EGTRAIN could not unpack the downloaded update. Nothing was changed. "
		"Choose Check for Updates in the Help menu to try again.");
	if (!detail.isEmpty())
		message += QStringLiteral("\n\nDetails: %1").arg(detail);
	return message;
}

bool canCreateFilesIn(const QString& directory) {
	if (!QFileInfo(directory).isDir())
		return false;
	QTemporaryFile probe(QDir(directory).filePath(QStringLiteral(".qegtrain-write-test-XXXXXX")));
	return probe.open() && probe.remove();
}

bool isReadOnlyLocation(const QString& path) {
	if (path.contains(QStringLiteral("/AppTranslocation/")))
		return true;
#if defined(Q_OS_UNIX)
	// The kernel answers for the volume that holds the path, also for the folders of the user
	// volume that macOS links into the read-only root.
	struct statvfs volume;
	return statvfs(QFile::encodeName(path).constData(), &volume) == 0 && (volume.f_flag & ST_RDONLY) != 0;
#else
	return false;
#endif
}

bool verifyDownloadedPackageHash(const QString& packagePath,
	const QString& expectedSha256, QString* error) {
	QFile package(packagePath);
	if (!package.open(QIODevice::ReadOnly)) {
		if (error)
			*error = QStringLiteral("Could not read the downloaded update.");
		return false;
	}
	QCryptographicHash hash(QCryptographicHash::Sha256);
	while (!package.atEnd())
		hash.addData(package.read(kHashChunkBytes));
	if (QString::fromLatin1(hash.result().toHex()) != expectedSha256) {
		if (error)
			*error = QStringLiteral("The downloaded update failed its SHA-256 check.");
		return false;
	}
	return true;
}

void removeStaleUpdateStaging(const QString& installationParent, std::chrono::minutes minimumAge) {
	const QStringList pattern = {QStringLiteral(".qegtrain-update-*")};
	// Without CaseSensitive the name filter would also match a folder that differs in case.
	const QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks | QDir::Hidden | QDir::CaseSensitive;
	const QFileInfoList folders = QDir(installationParent).entryInfoList(pattern, filters);
	const QDateTime limit = QDateTime::currentDateTime().addSecs(-std::chrono::seconds(minimumAge).count());
	for (const QFileInfo& folder : folders) {
		const QDateTime newest = newestModification(folder.absoluteFilePath());
		if (newest.isValid() && newest <= limit)
			QDir(folder.absoluteFilePath()).removeRecursively();
	}
}

void UpdatePreparationWorker::setPlatformStager(PlatformStager stager) {
	m_platformStager = std::move(stager);
}

void UpdatePreparationWorker::prepare(const UpdatePreparationInput& input) {
	UpdatePreparationResult result;
	QString error;
	QString stagedPath;
	if (verifyDownloadedPackageHash(input.packagePath, input.manifest.sha256, &error)) {
		if (m_platformStager)
			stagedPath = m_platformStager(input, &error);
		else
			stagedPath = stageUpdatePackage(input, &error);
	}
	result.success = !stagedPath.isEmpty();
	result.error = std::move(error);
	result.stagedPath = std::move(stagedPath);
	emit finished(result);
}
