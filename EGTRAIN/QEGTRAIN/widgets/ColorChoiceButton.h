#ifndef COLORCHOICEBUTTON_H
#define COLORCHOICEBUTTON_H

#include <QColor>
#include <QString>
#include <QWidget>

#include <string>

class QLabel;
class QPushButton;

// Edits an optional "#RRGGBB" colour text: a swatch, a Choose button that opens
// the colour dialog and a Default button that removes the colour. An empty text
// means the default. A stored text that is not a colour is shown as it is and
// kept until the user chooses a colour or presses Default.
class ColorChoiceButton : public QWidget {
	Q_OBJECT

public:
	explicit ColorChoiceButton(QWidget* parent = nullptr);

	// Shows the stored text without changing it; never emits colorChanged.
	void setColor(const std::string& text);
	// The colour of the swatch; invalid for the default and for an invalid text.
	QColor swatchColor() const { return m_swatchColor; }
	QString valueText() const;

public slots:
	// What the colour dialog result goes through. An invalid colour is ignored.
	void chooseColor(const QColor& color);
	// Removes the colour; does nothing when no colour is set.
	void resetColor();

signals:
	// Emitted once per user action with the lower-case "#rrggbb" text, or an
	// empty text for the default.
	void colorChanged(const QString& text);

private:
	void openColorDialog();

	QWidget* m_swatch = nullptr;
	QLabel* m_valueLabel = nullptr;
	QPushButton* m_chooseButton = nullptr;
	QPushButton* m_defaultButton = nullptr;
	QColor m_swatchColor;
	bool m_hasColorText = false;
};

#endif // COLORCHOICEBUTTON_H
