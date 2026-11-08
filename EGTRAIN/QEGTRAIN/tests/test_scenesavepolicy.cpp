#include "app/SceneSavePolicy.h"

#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <filesystem>
#include <optional>

namespace {
bool mkdir(const QString& path) {
	return QDir().mkpath(path);
}

bool touch(const QString& path) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly);
}

// The outcome of making a directory link.
enum class Link { Made,
	Unsupported,
	Failed };

// Makes alias a directory link to target. A file system or a user that cannot make links gives Unsupported and a SKIP
// line, any other error gives Failed and its message.
Link symlink(const QString& target, const QString& alias) {
	std::error_code error;
	std::filesystem::create_directory_symlink(std::filesystem::u8path(target.toUtf8().constData()),
		std::filesystem::u8path(alias.toUtf8().constData()), error);
	if (!error)
		return Link::Made;
	const bool unsupported = error == std::errc::operation_not_supported || error == std::errc::function_not_supported
		|| error == std::errc::operation_not_permitted || error == std::errc::permission_denied;
	if (!unsupported) {
		qWarning("%s", error.message().c_str());
		return Link::Failed;
	}
	qInfo("SKIP symlink %s: %s", qPrintable(alias), error.message().c_str());
	return Link::Unsupported;
}
}

class TestSceneSavePolicy : public QObject {
	Q_OBJECT

private slots:
	void init();
	void protectsNothingOutsideInstallation();
	void protectsInstalledLayout_data();
	void protectsInstalledLayout();
	void ignoresWorkingDirectoryScenes();
	void protectsLinkedRoots();
	void filesystemRootContainsDescendants();
	void danglingLinksKeepLexicalProtection();

private:
	// A new temporary directory for each slot, with an application folder and an independent checkout that holds a
	// bundle.
	std::optional<QTemporaryDir> m_temp;
	QString m_base;
	QString m_appDir;
	QString m_checkout;
};

void TestSceneSavePolicy::init() {
	m_temp.emplace();
	QVERIFY2(m_temp->isValid(), "cannot create temporary directory");
	m_base = m_temp->path();
	m_appDir = m_base + "/installation/bin";
	QVERIFY2(mkdir(m_appDir), "cannot create test directory");
	m_checkout = m_base + "/checkout/Scenes";
	QVERIFY2(mkdir(m_checkout), "cannot create test directory");
	QVERIFY2(touch(m_checkout + "/external.egscene"), "cannot create test file");
}

void TestSceneSavePolicy::protectsNothingOutsideInstallation() {
	QVERIFY2(!isInstalledScenePath(m_checkout, m_appDir), "independent checkout was protected");
	QVERIFY2(!isInstalledScenePath(m_checkout + "/external.egscene", m_appDir), "external bundle was protected");
	QVERIFY2(!isInstalledScenePath({}, m_appDir), "empty path was protected");
	QVERIFY2(!isInstalledScenePath(m_checkout, {}), "empty application path invented roots");
}

// Each application-relative layout, first absent and then populated.
void TestSceneSavePolicy::protectsInstalledLayout_data() {
	QTest::addColumn<QString>("layout");
	for (const char* layout : {"Scenes", "../Resources/Scenes", "../share/EGTRAIN/Scenes"})
		QTest::newRow(layout) << layout;
}

