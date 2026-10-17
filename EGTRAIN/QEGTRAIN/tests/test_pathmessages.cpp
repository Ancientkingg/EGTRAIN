#include "app/PathMessages.h"
#include "widgets/DisplayPath.h"

#include <QCoreApplication>
#include <QDir>

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

	// A dialog returns forward slashes. The texts show the separator of the system, here a backslash.
	display_path::separator() = QLatin1Char('\\');
	requireText(path_message::writeFailed("write the image to", "C:/Users/me/Documents/table.png"),
		"Could not write the image to:\nC:\\Users\\me\\Documents\\table.png", "write failure on Windows");
	requireText(path_message::outputFolder("D:/Runs/Case 1"), "Output folder: D:\\Runs\\Case 1", "output folder on Windows");

	// With the separator of this system the text is what QDir gives for the same path.
	display_path::separator() = QDir::separator();
	requireText(path_message::writeFailed("export the data and provenance to", "/data/out/table.csv"),
		"Could not export the data and provenance to:\n" + QDir::toNativeSeparators("/data/out/table.csv"), "write failure here");
	requireText(path_message::outputFolder("/data/out"), "Output folder: " + QDir::toNativeSeparators("/data/out"), "output folder here");
	return 0;
}
