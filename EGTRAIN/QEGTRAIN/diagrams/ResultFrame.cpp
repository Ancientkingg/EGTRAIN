#include "diagrams/ResultFrame.h"

#include "app/DialogFolders.h"
#include "app/PathMessages.h"
#include "diagrams/RunResults.h"
#include "telemetry/TelemetryOperation.h"
#include "widgets/DialogLayout.h"
#include "widgets/EditorLayout.h"
#include "widgets/ElidedLabel.h"
#include "widgets/PlainToolTip.h"

#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPen>
#include <QSaveFile>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtCharts/QAbstractAxis>
#include <QtCharts/QChart>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace {
// The width of the panel, and the narrower width of the panel while the window is narrower than kNarrowWindow.
constexpr int kPanelWidth = 260;
constexpr int kNarrowPanelWidth = 200;
constexpr int kNarrowWindow = 1000;
// The value of a key figure is this much larger than the text of the panel.
constexpr qreal kFigureScale = 1.45;
constexpr QMargins kChartMargins(12, 8, 12, 8);

bool writeAtomically(const QString& path, const std::string& bytes) {
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly))
		return false;
	const QByteArray data = QByteArray::fromStdString(bytes);
	return file.write(data) == data.size() && file.commit();
}
} // namespace

ResultPanel::ResultPanel(QWidget* parent)
	: QWidget(parent) {
	setObjectName("resultPanel");
	auto* scroll = new EditorLayout::VerticalScrollArea(this);
	scroll->setObjectName("resultPanelScroll");
	// The sections take the keyboard focus themselves; the area around them is not a stop of its own.
	scroll->setFocusPolicy(Qt::NoFocus);
	m_body = new QWidget(scroll);
	m_body->setObjectName("resultPanelBody");
	m_column = new QVBoxLayout(m_body);
	m_column->setContentsMargins(16, 12, 16, 12);
	m_column->setSpacing(12);
	m_column->addStretch(1);
	scroll->setWidget(m_body);

	m_readout = new QLabel(this);
	m_readout->setObjectName("resultReadout");
	m_readout->setTextFormat(Qt::PlainText);
	// A long reading is cut at the edge rather than widening the panel.
	m_readout->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_readout->setContentsMargins(16, 6, 16, 12);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(scroll, 1);
	layout->addWidget(m_readout);
}

QWidget* ResultPanel::addSection(const QString& title, QWidget* content, QWidget* headingWidget) {
	auto* rule = new QFrame(this);
	rule->setProperty("resultRule", true);
	rule->setFixedHeight(1);
	auto* section = new QWidget(this);
	auto* layout = new QVBoxLayout(section);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);
	if (!title.isEmpty() || headingWidget) {
		auto* headingRow = new QHBoxLayout();
		headingRow->setSpacing(8);
		QLabel* heading = EditorLayout::sectionHeading(title, section);
		heading->setProperty("resultSectionHeading", true);
		headingRow->addWidget(heading, 1);
		if (headingWidget)
			headingRow->addWidget(headingWidget, 0, Qt::AlignRight | Qt::AlignVCenter);
		layout->addLayout(headingRow);
	}
	layout->addWidget(content);
	// Before the stretch that keeps the sections at the top.
	m_column->insertWidget(m_column->count() - 1, rule);
	m_column->insertWidget(m_column->count() - 1, section);
	m_sections.append({section, rule});
	updateRules();
	return section;
}

void ResultPanel::setSectionVisible(QWidget* section, bool visible) {
	section->setVisible(visible);
	updateRules();
}

void ResultPanel::keepSectionsWhole() {
	m_sectionsWhole = true;
	m_body->installEventFilter(this);
	updateGeometry();
}

QSize ResultPanel::minimumSizeHint() const {
	QSize hint = QWidget::minimumSizeHint();
	if (m_sectionsWhole)
		hint.setHeight(std::max(hint.height(), m_body->sizeHint().height() + (m_readout->isHidden() ? 0 : m_readout->sizeHint().height())));
	return hint;
}

bool ResultPanel::eventFilter(QObject* watched, QEvent* event) {
	// The scroll area keeps a change of the height of the sections to itself, so the panel passes it on to its layout.
	if (watched == m_body && event->type() == QEvent::LayoutRequest)
		updateGeometry();
	return QWidget::eventFilter(watched, event);
}

// A rule lies above each shown section that follows another shown section.
void ResultPanel::updateRules() {
	bool above = false;
	for (const Section& section : m_sections) {
		const bool shown = !section.widget->isHidden();
		section.rule->setVisible(shown && above);
		above = above || shown;
	}
}

ResultFigure::ResultFigure(const QString& name, QWidget* parent)
	: QWidget(parent) {
	setObjectName(name);
	auto* value = new ElidedLabel(this);
	value->setObjectName("resultFigureValue");
	m_value = value;
	auto* caption = new ElidedLabel(this);
	caption->setObjectName("resultFigureCaption");
	m_caption = caption;
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(m_value);
	layout->addWidget(m_caption);
	scaleValue();
}

