#include "update/SelfUpdater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#ifndef EGTRAIN_PACKAGED_BUILD
#define EGTRAIN_PACKAGED_BUILD 0
#endif

namespace {

constexpr int kDownloadTimeoutMs = 30000;
constexpr qint64 kMaxManifestBytes = 128 * 1024;

QString executableName() {
#if defined(Q_OS_WIN)
	return QStringLiteral("QEGTRAIN.exe");
#else
	return QStringLiteral("QEGTRAIN");
#endif
}

QString helperName() {
#if defined(Q_OS_WIN)
	return QStringLiteral("egtrain_update_helper.exe");
#else
	return QStringLiteral("egtrain_update_helper");
#endif
}

bool writableParent(const QString& path) {
	const QFileInfo info(path);
	const QFileInfo parent(info.absolutePath());
	return parent.exists() && parent.isDir() && parent.isWritable();
}

bool copyTree(const QString& sourcePath, const QString& destinationPath) {
	const QFileInfo source(sourcePath);
	if (!source.isDir() || source.isSymLink())
		return false;
	if (!QDir().mkpath(destinationPath))
		return false;
	const QDir sourceDir(sourcePath);
	const QFileInfoList entries = sourceDir.entryInfoList(
		QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System,
		QDir::DirsFirst | QDir::Name);
	for (const QFileInfo& entry : entries) {
		if (entry.isSymLink())
			return false;
		const QString destination = QDir(destinationPath).filePath(entry.fileName());
		if (entry.isDir()) {
			if (!copyTree(entry.absoluteFilePath(), destination))
				return false;
			continue;
		}
		if (!entry.isFile())
			return false;
		QFile::remove(destination);
		if (!QFile::copy(entry.absoluteFilePath(), destination))
			return false;
	}
	return true;
}

bool cleanWindowsRuntime(const QString& directory) {
	QDir root(directory);
	for (const QFileInfo& dll : root.entryInfoList({QStringLiteral("*.dll")}, QDir::Files))
		if (!QFile::remove(dll.absoluteFilePath()))
			return false;
	for (const QString& executable : {QStringLiteral("QEGTRAIN.exe"),
		QStringLiteral("scene_tool.exe"), QStringLiteral("egtrain_update_helper.exe")})
		if (QFileInfo::exists(root.filePath(executable)) && !QFile::remove(root.filePath(executable)))
			return false;
	for (const QString& pluginDirectory : {QStringLiteral("platforms"), QStringLiteral("imageformats"),
		QStringLiteral("iconengines"), QStringLiteral("styles"), QStringLiteral("tls"),
		QStringLiteral("bearer")}) {
		const QString path = root.filePath(pluginDirectory);
		if (QFileInfo::exists(path) && !QDir(path).removeRecursively())
			return false;
	}
	return true;
}

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

#if defined(Q_OS_MACOS)
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
#endif

} // namespace

SelfUpdater::SelfUpdater(QObject* parent)
	: QObject(parent), m_network(this), m_timeout(this) {
	m_timeout.setSingleShot(true);
	connect(&m_timeout, &QTimer::timeout, this, [this]() {
		if (!m_reply)
			return;
		m_cancelRequested = false;
		QNetworkReply* reply = m_reply;
		m_reply = nullptr;
		reply->abort();
		reply->deleteLater();
		fail(QStringLiteral("The update download timed out."));
	});
}

