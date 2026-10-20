#include "update/ReleaseInfo.h"
#include "update/UpdateChecker.h"
#include "update/UpdateSettings.h"

#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QEventLoop>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <deque>
#include <iostream>
#include <string>
#include <utility>

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

// Runs the event loop for the given time.
static void spin(int milliseconds) {
	QEventLoop loop;
	QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
	loop.exec();
}

// What a fake request recorded. It outlives the reply, which the checker releases with deleteLater().
struct FakeCall {
	HttpRequest request;
	int aborts = 0;
};

class FakeReply : public HttpReply {
public:
	FakeReply(FakeCall* call, QObject* parent) : HttpReply(parent), m_call(call) {}

	QByteArray readAll() override { return std::exchange(body, QByteArray()); }
	void abort() override {
		++m_call->aborts;
		emit finished();
	}
	bool failed() const override { return failure; }
	QString errorString() const override { return error; }
	int statusCode() const override { return status; }
	qint64 contentLength() const override { return body.size(); }

	int status = 0;
	bool failure = false;
	QString error;
	QByteArray body;

private:
	FakeCall* m_call;
};

class FakeTransport : public HttpTransport {
public:
	HttpReply* get(const HttpRequest& request) override {
		calls.push_back(FakeCall{request, 0});
		auto* reply = new FakeReply(&calls.back(), this);
		replies.append(reply);
		return reply;
	}

	std::deque<FakeCall> calls;
	QList<QPointer<FakeReply>> replies;
};

// The reply of the given request, or null once the checker has released it.
static FakeReply* replyAt(const FakeTransport& transport, int index) {
	return index < transport.replies.size() ? transport.replies.at(index).data() : nullptr;
}

struct Answer {
	int status = 0;
	bool failure = false;
	QString error;
	QByteArray body;
};

// Finishes the reply of the given request with the answer.
static bool finishReply(const FakeTransport& transport, int index, const Answer& response) {
	FakeReply* reply = replyAt(transport, index);
	if (!reply)
		return false;
	reply->status = response.status;
	reply->failure = response.failure;
	reply->error = response.error;
	reply->body = response.body;
	emit reply->finished();
	return true;
}

// Collects the results of a checker.
struct Results {
	explicit Results(UpdateChecker& checker) {
		QObject::connect(&checker, &UpdateChecker::finished, &context, [this](const UpdateCheckResult& result) {
			list.append(result);
			if (loop)
				loop->quit();
		});
	}

	// Runs the event loop until a result is there or five seconds have passed.
	bool wait() {
		if (!list.isEmpty())
			return true;
		QEventLoop eventLoop;
		QTimer guard;
		guard.setSingleShot(true);
		QObject::connect(&guard, &QTimer::timeout, &eventLoop, &QEventLoop::quit);
		loop = &eventLoop;
		guard.start(5000);
		eventLoop.exec();
		loop = nullptr;
		return !list.isEmpty();
	}

	QObject context;
	QList<UpdateCheckResult> list;
	QEventLoop* loop = nullptr;
};

// Runs one check against a reply that finishes with the answer. True when exactly one result arrived.
static bool runCheck(const Answer& response, UpdateCheckResult* result) {
	FakeTransport transport;
	UpdateChecker checker(&transport, 5000);
	Results results(checker);
	checker.check();
	if (!finishReply(transport, 0, response) || !results.wait())
		return false;
	*result = results.list.first();
	return results.list.size() == 1 && !checker.isChecking();
}

static bool testCheckSuccess() {
	FakeTransport transport;
	UpdateChecker checker(&transport, 5000);
	Results results(checker);
	checker.check();
	bool ok = expect(checker.isChecking() && transport.calls.size() == 1u, "a started check sends one request and is pending");
	if (!ok)
		return false;
	const HttpRequest& request = transport.calls.front().request;
	ok &= expect(request.url == UpdateChecker::releasesUrl(), "the check requests the releases URL");
	ok &= expect(request.userAgent.startsWith("EGTRAIN/"), "the check sends an EGTRAIN user agent");
	ok &= expect(request.accept == "application/vnd.github+json", "the check accepts the GitHub JSON type");

	Answer response;
	response.status = 200;
	response.body = releaseJson("v1.10.0");
	ok &= expect(finishReply(transport, 0, response) && results.wait(), "the answer to the check arrives");
	ok &= expect(results.list.size() == 1 && results.list.first().success && results.list.first().error.isEmpty(),
		"a release list gives one successful result");
	ok &= expect(results.list.size() == 1 && results.list.first().release && results.list.first().release->tag == QStringLiteral("v1.10.0"),
		"the result holds the latest stable release");
	ok &= expect(!checker.isChecking(), "a finished check is not pending");
	return ok;
}