void ResultFigure::setFigure(const QString& value, const QString& caption, const QString& toolTip) {
	m_value->setText(value);
	m_caption->setText(caption);
	const QString whole = value + ' ' + caption;
	setToolTip(plainToolTip(toolTip.isEmpty() ? whole : whole + '\n' + toolTip));
}

QString ResultFigure::value() const {
	return m_value->text();
}

QString ResultFigure::caption() const {
	return m_caption->text();
}

void ResultFigure::changeEvent(QEvent* event) {
	QWidget::changeEvent(event);
	if (event->type() == QEvent::FontChange)
		scaleValue();
}

// The value is set in a semibold font larger than the font of the figure.
void ResultFigure::scaleValue() {
	QFont font = this->font();
	if (font.pointSizeF() > 0)
		font.setPointSizeF(font.pointSizeF() * kFigureScale);
	else
		font.setPixelSize(qRound(font.pixelSize() * kFigureScale));
	font.setWeight(QFont::DemiBold);
	m_value->setFont(font);
}

ResultFrame::ResultFrame(QWidget* main, const QString& name, QWidget* parent)
	: QWidget(parent) {
	m_header = new QFrame(this);
	m_header->setObjectName("resultHeader");
	m_headerLayout = new QHBoxLayout(m_header);
	// With the rule below it, the header is 40 px tall around buttons of 26 px.
	m_headerLayout->setContentsMargins(16, 7, 12, 6);
	m_headerLayout->setSpacing(4);
	// The header keeps that height also above a main widget whose height depends on its width, such as one with a
	// wrapped status line, which would otherwise squeeze the header to its smallest height.
	m_header->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	auto* context = new ElidedLabel(m_header);
	context->setObjectName(name + "Context");
	context->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	context->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	context->hide();
	m_context = context;
	m_headerLayout->addWidget(m_context, 1);
	// The space after the context, which stays when the context is hidden, keeps the buttons at the right.
	m_headerLayout->addStretch();

	m_panel = new ResultPanel(this);
	// A drag of the splitter stops at the narrow width of the panel, so that no heading or button text is cut.
	m_panel->setMinimumWidth(kNarrowPanelWidth);
	m_panelButton = addHeaderButton(name + "PanelToggle", "Panel", ":/icons/panel.svg", "Show or hide the panel");
	m_panelButton->setCheckable(true);
	m_panelButton->setChecked(true);
	connect(m_panelButton, &QToolButton::toggled, this, [this](bool shown) {
		m_panel->setVisible(shown);
		fitPanel();
	});

	auto* mainArea = new QWidget(this);
	mainArea->setObjectName("resultMain");
	m_mainColumn = new QVBoxLayout(mainArea);
	m_mainColumn->setContentsMargins(0, 0, 0, 0);
	m_mainColumn->setSpacing(0);
	m_mainColumn->addWidget(m_header);
	main->setParent(mainArea);
	m_mainColumn->addWidget(main, 1);

	m_splitter = new QSplitter(Qt::Horizontal, this);
	m_splitter->setObjectName("resultSplitter");
	m_splitter->addWidget(mainArea);
	m_splitter->addWidget(m_panel);
	// A drag stops at the minimum width of either side, and a larger window widens the main widget only.
	m_splitter->setChildrenCollapsible(false);
	m_splitter->setStretchFactor(0, 1);
	m_splitter->setStretchFactor(1, 0);
	// Only a drag of the user emits the signal, not the sizes that fitPanel sets.
	connect(m_splitter, &QSplitter::splitterMoved, this, [this] { m_userSized = true; });

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_splitter);
}

void ResultFrame::setContext(const QString& text, const QString& toolTip) {
	m_context->setText(text);
	m_context->setToolTip(plainToolTip(toolTip.isEmpty() ? text : text + '\n' + toolTip));
	m_context->setVisible(!text.isEmpty());
}

QLabel* ResultFrame::contextLabel() const {
	return m_context;
}

QToolButton* ResultFrame::addHeaderButton(const QString& objectName, const QString& text, const QString& iconPath,
	const QString& toolTip) {
	auto* button = new QToolButton(m_header);
	button->setObjectName(objectName);
	button->setProperty("resultHeaderButton", true);
	button->setText(text);
	button->setAccessibleName(text);
	button->setToolTip(toolTip);
	// A button without its icon shows its text instead.
	if (QFile::exists(iconPath))
		button->setIcon(QIcon(iconPath));
	button->setToolButtonStyle(Qt::ToolButtonIconOnly);
	// Before the panel button, which the constructor adds first.
	const int index = m_panelButton ? m_headerLayout->indexOf(m_panelButton) : m_headerLayout->count();
	m_headerLayout->insertWidget(index, button);
	return button;
}

void ResultFrame::addAboveMain(QWidget* widget) {
	m_mainColumn->insertWidget(m_mainColumn->count() - 1, widget);
}

void ResultFrame::installInDialog(QDialog& dialog, const QString& heading, QDialogButtonBox* buttons) {
	m_panel->keepSectionsWhole();
	DialogLayout::install(dialog, heading, QString(), this, buttons);
	QLayout* layout = dialog.layout();
	const QMargins margins = layout->contentsMargins();
	buttons->setContentsMargins(margins.left(), layout->spacing(), margins.right(), margins.bottom());
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
}