SelfUpdateCapability SelfUpdater::capability() const {
	SelfUpdateCapability result;
#if !EGTRAIN_PACKAGED_BUILD
	result.reason = QStringLiteral("Self-update is available only in packaged release builds.");
	return result;
#else
	const QString applicationPath = QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath();
	result.launchPath = applicationPath;
	result.helperPath = QDir(QCoreApplication::applicationDirPath()).filePath(helperName());

#if defined(Q_OS_MACOS)
	const QString applicationDirectory = QFileInfo(applicationPath).absolutePath();
	const QString bundlePath = QDir(applicationDirectory).filePath("../..");
	const QFileInfo bundleInfo(QDir(bundlePath).absolutePath());
	result.currentPath = bundleInfo.absoluteFilePath();
	if (!bundleInfo.isDir() || bundleInfo.fileName() != QStringLiteral("QEGTRAIN.app")) {
		result.reason = QStringLiteral("The running application is not a packaged macOS app.");
		return result;
	}
	if (!QFileInfo(QDir(result.currentPath).filePath("Contents/MacOS/QEGTRAIN")).isFile()
		|| !QFileInfo(QDir(result.currentPath).filePath("Contents/Info.plist")).isFile()) {
		result.reason = QStringLiteral("The packaged macOS app is incomplete.");
		return result;
	}
#elif defined(Q_OS_WIN)
	result.currentPath = QFileInfo(applicationPath).absolutePath();
	if (QFileInfo(applicationPath).fileName().compare(executableName(), Qt::CaseInsensitive) != 0
		|| !QFileInfo(applicationPath).isFile()) {
		result.reason = QStringLiteral("The running application is not a packaged Windows release.");
		return result;
	}
#elif defined(Q_OS_LINUX)
	const QString appImage = qEnvironmentVariable("APPIMAGE");
	const QFileInfo appImageInfo(appImage);
	const QString resolved = appImageInfo.canonicalFilePath();
	if (resolved.isEmpty() || !QFileInfo(resolved).isFile()) {
		result.reason = QStringLiteral("The application is not running from an AppImage.");
		return result;
	}
	result.currentPath = resolved;
	result.launchPath = resolved;
#else
	result.reason = QStringLiteral("Self-update is not supported on this platform.");
	return result;
#endif

	if (!QFileInfo(result.helperPath).isFile()) {
		result.reason = QStringLiteral("The packaged update helper is missing.");
		return result;
	}
	if (!writableParent(result.currentPath)) {
		result.reason = QStringLiteral("The packaged installation is not writable.");
		return result;
	}
	result.supported = true;
	return result;
#endif
}

bool SelfUpdater::canSelfUpdate(const StableRelease& release) const {
	const SelfUpdateCapability current = capability();
	return current.supported && release.asset(updateManifestAssetName())
		&& release.asset(updatePackageName());
}

void SelfUpdater::start(const StableRelease& release) {
	if (m_busy)
		return;
	const SelfUpdateCapability current = capability();
	if (!current.supported) {
		emit finished(false, current.reason);
		return;
	}
	if (!release.asset(updateManifestAssetName()) || !release.asset(updatePackageName())) {
		emit finished(false, QStringLiteral("This release does not provide a verified update package."));
		return;
	}
	m_busy = true;
	m_ready = false;
	m_cancelRequested = false;
	m_release = release;
	m_currentPath = current.currentPath;
	m_launchPath = current.launchPath;
	m_helperPath = current.helperPath;
	m_stagingRoot.clear();
	m_stagedPath.clear();
	m_manifestData.clear();
	requestManifest();
}

void SelfUpdater::requestManifest() {
	const ReleaseAsset* asset = m_release.asset(updateManifestAssetName());
	if (!asset) {
		fail(QStringLiteral("The release manifest is missing."));
		return;
	}
	QNetworkRequest request(asset->downloadUrl);
	request.setHeader(QNetworkRequest::UserAgentHeader,
		QStringLiteral("EGTRAIN/%1").arg(QCoreApplication::applicationVersion()));
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	m_manifestData.clear();
	QNetworkReply* reply = m_network.get(request);
	m_reply = reply;
	m_timeout.start(kDownloadTimeoutMs);
	connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
		if (reply != m_reply)
			return;
		m_timeout.start(kDownloadTimeoutMs);
		m_manifestData += reply->readAll();
		if (m_manifestData.size() > kMaxManifestBytes)
			fail(QStringLiteral("The update manifest is too large."));
	});
	connect(reply, &QNetworkReply::downloadProgress, this,
		[this, reply](qint64, qint64) {
			if (reply == m_reply)
				m_timeout.start(kDownloadTimeoutMs);
		});
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		if (reply == m_reply)
			handleReply(reply, true);
	});
}

