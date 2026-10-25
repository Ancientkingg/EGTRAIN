#include "update/ReleaseInfo.h"
#include "update/UpdateChecker.h"
#include "update/UpdateSettings.h"

#include <QEventLoop>
#include <QPointer>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <deque>
#include <utility>

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

// Hands the checker the next part of the body of the given request.
static bool sendChunk(const FakeTransport& transport, int index, const QByteArray& chunk) {
	FakeReply* reply = replyAt(transport, index);
	if (!reply)
		return false;
	reply->body += chunk;
	emit reply->readyRead();
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

// The variables that alone suppress update UI and network. The tests that read them start from none of them set and
// put them back as they were.
static const char* const kSuppressing[] = {"QEGTRAIN_DISABLE_UPDATES", "QEGTRAIN_AUTOSTART",
	"QEGTRAIN_STARTUP_TIMING", "QEGTRAIN_PLAYBACK_PROFILE", "QEGTRAIN_E2E_SCENE_DROP"};

class ClearedEnvironment {
public:
	ClearedEnvironment() {
		for (int i = 0; i < 5; ++i) {
			m_previous[i] = qgetenv(kSuppressing[i]);
			qunsetenv(kSuppressing[i]);
		}
	}
	~ClearedEnvironment() {
		for (int i = 0; i < 5; ++i) {
			if (m_previous[i].isNull())
				qunsetenv(kSuppressing[i]);
			else
				qputenv(kSuppressing[i], m_previous[i]);
		}
	}
	ClearedEnvironment(const ClearedEnvironment&) = delete;
	ClearedEnvironment& operator=(const ClearedEnvironment&) = delete;

private:
	QByteArray m_previous[5];
};

// Another end-to-end hook in the caller's environment suppresses updates too; the negative checks only hold without one.
static bool otherEndToEndHook() {
	bool otherHook = false;
	for (const QString& key : QProcessEnvironment::systemEnvironment().keys())
		otherHook = otherHook || key.startsWith(QStringLiteral("QEGTRAIN_E2E_"));
	return otherHook;
}

static const QByteArray kValidHash(64, 'a');

static QByteArray manifestFor(const char* distribution, const QByteArray& name) {
	return QByteArray("{\"version\":\"1.10.0\",\"assets\":{\"") + distribution + "\":{\"name\":\"" + name
		+ "\",\"sha256\":\"" + kValidHash + "\",\"size\":12345}}}";
}

// The package name that the manifest of the distribution selects, or an empty string.
static QString selectedName(const char* distribution, const QByteArray& name) {
	const auto parsed = parseUpdateManifest(manifestFor(distribution, name), QStringLiteral("v1.10.0"),
		QString::fromLatin1(distribution));
	return parsed ? parsed->assetName : QString();
}

static std::optional<StableRelease> releaseWith(const QList<const char*>& names) {
	QByteArray assets;
	for (const char* name : names) {
		if (!assets.isEmpty())
			assets += ',';
		assets += QByteArray("{\"name\":\"") + name
			+ "\",\"browser_download_url\":\"https://github.com/Ancientkingg/EGTRAIN/releases/download/v1.10.0/" + name + "\"}";
	}
	return parseLatestStableRelease(
		QByteArray("[{\"tag_name\":\"v1.10.0\",\"draft\":false,\"prerelease\":false,\"assets\":[") + assets + "]}]");
}

static QByteArray manifestWithFiles(const QByteArray& filesMember) {
	return QByteArray("{\"version\":\"1.10.0\",\"assets\":{\"windows-x64\":{"
					  "\"name\":\"QEGTRAIN-windows-x64.zip\",\"sha256\":\"")
		+ kValidHash
		+ QByteArray("\",\"size\":12345") + filesMember + QByteArray("}}}");
}

static std::optional<UpdateManifest> parseWindows(const QByteArray& filesMember, QString* error = nullptr) {
	return parseUpdateManifest(manifestWithFiles(filesMember), QStringLiteral("v1.10.0"),
		QStringLiteral("windows-x64"), error);
}

static const QByteArray kExecutable = "\"QEGTRAIN.exe\"";

// A file list of the executable and count more files, without the closing bracket.
static QByteArray fileListOf(int count) {
	QByteArray files = ",\"files\":[" + kExecutable;
	for (int index = 0; index < count; ++index)
		files += ",\"f" + QByteArray::number(index) + ".dll\"";
	return files;
}

static constexpr int kListLimit = 8 * 1024 * 1024;
static const QString kTooLarge = QStringLiteral("The release list from GitHub is too large.");

class TestUpdateChecker : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void storesCheckPreference();
	void keepsManualChecksWhenAutomaticOff();
	void storesFailedVersion();
	void holdsBackOnlyFailedVersion();
	void interactiveLaunchDoesNotSuppress();
	void scriptedLaunchSuppresses_data();
	void scriptedLaunchSuppresses();
	void measurementModeOffDoesNotSuppress();
	void comparesVersionsNumerically();
	void ignoresInvalidRelease_data();
	void ignoresInvalidRelease();
	void selectsLatestStableRelease();
	void constrainsReleasePage();
	void keepsOnlyExactPackageAssets();
	void parsesUpdateManifest();
	void selectsAssetName_data();
	void selectsAssetName();
	void rejectsAssetName_data();
	void rejectsAssetName();
	void rejectsUnversionedNameAndMissingKey();
	void selectsReleasePackages();
	void readsManifestFileList();
	void rejectsInvalidFileList_data();
	void rejectsInvalidFileList();
	void acceptsFileListAtLimits();
	void checkSucceeds();
	void reportsFailureTexts();
	void timesOut();
	void replacesPendingCheck();
	void refusesGrowingReleaseList();
	void refusesLargeReleaseListAtEnd();
	void parsesReleaseListOfLimit();

private:
	QTemporaryDir m_temp;
};

