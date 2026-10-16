#include "app/CannotOpenMessage.h"

#include <QCoreApplication>

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

	// The file name and a plain sentence come first, the internal error after them.
	requireText(cannot_open::title(), "Cannot Open Case Study", "title");
	requireText(cannot_open::text("/data/cases/broken.egscene", "error [scene.bundle.archive] failed finding central directory"),
		"broken.egscene could not be opened.\nThe file is damaged or is not an EGTRAIN case study.\n\n"
		"error [scene.bundle.archive] failed finding central directory",
		"text with an error");
	requireText(cannot_open::text("/data/cases/broken.egscene", {}),
		"broken.egscene could not be opened.\nThe file is damaged or is not an EGTRAIN case study.", "text without an error");
	if (cannot_open::text("/data/cases/broken.egscene", "x").contains("/data/cases")) {
		std::cerr << "failed: the text shows the folder of the file\n";
		return 1;
	}
	return 0;
}