void TestSceneSavePolicy::protectsInstalledLayout() {
	QFETCH(QString, layout);
	const QString root = QDir::cleanPath(QDir(m_appDir).absoluteFilePath(layout));
	QVERIFY2(isInstalledScenePath(root, m_appDir), "missing installed root lost lexical protection");
	QVERIFY2(isInstalledScenePath(root + "/new/sub/copy.egscene", m_appDir), "multiple missing suffixes lost protection");
	QVERIFY2(mkdir(root + "/Paimpol"), "cannot create test directory");
	QVERIFY2(touch(root + "/Paimpol/scene.json"), "cannot create test file");
	QVERIFY2(touch(root + "/sample.egscene"), "cannot create test file");
	QVERIFY2(isInstalledScenePath(root, m_appDir), "installed root was not protected");
	QVERIFY2(isInstalledScenePath(root + "/Paimpol", m_appDir), "installed scene folder was not protected");
	QVERIFY2(isInstalledScenePath(root + "/Paimpol/scene.json", m_appDir), "installed file was not protected");
	QVERIFY2(isInstalledScenePath(root + "/sample.egscene", m_appDir), "installed bundle was not protected");
	QVERIFY2(isInstalledScenePath(root + "/Paimpol/.././sample.egscene", m_appDir), "normalized dots bypassed protection");
	QVERIFY2(isInstalledScenePath(root + "/new.egscene", m_appDir), "missing bundle leaf was not protected");
	QVERIFY2(isInstalledScenePath(root.toUpper() + "/NEW/sub/scene.egscene", m_appDir), "case-only spelling bypassed protection");
	const QString caseSibling = QFileInfo(root).absolutePath() + "/scenes";
	QVERIFY2(mkdir(caseSibling), "cannot create test directory");
	QVERIFY2(isInstalledScenePath(caseSibling + "/copy.egscene", m_appDir), "case-only sibling was not conservatively protected");
	QVERIFY2(mkdir(root + "-copy"), "cannot create test directory");
	QVERIFY2(!isInstalledScenePath(root + "-copy/copy.egscene", m_appDir), "prefix sibling was protected");
	QVERIFY2(!isInstalledScenePath(root + "/../outside.egscene", m_appDir), "dot escape was protected");

	const QString alias = root + "-alias";
	const Link aliasLink = symlink(root, alias);
	QVERIFY2(aliasLink != Link::Failed, "unexpected symlink creation failure");
	if (aliasLink == Link::Made) {
		QVERIFY2(isInstalledScenePath(alias + "/Paimpol", m_appDir), "external symlink alias bypassed protection");
		QVERIFY2(isInstalledScenePath(alias + "/sample.egscene", m_appDir), "bundle through alias bypassed protection");
		QVERIFY2(isInstalledScenePath(alias + "/new/sub/copy.egscene", m_appDir), "missing alias suffixes bypassed protection");
	}
}

// A working-directory Scenes folder is not an installed namespace.
void TestSceneSavePolicy::ignoresWorkingDirectoryScenes() {
	QVERIFY2(mkdir(m_appDir + "/Scenes/Paimpol"), "cannot create test directory");
	const QString cwd = QDir::currentPath();
	const auto restore = qScopeGuard([&cwd] { QDir::setCurrent(cwd); });
	QVERIFY2(QDir::setCurrent(m_base + "/checkout"), "cannot change test working directory");
	QVERIFY2(!isInstalledScenePath("Scenes", m_appDir), "working-directory Scenes was treated as installed");
	QVERIFY2(isInstalledScenePath("../installation/bin/Scenes/Paimpol", m_appDir), "relative installed path bypassed protection");
	QVERIFY2(QDir::setCurrent(cwd), "cannot restore working directory");
}

void TestSceneSavePolicy::protectsLinkedRoots() {
	// A deliberately symlinked installed root protects its direct target and other aliases too.
	const QString linkedApp = m_base + "/linked-app";
	QVERIFY2(mkdir(linkedApp), "cannot create test directory");
	const Link rootLink = symlink(m_checkout, linkedApp + "/Scenes");
	QVERIFY2(rootLink != Link::Failed, "unexpected symlink creation failure");
	if (rootLink == Link::Made) {
		QVERIFY2(isInstalledScenePath(linkedApp + "/Scenes/external.egscene", linkedApp), "installed-root link bypassed protection");
		QVERIFY2(isInstalledScenePath(m_checkout, linkedApp), "direct installed-root target bypassed protection");
		QVERIFY2(isInstalledScenePath(m_checkout + "/external.egscene", linkedApp), "direct target bundle bypassed protection");
		QVERIFY2(isInstalledScenePath(m_checkout + "/new/sub/copy.egscene", linkedApp),
			"missing direct target suffixes bypassed protection");
		const Link aliasLink = symlink(m_checkout, m_base + "/checkout-alias");
		QVERIFY2(aliasLink != Link::Failed, "unexpected symlink creation failure");
		if (aliasLink == Link::Made)
			QVERIFY2(isInstalledScenePath(m_base + "/checkout-alias/new/sub/copy.egscene", linkedApp),
				"second target alias bypassed protection");
		QVERIFY2(!isInstalledScenePath(m_base + "/other/Scenes/copy.egscene", linkedApp),
			"unrelated path was protected by installed-root link");
	}

	// Resolve a missing installed root through an existing application-directory alias.
	const Link appLink = symlink(linkedApp, m_base + "/app-alias");
	QVERIFY2(appLink != Link::Failed, "unexpected symlink creation failure");
	if (appLink == Link::Made) {
		const QString missingRoot = linkedApp + "/Resources/Scenes/new/sub/copy.egscene";
		QVERIFY2(isInstalledScenePath(missingRoot, m_base + "/app-alias/bin"), "missing root through ancestor alias lost protection");
	}
}