static bool testCheckFailureTexts() {
	UpdateCheckResult result;
	Answer response;
	response.failure = true;
	response.error = QStringLiteral("boom");
	bool ok = expect(runCheck(response, &result) && !result.success && !result.release && result.error == QStringLiteral("boom"),
		"a failed request reports its error text");

	response.error.clear();
	ok &= expect(runCheck(response, &result) && !result.success && result.error == QStringLiteral("GitHub update check failed."),
		"a failed request without an error text reports the generic text");

	response = Answer();
	response.status = 500;
	ok &= expect(runCheck(response, &result) && !result.success && result.error == QStringLiteral("GitHub returned HTTP 500."),
		"an HTTP error status is reported with its code");

	response.status = 204;
	QString parseError;
	parseLatestStableRelease(QByteArray(), &parseError);
	const QString expected = parseError.isEmpty() ? QStringLiteral("No stable release was found.") : parseError;
	ok &= expect(runCheck(response, &result) && !result.success && !result.release && result.error == expected,
		"an empty answer reports what the release parser reports");

	response.status = 200;
	response.body = QByteArrayLiteral("not json");
	ok &= expect(runCheck(response, &result) && !result.success && !result.error.isEmpty(), "an answer that is not JSON fails with a text");
	return ok;
}

static bool testCheckTimeout() {
	FakeTransport transport;
	UpdateChecker checker(&transport, 30);
	Results results(checker);
	checker.check();
	bool ok = expect(results.wait(), "a check that never finishes times out");
	spin(50);
	ok &= expect(results.list.size() == 1, "a timeout gives exactly one result");
	ok &= expect(!results.list.isEmpty() && !results.list.first().success
			&& results.list.first().error == QStringLiteral("The update check timed out."),
		"the timeout result is a failure with the timeout text");
	ok &= expect(transport.calls.size() == 1u && transport.calls.front().aborts == 1, "a timeout aborts the request once");
	ok &= expect(!checker.isChecking(), "a timed out check is not pending");
	return ok;
}

