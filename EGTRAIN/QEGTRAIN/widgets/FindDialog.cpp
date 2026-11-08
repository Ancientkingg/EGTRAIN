#include "widgets/FindDialog.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include <algorithm>

FindDialog::FindDialog(QWidget* parent)
	: QDialog(parent), m_field(new QLineEdit(this)), m_results(new QListWidget(this)) {
	setObjectName(QStringLiteral("findDialog"));
	setWindowTitle(tr("Find"));
	m_field->setPlaceholderText(tr("Station, signal or train"));
	m_field->setClearButtonEnabled(true);
	m_field->installEventFilter(this);
	m_results->installEventFilter(this);
	m_results->setAccessibleName(tr("Results"));
	auto* column = new QVBoxLayout(this);
	column->addWidget(m_field);
	column->addWidget(m_results);
	resize(fontMetrics().averageCharWidth() * 60, fontMetrics().height() * 20);

	connect(m_field, &QLineEdit::textChanged, this, &FindDialog::filter);
	connect(m_field, &QLineEdit::returnPressed, this, &FindDialog::choose);
	connect(m_results, &QListWidget::itemActivated, this, &FindDialog::choose);
}

void FindDialog::setLabels(const QStringList& labels, const QStringList& keys) {
	m_labels = labels;
	m_keys = keys;
	m_field->clear();
	m_results->clear();
	m_field->setFocus();
}

bool FindDialog::eventFilter(QObject* watched, QEvent* event) {
	if (event->type() == QEvent::KeyPress) {
		const int key = static_cast<QKeyEvent*>(event)->key();
		if (watched == m_field && (key == Qt::Key_Up || key == Qt::Key_Down)) {
			QApplication::sendEvent(m_results, event);
			return true;
		}
		// On macOS the list does not activate its item on Enter.
		if (watched == m_results && (key == Qt::Key_Return || key == Qt::Key_Enter)) {
			choose();
			return true;
		}
	}
	return QDialog::eventFilter(watched, event);
}

bool FindDialog::matches(const QString& text, const QStringList& texts) {
	const QStringList terms = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
	return std::all_of(terms.cbegin(), terms.cend(), [&texts](const QString& term) {
		return std::any_of(texts.cbegin(), texts.cend(), [&term](const QString& candidate) { return candidate.contains(term, Qt::CaseInsensitive); });
	});
}

void FindDialog::filter(const QString& text) {
	m_results->clear();
	if (text.trimmed().isEmpty())
		return;
	for (qsizetype i = 0; i < m_labels.size(); ++i) {
		const QString& label = m_labels.at(i);
		if (matches(text, {label, m_keys.value(i)}))
			m_results->addItem(label);
	}
	m_results->setCurrentRow(0);
}

void FindDialog::choose() {
	const QListWidgetItem* item = m_results->currentItem();
	if (!item)
		return;
	const QString label = item->text();
	accept();
	emit chosen(label);
}