void ResultFrame::resizeEvent(QResizeEvent* event) {
	QWidget::resizeEvent(event);
	fitPanel();
}

void ResultFrame::showEvent(QShowEvent* event) {
	QWidget::showEvent(event);
	fitPanel();
}

// The width of the panel follows the width of the window until the user drags the splitter.
void ResultFrame::fitPanel() {
	if (m_userSized || m_panel->isHidden())
		return;
	const int panel = window()->width() < kNarrowWindow ? kNarrowPanelWidth : kPanelWidth;
	const int main = std::max(1, m_splitter->width() - m_splitter->handleWidth() - panel);
	m_splitter->setSizes({main, panel});
}

KeepInFront::KeepInFront(QWidget* widget)
	: m_widget(widget) {}

KeepInFront::~KeepInFront() {
	if (!m_widget) return;
	QWidget* window = m_widget->window();
	window->raise();
	window->activateWindow();
}

void styleResultChart(QChart* chart, const QPalette& palette) {
	chart->setBackgroundRoundness(0);
	chart->setBackgroundBrush(palette.brush(QPalette::Base));
	chart->setMargins(kChartMargins);
	QColor gridColor = palette.color(QPalette::WindowText);
	gridColor.setAlpha(30);
	QColor lineColor = palette.color(QPalette::WindowText);
	lineColor.setAlpha(90);
	for (QAbstractAxis* axis : chart->axes()) {
		axis->setGridLinePen(QPen(gridColor));
		axis->setLabelsBrush(palette.brush(QPalette::WindowText));
		axis->setTitleBrush(palette.brush(QPalette::WindowText));
		axis->setLinePen(QPen(lineColor));
	}
}

double roundTickStep(double span, int steps) {
	const double power = std::pow(10.0, std::floor(std::log10(span / steps)));
	double step = power;
	for (const double factor : {2.0, 5.0, 10.0})
		if (span / step > steps) step = factor * power;
	return step;
}

QString saveExportInteractive(QWidget* parent, telemetry::ExportKind kind, const QString& title, const QString& suggestedName,
	const telemetry::OperationObservation& operation, const std::function<std::optional<std::string>()>& bytes,
	const ExportWriter& write) {
	// The window comes back in front once the save dialog and its messages close.
	const KeepInFront front(parent);
	const bool image = kind == telemetry::ExportKind::Png;
	const char* suffix = image ? "png" : "csv";
	const QString failed = image ? QStringLiteral("save the image to") : QStringLiteral("save the data to");
	QString path = QFileDialog::getSaveFileName(parent, title, dialog_folders::suggestedPath(suggestedName),
		image ? QStringLiteral("PNG Image (*.png)") : QStringLiteral("CSV File (*.csv)"));
	if (path.isEmpty())
		return QString(); // cancelled: no file is written
	if (QFileInfo(path).suffix().compare(QLatin1String(suffix), Qt::CaseInsensitive) != 0)
		path += QLatin1Char('.') + QLatin1String(suffix);
	const std::optional<std::string> content = bytes();
	if (!content) {
		operation.failure(telemetry::Operation::Export, telemetry::Error::InternalFailure);
		QMessageBox::warning(parent, "Export failed", path_message::writeFailed(failed, path));
		return QString();
	}
	const bool written = !content->empty() && (write ? write(path, suffix, *content) : writeAtomically(path, *content));
	operation.exportFinished(kind, written, true, telemetry::Error::IoFailure);
	if (!written) {
		QMessageBox::warning(parent, "Export failed", path_message::writeFailed(failed, path));
		return QString();
	}
	return path;
}

std::optional<std::string> pngBytes(const QImage& image) {
	QByteArray png;
	QBuffer buffer(&png);
	if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
		return std::nullopt;
	return std::string(png.constData(), static_cast<std::size_t>(png.size()));
}

ExportWriter runProvenanceWriter(RunProvenance provenance) {
	return [provenance = std::move(provenance)](const QString& path, const char* suffix, const std::string& bytes) {
		return writeRunArtifactWithProvenance(path.toStdString(), suffix, bytes, provenance);
	};
}

QString runContextText(const RunProvenance& provenance) {
	const QString caseName = QString::fromStdString(provenance.caseName);
	const QString scenario = QString::fromStdString(provenance.appliedScenario);
	if (caseName.isEmpty())
		return scenario.isEmpty() ? QStringLiteral("(none)") : scenario;
	return scenario.isEmpty() ? caseName : QStringLiteral("%1 / %2").arg(caseName, scenario);
}

QWidget* resultTableArea(const QList<QWidget*>& widgets) {
	auto* area = new QWidget;
	area->setObjectName("resultTableArea");
	auto* column = new QVBoxLayout(area);
	column->setContentsMargins(16, 12, 12, 12);
	column->setSpacing(8);
	for (QWidget* widget : widgets)
		column->addWidget(widget, widget == widgets.last() ? 1 : 0);
	return area;
}
