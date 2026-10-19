#include "app/CannotOpenMessage.h"
#include "widgets/DisplayPath.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {
void requireText(const QString& actual, const QString& expected, const char* message) {
	if (actual != expected) {
		std::cerr << "failed: " << message << ": expected '" << expected.toStdString() << "', got '" << actual.toStdString() << "'\n";
		std::exit(1);
	}
}
}

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	QFile broken(QDir(temp.path()).filePath("broken.egscene"));
	if (!temp.isValid() || !QDir(temp.path()).mkdir("plain") || !QDir(temp.path()).mkdir("scene")
		|| !broken.open(QIODevice::WriteOnly)) {
		std::cerr << "failed: the test files could not be created\n";
		return 1;
	}
	broken.close();
	const QString brokenPath = broken.fileName();

	// The file name and a plain sentence come first, the internal error after them.
	requireText(cannot_open::title(), "Cannot Open Case Study", "title");
	requireText(cannot_open::text(brokenPath, "error [scene.bundle.archive] failed finding central directory"),
		"broken.egscene could not be opened.\nThe file is damaged or is not an EGTRAIN case study.\n\n"
		"error [scene.bundle.archive] failed finding central directory",
		"text with an error");
	requireText(cannot_open::text(brokenPath, {}),
		"broken.egscene could not be opened.\nThe file is damaged or is not an EGTRAIN case study.", "text without an error");
	if (cannot_open::text(brokenPath, "x").contains(temp.path())) {
		std::cerr << "failed: the text shows the folder of the file\n";
		return 1;
	}

	// A path that cannot be found or read is not called damaged. A path without a file name is shown with the
	// separators of the system.
	display_path::separator() = QLatin1Char('\\');
	requireText(cannot_open::text("/no/such/place/", {}), "\\no\\such\\place\\ could not be opened.\nEGTRAIN could not read it.",
		"text for a path that does not exist");
	display_path::separator() = QDir::separator();

	// A folder is not a damaged file: the sentence says what a scene folder holds.
	QFile sceneJson(QDir(temp.path()).filePath("scene/scene.json"));
	if (!sceneJson.open(QIODevice::WriteOnly)) {
		std::cerr << "failed: scene.json could not be created\n";
		return 1;
	}
	sceneJson.close();
	requireText(cannot_open::text(QDir(temp.path()).filePath("plain"), "error x"),
		"plain could not be opened.\nThe folder holds no EGTRAIN scene. A scene folder holds a scene.json file.\n\nerror x",
		"text for a folder without scene.json");
	requireText(cannot_open::text(QDir(temp.path()).filePath("scene"), {}),
		"scene could not be opened.\nThe folder holds a damaged or incomplete EGTRAIN scene.",
		"text for a folder with a damaged scene.json");
	return 0;
}