// A resolved filesystem root still contains its descendants (without a doubled slash).
void TestSceneSavePolicy::filesystemRootContainsDescendants() {
	QDir filesystemRoot(m_base);
	while (!filesystemRoot.isRoot())
		QVERIFY2(filesystemRoot.cdUp(), "cannot find temporary directory root");
	const QString rootApp = m_base + "/root-app";
	QVERIFY2(mkdir(rootApp), "cannot create test directory");
	const Link rootLink = symlink(filesystemRoot.absolutePath(), rootApp + "/Scenes");
	QVERIFY2(rootLink != Link::Failed, "unexpected symlink creation failure");
	if (rootLink == Link::Made)
		QVERIFY2(isInstalledScenePath(m_checkout + "/external.egscene", rootApp), "filesystem-root target lost subtree protection");
	// On Windows the system drive need not contain the temporary directory.
	if (filesystemRoot.absolutePath().compare(QDir::rootPath(), Qt::CaseInsensitive) != 0) {
		const QString systemRootApp = m_base + "/system-root-app";
		QVERIFY2(mkdir(systemRootApp), "cannot create test directory");
		const Link systemLink = symlink(QDir::rootPath(), systemRootApp + "/Scenes");
		QVERIFY2(systemLink != Link::Failed, "unexpected symlink creation failure");
		if (systemLink == Link::Made)
			QVERIFY2(!isInstalledScenePath(m_checkout + "/external.egscene", systemRootApp), "different drive was protected");
	}
}

// A dangling root retains only lexical protection, not an invented resolved target.
void TestSceneSavePolicy::danglingLinksKeepLexicalProtection() {
	const QString danglingApp = m_base + "/dangling-app";
	QVERIFY2(mkdir(danglingApp), "cannot create test directory");
	const Link rootLink = symlink(m_base + "/absent-target", danglingApp + "/Scenes");
	QVERIFY2(rootLink != Link::Failed, "unexpected symlink creation failure");
	if (rootLink == Link::Made) {
		QVERIFY2(scene_save_policy::resolvedPath(danglingApp + "/Scenes").isEmpty(), "dangling link acquired a resolved spelling");
		QVERIFY2(scene_save_policy::resolvedPath(danglingApp + "/Scenes/new/sub").isEmpty(),
			"dangling ancestor acquired a resolved spelling");
		QVERIFY2(isInstalledScenePath(danglingApp + "/Scenes/new/sub/copy.egscene", danglingApp), "dangling root lost lexical protection");
		QVERIFY2(!isInstalledScenePath(m_base + "/absent-target/new/sub/copy.egscene", danglingApp),
			"dangling root invented a resolved target");
		QVERIFY2(!isInstalledScenePath(m_checkout, danglingApp), "empty canonical root protected unrelated data");
	}
	const Link aliasLink = symlink(m_base + "/missing", m_base + "/dangling-alias");
	QVERIFY2(aliasLink != Link::Failed, "unexpected symlink creation failure");
	if (aliasLink == Link::Made)
		QVERIFY2(!isInstalledScenePath(m_base + "/dangling-alias/new/sub/copy.egscene", m_appDir),
			"dangling candidate invented installed ownership");
}

QTEST_GUILESS_MAIN(TestSceneSavePolicy)
#include "test_scenesavepolicy.moc"
