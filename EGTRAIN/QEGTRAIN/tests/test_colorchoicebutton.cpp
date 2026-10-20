#include "widgets/ColorChoiceButton.h"

#include <QApplication>
#include <QPushButton>
#include <QStringList>

#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	bool ok = true;

	ColorChoiceButton button;
	button.show();
	QApplication::processEvents();
	QStringList emitted;
	QObject::connect(&button, &ColorChoiceButton::colorChanged, [&emitted](const QString& text) {
		emitted << (text.isEmpty() ? QStringLiteral("<default>") : text);
	});
	auto* chooseButton = button.findChild<QPushButton*>("serviceColorChooseButton");
	auto* defaultButton = button.findChild<QPushButton*>("serviceColorDefaultButton");
	ok &= expect(button.objectName() == "serviceColorButton" && button.findChild<QWidget*>("serviceColorSwatch")
			&& chooseButton && chooseButton->text() == "Choose..." && defaultButton
			&& defaultButton->text() == "Default",
		"the widget and its parts have their object names and labels");

	ok &= expect(button.valueText() == "Default" && !button.swatchColor().isValid(),
		"a new widget shows the default look");
	button.setColor("#3C8DD2");
	ok &= expect(button.valueText() == "#3C8DD2" && button.swatchColor() == QColor(0x3C, 0x8D, 0xD2),
		"a valid text shows its swatch and is not rewritten");
	button.setColor("");
	ok &= expect(button.valueText() == "Default" && !button.swatchColor().isValid(),
		"an empty text shows the default look");
	button.setColor("red");
	ok &= expect(button.valueText() == "Invalid: red" && !button.swatchColor().isValid(),
		"another text is shown as invalid");
	button.setColor("#12345");
	ok &= expect(button.valueText() == "Invalid: #12345", "a short hex text is shown as invalid");
	ok &= expect(emitted.isEmpty(), "setColor never emits");

	// An invalid stored text stays until a user action.
	button.setColor("not-a-colour");
	ok &= expect(button.valueText() == "Invalid: not-a-colour" && emitted.isEmpty(),
		"an invalid text stays shown without a user action");
	button.chooseColor(QColor());
	ok &= expect(button.valueText() == "Invalid: not-a-colour" && emitted.isEmpty(),
		"cancelling the colour dialog keeps the invalid text");
	button.chooseColor(QColor(0xAB, 0xCD, 0xEF));
	ok &= expect(emitted == QStringList{"#abcdef"}
			&& button.valueText() == "#abcdef" && button.swatchColor() == QColor(0xAB, 0xCD, 0xEF),
		"choosing a colour emits once with the lower-case text and shows it");

	emitted.clear();
	defaultButton->click();
	ok &= expect(emitted == QStringList{"<default>"} && button.valueText() == "Default"
			&& !button.swatchColor().isValid(),
		"Default emits once with an empty text and shows the default look");
	emitted.clear();
	defaultButton->click();
	ok &= expect(emitted.isEmpty(), "Default does nothing when no colour is set");

	button.setColor("not-a-colour");
	defaultButton->click();
	ok &= expect(emitted == QStringList{"<default>"} && button.valueText() == "Default",
		"Default replaces an invalid text");

	emitted.clear();
	button.chooseColor(QColor(Qt::black));
	ok &= expect(emitted == QStringList{"#000000"},
		"black is a colour like any other");

	return ok ? 0 : 1;
}
