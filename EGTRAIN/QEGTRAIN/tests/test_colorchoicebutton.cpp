#include "widgets/ColorChoiceButton.h"

#include <QApplication>
#include <QPushButton>
#include <QStringList>
#include <QTest>

// Records each colorChanged text, an empty one as <default>.
static void record(ColorChoiceButton& button, QStringList& emitted) {
	QObject::connect(&button, &ColorChoiceButton::colorChanged,
		[&emitted](const QString& text) { emitted << (text.isEmpty() ? QStringLiteral("<default>") : text); });
}

// The colour button shows the colour text of a service with its swatch, and emits only for a user action.
class TestColorChoiceButton : public QObject {
	Q_OBJECT

private slots:
	void objectNamesAndLabels();
	void setColorNeverEmits();
	void userActionsEmitOnce();
};

void TestColorChoiceButton::objectNamesAndLabels() {
	ColorChoiceButton button;
	button.show();
	QApplication::processEvents();
	auto* chooseButton = button.findChild<QPushButton*>("serviceColorChooseButton");
	auto* defaultButton = button.findChild<QPushButton*>("serviceColorDefaultButton");
	// The widget and its parts have their object names and labels.
	QCOMPARE(button.objectName(), QStringLiteral("serviceColorButton"));
	QVERIFY(button.findChild<QWidget*>("serviceColorSwatch") != nullptr);
	if (chooseButton == nullptr)
		QFAIL("the widget and its parts have their object names and labels");
	QCOMPARE(chooseButton->text(), QStringLiteral("Choose..."));
	if (defaultButton == nullptr)
		QFAIL("the widget and its parts have their object names and labels");
	QCOMPARE(defaultButton->text(), QStringLiteral("Default"));
}

void TestColorChoiceButton::setColorNeverEmits() {
	ColorChoiceButton button;
	button.show();
	QApplication::processEvents();
	QStringList emitted;
	record(button, emitted);
	// A new widget shows the default look.
	QCOMPARE(button.valueText(), QStringLiteral("Default"));
	QVERIFY(!button.swatchColor().isValid());
	button.setColor("#3C8DD2");
	// A valid text shows its swatch and is not rewritten.
	QCOMPARE(button.valueText(), QStringLiteral("#3C8DD2"));
	QCOMPARE(button.swatchColor(), QColor(0x3C, 0x8D, 0xD2));
	button.setColor("");
	// An empty text shows the default look.
	QCOMPARE(button.valueText(), QStringLiteral("Default"));
	QVERIFY(!button.swatchColor().isValid());
	button.setColor("red");
	// Another text is shown as invalid.
	QCOMPARE(button.valueText(), QStringLiteral("Invalid: red"));
	QVERIFY(!button.swatchColor().isValid());
	button.setColor("#12345");
	// A short hex text is shown as invalid.
	QCOMPARE(button.valueText(), QStringLiteral("Invalid: #12345"));
	QVERIFY2(emitted.isEmpty(), "setColor never emits");
}

// An invalid stored text stays until a user action. Choose and Default then emit once each.
void TestColorChoiceButton::userActionsEmitOnce() {
	ColorChoiceButton button;
	button.show();
	QApplication::processEvents();
	QStringList emitted;
	record(button, emitted);
	auto* defaultButton = button.findChild<QPushButton*>("serviceColorDefaultButton");
	if (defaultButton == nullptr)
		QFAIL("the widget has no Default button");

	button.setColor("not-a-colour");
	// An invalid text stays shown without a user action.
	QCOMPARE(button.valueText(), QStringLiteral("Invalid: not-a-colour"));
	QVERIFY(emitted.isEmpty());
	button.chooseColor(QColor());
	// Cancelling the colour dialog keeps the invalid text.
	QCOMPARE(button.valueText(), QStringLiteral("Invalid: not-a-colour"));
	QVERIFY(emitted.isEmpty());
	button.chooseColor(QColor(0xAB, 0xCD, 0xEF));
	// Choosing a colour emits once with the lower-case text and shows it.
	QCOMPARE(emitted, QStringList{"#abcdef"});
	QCOMPARE(button.valueText(), QStringLiteral("#abcdef"));
	QCOMPARE(button.swatchColor(), QColor(0xAB, 0xCD, 0xEF));

	emitted.clear();
	defaultButton->click();
	// Default emits once with an empty text and shows the default look.
	QCOMPARE(emitted, QStringList{"<default>"});
	QCOMPARE(button.valueText(), QStringLiteral("Default"));
	QVERIFY(!button.swatchColor().isValid());
	emitted.clear();
	defaultButton->click();
	QVERIFY2(emitted.isEmpty(), "Default does nothing when no colour is set");

	button.setColor("not-a-colour");
	defaultButton->click();
	// Default replaces an invalid text.
	QCOMPARE(emitted, QStringList{"<default>"});
	QCOMPARE(button.valueText(), QStringLiteral("Default"));

	emitted.clear();
	button.chooseColor(QColor(Qt::black));
	// Black is a colour like any other.
	QCOMPARE(emitted, QStringList{"#000000"});
}

// The test runs offscreen also outside CTest, so the platform is set before the application exists.
int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	TestColorChoiceButton test;
	return QTest::qExec(&test, argc, argv);
}

#include "test_colorchoicebutton.moc"