void TestUpdateChecker::initTestCase() {
	QVERIFY(m_temp.isValid());
}

void TestUpdateChecker::storesCheckPreference() {
	QSettings settings(m_temp.filePath(QStringLiteral("updates.ini")), QSettings::IniFormat);
	settings.clear();
	// A missing preference is unknown.
	QCOMPARE(readUpdateCheckState(settings), UpdateCheckState::Unknown);
	writeUpdateCheckState(settings, UpdateCheckState::Enabled);
	// The enabled preference round-trips.
	QCOMPARE(readUpdateCheckState(settings), UpdateCheckState::Enabled);
	writeUpdateCheckState(settings, UpdateCheckState::Disabled);
	// Stop checking persists disabled.
	QCOMPARE(readUpdateCheckState(settings), UpdateCheckState::Disabled);
	writeUpdateCheckState(settings, UpdateCheckState::Enabled);
	// Automatic checking can be re-enabled.
	QCOMPARE(readUpdateCheckState(settings), UpdateCheckState::Enabled);
}

void TestUpdateChecker::keepsManualChecksWhenAutomaticOff() {
	// Manual checks remain available while automatic checks are disabled.
	QVERIFY(!shouldCheckForUpdates(UpdateCheckState::Disabled, false));
	QVERIFY(shouldCheckForUpdates(UpdateCheckState::Disabled, true));
}

void TestUpdateChecker::storesFailedVersion() {
	QSettings settings(m_temp.filePath(QStringLiteral("failed.ini")), QSettings::IniFormat);
	settings.clear();
	QVERIFY2(!readFailedUpdateVersion(settings), "no failed version is stored at first");
	const SemanticVersion failed{1, 2, 3};
	writeFailedUpdateVersion(settings, failed);
	const std::optional<SemanticVersion> storedFailure = readFailedUpdateVersion(settings);
	// The failed version round-trips.
	QVERIFY(storedFailure.has_value());
	QVERIFY(*storedFailure == failed);
	settings.setValue(QStringLiteral("updates/failedVersion"), QStringLiteral("1.2"));
	QVERIFY2(!readFailedUpdateVersion(settings), "a stored text that is not a stable version is no failed version");
	settings.setValue(QStringLiteral("updates/failedVersion"), QStringLiteral("not a version"));
	QVERIFY2(!readFailedUpdateVersion(settings), "a stored text that is no version at all is no failed version");
	settings.remove(QStringLiteral("updates/failedVersion"));
}

