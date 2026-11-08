#include "app/SceneOpenRequest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

static bool writeFile(const QString& path, const QByteArray& contents) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

static bool exactlyOne(const SceneOpenChoice& choice) {
	return choice.path.isEmpty() != choice.rejection.isEmpty();
}

// The name has two characters outside the Western Windows code page.
static QString unicodeName() {
	return QString::fromUtf16(u"é東京");
}

class TestSceneOpenRequest : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void rejectsPaths_data();
	void rejectsPaths();
	void acceptsPaths_data();
	void acceptsPaths();
	void keepsNonAsciiCharacters();
	void makesRelativePathAbsolute();
	void returnsPositionalArguments_data();
	void returnsPositionalArguments();
	void returnsOptionValue_data();
	void returnsOptionValue();

private:
	// The path of name in the temporary directory.
	QString path(const QString& name) const;

	QTemporaryDir m_temp;
};

QString TestSceneOpenRequest::path(const QString& name) const {
	return QDir(m_temp.path()).filePath(name);
}

void TestSceneOpenRequest::initTestCase() {
	QVERIFY2(m_temp.isValid(), "temporary directory creation");
	// Classification fixtures are writable.
	QVERIFY(writeFile(path("case.egscene"), "a"));
	QVERIFY(writeFile(path("my case study.egscene"), "a"));
	QVERIFY(writeFile(path("CASE.EGSCENE"), "a"));
	QVERIFY(writeFile(path("x.tar.egscene"), "a"));
	QVERIFY(writeFile(path("x.egscene.txt"), "a"));
	QVERIFY(writeFile(path("notes.txt"), "a"));
	QVERIFY(writeFile(path(unicodeName() + ".egscene"), "a"));
	QVERIFY(QDir().mkpath(path("x.egscene")));
	QVERIFY(QDir().mkpath(path("folder.txt")));
	QVERIFY2(QFileInfo::exists(path(unicodeName() + ".egscene")), "the file with the non-ASCII name exists");
}

void TestSceneOpenRequest::rejectsPaths_data() {
	QTest::addColumn<QStringList>("paths");
	QTest::addColumn<QString>("rejection");
	const QString plain = path("case.egscene");
	const QString missing = path("missing.egscene");
	QTest::newRow("an empty list is rejected") << QStringList() << "no file was given";
	QTest::newRow("two valid files are rejected") << QStringList({plain, path("my case study.egscene")})
												  << "more than one file was given";
	QTest::newRow("the count is checked before the files") << QStringList({plain, missing}) << "more than one file was given";
	QTest::newRow("a missing file is rejected") << QStringList({missing}) << "the file does not exist";
	QTest::newRow("existence is checked before the extension") << QStringList({path("missing.txt")})
															   << "the file does not exist";
	QTest::newRow("an empty path is rejected as missing") << QStringList({QString()}) << "the file does not exist";
	QTest::newRow("a directory called x.egscene is rejected") << QStringList({path("x.egscene")})
															  << "it is a folder, not an .egscene file";
	QTest::newRow("the kind is checked before the extension") << QStringList({path("folder.txt")})
															  << "it is a folder, not an .egscene file";
	QTest::newRow("a .txt file is rejected") << QStringList({path("notes.txt")}) << "it is not an .egscene file";
	QTest::newRow("x.egscene.txt is rejected") << QStringList({path("x.egscene.txt")}) << "it is not an .egscene file";
}

void TestSceneOpenRequest::rejectsPaths() {
	QFETCH(QStringList, paths);
	QFETCH(QString, rejection);
	const SceneOpenChoice choice = classifyOpenPaths(paths);
	QVERIFY(exactlyOne(choice));
	QVERIFY(choice.path.isEmpty());
	QCOMPARE(choice.rejection, rejection);
}

void TestSceneOpenRequest::acceptsPaths_data() {
	QTest::addColumn<QString>("file");
	QTest::newRow("a plain name is accepted") << path("case.egscene");
	QTest::newRow("a name with spaces is accepted") << path("my case study.egscene");
	QTest::newRow("the extension is compared without regard to case") << path("CASE.EGSCENE");
	QTest::newRow("x.tar.egscene is accepted") << path("x.tar.egscene");
	QTest::newRow("a name with non-ASCII characters is accepted") << path(unicodeName() + ".egscene");
}

void TestSceneOpenRequest::acceptsPaths() {
	QFETCH(QString, file);
	const SceneOpenChoice choice = classifyOpenPaths({file});
	QVERIFY(exactlyOne(choice));
	QCOMPARE(choice.path, QFileInfo(file).absoluteFilePath());
}

// The accepted path keeps the non-ASCII characters.
void TestSceneOpenRequest::keepsNonAsciiCharacters() {
	const QString unicode = path(unicodeName() + ".egscene");
	QCOMPARE(classifyOpenPaths({unicode}).path, QFileInfo(unicode).absoluteFilePath());
	QVERIFY(classifyOpenPaths({unicode}).path.contains(unicodeName()));
}

