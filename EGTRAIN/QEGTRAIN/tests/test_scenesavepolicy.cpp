#include "app/SceneSavePolicy.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "failed: " << message << '\n';
		std::exit(1);
	}
}

void mkdir(const QString& path) {
	require(QDir().mkpath(path), "cannot create test directory");
}

void touch(const QString& path) {
	QFile file(path);
	require(file.open(QIODevice::WriteOnly), "cannot create test file");
}

bool symlink(const QString& target, const QString& alias) {
	std::error_code error;
	std::filesystem::create_directory_symlink(std::filesystem::u8path(target.toUtf8().constData()),
		std::filesystem::u8path(alias.toUtf8().constData()), error);
	if (error) {
		const bool unsupported = error == std::errc::operation_not_supported || error == std::errc::function_not_supported
			|| error == std::errc::operation_not_permitted || error == std::errc::permission_denied;
		if (!unsupported) std::cerr << error.message() << '\n';
		require(unsupported, "unexpected symlink creation failure");
		std::cout << "SKIP symlink " << alias.toStdString() << ": " << error.message() << '\n';
	}
	return !error;
}
}

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	require(temp.isValid(), "cannot create temporary directory");
	const QString base = temp.path();
	const QString appDir = base + "/installation/bin";
	mkdir(appDir);
	const QString checkout = base + "/checkout/Scenes";
	mkdir(checkout);
	touch(checkout + "/external.egscene");
	require(!isInstalledScenePath(checkout, appDir), "independent checkout was protected");
	require(!isInstalledScenePath(checkout + "/external.egscene", appDir), "external bundle was protected");
	require(!isInstalledScenePath({}, appDir), "empty path was protected");
	require(!isInstalledScenePath(checkout, {}), "empty application path invented roots");

	// Each application-relative layout, first absent and then populated.
	for (const QString& layout : {QStringLiteral("Scenes"), QStringLiteral("../Resources/Scenes"), QStringLiteral("../share/EGTRAIN/Scenes")}) {
		const QString root = QDir::cleanPath(QDir(appDir).absoluteFilePath(layout));
		require(isInstalledScenePath(root, appDir), "missing installed root lost lexical protection");
		require(isInstalledScenePath(root + "/new/sub/copy.egscene", appDir), "multiple missing suffixes lost protection");
		mkdir(root + "/Paimpol");
		touch(root + "/Paimpol/scene.json");
		touch(root + "/sample.egscene");
		require(isInstalledScenePath(root, appDir), "installed root was not protected");
		require(isInstalledScenePath(root + "/Paimpol", appDir), "installed scene folder was not protected");
		require(isInstalledScenePath(root + "/Paimpol/scene.json", appDir), "installed file was not protected");
		require(isInstalledScenePath(root + "/sample.egscene", appDir), "installed bundle was not protected");
		require(isInstalledScenePath(root + "/Paimpol/.././sample.egscene", appDir), "normalized dots bypassed protection");
		require(isInstalledScenePath(root + "/new.egscene", appDir), "missing bundle leaf was not protected");
		require(isInstalledScenePath(root.toUpper() + "/NEW/sub/scene.egscene", appDir), "case-only spelling bypassed protection");
		const QString caseSibling = QFileInfo(root).absolutePath() + "/scenes";
		mkdir(caseSibling);
		require(isInstalledScenePath(caseSibling + "/copy.egscene", appDir), "case-only sibling was not conservatively protected");
		mkdir(root + "-copy");
		require(!isInstalledScenePath(root + "-copy/copy.egscene", appDir), "prefix sibling was protected");
		require(!isInstalledScenePath(root + "/../outside.egscene", appDir), "dot escape was protected");

		const QString alias = root + "-alias";
		if (symlink(root, alias)) {
			require(isInstalledScenePath(alias + "/Paimpol", appDir), "external symlink alias bypassed protection");
			require(isInstalledScenePath(alias + "/sample.egscene", appDir), "bundle through alias bypassed protection");
			require(isInstalledScenePath(alias + "/new/sub/copy.egscene", appDir), "missing alias suffixes bypassed protection");
		}
	}

	// A working-directory Scenes folder is not an installed namespace.
	const QString cwd = QDir::currentPath();
	require(QDir::setCurrent(base + "/checkout"), "cannot change test working directory");
	require(!isInstalledScenePath("Scenes", appDir), "working-directory Scenes was treated as installed");
	require(isInstalledScenePath("../installation/bin/Scenes/Paimpol", appDir), "relative installed path bypassed protection");
	require(QDir::setCurrent(cwd), "cannot restore working directory");

	// A deliberately symlinked installed root protects its direct target and other aliases too.
	const QString linkedApp = base + "/linked-app";
	mkdir(linkedApp);
	if (symlink(checkout, linkedApp + "/Scenes")) {
		require(isInstalledScenePath(linkedApp + "/Scenes/external.egscene", linkedApp), "installed-root link bypassed protection");
		require(isInstalledScenePath(checkout, linkedApp), "direct installed-root target bypassed protection");
		require(isInstalledScenePath(checkout + "/external.egscene", linkedApp), "direct target bundle bypassed protection");
		require(isInstalledScenePath(checkout + "/new/sub/copy.egscene", linkedApp), "missing direct target suffixes bypassed protection");
		if (symlink(checkout, base + "/checkout-alias"))
			require(isInstalledScenePath(base + "/checkout-alias/new/sub/copy.egscene", linkedApp), "second target alias bypassed protection");
		require(!isInstalledScenePath(base + "/other/Scenes/copy.egscene", linkedApp), "unrelated path was protected by installed-root link");
	}

	// Resolve a missing installed root through an existing application-directory alias.
	if (symlink(linkedApp, base + "/app-alias")) {
		const QString missingRoot = linkedApp + "/Resources/Scenes/new/sub/copy.egscene";
		require(isInstalledScenePath(missingRoot, base + "/app-alias/bin"), "missing root through ancestor alias lost protection");
	}

	// A resolved filesystem root still contains its descendants (without a doubled slash).
	QDir filesystemRoot(base);
	while (!filesystemRoot.isRoot())
		require(filesystemRoot.cdUp(), "cannot find temporary directory root");
	const QString rootApp = base + "/root-app";
	mkdir(rootApp);
	if (symlink(filesystemRoot.absolutePath(), rootApp + "/Scenes"))
		require(isInstalledScenePath(checkout + "/external.egscene", rootApp), "filesystem-root target lost subtree protection");
	// On Windows the system drive need not contain the temporary directory.
	if (filesystemRoot.absolutePath().compare(QDir::rootPath(), Qt::CaseInsensitive) != 0) {
		const QString systemRootApp = base + "/system-root-app";
		mkdir(systemRootApp);
		if (symlink(QDir::rootPath(), systemRootApp + "/Scenes"))
			require(!isInstalledScenePath(checkout + "/external.egscene", systemRootApp), "different drive was protected");
	}

	// A dangling root retains only lexical protection, not an invented resolved target.
	const QString danglingApp = base + "/dangling-app";
	mkdir(danglingApp);
	if (symlink(base + "/absent-target", danglingApp + "/Scenes")) {
		require(scene_save_policy::resolvedPath(danglingApp + "/Scenes").isEmpty(), "dangling link acquired a resolved spelling");
		require(scene_save_policy::resolvedPath(danglingApp + "/Scenes/new/sub").isEmpty(), "dangling ancestor acquired a resolved spelling");
		require(isInstalledScenePath(danglingApp + "/Scenes/new/sub/copy.egscene", danglingApp), "dangling root lost lexical protection");
		require(!isInstalledScenePath(base + "/absent-target/new/sub/copy.egscene", danglingApp), "dangling root invented a resolved target");
		require(!isInstalledScenePath(checkout, danglingApp), "empty canonical root protected unrelated data");
	}
	if (symlink(base + "/missing", base + "/dangling-alias"))
		require(!isInstalledScenePath(base + "/dangling-alias/new/sub/copy.egscene", appDir), "dangling candidate invented installed ownership");

	std::cout << "Scene save policy checks passed\n";
	return 0;
}