void TestUpdateChecker::holdsBackOnlyFailedVersion() {
	const SemanticVersion failed{1, 2, 3};
	QVERIFY2(shouldOfferRelease(failed, failed, true), "a manual check offers the failed version");
	QVERIFY2(!shouldOfferRelease(failed, failed, false), "an automatic check does not offer the failed version");
	QVERIFY2(shouldOfferRelease(SemanticVersion{1, 2, 4}, failed, false),
		"an automatic check offers a newer version than the failed one");
	QVERIFY2(shouldOfferRelease(SemanticVersion{1, 2, 4}, failed, true),
		"a manual check offers a newer version than the failed one");
	QVERIFY2(shouldOfferRelease(SemanticVersion{1, 2, 2}, failed, false),
		"only the failed version itself is held back, not older releases");
	QVERIFY2(shouldOfferRelease(failed, std::nullopt, false), "an automatic check offers a release when nothing failed");
}

void TestUpdateChecker::interactiveLaunchDoesNotSuppress() {
	const ClearedEnvironment environment;
	QVERIFY2(otherEndToEndHook() || !updatesSuppressedByEnvironment(), "an interactive launch does not suppress updates");
}

void TestUpdateChecker::scriptedLaunchSuppresses_data() {
	QTest::addColumn<QByteArray>("name");
	for (const char* name : kSuppressing)
		QTest::newRow(name) << QByteArray(name);
}

void TestUpdateChecker::scriptedLaunchSuppresses() {
	QFETCH(QByteArray, name);
	const ClearedEnvironment environment;
	qputenv(name.constData(), "1");
	QVERIFY2(updatesSuppressedByEnvironment(), "a scripted launch suppresses update UI and network");
}

void TestUpdateChecker::measurementModeOffDoesNotSuppress() {
	const ClearedEnvironment environment;
	qputenv("QEGTRAIN_STARTUP_TIMING", "0");
	qputenv("QEGTRAIN_PLAYBACK_PROFILE", "0");
	QVERIFY2(otherEndToEndHook() || !updatesSuppressedByEnvironment(),
		"a measurement mode that is switched off does not suppress updates");
}

void TestUpdateChecker::comparesVersionsNumerically() {
	const auto current = parseStableVersion("1.9.0");
	const auto release = parseLatestStableRelease(releaseJson("v1.10.0"));
	// Release comparison uses numeric version components.
	QVERIFY(current.has_value());
	QVERIFY(release.has_value());
	QVERIFY(isUpdateAvailable(*current, *release));
	QVERIFY2(!isUpdateAvailable(release->version, *release), "same version is up to date");
}

void TestUpdateChecker::ignoresInvalidRelease_data() {
	QTest::addColumn<QByteArray>("json");
	QTest::newRow("not json") << QByteArray("not json");
	QTest::newRow("an object") << QByteArray("{}");
	QTest::newRow("a prerelease") << QByteArray("[{\"tag_name\":\"v1.0.0\",\"draft\":false,\"prerelease\":true}]");
	QTest::newRow("a draft") << QByteArray("[{\"tag_name\":\"v1.0.0\",\"draft\":true,\"prerelease\":false}]");
	QTest::newRow("a tag without v") << QByteArray("[{\"tag_name\":\"1.0.0\",\"draft\":false,\"prerelease\":false}]");
	QTest::newRow("a tag of two parts") << QByteArray("[{\"tag_name\":\"v1.0\",\"draft\":false,\"prerelease\":false}]");
	QTest::newRow("no prerelease field") << QByteArray("[{\"tag_name\":\"v1.0.0\",\"draft\":false}]");
}

void TestUpdateChecker::ignoresInvalidRelease() {
	QFETCH(QByteArray, json);
	QVERIFY2(!parseLatestStableRelease(json), "invalid release is ignored");
}

void TestUpdateChecker::selectsLatestStableRelease() {
	QByteArray releaseList = releaseJson("v1.9.9");
	releaseList.chop(1);
	releaseList += ',';
	releaseList += releaseJson("v1.10.0").mid(1);
	const auto releases = parseLatestStableRelease(releaseList);
	// The latest stable release is selected without package information.
	QVERIFY(releases.has_value());
	QCOMPARE(releases->version.major, 1u);
	QCOMPARE(releases->version.minor, 10u);
}

void TestUpdateChecker::constrainsReleasePage() {
	const auto wrongPage = parseLatestStableRelease(
		QByteArray("[{\"tag_name\":\"v1.10.0\",\"draft\":false,\"prerelease\":false,"
				   "\"html_url\":\"https://example.com/phishing\"}]"));
	// The release page is constrained to the expected GitHub repository.
	QVERIFY(wrongPage.has_value());
	QCOMPARE(wrongPage->releasePage.host(), QStringLiteral("github.com"));
	QVERIFY(wrongPage->releasePage.path().startsWith(QStringLiteral("/Ancientkingg/EGTRAIN/")));
}