void SelfUpdater::requestPackage(const UpdateManifest& manifest) {
	const ReleaseAsset* asset = m_release.asset(manifest.assetName);
	if (!asset || !isExpectedReleaseAssetUrl(m_release.tag, manifest.assetName, asset->downloadUrl)) {
		fail(QStringLiteral("The release package URL is not trusted."));
		return;
	}
	QTemporaryDir staging(QDir(QFileInfo(m_currentPath).absolutePath()).filePath(
		QStringLiteral(".qegtrain-update-XXXXXX")));
	if (!staging.isValid()) {
		fail(QStringLiteral("Could not create update staging storage."));
		return;
	}
	staging.setAutoRemove(false);
	m_stagingRoot = staging.path();
	const QString packagePath = QDir(m_stagingRoot).filePath(manifest.assetName);
	m_packageFile.setFileName(packagePath);
	if (!m_packageFile.open(QIODevice::WriteOnly)) {
		fail(QStringLiteral("Could not create the update download file."));
		return;
	}
	m_manifest = manifest;
	m_packageBytes = 0;
	QNetworkRequest request(asset->downloadUrl);
	request.setHeader(QNetworkRequest::UserAgentHeader,
		QStringLiteral("EGTRAIN/%1").arg(QCoreApplication::applicationVersion()));
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	QNetworkReply* reply = m_network.get(request);
	m_reply = reply;
	m_timeout.start(kDownloadTimeoutMs);
	connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
		if (reply != m_reply)
			return;
		m_timeout.start(kDownloadTimeoutMs);
		const QByteArray data = reply->readAll();
		if (data.size() > m_manifest.assetSize - m_packageBytes) {
			fail(QStringLiteral("The update package is larger than the release manifest."));
			return;
		}
		if (m_packageFile.write(data) != data.size()) {
			fail(QStringLiteral("Could not save the downloaded update."));
			return;
		}
		m_packageBytes += data.size();
	});
	connect(reply, &QNetworkReply::downloadProgress, this,
		[this, reply](qint64 received, qint64 total) {
			if (reply != m_reply)
				return;
			m_timeout.start(kDownloadTimeoutMs);
			emit progress(received, total);
		});
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		if (reply == m_reply)
			handleReply(reply, false);
	});
}

void SelfUpdater::handleReply(QNetworkReply* reply, bool manifestReply) {
	if (reply != m_reply)
		return;
	m_reply = nullptr;
	m_timeout.stop();
	if (m_cancelRequested) {
		reply->deleteLater();
		fail(QStringLiteral("Update cancelled."));
		return;
	}
	if (reply->error() != QNetworkReply::NoError) {
		const QString error = reply->errorString();
		reply->deleteLater();
		fail(error.isEmpty() ? QStringLiteral("The update download failed.") : error);
		return;
	}
	const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (status < 200 || status >= 300) {
		reply->deleteLater();
		fail(QStringLiteral("GitHub returned HTTP %1 while downloading the update.").arg(status));
		return;
	}
	if (manifestReply) {
		const qint64 contentLength = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
		if (contentLength > kMaxManifestBytes) {
			reply->deleteLater();
			fail(QStringLiteral("The update manifest is too large."));
			return;
		}
		m_manifestData += reply->readAll();
		if (m_manifestData.size() > kMaxManifestBytes) {
			reply->deleteLater();
			fail(QStringLiteral("The update manifest is too large."));
			return;
		}
		QString error;
		const std::optional<UpdateManifest> manifest = parseUpdateManifest(
			m_manifestData, m_release.tag, updatePlatformKey(), &error);
		m_manifestData.clear();
		reply->deleteLater();
		if (!manifest) {
			fail(error.isEmpty() ? QStringLiteral("The update manifest is invalid.") : error);
			return;
		}
		requestPackage(*manifest);
		return;
	}
	const QByteArray tail = reply->readAll();
	if (tail.size() > m_manifest.assetSize - m_packageBytes) {
		reply->deleteLater();
		fail(QStringLiteral("The update package is larger than the release manifest."));
		return;
	}
	if (m_packageFile.write(tail) != tail.size()) {
		reply->deleteLater();
		fail(QStringLiteral("Could not save the downloaded update."));
		return;
	}
	m_packageBytes += tail.size();
	m_packageFile.close();
	reply->deleteLater();
	if (m_packageBytes != m_manifest.assetSize) {
		fail(QStringLiteral("The update package size does not match the release manifest."));
		return;
	}
	QFile package(m_packageFile.fileName());
	if (!package.open(QIODevice::ReadOnly)) {
		fail(QStringLiteral("Could not read the downloaded update."));
		return;
	}
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&package)) {
		fail(QStringLiteral("Could not verify the downloaded update."));
		return;
	}
	if (QString::fromLatin1(hash.result().toHex()) != m_manifest.sha256) {
		fail(QStringLiteral("The downloaded update failed its SHA-256 check."));
		return;
	}
	QString error;
	if (!stagePackage(package.fileName(), &error)) {
		fail(error.isEmpty() ? QStringLiteral("The update package is incomplete.") : error);
		return;
	}
	m_busy = false;
	m_ready = true;
	emit finished(true, QString());
}

