#include "widgets/ElidedLabel.h"

#include <QApplication>
#include <QImage>
#include <QMainWindow>
#include <QStatusBar>
#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	const QString sentence = QStringLiteral("Rail-1 has not entered the network yet. It is scheduled to enter at 08:25:50. Follow starts when it enters.");
	bool ok = true;

	ElidedLabel label;
	label.setText(sentence);
	const int full = label.fontMetrics().horizontalAdvance(sentence);
	ok &= expect(label.sizeHint().width() >= full, "the size hint shows the whole sentence");
	ok &= expect(label.minimumSizeHint().width() == 0, "the label can shrink below its sentence");

	// A label that is wide enough draws the sentence as it is.
	label.resize(full + 10, 24);
	ok &= expect(label.displayText() == sentence, "a wide label elided its sentence");

	// A narrow label draws the beginning and an ellipsis, and keeps the whole text.
	label.resize(150, 24);
	const QString shown = label.displayText();
	ok &= expect(shown != sentence && shown.endsWith(QChar(0x2026)) && sentence.startsWith(shown.left(shown.size() - 1)),
		"a narrow label did not elide the end of its sentence");
	ok &= expect(label.fontMetrics().horizontalAdvance(shown) <= 150, "the elided text is wider than the label");
	ok &= expect(label.text() == sentence, "eliding changed the text of the label");

	// The label paints what displayText() says. A plain label that holds that text paints the same.
	QLabel plain;
	plain.setTextFormat(Qt::PlainText);
	plain.setText(shown);
	plain.resize(150, 24);
	const QImage painted = label.grab().toImage();
	const QImage expected = plain.grab().toImage();
	ok &= expect(painted.size() == expected.size() && painted == expected, "the label does not paint its elided text");
	QLabel blank;
	blank.resize(150, 24);
	ok &= expect(painted != blank.grab().toImage(), "the label paints nothing");
	label.resize(full + 10, 24);
	plain.setText(sentence);
	plain.resize(full + 10, 24);
	ok &= expect(label.grab().toImage() == plain.grab().toImage(), "the label does not paint a text that fits");

	// A short text is not changed.
	label.setText(QStringLiteral("Rail-1 is running."));
	label.resize(150, 24);
	ok &= expect(label.displayText() == label.text(), "a short sentence was elided");

	// In a status bar a long sentence does not raise the width that the bar asks for.
	QMainWindow window;
	window.setCentralWidget(new QWidget);
	auto* shortLabel = new ElidedLabel;
	shortLabel->setText(QStringLiteral("Rail-1 is running."));
	window.statusBar()->addPermanentWidget(shortLabel);
	window.show();
	QApplication::processEvents();
	const int shortBar = window.statusBar()->minimumSizeHint().width();
	shortLabel->setText(sentence);
	QApplication::processEvents();
	ok &= expect(window.statusBar()->minimumSizeHint().width() == shortBar, "a long sentence widened the status bar");

	return ok ? 0 : 1;
}
