#include "widgets/CancelServiceDialog.h"

#include "widgets/DialogLayout.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

CancelServiceDialog::CancelServiceDialog(const QString& trainName, const QStringList& stopsAhead, QWidget* parent) : QDialog(parent) {
	setObjectName("cancelServiceDialog");
	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);
	layout->setContentsMargins(0, 0, 0, 0);
	m_completeButton = new QRadioButton("Completely, from now", body);
	m_completeButton->setObjectName("cancelCompleteButton");
	m_completeButton->setChecked(true);
	layout->addWidget(m_completeButton);
	auto* completeHelp = new QLabel(body);
	DialogLayout::setHelp(completeHelp, "A train that runs ends its run at its next stop. A train that has not entered does not enter.");
	layout->addWidget(completeHelp);
	m_fromStationButton = new QRadioButton("From a station on", body);
	m_fromStationButton->setObjectName("cancelFromStationButton");
	layout->addWidget(m_fromStationButton);
	m_stationCombo = new QComboBox(body);
	m_stationCombo->setObjectName("cancelStationCombo");
	m_stationCombo->setAccessibleName("Station");
	for (const QString& station : stopsAhead)
		if (m_stationCombo->findText(station) < 0)
			m_stationCombo->addItem(station);
	m_stationCombo->setEnabled(false);
	layout->addWidget(m_stationCombo);
	auto* stationHelp = new QLabel(body);
	DialogLayout::setHelp(stationHelp, "The train runs to the station, lets its passengers alight and leaves the network there.");
	layout->addWidget(stationHelp);
	m_fromStationButton->setEnabled(m_stationCombo->count() > 0);
	connect(m_fromStationButton, &QRadioButton::toggled, m_stationCombo, &QComboBox::setEnabled);

	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	m_buttons->button(QDialogButtonBox::Ok)->setText("Cancel Service");
	m_buttons->button(QDialogButtonBox::Ok)->setDefault(true);
	m_buttons->button(QDialogButtonBox::Cancel)->setText("Keep Running");
	connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	DialogLayout::install(*this, "Cancel Service", QString("Cancel %1 as a dispatching measure.").arg(trainName), body, m_buttons);
}

QString CancelServiceDialog::fromStation() const {
	return m_fromStationButton->isChecked() ? m_stationCombo->currentText() : QString();
}