static bool testCheckWhilePending() {
	FakeTransport transport;
	UpdateChecker checker(&transport, 5000);
	Results results(checker);
	checker.check();
	checker.check();
	bool ok = expect(transport.calls.size() == 2u, "each check sends a request");
	if (!ok)
		return false;
	ok &= expect(transport.calls.front().aborts == 1, "a new check aborts the pending request once");
	ok &= expect(checker.isChecking(), "the new check is pending");
	ok &= expect(results.list.isEmpty(), "aborting the pending request gives no result");

	Answer response;
	response.status = 200;
	response.body = releaseJson("v1.10.0");
	ok &= expect(finishReply(transport, 1, response) && results.wait(), "the answer to the new check arrives");
	spin(50);
	ok &= expect(results.list.size() == 1 && results.list.first().success, "two checks in a row give one successful result");
	ok &= expect(transport.calls.back().aborts == 0, "the answered request is not aborted");
	return ok;
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
	ok &= expect(!readFailedUpdateVersion(settings), "no failed version is stored at first");
	writeFailedUpdateVersion(settings, SemanticVersion{1, 2, 3});
	const std::optional<SemanticVersion> storedFailure = readFailedUpdateVersion(settings);
	ok &= expect(storedFailure && *storedFailure == SemanticVersion{1, 2, 3}, "the failed version round-trips");
	settings.setValue(QStringLiteral("updates/failedVersion"), QStringLiteral("1.2"));
	ok &= expect(!readFailedUpdateVersion(settings), "a stored text that is not a stable version is no failed version");
	settings.setValue(QStringLiteral("updates/failedVersion"), QStringLiteral("not a version"));
	ok &= expect(!readFailedUpdateVersion(settings), "a stored text that is no version at all is no failed version");
	settings.remove(QStringLiteral("updates/failedVersion"));
	const SemanticVersion failed{1, 2, 3};
	ok &= expect(shouldOfferRelease(failed, failed, true), "a manual check offers the failed version");
	ok &= expect(!shouldOfferRelease(failed, failed, false), "an automatic check does not offer the failed version");
	ok &= expect(shouldOfferRelease(SemanticVersion{1, 2, 4}, failed, false),
		"an automatic check offers a newer version than the failed one");
	ok &= expect(shouldOfferRelease(SemanticVersion{1, 2, 4}, failed, true),
		"a manual check offers a newer version than the failed one");
	ok &= expect(shouldOfferRelease(SemanticVersion{1, 2, 2}, failed, false),
		"only the failed version itself is held back, not older releases");
	ok &= expect(shouldOfferRelease(failed, std::nullopt, false), "an automatic check offers a release when nothing failed");
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
				   "\"html_url\":\"https://example.com/phishing\"}]"));
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
			&& packaged->asset(QStringLiteral("QEGTRAIN-linux-x86_64.AppImage")),
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
					 QStringLiteral("linux-x86_64")),
		"manifest version must equal the stable tag");
	QByteArray uppercaseManifest = manifest;
	uppercaseManifest.replace("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
	ok &= expect(!parseUpdateManifest(uppercaseManifest,
					 QStringLiteral("v1.10.0"), QStringLiteral("linux-x86_64")),
		"manifest hash must be lowercase hexadecimal");

	// Asset names: the names of the released versions and the renamed ones.
	const auto manifestFor = [&](const char* distribution, const QByteArray& name) {
		return QByteArray("{\"version\":\"1.10.0\",\"assets\":{\"") + distribution + "\":{\"name\":\"" + name
			+ "\",\"sha256\":\"" + validHash + "\",\"size\":12345}}}";
	};
	const auto selectedName = [&](const char* distribution, const QByteArray& name) {
		const auto parsed = parseUpdateManifest(manifestFor(distribution, name), QStringLiteral("v1.10.0"),
			QString::fromLatin1(distribution));
		return parsed ? parsed->assetName : QString();
	};
	struct NameCase {
		const char* distribution;
		const char* name;
	};
	const NameCase acceptedNames[] = {
		{"windows-x64", "QEGTRAIN-windows-x64.zip"},
		{"windows-x64", "EGTRAIN-Portable-1.10.0-x64.zip"},
		{"windows-x64-installer", "EGTRAIN-Setup-1.10.0-x64.exe"},
		{"macos-arm64", "QEGTRAIN-macos-arm64.zip"},
		{"macos-arm64", "EGTRAIN-1.10.0-macOS-arm64.dmg"},
		{"linux-x86_64", "QEGTRAIN-linux-x86_64.AppImage"}};
	for (const NameCase& accepted : acceptedNames)
		ok &= expect(selectedName(accepted.distribution, accepted.name) == QString::fromLatin1(accepted.name),
			(std::string("manifest selects ") + accepted.name + " for " + accepted.distribution).c_str());
	const NameCase rejectedNames[] = {
		{"windows-x64", "EGTRAIN-Portable-1.9.0-x64.zip"},
		{"windows-x64", "EGTRAIN-Portable-1.10.0-x64.exe"},
		{"windows-x64", "EGTRAIN-Setup-1.10.0-x64.exe"},
		{"windows-x64", "QEGTRAIN-windows-x64.exe"},
		{"windows-x64-installer", "EGTRAIN-Setup-1.9.0-x64.exe"},
		{"windows-x64-installer", "EGTRAIN-Portable-1.10.0-x64.zip"},
		{"windows-x64-installer", "QEGTRAIN-windows-x64.zip"},
		{"windows-x64-installer", "EGTRAIN-Setup-1.10.0-x64.zip"},
		{"macos-arm64", "EGTRAIN-1.9.0-macOS-arm64.dmg"},
		{"macos-arm64", "QEGTRAIN-windows-x64.zip"},
		{"linux-x86_64", "QEGTRAIN-macos-arm64.zip"},
		{"linux-x86_64", "../QEGTRAIN-linux-x86_64.AppImage"},
		{"linux-x86_64", "dir/QEGTRAIN-linux-x86_64.AppImage"},
		{"linux-x86_64", "dir\\\\QEGTRAIN-linux-x86_64.AppImage"},
		{"linux-x86_64", "unknown.AppImage"},
		{"linux-x86_64", "QEGTRAIN-Linux-x86_64.AppImage"},
		{"linux-x86_64", "QEGTRAIN-linux-x86_64.AppImage "},
		{"linux-x86_64", "QEGTRAIN-linux-x86_64.AppImage.exe"},
		{"windows-x64", ""},
		{"windows-x64-portable", "QEGTRAIN-windows-x64.zip"}};
	for (const NameCase& rejected : rejectedNames)
		ok &= expect(selectedName(rejected.distribution, rejected.name).isEmpty(),
			(std::string("manifest rejects ") + rejected.name + " for " + rejected.distribution).c_str());
	ok &= expect(!isUpdateAssetName(QStringLiteral("windows-x64"), QStringLiteral("EGTRAIN-Portable--x64.zip"), QString()),
		"an empty version never matches a versioned name");
	ok &= expect(!parseUpdateManifest(manifestFor("windows-x64", "QEGTRAIN-windows-x64.zip"), QStringLiteral("v1.10.0"),
					 QStringLiteral("windows-x64-installer")),
		"a manifest without an entry for the distribution key is rejected");

	// Package selection in a release.
	const auto releaseWith = [&](const QList<const char*>& names) {
		QByteArray assets;
		for (const char* name : names) {
			if (!assets.isEmpty())
				assets += ',';
			assets += QByteArray("{\"name\":\"") + name
				+ "\",\"browser_download_url\":\"https://github.com/Ancientkingg/EGTRAIN/releases/download/v1.10.0/" + name + "\"}";
		}
		return parseLatestStableRelease(
			QByteArray("[{\"tag_name\":\"v1.10.0\",\"draft\":false,\"prerelease\":false,\"assets\":[") + assets + "]}]");
	};
	const auto renamed = releaseWith({"update-manifest.json", "EGTRAIN-Portable-1.10.0-x64.zip", "EGTRAIN-Setup-1.10.0-x64.exe",
		"EGTRAIN-1.10.0-macOS-arm64.dmg", "QEGTRAIN-linux-x86_64.AppImage", "EGTRAIN-Portable-1.9.0-x64.zip",
		"EGTRAIN-Setup-1.10.0-x64.zip", "notes.txt", "Paimpol.egscene"});
	ok &= expect(renamed && renamed->assets.size() == 5 && renamed->asset(QStringLiteral("EGTRAIN-Setup-1.10.0-x64.exe"))
			&& renamed->asset(QStringLiteral("EGTRAIN-1.10.0-macOS-arm64.dmg")),
		"the release filter keeps manifest and known package names of the release version");
	ok &= expect(renamed && !renamed->asset(QStringLiteral("EGTRAIN-Portable-1.9.0-x64.zip"))
			&& !renamed->asset(QStringLiteral("EGTRAIN-Setup-1.10.0-x64.zip")) && !renamed->asset(QStringLiteral("notes.txt")),
		"the release filter ignores unknown names and names of another version");
	ok &= expect(renamed && releaseHasUpdatePackage(*renamed, QStringLiteral("windows-x64"))
			&& releaseHasUpdatePackage(*renamed, QStringLiteral("windows-x64-installer"))
			&& releaseHasUpdatePackage(*renamed, QStringLiteral("macos-arm64"))
			&& releaseHasUpdatePackage(*renamed, QStringLiteral("linux-x86_64")),
		"a release with renamed packages offers a package for every key");
	const auto legacy = releaseWith({"update-manifest.json", "QEGTRAIN-windows-x64.zip", "QEGTRAIN-macos-arm64.zip"});
	ok &= expect(legacy && releaseHasUpdatePackage(*legacy, QStringLiteral("windows-x64"))
			&& releaseHasUpdatePackage(*legacy, QStringLiteral("macos-arm64"))
			&& !releaseHasUpdatePackage(*legacy, QStringLiteral("windows-x64-installer"))
			&& !releaseHasUpdatePackage(*legacy, QStringLiteral("linux-x86_64")),
		"a release offers only the packages of the key");
	const auto wrongKind = releaseWith({"update-manifest.json", "EGTRAIN-Setup-1.10.0-x64.exe"});
	ok &= expect(wrongKind && !releaseHasUpdatePackage(*wrongKind, QStringLiteral("windows-x64")),
		"an installer is not a package for the portable key");

	// Package file list of the manifest entry.
	const auto manifestWithFiles = [&](const QByteArray& filesMember) {
		return QByteArray("{\"version\":\"1.10.0\",\"assets\":{\"windows-x64\":{"
						  "\"name\":\"QEGTRAIN-windows-x64.zip\",\"sha256\":\"")
			+ validHash
			+ QByteArray("\",\"size\":12345") + filesMember + QByteArray("}}}");
	};
	const auto parseWindows = [&](const QByteArray& filesMember, QString* error = nullptr) {
		return parseUpdateManifest(manifestWithFiles(filesMember), QStringLiteral("v1.10.0"),
			QStringLiteral("windows-x64"), error);
	};
	const auto parsedList = parseWindows(QByteArray(
		",\"files\":[\"QEGTRAIN.exe\",\"platforms/qwindows.dll\",\"Scenes/Paimpol/scene.json\"]"));
	ok &= expect(
		parsedList
			&& parsedList->files
				== QStringList({QStringLiteral("QEGTRAIN.exe"), QStringLiteral("platforms/qwindows.dll"),
					QStringLiteral("Scenes/Paimpol/scene.json")}),
		"manifest file list is retained");
	const auto parsedBackslash = parseWindows(QByteArray(
		",\"files\":[\"QEGTRAIN.exe\",\"platforms\\\\qwindows.dll\"]"));
	ok &= expect(parsedBackslash && parsedBackslash->files.contains(QStringLiteral("platforms/qwindows.dll")), "manifest file list uses forward slashes");
	const auto parsedWithoutList = parseWindows(QByteArray());
	ok &= expect(parsedWithoutList && parsedWithoutList->files.isEmpty(),
		"a manifest without a file list stays valid");

	const QByteArray executable = "\"QEGTRAIN.exe\"";
	QByteArray manyFiles = ",\"files\":[" + executable;
	for (int index = 0; index < 4096; ++index)
		manyFiles += ",\"f" + QByteArray::number(index) + ".dll\"";
	QByteArray maxFiles = ",\"files\":[" + executable;
	for (int index = 0; index < 4095; ++index)
		maxFiles += ",\"f" + QByteArray::number(index) + ".dll\"";
	const QByteArray longName = "\"" + QByteArray(257, 'a') + ".dll\"";
	const QByteArray maxName = "\"" + QByteArray(256, 'a') + ".dll\"";
	struct InvalidListCase {
		const char* label;
		QByteArray filesMember;
	};
	const QList<InvalidListCase> invalidLists = {
		{"a file list that is not an array", ",\"files\":\"QEGTRAIN.exe\""},
		{"a null file list", ",\"files\":null"},
		{"a file list with a non-string entry", ",\"files\":[" + executable + ",7]"},
		{"a file list with more than 4096 entries", manyFiles + "]"},
		{"an empty entry", ",\"files\":[" + executable + ",\"\"]"},
		{"an entry longer than 260 characters", ",\"files\":[" + executable + "," + longName + "]"},
		{"an absolute path", ",\"files\":[" + executable + ",\"/abs/file.dll\"]"},
		{"a drive colon", ",\"files\":[" + executable + ",\"C:/dir/x.dll\"]"},
		{"a stream colon", ",\"files\":[" + executable + ",\"a.dll:stream\"]"},
		{"a NUL character", ",\"files\":[" + executable + ",\"a\\u0000.dll\"]"},
		{"an empty segment", ",\"files\":[" + executable + ",\"a//b.dll\"]"},
		{"a trailing slash", ",\"files\":[" + executable + ",\"a/\"]"},
		{"a dot segment", ",\"files\":[" + executable + ",\"./a.dll\"]"},
		{"a parent segment", ",\"files\":[" + executable + ",\"a/../b.dll\"]"},
		{"a backslash parent segment", ",\"files\":[" + executable + ",\"..\\\\b.dll\"]"},
		{"a file list without QEGTRAIN.exe", ",\"files\":[\"Qt5Core.dll\"]"},
		{"an empty file list", ",\"files\":[]"}};
	for (const InvalidListCase& invalid : invalidLists) {
		QString listError;
		ok &= expect(!parseWindows(invalid.filesMember, &listError)
				&& listError == QStringLiteral("Update manifest has an invalid file list."),
			(std::string("manifest rejects ") + invalid.label).c_str());
	}
	ok &= expect(parseWindows(maxFiles + "]").has_value(),
		"manifest accepts 4096 file entries");
	ok &= expect(parseWindows(",\"files\":[" + executable + "," + maxName + "]").has_value(),
		"manifest accepts a 260 character entry");

	ok &= testCheckSuccess();
	ok &= testCheckFailureTexts();
	ok &= testCheckTimeout();
	ok &= testCheckWhilePending();

	return ok ? 0 : 1;
}
