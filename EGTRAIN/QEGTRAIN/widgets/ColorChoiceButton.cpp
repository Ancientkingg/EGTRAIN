#include "widgets/ColorChoiceButton.h"

#include "graphics/VisualPolish.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>

// The scene model has a member named "signals".
#ifdef signals
#define EGTRAIN_RESTORE_SIGNALS_KEYWORD
#undef signals
#endif
#include "scene/SceneModel.h"
#ifdef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#define signals Q_SIGNALS
#undef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#endif

namespace {

class ColorSwatch : public QWidget {
public:
	explicit ColorSwatch(QWidget* parent = nullptr)
		: QWidget(parent) {
		setObjectName("serviceColorSwatch");
		setFixedSize(34, 18);
	}

	void setColor(const QColor& color) {
		m_color = color;
		update();
	}

protected:
	void paintEvent(QPaintEvent*) override {
		QPainter painter(this);
		const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
		painter.setPen(QPen(palette().color(QPalette::Mid), 1.0));
		if (m_color.isValid()) {
			painter.setBrush(m_color);
			painter.drawRect(box);
			return;
		}
		// No colour: an empty box with a strike.
		painter.setBrush(palette().color(QPalette::Base));
		painter.drawRect(box);
		painter.drawLine(box.bottomLeft(), box.topRight());
	}

private:
	QColor m_color;
};

} // namespace

ColorChoiceButton::ColorChoiceButton(QWidget* parent)
	: QWidget(parent) {
	setObjectName("serviceColorButton");
	auto* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	m_swatch = new ColorSwatch(this);
	layout->addWidget(m_swatch);
	m_valueLabel = new QLabel(this);
	m_valueLabel->setObjectName("serviceColorValue");
	// A long invalid text must not widen the form.
	m_valueLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	layout->addWidget(m_valueLabel, 1);
	m_chooseButton = new QPushButton("Choose...", this);
	m_chooseButton->setObjectName("serviceColorChooseButton");
	m_chooseButton->setAutoDefault(false);
	layout->addWidget(m_chooseButton);
	m_defaultButton = new QPushButton("Default", this);
	m_defaultButton->setObjectName("serviceColorDefaultButton");
	m_defaultButton->setAutoDefault(false);
	layout->addWidget(m_defaultButton);

	connect(m_chooseButton, &QPushButton::clicked, this, &ColorChoiceButton::openColorDialog);
	connect(m_defaultButton, &QPushButton::clicked, this, &ColorChoiceButton::resetColor);
	setColor(std::string());
}

void ColorChoiceButton::setColor(const std::string& text) {
	m_hasColorText = !text.empty();
	m_swatchColor = QColor();
	QString shown = QStringLiteral("Default");
	int red = 0, green = 0, blue = 0;
	if (sceneParseVisualizationColor(text, &red, &green, &blue)) {
		m_swatchColor = QColor(red, green, blue);
		shown = QString::fromStdString(text);
	} else if (m_hasColorText) {
		shown = QString("Invalid: %1").arg(QString::fromStdString(text));
	}
	static_cast<ColorSwatch*>(m_swatch)->setColor(m_swatchColor);
	m_valueLabel->setText(shown);
	m_valueLabel->setToolTip(shown);
}

QString ColorChoiceButton::valueText() const {
	return m_valueLabel->text();
}

void ColorChoiceButton::chooseColor(const QColor& color) {
	if (!color.isValid())
		return;
	const QString text = color.name();
	setColor(text.toStdString());
	emit colorChanged(text);
}

void ColorChoiceButton::resetColor() {
	if (!m_hasColorText)
		return;
	setColor(std::string());
	emit colorChanged(QString());
}

void ColorChoiceButton::openColorDialog() {
	QColorDialog dialog(m_swatchColor.isValid() ? m_swatchColor : defaultTrainFill(), this);
	dialog.setWindowTitle("Service visualization colour");
	dialog.setOption(QColorDialog::DontUseNativeDialog);
	if (dialog.exec() == QDialog::Accepted)
		chooseColor(dialog.selectedColor());
}
