#include "update/ReleaseInfo.h"
#include "update/UpdateSettings.h"

#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static QByteArray releaseJson(const char* tag, bool draft = false, bool prerelease = false) {
	return QByteArray("[{\"tag_name\":\"") + tag
		+ "\",\"draft\":" + (draft ? "true" : "false")
		+ ",\"prerelease\":" + (prerelease ? "true" : "false")
		+ ",\"html_url\":\"https://github.com/Ancientkingg/EGTRAIN/releases/tag/"
		+ tag + "\",\"body\":\"Notes\"}]";
}

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid())
		return 1;
	QSettings settings(temp.filePath(QStringLiteral("updates.ini")), QSettings::IniFormat);
	settings.clear();

	bool ok = true;
	ok &= expect(readUpdateCheckState(settings) == UpdateCheckState::Unknown,
		"missing preference is unknown");
	writeUpdateCheckState(settings, UpdateCheckState::Enabled);
	ok &= expect(readUpdateCheckState(settings) == UpdateCheckState::Enabled,
		"enabled preference round-trips");
	writeUpdateCheckState(settings, UpdateCheckState::Disabled);
	ok &= expect(readUpdateCheckState(settings) == UpdateCheckState::Disabled,
		"stop checking persists disabled");
	writeUpdateCheckState(settings, UpdateCheckState::Enabled);
	ok &= expect(readUpdateCheckState(settings) == UpdateCheckState::Enabled,
		"automatic checking can be re-enabled");
	ok &= expect(!shouldCheckForUpdates(UpdateCheckState::Disabled, false)
		&& shouldCheckForUpdates(UpdateCheckState::Disabled, true),
		"manual checks remain available while automatic checks are disabled");
	// Each variable alone suppresses update UI and network; the test starts from none of them set.
	const char* const suppressing[] = {"QEGTRAIN_DISABLE_UPDATES", "QEGTRAIN_AUTOSTART",
		"QEGTRAIN_STARTUP_TIMING", "QEGTRAIN_PLAYBACK_PROFILE", "QEGTRAIN_E2E_SCENE_DROP"};
	QByteArray previous[5];
	for (int i = 0; i < 5; ++i) {
		previous[i] = qgetenv(suppressing[i]);
		qunsetenv(suppressing[i]);
	}
	// Another end-to-end hook in the caller's environment suppresses updates too; the two
	// negative checks only hold without one.
	bool otherHook = false;
	for (const QString& key : QProcessEnvironment::systemEnvironment().keys())
		otherHook = otherHook || key.startsWith(QStringLiteral("QEGTRAIN_E2E_"));
	ok &= expect(otherHook || !updatesSuppressedByEnvironment(), "an interactive launch does not suppress updates");
	for (const char* name : suppressing) {
		qputenv(name, "1");
		ok &= expect(updatesSuppressedByEnvironment(), "a scripted launch suppresses update UI and network");
		qunsetenv(name);
	}
	qputenv("QEGTRAIN_STARTUP_TIMING", "0");
	qputenv("QEGTRAIN_PLAYBACK_PROFILE", "0");
	ok &= expect(otherHook || !updatesSuppressedByEnvironment(),
		"a measurement mode that is switched off does not suppress updates");
	qunsetenv("QEGTRAIN_STARTUP_TIMING");
	qunsetenv("QEGTRAIN_PLAYBACK_PROFILE");
	for (int i = 0; i < 5; ++i)
		if (!previous[i].isNull())
			qputenv(suppressing[i], previous[i]);

	const auto current = parseStableVersion("1.9.0");
	const auto release = parseLatestStableRelease(releaseJson("v1.10.0"));
	ok &= expect(current && release && isUpdateAvailable(*current, *release),
		"release comparison uses numeric version components");
	ok &= expect(release && !isUpdateAvailable(release->version, *release), "same version is up to date");

	for (const QByteArray& malformed : {QByteArray("not json"), QByteArray("{}"),
		QByteArray("[{\"tag_name\":\"v1.0.0\",\"draft\":false,\"prerelease\":true}]"),
		QByteArray("[{\"tag_name\":\"v1.0.0\",\"draft\":true,\"prerelease\":false}]"),
		QByteArray("[{\"tag_name\":\"1.0.0\",\"draft\":false,\"prerelease\":false}]"),
		QByteArray("[{\"tag_name\":\"v1.0\",\"draft\":false,\"prerelease\":false}]"),
		QByteArray("[{\"tag_name\":\"v1.0.0\",\"draft\":false}]")}) {
		ok &= expect(!parseLatestStableRelease(malformed), "invalid release is ignored");
	}

	QByteArray releaseList = releaseJson("v1.9.9");
	releaseList.chop(1);
	releaseList += ',';
	releaseList += releaseJson("v1.10.0").mid(1);
	const auto releases = parseLatestStableRelease(releaseList);
	ok &= expect(releases && releases->version.major == 1 && releases->version.minor == 10,
		"latest stable release is selected without package information");
	const auto wrongPage = parseLatestStableRelease(
		QByteArray("[{\"tag_name\":\"v1.10.0\",\"draft\":false,\"prerelease\":false,"
			"\"html_url\":\"https://example.com/phishing\"}]") );
	ok &= expect(wrongPage && wrongPage->releasePage.host() == QStringLiteral("github.com")
		&& wrongPage->releasePage.path().startsWith(QStringLiteral("/Ancientkingg/EGTRAIN/")),
		"release page is constrained to the expected GitHub repository");
	const QByteArray packagedRelease = QByteArray(
		"[{\"tag_name\":\"v1.10.0\",\"draft\":false,\"prerelease\":false,"
		"\"html_url\":\"https://github.com/Ancientkingg/EGTRAIN/releases/tag/v1.10.0\","
		"\"assets\":["
		"{\"name\":\"update-manifest.json\",\"browser_download_url\":\"https://github.com/Ancientkingg/EGTRAIN/releases/download/v1.10.0/update-manifest.json\"},"
		"{\"name\":\"QEGTRAIN-linux-x86_64.AppImage\",\"browser_download_url\":\"https://github.com/Ancientkingg/EGTRAIN/releases/download/v1.10.0/QEGTRAIN-linux-x86_64.AppImage\"},"
		"{\"name\":\"untrusted.zip\",\"browser_download_url\":\"https://example.com/untrusted.zip\"}]}]");
	const auto packaged = parseLatestStableRelease(packagedRelease);
	ok &= expect(packaged && packaged->asset(updateManifestAssetName())
		&& packaged->asset(updatePackageName(QStringLiteral("linux-x86_64"))),
		"only exact release package assets are retained");
	ok &= expect(packaged && !packaged->asset(QStringLiteral("untrusted.zip")),
		"untrusted release assets are ignored");
	const QByteArray validHash(64, 'a');
	const QByteArray manifest = QByteArray("{\"version\":\"1.10.0\",\"assets\":{\"linux-x86_64\":{\"name\":\"QEGTRAIN-linux-x86_64.AppImage\",\"sha256\":\"")
		+ validHash + QByteArray("\",\"size\":12345}}}");
	const auto parsedManifest = parseUpdateManifest(manifest, QStringLiteral("v1.10.0"),
		QStringLiteral("linux-x86_64"));
	ok &= expect(parsedManifest && parsedManifest->sha256 == QString::fromLatin1(validHash),
		"valid update manifest parses");
	ok &= expect(parsedManifest && parsedManifest->assetSize == 12345,
		"manifest package size is retained");
	ok &= expect(!parseUpdateManifest(manifest, QStringLiteral("v1.10.1"),
		QStringLiteral("linux-x86_64")), "manifest version must equal the stable tag");
	QByteArray uppercaseManifest = manifest;
	uppercaseManifest.replace("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
	ok &= expect(!parseUpdateManifest(uppercaseManifest,
		QStringLiteral("v1.10.0"), QStringLiteral("linux-x86_64")),
		"manifest hash must be lowercase hexadecimal");
	return ok ? 0 : 1;
}
