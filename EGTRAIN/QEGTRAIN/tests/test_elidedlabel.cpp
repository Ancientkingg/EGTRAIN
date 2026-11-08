#include "widgets/ElidedLabel.h"

#include <QApplication>
#include <QImage>
#include <QMainWindow>
#include <QStatusBar>
#include <QTest>

// An elided label draws as much of its sentence as fits, ending in an ellipsis, and keeps the whole text.
class TestElidedLabel : public QObject {
	Q_OBJECT

private slots:
	void sizeHints();
	void wideLabelDrawsSentence();
	void narrowLabelElidesEnd();
	void paintsDisplayText();
	void shortTextIsNotChanged();
	void statusBarKeepsItsWidth();

private:
	const QString m_sentence = QStringLiteral("Rail-1 is scheduled to enter at 08:25:50. Follow starts when it enters.");
};

void TestElidedLabel::sizeHints() {
	ElidedLabel label;
	label.setText(m_sentence);
	const int full = label.fontMetrics().horizontalAdvance(m_sentence);
	// The size hint shows the whole sentence.
	QCOMPARE_GE(label.sizeHint().width(), full);
	// The label can shrink below its sentence.
	QCOMPARE(label.minimumSizeHint().width(), 0);
}

// A label that is wide enough draws the sentence as it is.
void TestElidedLabel::wideLabelDrawsSentence() {
	ElidedLabel label;
	label.setText(m_sentence);
	label.resize(label.fontMetrics().horizontalAdvance(m_sentence) + 10, 24);
	// A wide label does not elide its sentence.
	QCOMPARE(label.displayText(), m_sentence);
}

// A narrow label draws the beginning and an ellipsis, and keeps the whole text.
void TestElidedLabel::narrowLabelElidesEnd() {
	ElidedLabel label;
	label.setText(m_sentence);
	label.resize(150, 24);
	const QString shown = label.displayText();
	// A narrow label elides the end of its sentence.
	QCOMPARE_NE(shown, m_sentence);
	QVERIFY(shown.endsWith(QChar(0x2026)));
	QVERIFY(m_sentence.startsWith(shown.left(shown.size() - 1)));
	// The elided text is not wider than the label.
	QCOMPARE_LE(label.fontMetrics().horizontalAdvance(shown), 150);
	// Eliding does not change the text of the label.
	QCOMPARE(label.text(), m_sentence);
}

// The label paints what displayText() says. A plain label that holds that text paints the same.
void TestElidedLabel::paintsDisplayText() {
	ElidedLabel label;
	label.setText(m_sentence);
	const int full = label.fontMetrics().horizontalAdvance(m_sentence);
	label.resize(150, 24);
	const QString shown = label.displayText();
	QLabel plain;
	plain.setTextFormat(Qt::PlainText);
	plain.setText(shown);
	plain.resize(150, 24);
	const QImage painted = label.grab().toImage();
	const QImage expected = plain.grab().toImage();
	// The label paints its elided text.
	QCOMPARE(painted.size(), expected.size());
	QVERIFY(painted == expected);
	QLabel blank;
	blank.resize(150, 24);
	QVERIFY2(painted != blank.grab().toImage(), "the label paints nothing");
	label.resize(full + 10, 24);
	plain.setText(m_sentence);
	plain.resize(full + 10, 24);
	QVERIFY2(label.grab().toImage() == plain.grab().toImage(), "the label does not paint a text that fits");
}

// A short text is not changed.
void TestElidedLabel::shortTextIsNotChanged() {
	ElidedLabel label;
	label.setText(QStringLiteral("Rail-1 is running."));
	label.resize(150, 24);
	// A short sentence is not elided.
	QCOMPARE(label.displayText(), label.text());
}

// In a status bar a long sentence does not raise the width that the bar asks for.
void TestElidedLabel::statusBarKeepsItsWidth() {
	QMainWindow window;
	window.setCentralWidget(new QWidget);
	auto* shortLabel = new ElidedLabel;
	shortLabel->setText(QStringLiteral("Rail-1 is running."));
	window.statusBar()->addPermanentWidget(shortLabel);
	window.show();
	QApplication::processEvents();
	const int shortBar = window.statusBar()->minimumSizeHint().width();
	shortLabel->setText(m_sentence);
	QApplication::processEvents();
	// A long sentence does not widen the status bar.
	QCOMPARE(window.statusBar()->minimumSizeHint().width(), shortBar);
}

QTEST_MAIN(TestElidedLabel)
#include "test_elidedlabel.moc"
