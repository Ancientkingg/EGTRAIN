#include "app/SceneOpenRequest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static bool writeFile(const QString& path, const QByteArray& contents) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

static bool exactlyOne(const SceneOpenChoice& choice) {
	return choice.path.isEmpty() != choice.rejection.isEmpty();
}

static bool accepted(const QString& path, const char* message) {
	const SceneOpenChoice choice = classifyOpenPaths({path});
	return expect(exactlyOne(choice) && choice.path == QFileInfo(path).absoluteFilePath(), message);
}

static bool rejected(const QStringList& paths, const QString& rejection, const char* message) {
	const SceneOpenChoice choice = classifyOpenPaths(paths);
	return expect(exactlyOne(choice) && choice.path.isEmpty() && choice.rejection == rejection, message);
}

static bool testClassify(const QString& base) {
	const QDir dir(base);
	// The name has two characters outside the Western Windows code page.
	const QString unicodeName = QString::fromUtf16(u"\u00e9\u6771\u4eac");
	const QString plain = dir.filePath("case.egscene");
	const QString spaces = dir.filePath("my case study.egscene");
	const QString upper = dir.filePath("CASE.EGSCENE");
	const QString doubleOk = dir.filePath("x.tar.egscene");
	const QString doubleBad = dir.filePath("x.egscene.txt");
	const QString text = dir.filePath("notes.txt");
	const QString unicode = dir.filePath(unicodeName + ".egscene");
	const QString directory = dir.filePath("x.egscene");
	const QString textDirectory = dir.filePath("folder.txt");
	const QString missing = dir.filePath("missing.egscene");
	const QString missingText = dir.filePath("missing.txt");
	bool ok = expect(writeFile(plain, "a") && writeFile(spaces, "a") && writeFile(upper, "a") && writeFile(doubleOk, "a")
			&& writeFile(doubleBad, "a") && writeFile(text, "a") && writeFile(unicode, "a")
			&& QDir().mkpath(directory) && QDir().mkpath(textDirectory),
		"classification fixtures are writable");
	ok &= expect(QFileInfo::exists(unicode), "the file with the non-ASCII name exists");

	ok &= rejected({}, "no file was given", "an empty list is rejected");
	ok &= rejected({plain, spaces}, "open exactly one .egscene file", "two valid files are rejected");
	ok &= rejected({plain, missing}, "open exactly one .egscene file", "the count is checked before the files");
	ok &= rejected({missing}, "the file does not exist", "a missing file is rejected");
	ok &= rejected({missingText}, "the file does not exist", "existence is checked before the extension");
	ok &= rejected({QString()}, "the file does not exist", "an empty path is rejected as missing");
	ok &= rejected({directory}, "an .egscene file is needed, not a directory", "a directory called x.egscene is rejected");
	ok &= rejected({textDirectory}, "an .egscene file is needed, not a directory", "the kind is checked before the extension");
	ok &= rejected({text}, "the file must use the .egscene extension", "a .txt file is rejected");
	ok &= rejected({doubleBad}, "the file must use the .egscene extension", "x.egscene.txt is rejected");

	ok &= accepted(plain, "a plain name is accepted");
	ok &= accepted(spaces, "a name with spaces is accepted");
	ok &= accepted(upper, "the extension is compared without regard to case");
	ok &= accepted(doubleOk, "x.tar.egscene is accepted");
	ok &= accepted(unicode, "a name with non-ASCII characters is accepted");
	ok &= expect(classifyOpenPaths({unicode}).path == QFileInfo(unicode).absoluteFilePath()
			&& classifyOpenPaths({unicode}).path.contains(unicodeName),
		"the accepted path keeps the non-ASCII characters");

	const QString previous = QDir::currentPath();
	ok &= expect(QDir::setCurrent(base), "the current directory can be changed");
	const QString relative = QStringLiteral("case.egscene");
	const SceneOpenChoice relativeChoice = classifyOpenPaths({relative});
	ok &= expect(exactlyOne(relativeChoice) && relativeChoice.path == QFileInfo(relative).absoluteFilePath()
			&& QFileInfo(relativeChoice.path).isAbsolute() && relativeChoice.path != relative,
		"a relative path is made absolute");
	// Windows cannot delete the current directory.
	ok &= expect(QDir::setCurrent(previous), "the current directory is restored");
	return ok;
}

