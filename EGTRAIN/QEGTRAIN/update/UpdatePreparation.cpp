#include "update/UpdatePreparation.h"

#include "update/WindowsStaging.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QXmlStreamReader>

namespace {

constexpr qint64 kHashChunkBytes = 1024 * 1024;

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

QString stageMacPackage(const UpdatePreparationInput& input, QString* error) {
	const QString root = input.stagingRoot;
	const QString extract = QDir(root).filePath(QStringLiteral("extract"));
	if (!QDir().mkpath(extract))
		return {};
	if (!runProcess(QStringLiteral("/usr/bin/ditto"),
		{QStringLiteral("-x"), QStringLiteral("-k"), input.packagePath, extract}, 60000, error))
		return {};
	const QString source = QDir(extract).filePath(QStringLiteral("QEGTRAIN-Lebanon/QEGTRAIN.app"));
	const QFileInfo app(source);
	if (!app.isDir() || app.fileName() != QStringLiteral("QEGTRAIN.app")
		|| !QFileInfo(QDir(source).filePath("Contents/MacOS/QEGTRAIN")).isFile()
		|| !QFileInfo(QDir(source).filePath("Contents/Info.plist")).isFile()) {
		if (error)
			*error = QStringLiteral("The macOS update package has an invalid app bundle.");
		return {};
	}
	const QString stagedPath = QDir(root).filePath(QStringLiteral("QEGTRAIN.app"));
	if (!runProcess(QStringLiteral("/usr/bin/ditto"), {source, stagedPath}, 60000, error))
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

QString stageWindowsPackage(const UpdatePreparationInput& input, QString* error) {
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
	if (!runProcess(QStringLiteral("powershell.exe"),
		{QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"), script},
		60000, error, &environment))
		return {};
	const QString stagedPath = QDir(root).filePath(QStringLiteral("QEGTRAIN"));
	if (!WindowsStaging::buildStage(extract, stagedPath, error))
		return {};
	return stagedPath;
}

QString stageLinuxPackage(const UpdatePreparationInput& input, QString* error) {
	const QString root = input.stagingRoot;
	const QString stagedPath = QDir(root).filePath(QStringLiteral("QEGTRAIN-linux-x86_64.AppImage"));
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

QString stagePlatformPackage(const UpdatePreparationInput& input, QString* error) {
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

} // namespace

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

void UpdatePreparationWorker::setPlatformStager(PlatformStager stager) {
	m_platformStager = std::move(stager);
}

void UpdatePreparationWorker::prepare(UpdatePreparationInput input) {
	UpdatePreparationResult result;
	QString error;
	QString stagedPath;
	if (verifyDownloadedPackageHash(input.packagePath, input.manifest.sha256, &error)) {
		if (m_platformStager)
			stagedPath = m_platformStager(input, &error);
		else
			stagedPath = stagePlatformPackage(input, &error);
	}
	result.success = !stagedPath.isEmpty();
	result.error = error;
	result.stagedPath = stagedPath;
	emit finished(result);
}