void TestUpdateChecker::keepsOnlyExactPackageAssets() {
	const QByteArray packagedRelease = QByteArray(
		"[{\"tag_name\":\"v1.10.0\",\"draft\":false,\"prerelease\":false,"
		"\"html_url\":\"https://github.com/Ancientkingg/EGTRAIN/releases/tag/v1.10.0\","
		"\"assets\":["
		"{\"name\":\"update-manifest.json\",\"browser_download_url\":\"https://github.com/Ancientkingg/EGTRAIN/releases/download/v1.10.0/update-manifest.json\"},"
		"{\"name\":\"QEGTRAIN-linux-x86_64.AppImage\",\"browser_download_url\":\"https://github.com/Ancientkingg/EGTRAIN/releases/download/v1.10.0/QEGTRAIN-linux-x86_64.AppImage\"},"
		"{\"name\":\"untrusted.zip\",\"browser_download_url\":\"https://example.com/untrusted.zip\"}]}]");
	const auto packaged = parseLatestStableRelease(packagedRelease);
	// Only exact release package assets are retained.
	QVERIFY(packaged.has_value());
	QVERIFY(packaged->asset(updateManifestAssetName()) != nullptr);
	QVERIFY(packaged->asset(QStringLiteral("QEGTRAIN-linux-x86_64.AppImage")) != nullptr);
	QVERIFY2(packaged->asset(QStringLiteral("untrusted.zip")) == nullptr, "untrusted release assets are ignored");
}