static bool testPositional() {
	const QStringList withValue = {"-n", "-h", "-g", "--seed", "--scene"};
	bool ok = expect(positionalArguments({}, withValue).isEmpty(), "no arguments give no positional argument");
	ok &= expect(positionalArguments({"QEGTRAIN"}, withValue).isEmpty(), "the program name alone gives none");
	ok &= expect(positionalArguments({"case.egscene"}, withValue).isEmpty(), "the first entry is the program and is never returned");
	ok &= expect(positionalArguments({"QEGTRAIN", "case.egscene"}, withValue) == QStringList({"case.egscene"}),
		"one positional argument is returned");
	ok &= expect(positionalArguments({"QEGTRAIN", "-g", "0", "-n", "2", "case.egscene"}, withValue) == QStringList({"case.egscene"}),
		"a positional argument after options is returned");
	ok &= expect(positionalArguments({"QEGTRAIN", "-h", "1"}, withValue).isEmpty(), "the value of an option is skipped");
	ok &= expect(positionalArguments({"QEGTRAIN", "-h", "1"}, {}) == QStringList({"1"}),
		"a value is positional when its option is not in the list");
	ok &= expect(positionalArguments({"QEGTRAIN", "--scene", "a.egscene", "b.egscene"}, withValue) == QStringList({"b.egscene"}),
		"the value of --scene is not positional");
	ok &= expect(positionalArguments({"QEGTRAIN", "-h", "-5", "x"}, withValue) == QStringList({"x"}),
		"a value that starts with a dash is skipped as a value");
	ok &= expect(positionalArguments({"QEGTRAIN", "-g"}, withValue).isEmpty(), "an option without a value at the end gives none");
	ok &= expect(positionalArguments({"QEGTRAIN", "--interactive", "--detailed-trajectories", "x"}, withValue) == QStringList({"x"}),
		"flags without a value skip nothing after them");
	ok &= expect(positionalArguments({"QEGTRAIN", "-unknown", "word", "x"}, withValue) == QStringList({"word", "x"}),
		"an unknown option is skipped without its value");
	ok &= expect(positionalArguments({"QEGTRAIN", "-x.egscene"}, withValue).isEmpty(), "a name that starts with a dash is never positional");
	ok &= expect(positionalArguments({"QEGTRAIN", "a.egscene", "-g", "0", "b.egscene"}, withValue) == QStringList({"a.egscene", "b.egscene"}),
		"positional arguments keep their order");
	return ok;
}

static bool testOptionValue() {
	bool ok = expect(optionValue({"QEGTRAIN", "--scene", "a.egscene"}, "--scene") == "a.egscene", "the value of a present option is returned");
	ok &= expect(optionValue({"QEGTRAIN", "-g", "0"}, "--scene").isEmpty(), "an absent option gives an empty string");
	ok &= expect(optionValue({"QEGTRAIN", "--scene"}, "--scene").isEmpty(), "an option in the last entry gives an empty string");
	ok &= expect(optionValue({"QEGTRAIN", "--scene", "a", "--scene", "b"}, "--scene") == "a", "the first occurrence counts");
	ok &= expect(optionValue({}, "--scene").isEmpty(), "an empty list gives an empty string");
	return ok;
}

int main() {
	QTemporaryDir temp;
	if (!temp.isValid()) {
		std::cerr << "failed: temporary directory creation\n";
		return 1;
	}
	bool ok = testClassify(temp.path());
	ok &= testPositional();
	ok &= testOptionValue();
	return ok ? 0 : 1;
}
