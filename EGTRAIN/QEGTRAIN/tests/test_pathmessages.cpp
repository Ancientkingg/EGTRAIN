#include "app/PathMessages.h"
#include "widgets/DisplayPath.h"

#include <QDir>
#include <QScopeGuard>
#include <QTest>

// The texts that name a path the person chose write it with the separator of the system.
class TestPathMessages : public QObject {
	Q_OBJECT

private slots:
	void windowsSeparator();
	void systemSeparator();
};

// A dialog returns forward slashes. The texts show the separator of the system, here a backslash.
void TestPathMessages::windowsSeparator() {
	display_path::separator() = QLatin1Char('\\');
	const auto restoreSeparator = qScopeGuard([] { display_path::separator() = QDir::separator(); });
	// Write failure on Windows.
	QCOMPARE(path_message::writeFailed("write the image to", "C:/Users/me/Documents/table.png"),
		QStringLiteral("Could not write the image to:\nC:\\Users\\me\\Documents\\table.png"));
	// Output folder on Windows.
	QCOMPARE(path_message::outputFolder("D:/Runs/Case 1"), QStringLiteral("Output folder: D:\\Runs\\Case 1"));
}

// With the separator of this system the text is what QDir gives for the same path.
void TestPathMessages::systemSeparator() {
	display_path::separator() = QDir::separator();
	// Write failure here.
	QCOMPARE(path_message::writeFailed("export the data and provenance to", "/data/out/table.csv"),
		"Could not export the data and provenance to:\n" + QDir::toNativeSeparators("/data/out/table.csv"));
	// Output folder here.
	QCOMPARE(path_message::outputFolder("/data/out"), "Output folder: " + QDir::toNativeSeparators("/data/out"));
}

QTEST_GUILESS_MAIN(TestPathMessages)
#include "test_pathmessages.moc"