void SelfUpdater::fail(const QString& error) {
	if (m_reply) {
		QNetworkReply* reply = m_reply;
		m_reply = nullptr;
		reply->abort();
		reply->deleteLater();
	}
	m_timeout.stop();
	if (m_packageFile.isOpen())
		m_packageFile.close();
	clearStaging();
	m_busy = false;
	m_ready = false;
	m_cancelRequested = false;
	emit finished(false, error);
}

void SelfUpdater::cancel() {
	if (!m_busy)
		return;
	m_cancelRequested = true;
	if (m_reply) {
		m_reply->abort();
		return;
	}
	fail(QStringLiteral("Update cancelled."));
}

bool SelfUpdater::stagePackage(const QString& packagePath, QString* error) {
	if (m_stagingRoot.isEmpty()) {
		if (error)
			*error = QStringLiteral("Update staging storage is unavailable.");
		return false;
	}
#if defined(Q_OS_MACOS)
	return stageMacPackage(packagePath, error);
#elif defined(Q_OS_WIN)
	return stageWindowsPackage(packagePath, error);
#elif defined(Q_OS_LINUX)
	return stageLinuxPackage(packagePath, error);
#else
	Q_UNUSED(packagePath);
	if (error)
		*error = QStringLiteral("Self-update is not supported on this platform.");
	return false;
#endif
}

bool SelfUpdater::stageMacPackage(const QString& packagePath, QString* error) {
	const QString& root = m_stagingRoot;
	const QString extract = QDir(root).filePath(QStringLiteral("extract"));
	if (!QDir().mkpath(extract))
		return false;
	if (!runProcess(QStringLiteral("/usr/bin/ditto"),
		{QStringLiteral("-x"), QStringLiteral("-k"), packagePath, extract}, 60000, error))
		return false;
	const QString source = QDir(extract).filePath(QStringLiteral("QEGTRAIN-Lebanon/QEGTRAIN.app"));
	const QFileInfo app(source);
	if (!app.isDir() || app.fileName() != QStringLiteral("QEGTRAIN.app")
		|| !QFileInfo(QDir(source).filePath("Contents/MacOS/QEGTRAIN")).isFile()
		|| !QFileInfo(QDir(source).filePath("Contents/Info.plist")).isFile()) {
		if (error)
			*error = QStringLiteral("The macOS update package has an invalid app bundle.");
		return false;
	}
	m_stagedPath = QDir(root).filePath(QStringLiteral("QEGTRAIN.app"));
	if (!runProcess(QStringLiteral("/usr/bin/ditto"), {source, m_stagedPath}, 60000, error))
		return false;
	if (!runProcess(QStringLiteral("/usr/bin/codesign"),
		{QStringLiteral("--verify"), QStringLiteral("--deep"), QStringLiteral("--strict"), m_stagedPath},
		60000, error))
		return false;
	const std::optional<QString> version = macBundleVersion(m_stagedPath);
	if (!version || *version != m_manifest.version) {
		if (error)
			*error = QStringLiteral("The macOS update app version does not match the release manifest.");
		return false;
	}
	return true;
}

