#include "app/DialogFolders.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "failed: " << message << '\n';
		std::exit(1);
	}
}

void requireFolder(const QString& actual, const QString& expected, const char* message) {
	if (actual != expected) {
		std::cerr << "failed: " << message << ": expected '" << expected.toStdString() << "', got '" << actual.toStdString() << "'\n";
		std::exit(1);
	}
}

const QString kDocuments = QStringLiteral("C:/Users/me/Documents");
const QString kHome = QStringLiteral("C:/Users/me");

QString windowsFolder(const QString& casePath, const QString& applicationDir = QStringLiteral("C:/Program Files/EGTRAIN")) {
	return dialog_folders::startFolder(casePath, applicationDir, kDocuments, kHome);
}
}

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);

	// Windows paths: backslashes, another letter case, dots and the prefix of long paths.
	requireFolder(windowsFolder("C:\\Program Files\\EGTRAIN\\Scenes\\Paimpol"), kDocuments, "installed scene with backslashes");
	requireFolder(windowsFolder("c:\\program files\\egtrain\\scenes\\paimpol"), kDocuments, "installed scene in another letter case");
	requireFolder(windowsFolder("C:\\PROGRAM FILES\\EGTRAIN\\SCENES\\Paimpol", "c:\\Program Files\\EGTRAIN"), kDocuments,
		"installed scene with the installation folder in backslashes");
	requireFolder(windowsFolder("C:\\Program Files\\EGTRAIN\\Scenes\\sample.egscene"), kDocuments, "installed bundle");
	requireFolder(windowsFolder("C:\\Program Files\\EGTRAIN\\Scenes\\..\\Scenes\\Paimpol"), kDocuments, "installed scene through dots");
	requireFolder(windowsFolder("\\\\?\\C:\\Program Files\\EGTRAIN\\Scenes\\Paimpol"), kDocuments, "installed scene with the long path prefix");
	requireFolder(windowsFolder("C:\\Program Files\\EGTRAIN\\my.egscene"), kDocuments, "bundle next to the program");
	requireFolder(windowsFolder("C:\\Program Files\\EGTRAIN\\"), kDocuments, "installation folder");
	requireFolder(windowsFolder("C:/Program Files/EGTRAIN/Scenes/Paimpol", "C:/Program Files/EGTRAIN/"), kDocuments,
		"installation folder with a trailing slash");

	// A case outside the installation: its folder.
	requireFolder(windowsFolder("D:\\Cases\\Mine\\case.egscene"), "D:/Cases/Mine", "bundle outside the installation");
	requireFolder(windowsFolder("d:\\cases\\mine\\Scene Folder"), "d:/cases/mine", "scene folder outside the installation");
	requireFolder(windowsFolder("C:\\Program Files\\EGTRAIN-copy\\x.egscene"), "C:/Program Files/EGTRAIN-copy", "prefix sibling of the installation");
	requireFolder(windowsFolder("C:\\Program Files\\EGTRAIN\\..\\Cases\\x.egscene"), "C:/Program Files/Cases", "case reached through dots");
	requireFolder(windowsFolder("D:\\case.egscene"), "D:/", "bundle at the root of a drive");

	// No case, and no Documents folder.
	requireFolder(windowsFolder({}), kDocuments, "no case");
	requireFolder(dialog_folders::startFolder({}, "C:/Program Files/EGTRAIN", {}, kHome), kHome, "no case and no Documents");
	requireFolder(dialog_folders::startFolder("C:\\Program Files\\EGTRAIN\\Scenes\\Paimpol", "C:/Program Files/EGTRAIN", {}, kHome), kHome,
		"installed scene and no Documents");
	requireFolder(dialog_folders::startFolder("D:\\Cases\\x.egscene", {}, kDocuments, kHome), "D:/Cases", "unknown installation folder");

	// The folders of the other layouts: a macOS bundle and a Linux prefix.
	requireFolder(dialog_folders::startFolder("/Applications/EGTRAIN.app/Contents/Resources/Scenes/Paimpol", "/Applications/EGTRAIN.app/Contents/MacOS",
					  "/Users/me/Documents", "/Users/me"),
		"/Users/me/Documents", "scene in the bundle resources");
	requireFolder(dialog_folders::startFolder("/usr/share/EGTRAIN/Scenes/Paimpol", "/usr/bin", "/home/me/Documents", "/home/me"),
		"/home/me/Documents", "scene in the Linux share folder");
	requireFolder(dialog_folders::startFolder("/home/me/cases/x.egscene", "/usr/bin", "/home/me/Documents", "/home/me"),
		"/home/me/cases", "bundle outside the Linux prefix");

	// A relative case path and the working directory of a Start Menu launch, which is the installation folder.
	QTemporaryDir temp;
	require(temp.isValid(), "cannot create temporary directory");
	const QString installation = temp.path() + "/installation";
	require(QDir().mkpath(installation + "/Scenes/Paimpol"), "cannot create installation");
	require(QDir().mkpath(temp.path() + "/cases/mine"), "cannot create cases");
	const QString before = QDir::currentPath();
	require(QDir::setCurrent(installation), "cannot change the working directory");
	requireFolder(dialog_folders::startFolder("Scenes/Paimpol", installation, kDocuments, kHome), kDocuments, "relative installed scene");
	const QString start = dialog_folders::startFolder();
	require(!dialog_folders::inInstallation(start, installation), "default start folder is inside the installation");
	require(QDir::cleanPath(start) != QDir::cleanPath(QDir::currentPath()), "default start folder is the working directory");
	require(dialog_folders::suggestedPath("run_summary.csv").endsWith("/run_summary.csv"), "suggested path lacks the file name");
	requireFolder(QFileInfo(dialog_folders::suggestedPath("a.csv", temp.path() + "/cases/mine/case.egscene")).path(),
		QDir::cleanPath(temp.path() + "/cases/mine"), "suggested path beside the case");
	requireFolder(dialog_folders::suggestedPath({}, temp.path() + "/cases/mine/case.egscene"), QDir::cleanPath(temp.path() + "/cases/mine"),
		"folder when there is no file name");
	QDir::setCurrent(before);
	return 0;
}