void TestSceneOpenRequest::makesRelativePathAbsolute() {
	const QString previous = QDir::currentPath();
	// Windows cannot delete the current directory.
	const auto restore = qScopeGuard([&previous] { QDir::setCurrent(previous); });
	QVERIFY2(QDir::setCurrent(m_temp.path()), "the current directory can be changed");
	const QString relative = QStringLiteral("case.egscene");
	const SceneOpenChoice relativeChoice = classifyOpenPaths({relative});
	// A relative path is made absolute.
	QVERIFY(exactlyOne(relativeChoice));
	QCOMPARE(relativeChoice.path, QFileInfo(relative).absoluteFilePath());
	QVERIFY(QFileInfo(relativeChoice.path).isAbsolute());
	QVERIFY(relativeChoice.path != relative);
	QVERIFY2(QDir::setCurrent(previous), "the current directory is restored");
}

void TestSceneOpenRequest::returnsPositionalArguments_data() {
	const QStringList withValue = {"-n", "-h", "-g", "--seed", "--scene"};
	QTest::addColumn<QStringList>("arguments");
	QTest::addColumn<QStringList>("optionsWithValue");
	QTest::addColumn<QStringList>("expected");
	QTest::newRow("no arguments give no positional argument") << QStringList() << withValue << QStringList();
	QTest::newRow("the program name alone gives none") << QStringList({"QEGTRAIN"}) << withValue << QStringList();
	QTest::newRow("the first entry is the program and is never returned") << QStringList({"case.egscene"}) << withValue
																		  << QStringList();
	QTest::newRow("one positional argument is returned") << QStringList({"QEGTRAIN", "case.egscene"}) << withValue
														 << QStringList({"case.egscene"});
	QTest::newRow("a positional argument after options is returned")
		<< QStringList({"QEGTRAIN", "-g", "0", "-n", "2", "case.egscene"}) << withValue << QStringList({"case.egscene"});
	QTest::newRow("the value of an option is skipped") << QStringList({"QEGTRAIN", "-h", "1"}) << withValue << QStringList();
	QTest::newRow("a value is positional when its option is not in the list") << QStringList({"QEGTRAIN", "-h", "1"})
																			  << QStringList() << QStringList({"1"});
	QTest::newRow("the value of --scene is not positional") << QStringList({"QEGTRAIN", "--scene", "a.egscene", "b.egscene"})
															<< withValue << QStringList({"b.egscene"});
	QTest::newRow("a value that starts with a dash is skipped as a value") << QStringList({"QEGTRAIN", "-h", "-5", "x"})
																		   << withValue << QStringList({"x"});
	QTest::newRow("an option without a value at the end gives none") << QStringList({"QEGTRAIN", "-g"}) << withValue
																	 << QStringList();
	QTest::newRow("flags without a value skip nothing after them")
		<< QStringList({"QEGTRAIN", "--interactive", "--detailed-trajectories", "x"}) << withValue << QStringList({"x"});
	QTest::newRow("an unknown option is skipped without its value") << QStringList({"QEGTRAIN", "-unknown", "word", "x"})
																	<< withValue << QStringList({"word", "x"});
	QTest::newRow("a name that starts with a dash is never positional") << QStringList({"QEGTRAIN", "-x.egscene"})
																		<< withValue << QStringList();
	QTest::newRow("positional arguments keep their order") << QStringList({"QEGTRAIN", "a.egscene", "-g", "0", "b.egscene"})
														   << withValue << QStringList({"a.egscene", "b.egscene"});
}

void TestSceneOpenRequest::returnsPositionalArguments() {
	QFETCH(QStringList, arguments);
	QFETCH(QStringList, optionsWithValue);
	QFETCH(QStringList, expected);
	QCOMPARE(positionalArguments(arguments, optionsWithValue), expected);
}

void TestSceneOpenRequest::returnsOptionValue_data() {
	QTest::addColumn<QStringList>("arguments");
	QTest::addColumn<QString>("expected");
	QTest::newRow("the value of a present option is returned") << QStringList({"QEGTRAIN", "--scene", "a.egscene"})
															   << "a.egscene";
	QTest::newRow("an absent option gives an empty string") << QStringList({"QEGTRAIN", "-g", "0"}) << QString();
	QTest::newRow("an option in the last entry gives an empty string") << QStringList({"QEGTRAIN", "--scene"}) << QString();
	QTest::newRow("the first occurrence counts") << QStringList({"QEGTRAIN", "--scene", "a", "--scene", "b"}) << "a";
	QTest::newRow("an empty list gives an empty string") << QStringList() << QString();
}

void TestSceneOpenRequest::returnsOptionValue() {
	QFETCH(QStringList, arguments);
	QFETCH(QString, expected);
	QCOMPARE(optionValue(arguments, "--scene"), expected);
}

QTEST_APPLESS_MAIN(TestSceneOpenRequest)
#include "test_sceneopenrequest.moc"