bool SelfUpdater::stageWindowsPackage(const QString& packagePath, QString* error) {
	const QString& root = m_stagingRoot;
	const QString extract = QDir(root).filePath(QStringLiteral("extract"));
	if (!QDir().mkpath(extract))
		return false;
	QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	environment.insert(QStringLiteral("EGTRAIN_UPDATE_ZIP"), packagePath);
	environment.insert(QStringLiteral("EGTRAIN_UPDATE_DEST"), extract);
	const QString script = QStringLiteral(
		"$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath $env:EGTRAIN_UPDATE_ZIP "
		"-DestinationPath $env:EGTRAIN_UPDATE_DEST -Force");
	if (!runProcess(QStringLiteral("powershell.exe"),
		{QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"), script},
		60000, error, &environment))
		return false;
	if (!QFileInfo(QDir(extract).filePath(executableName())).isFile()) {
		if (error)
			*error = QStringLiteral("The Windows update package does not contain QEGTRAIN.exe.");
		return false;
	}
	m_stagedPath = QDir(root).filePath(QStringLiteral("QEGTRAIN"));
	if (!copyTree(m_currentPath, m_stagedPath) || !cleanWindowsRuntime(m_stagedPath)) {
		if (error)
			*error = QStringLiteral("Could not stage the Windows update without touching user files.");
		return false;
	}
	// Keep any case studies stored beside the portable application. Application
	// binaries and runtime plugins still come exclusively from the new package.
	if (QFileInfo(QDir(m_currentPath).filePath(QStringLiteral("Scenes"))).isDir()) {
		const QString packagedScenes = QDir(extract).filePath(QStringLiteral("Scenes"));
		const QFileInfo packagedScenesInfo(packagedScenes);
		if (packagedScenesInfo.isSymLink()
			|| (packagedScenesInfo.exists() && !QDir(packagedScenes).removeRecursively())) {
			if (error)
				*error = QStringLiteral("Could not preserve portable case studies while staging the update.");
			return false;
		}
	}
	if (!copyTree(extract, m_stagedPath)) {
		if (error)
			*error = QStringLiteral("Could not finish staging the Windows update.");
		return false;
	}
	if (!QFileInfo(QDir(m_stagedPath).filePath(executableName())).isFile()
		|| !QFileInfo(QDir(m_stagedPath).filePath(QStringLiteral("Qt5Network.dll"))).isFile()
		|| !QFileInfo(QDir(m_stagedPath).filePath(QStringLiteral("platforms/qwindows.dll"))).isFile()) {
		if (error)
			*error = QStringLiteral("The Windows update package is missing required runtime files.");
		return false;
	}
	return true;
}

bool SelfUpdater::stageLinuxPackage(const QString& packagePath, QString* error) {
	const QString& root = m_stagingRoot;
	m_stagedPath = QDir(root).filePath(QStringLiteral("QEGTRAIN-linux-x86_64.AppImage"));
	if (!QFile::copy(packagePath, m_stagedPath)) {
		if (error)
			*error = QStringLiteral("Could not stage the AppImage.");
		return false;
	}
	QFile::Permissions permissions = QFileInfo(m_currentPath).permissions();
	permissions |= QFile::ExeOwner;
	if (!QFile::setPermissions(m_stagedPath, permissions) || !validElf(m_stagedPath)) {
		if (error)
			*error = QStringLiteral("The downloaded AppImage is structurally invalid.");
		return false;
	}
	return true;
}

void SelfUpdater::clearStaging() {
	if (!m_stagingRoot.isEmpty())
		QDir(m_stagingRoot).removeRecursively();
	m_stagingRoot.clear();
	m_stagedPath.clear();
}

bool SelfUpdater::restart() {
	if (!m_ready || m_stagedPath.isEmpty())
		return false;
	const QString helperCopy = QDir(m_stagingRoot).filePath(helperName());
	QFile::remove(helperCopy);
	if (!QFile::copy(m_helperPath, helperCopy))
		return false;
#if !defined(Q_OS_WIN)
	QFile::setPermissions(helperCopy, QFileInfo(m_helperPath).permissions()
		| QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
#endif
	QString backup = m_currentPath + QStringLiteral(".egtrain-old");
	const QFileInfo previousBackup(backup);
	if (previousBackup.isSymLink())
		return false;
	if (previousBackup.exists()) {
		const bool removed = previousBackup.isDir()
			? QDir(backup).removeRecursively() : QFile::remove(backup);
		if (!removed)
			return false;
	}
	const QStringList arguments = {
		QStringLiteral("--parent-pid"), QString::number(QCoreApplication::applicationPid()),
		QStringLiteral("--current"), m_currentPath,
		QStringLiteral("--staged"), m_stagedPath,
		QStringLiteral("--backup"), backup,
		QStringLiteral("--launch"), m_launchPath};
	if (!QProcess::startDetached(helperCopy, arguments)) {
		clearStaging();
		m_ready = false;
		return false;
	}
	return true;
}