void TestUpdateChecker::parsesUpdateManifest() {
	const QByteArray manifest = QByteArray("{\"version\":\"1.10.0\",\"assets\":{\"linux-x86_64\":{\"name\":\"QEGTRAIN-linux-x86_64.AppImage\",\"sha256\":\"")
		+ kValidHash + QByteArray("\",\"size\":12345}}}");
	const auto parsedManifest = parseUpdateManifest(manifest, QStringLiteral("v1.10.0"),
		QStringLiteral("linux-x86_64"));
	// A valid update manifest parses.
	QVERIFY(parsedManifest.has_value());
	QCOMPARE(parsedManifest->sha256, QString::fromLatin1(kValidHash));
	// The manifest package size is retained.
	QCOMPARE(parsedManifest->assetSize, 12345);
	QVERIFY2(!parseUpdateManifest(manifest, QStringLiteral("v1.10.1"),
				 QStringLiteral("linux-x86_64")),
		"manifest version must equal the stable tag");
	QByteArray uppercaseManifest = manifest;
	uppercaseManifest.replace("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
	QVERIFY2(!parseUpdateManifest(uppercaseManifest,
				 QStringLiteral("v1.10.0"), QStringLiteral("linux-x86_64")),
		"manifest hash must be lowercase hexadecimal");
}

// Asset names: the names of the released versions and the renamed ones.
void TestUpdateChecker::selectsAssetName_data() {
	QTest::addColumn<QByteArray>("distribution");
	QTest::addColumn<QByteArray>("name");
	const auto add = [](const char* distribution, const char* name) {
		QTest::addRow("%s for %s", name, distribution) << QByteArray(distribution) << QByteArray(name);
	};
	add("windows-x64", "QEGTRAIN-windows-x64.zip");
	add("windows-x64", "EGTRAIN-Portable-1.10.0-x64.zip");
	add("windows-x64-installer", "EGTRAIN-Setup-1.10.0-x64.exe");
	add("macos-arm64", "QEGTRAIN-macos-arm64.zip");
	add("macos-arm64", "EGTRAIN-1.10.0-macOS-arm64.dmg");
	add("linux-x86_64", "QEGTRAIN-linux-x86_64.AppImage");
}

void TestUpdateChecker::selectsAssetName() {
	QFETCH(QByteArray, distribution);
	QFETCH(QByteArray, name);
	// The manifest selects the name for the distribution.
	QCOMPARE(selectedName(distribution.constData(), name), QString::fromLatin1(name));
}

void TestUpdateChecker::rejectsAssetName_data() {
	QTest::addColumn<QByteArray>("distribution");
	QTest::addColumn<QByteArray>("name");
	const auto add = [](const char* distribution, const char* name) {
		QTest::addRow("%s for %s", name, distribution) << QByteArray(distribution) << QByteArray(name);
	};
	add("windows-x64", "EGTRAIN-Portable-1.9.0-x64.zip");
	add("windows-x64", "EGTRAIN-Portable-1.10.0-x64.exe");
	add("windows-x64", "EGTRAIN-Setup-1.10.0-x64.exe");
	add("windows-x64", "QEGTRAIN-windows-x64.exe");
	add("windows-x64-installer", "EGTRAIN-Setup-1.9.0-x64.exe");
	add("windows-x64-installer", "EGTRAIN-Portable-1.10.0-x64.zip");
	add("windows-x64-installer", "QEGTRAIN-windows-x64.zip");
	add("windows-x64-installer", "EGTRAIN-Setup-1.10.0-x64.zip");
	add("macos-arm64", "EGTRAIN-1.9.0-macOS-arm64.dmg");
	add("macos-arm64", "QEGTRAIN-windows-x64.zip");
	add("linux-x86_64", "QEGTRAIN-macos-arm64.zip");
	add("linux-x86_64", "../QEGTRAIN-linux-x86_64.AppImage");
	add("linux-x86_64", "dir/QEGTRAIN-linux-x86_64.AppImage");
	add("linux-x86_64", "dir\\\\QEGTRAIN-linux-x86_64.AppImage");
	add("linux-x86_64", "unknown.AppImage");
	add("linux-x86_64", "QEGTRAIN-Linux-x86_64.AppImage");
	add("linux-x86_64", "QEGTRAIN-linux-x86_64.AppImage ");
	add("linux-x86_64", "QEGTRAIN-linux-x86_64.AppImage.exe");
	add("windows-x64", "");
	add("windows-x64-portable", "QEGTRAIN-windows-x64.zip");
}

void TestUpdateChecker::rejectsAssetName() {
	QFETCH(QByteArray, distribution);
	QFETCH(QByteArray, name);
	QVERIFY2(selectedName(distribution.constData(), name).isEmpty(), "the manifest rejects the name for the distribution");
}

void TestUpdateChecker::rejectsUnversionedNameAndMissingKey() {
	QVERIFY2(!isUpdateAssetName(QStringLiteral("windows-x64"), QStringLiteral("EGTRAIN-Portable--x64.zip"), QString()),
		"an empty version never matches a versioned name");
	QVERIFY2(!parseUpdateManifest(manifestFor("windows-x64", "QEGTRAIN-windows-x64.zip"), QStringLiteral("v1.10.0"),
				 QStringLiteral("windows-x64-installer")),
		"a manifest without an entry for the distribution key is rejected");
}

// Package selection in a release.
void TestUpdateChecker::selectsReleasePackages() {
	const auto renamed = releaseWith({"update-manifest.json", "EGTRAIN-Portable-1.10.0-x64.zip", "EGTRAIN-Setup-1.10.0-x64.exe",
		"EGTRAIN-1.10.0-macOS-arm64.dmg", "QEGTRAIN-linux-x86_64.AppImage", "EGTRAIN-Portable-1.9.0-x64.zip",
		"EGTRAIN-Setup-1.10.0-x64.zip", "notes.txt", "Paimpol.egscene"});
	// The release filter keeps manifest and known package names of the release version.
	QVERIFY(renamed.has_value());
	QCOMPARE(renamed->assets.size(), 5);
	QVERIFY(renamed->asset(QStringLiteral("EGTRAIN-Setup-1.10.0-x64.exe")) != nullptr);
	QVERIFY(renamed->asset(QStringLiteral("EGTRAIN-1.10.0-macOS-arm64.dmg")) != nullptr);
	// The release filter ignores unknown names and names of another version.
	QVERIFY(renamed->asset(QStringLiteral("EGTRAIN-Portable-1.9.0-x64.zip")) == nullptr);
	QVERIFY(renamed->asset(QStringLiteral("EGTRAIN-Setup-1.10.0-x64.zip")) == nullptr);
	QVERIFY(renamed->asset(QStringLiteral("notes.txt")) == nullptr);
	// A release with renamed packages offers a package for every key.
	QVERIFY(releaseHasUpdatePackage(*renamed, QStringLiteral("windows-x64")));
	QVERIFY(releaseHasUpdatePackage(*renamed, QStringLiteral("windows-x64-installer")));
	QVERIFY(releaseHasUpdatePackage(*renamed, QStringLiteral("macos-arm64")));
	QVERIFY(releaseHasUpdatePackage(*renamed, QStringLiteral("linux-x86_64")));
	const auto legacy = releaseWith({"update-manifest.json", "QEGTRAIN-windows-x64.zip", "QEGTRAIN-macos-arm64.zip"});
	// A release offers only the packages of the key.
	QVERIFY(legacy.has_value());
	QVERIFY(releaseHasUpdatePackage(*legacy, QStringLiteral("windows-x64")));
	QVERIFY(releaseHasUpdatePackage(*legacy, QStringLiteral("macos-arm64")));
	QVERIFY(!releaseHasUpdatePackage(*legacy, QStringLiteral("windows-x64-installer")));
	QVERIFY(!releaseHasUpdatePackage(*legacy, QStringLiteral("linux-x86_64")));
	const auto wrongKind = releaseWith({"update-manifest.json", "EGTRAIN-Setup-1.10.0-x64.exe"});
	// An installer is not a package for the portable key.
	QVERIFY(wrongKind.has_value());
	QVERIFY(!releaseHasUpdatePackage(*wrongKind, QStringLiteral("windows-x64")));
}

// Package file list of the manifest entry.
void TestUpdateChecker::readsManifestFileList() {
	const auto parsedList = parseWindows(QByteArray(
		",\"files\":[\"QEGTRAIN.exe\",\"platforms/qwindows.dll\",\"Scenes/Paimpol/scene.json\"]"));
	// The manifest file list is retained.
	QVERIFY(parsedList.has_value());
	QCOMPARE(parsedList->files,
		QStringList({QStringLiteral("QEGTRAIN.exe"), QStringLiteral("platforms/qwindows.dll"),
			QStringLiteral("Scenes/Paimpol/scene.json")}));
	const auto parsedBackslash = parseWindows(QByteArray(
		",\"files\":[\"QEGTRAIN.exe\",\"platforms\\\\qwindows.dll\"]"));
	// The manifest file list uses forward slashes.
	QVERIFY(parsedBackslash.has_value());
	QVERIFY(parsedBackslash->files.contains(QStringLiteral("platforms/qwindows.dll")));
	const auto parsedWithoutList = parseWindows(QByteArray());
	// A manifest without a file list stays valid.
	QVERIFY(parsedWithoutList.has_value());
	QVERIFY(parsedWithoutList->files.isEmpty());
}

void TestUpdateChecker::rejectsInvalidFileList_data() {
	const QByteArray longName = "\"" + QByteArray(257, 'a') + ".dll\"";
	QTest::addColumn<QByteArray>("filesMember");
	QTest::newRow("a file list that is not an array") << QByteArray(",\"files\":\"QEGTRAIN.exe\"");
	QTest::newRow("a null file list") << QByteArray(",\"files\":null");
	QTest::newRow("a file list with a non-string entry") << ",\"files\":[" + kExecutable + ",7]";
	QTest::newRow("a file list with more than 4096 entries") << fileListOf(4096) + "]";
	QTest::newRow("an empty entry") << ",\"files\":[" + kExecutable + ",\"\"]";
	QTest::newRow("an entry longer than 260 characters") << ",\"files\":[" + kExecutable + "," + longName + "]";
	QTest::newRow("an absolute path") << ",\"files\":[" + kExecutable + ",\"/abs/file.dll\"]";
	QTest::newRow("a drive colon") << ",\"files\":[" + kExecutable + ",\"C:/dir/x.dll\"]";
	QTest::newRow("a stream colon") << ",\"files\":[" + kExecutable + ",\"a.dll:stream\"]";
	QTest::newRow("a NUL character") << ",\"files\":[" + kExecutable + ",\"a\\u0000.dll\"]";
	QTest::newRow("an empty segment") << ",\"files\":[" + kExecutable + ",\"a//b.dll\"]";
	QTest::newRow("a trailing slash") << ",\"files\":[" + kExecutable + ",\"a/\"]";
	QTest::newRow("a dot segment") << ",\"files\":[" + kExecutable + ",\"./a.dll\"]";
	QTest::newRow("a parent segment") << ",\"files\":[" + kExecutable + ",\"a/../b.dll\"]";
	QTest::newRow("a backslash parent segment") << ",\"files\":[" + kExecutable + ",\"..\\\\b.dll\"]";
	QTest::newRow("a file list without QEGTRAIN.exe") << QByteArray(",\"files\":[\"Qt6Core.dll\"]");
	QTest::newRow("an empty file list") << QByteArray(",\"files\":[]");
}

void TestUpdateChecker::rejectsInvalidFileList() {
	QFETCH(QByteArray, filesMember);
	QString listError;
	// The manifest rejects the file list.
	QVERIFY(!parseWindows(filesMember, &listError));
	QCOMPARE(listError, QStringLiteral("Update manifest has an invalid file list."));
}

void TestUpdateChecker::acceptsFileListAtLimits() {
	const QByteArray maxName = "\"" + QByteArray(256, 'a') + ".dll\"";
	QVERIFY2(parseWindows(fileListOf(4095) + "]").has_value(),
		"manifest accepts 4096 file entries");
	QVERIFY2(parseWindows(",\"files\":[" + kExecutable + "," + maxName + "]").has_value(),
		"manifest accepts a 260 character entry");
}

void TestUpdateChecker::checkSucceeds() {
	FakeTransport transport;
	UpdateChecker checker(&transport, 5000);
	Results results(checker);
	checker.check();
	// A started check sends one request and is pending.
	QVERIFY(checker.isChecking());
	QCOMPARE(transport.calls.size(), std::size_t(1));
	const HttpRequest& request = transport.calls.front().request;
	// The check requests the releases URL.
	QCOMPARE(request.url, UpdateChecker::releasesUrl());
	QVERIFY2(request.userAgent.startsWith("EGTRAIN/"), "the check sends an EGTRAIN user agent");
	// The check accepts the GitHub JSON type.
	QCOMPARE(request.accept, QByteArray("application/vnd.github+json"));

	Answer response;
	response.status = 200;
	response.body = releaseJson("v1.10.0");
	// The answer to the check arrives.
	QVERIFY(finishReply(transport, 0, response));
	QVERIFY(results.wait());
	// A release list gives one successful result.
	QCOMPARE(results.list.size(), 1);
	QVERIFY(results.list.first().success);
	QVERIFY(results.list.first().error.isEmpty());
	// The result holds the latest stable release.
	QVERIFY(results.list.first().release.has_value());
	QCOMPARE(results.list.first().release->tag, QStringLiteral("v1.10.0"));
	QVERIFY2(!checker.isChecking(), "a finished check is not pending");
}

void TestUpdateChecker::reportsFailureTexts() {
	UpdateCheckResult result;
	Answer response;
	response.failure = true;
	response.error = QStringLiteral("boom");
	// A failed request reports its error text.
	QVERIFY(runCheck(response, &result));
	QVERIFY(!result.success);
	QVERIFY(!result.release);
	QCOMPARE(result.error, QStringLiteral("boom"));

	response.error.clear();
	// A failed request without an error text reports the generic text.
	QVERIFY(runCheck(response, &result));
	QVERIFY(!result.success);
	QCOMPARE(result.error, QStringLiteral("GitHub update check failed."));

	response = Answer();
	response.status = 500;
	// An HTTP error status is reported with its code.
	QVERIFY(runCheck(response, &result));
	QVERIFY(!result.success);
	QCOMPARE(result.error, QStringLiteral("GitHub returned HTTP 500."));

	response.status = 204;
	QString parseError;
	parseLatestStableRelease(QByteArray(), &parseError);
	const QString expected = parseError.isEmpty() ? QStringLiteral("No stable release was found.") : parseError;
	// An empty answer reports what the release parser reports.
	QVERIFY(runCheck(response, &result));
	QVERIFY(!result.success);
	QVERIFY(!result.release);
	QCOMPARE(result.error, expected);

	response.status = 200;
	response.body = QByteArrayLiteral("not json");
	// An answer that is not JSON fails with a text.
	QVERIFY(runCheck(response, &result));
	QVERIFY(!result.success);
	QVERIFY(!result.error.isEmpty());
}

void TestUpdateChecker::timesOut() {
	FakeTransport transport;
	UpdateChecker checker(&transport, 30);
	Results results(checker);
	checker.check();
	QVERIFY2(results.wait(), "a check that never finishes times out");
	spin(50);
	// A timeout gives exactly one result.
	QCOMPARE(results.list.size(), 1);
	// The timeout result is a failure with the timeout text.
	QVERIFY(!results.list.first().success);
	QCOMPARE(results.list.first().error, QStringLiteral("The update check timed out."));
	// A timeout aborts the request once.
	QCOMPARE(transport.calls.size(), std::size_t(1));
	QCOMPARE(transport.calls.front().aborts, 1);
	QVERIFY2(!checker.isChecking(), "a timed out check is not pending");
}

void TestUpdateChecker::replacesPendingCheck() {
	FakeTransport transport;
	UpdateChecker checker(&transport, 5000);
	Results results(checker);
	checker.check();
	checker.check();
	// Each check sends a request.
	QCOMPARE(transport.calls.size(), std::size_t(2));
	// A new check aborts the pending request once.
	QCOMPARE(transport.calls.front().aborts, 1);
	QVERIFY2(checker.isChecking(), "the new check is pending");
	QVERIFY2(results.list.isEmpty(), "aborting the pending request gives no result");

	Answer response;
	response.status = 200;
	response.body = releaseJson("v1.10.0");
	// The answer to the new check arrives.
	QVERIFY(finishReply(transport, 1, response));
	QVERIFY(results.wait());
	spin(50);
	// Two checks in a row give one successful result.
	QCOMPARE(results.list.size(), 1);
	QVERIFY(results.list.first().success);
	// The answered request is not aborted.
	QCOMPARE(transport.calls.back().aborts, 0);
}

// A list that grows beyond the limit while it arrives is refused at once, and its request is cancelled.
void TestUpdateChecker::refusesGrowingReleaseList() {
	FakeTransport transport;
	UpdateChecker checker(&transport, 5000);
	Results results(checker);
	checker.check();
	const QByteArray half(kListLimit / 2, ' ');
	// A release list as large as the limit is still read.
	QVERIFY(sendChunk(transport, 0, half));
	QVERIFY(sendChunk(transport, 0, half));
	QVERIFY(results.list.isEmpty());
	QVERIFY(checker.isChecking());
	// A release list beyond the limit ends the check.
	QVERIFY(sendChunk(transport, 0, " "));
	QVERIFY(results.wait());
	spin(50);
	// A release list beyond the limit gives one failure that says it is too large.
	QCOMPARE(results.list.size(), 1);
	QVERIFY(!results.list.first().success);
	QVERIFY(!results.list.first().release);
	QCOMPARE(results.list.first().error, kTooLarge);
	// A release list beyond the limit cancels its request once.
	QCOMPARE(transport.calls.size(), std::size_t(1));
	QCOMPARE(transport.calls.front().aborts, 1);
	QVERIFY(!checker.isChecking());
}

// The limit holds also for a list that arrives with the end of the reply.
void TestUpdateChecker::refusesLargeReleaseListAtEnd() {
	UpdateCheckResult result;
	Answer response;
	response.status = 200;
	response.body = QByteArray(kListLimit + 1, ' ');
	// A release list beyond the limit at the end of the reply is refused.
	QVERIFY(runCheck(response, &result));
	QVERIFY(!result.success);
	QCOMPARE(result.error, kTooLarge);
}

// A list of exactly the limit, partly read before the end, is parsed whole.
void TestUpdateChecker::parsesReleaseListOfLimit() {
	FakeTransport exact;
	UpdateChecker exactChecker(&exact, 5000);
	Results exactResults(exactChecker);
	exactChecker.check();
	const QByteArray list = releaseJson("v1.10.0");
	FakeReply* reply = replyAt(exact, 0);
	// The start of the release list arrives.
	if (reply == nullptr)
		QFAIL("the start of the release list arrives");
	QVERIFY(sendChunk(exact, 0, list));
	reply->status = 200;
	reply->body += QByteArray(kListLimit - static_cast<int>(list.size()), ' ');
	emit reply->finished();
	// A release list as large as the limit gives its latest stable release.
	QVERIFY(exactResults.wait());
	QCOMPARE(exactResults.list.size(), 1);
	QVERIFY(exactResults.list.first().success);
	QVERIFY(exactResults.list.first().release.has_value());
	QCOMPARE(exactResults.list.first().release->tag, QStringLiteral("v1.10.0"));
}

QTEST_GUILESS_MAIN(TestUpdateChecker)
#include "test_updatechecker.moc"
