#include "editors/RollingStockLibraryDialog.h"

#include "util/SpeedFormat.h"
#include "widgets/DialogLayout.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFont>
#include <QHeaderView>
#include <QPushButton>
#include <QScrollBar>
#include <QStringList>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <string>
#include <utility>

RollingStockLibrary bundledRollingStockLibrary() {
	QFile file(QStringLiteral(":/rolling_stock_library.json"));
	// A resource that cannot be read gives no text, which the parser reports.
	return parseRollingStockLibrary(file.open(QIODevice::ReadOnly) ? file.readAll().toStdString() : std::string());
}

RollingStockLibraryDialog::RollingStockLibraryDialog(std::vector<RollingStockLibraryUnit> units, QWidget* parent)
	: QDialog(parent), m_units(std::move(units)) {
	setObjectName("rollingStockLibraryDialog");
	// Set before the footer buttons exist, because a button is styled as soon as it is built and a style that was cached
	// before the property would miss the rules that name it. DialogLayout::install sets it again.
	setProperty("dialogPresentation", true);
	auto* body = new QWidget;
	auto* bodyLayout = new QVBoxLayout(body);
	bodyLayout->setContentsMargins(0, 0, 0, 0);
	m_tree = new QTreeWidget(body);
	m_tree->setObjectName("rollingStockLibraryTree");
	m_tree->setAccessibleName("Rolling stock units of the bundled cases");
	m_tree->setHeaderLabels({"ID", "Maximum speed", "Length", "Traction rows"});
	m_tree->setRootIsDecorated(false);
	m_tree->setUniformRowHeights(true);
	m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
	m_tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_tree->header()->setStretchLastSection(false);
	m_tree->setMinimumHeight(m_tree->fontMetrics().lineSpacing() * 16);
	QFont headingFont = m_tree->font();
	headingFont.setBold(true);
	// The units of each list of cases follow a heading that names the cases, in the order in which the units name them. A
	// heading is a row that cannot be selected or reached with the arrow keys, as in the case chooser.
	QStringList headings;
	std::vector<std::vector<std::size_t>> groups;
	for (std::size_t index = 0; index < m_units.size(); ++index) {
		QStringList cases;
		for (const std::string& name : m_units[index].cases)
			cases << QString::fromStdString(name);
		const QString heading = cases.join(", ");
		if (!headings.contains(heading)) {
			headings << heading;
			groups.emplace_back();
		}
		groups[static_cast<std::size_t>(headings.indexOf(heading))].push_back(index);
	}
	QTreeWidgetItem* firstUnit = nullptr;
	for (std::size_t group = 0; group < groups.size(); ++group) {
		auto* heading = new QTreeWidgetItem(m_tree, {headings[static_cast<int>(group)]});
		heading->setFlags(Qt::NoItemFlags);
		heading->setFont(0, headingFont);
		heading->setFirstColumnSpanned(true);
		for (const std::size_t index : groups[group]) {
			const SceneTrainUnit& unit = m_units[index].unit;
			auto* row = new QTreeWidgetItem(m_tree, {QString::fromStdString(unit.id), unit.hasPhysical ? QString::fromStdString(formatSpeedLabel(unit.physical.max_speed_ms * 3.6)) : QString(), unit.hasPhysical ? QString("%1 m").arg(unit.physical.length_m) : QString(), QString::number(unit.tractionCurve.size())});
			row->setData(0, Qt::UserRole, static_cast<qulonglong>(index));
			for (int column = 1; column < m_tree->columnCount(); ++column)
				row->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
			if (firstUnit == nullptr)
				firstUnit = row;
		}
	}
	// The columns show every id and number whole.
	m_tree->setMinimumWidth(m_tree->header()->length() + 2 * m_tree->frameWidth() + m_tree->verticalScrollBar()->sizeHint().width());
	bodyLayout->addWidget(m_tree, 1);
	m_compositionCheck = new QCheckBox("Also add a composition with only this unit", body);
	m_compositionCheck->setObjectName("rollingStockLibraryCompositionCheck");
	m_compositionCheck->setChecked(true);
	bodyLayout->addWidget(m_compositionCheck);

	auto* buttons = new QDialogButtonBox(this);
	buttons->addButton(QDialogButtonBox::Cancel);
	m_addButton = buttons->addButton("Add", QDialogButtonBox::AcceptRole);
	m_addButton->setObjectName("rollingStockLibraryAddButton");
	m_addButton->setDefault(true);
	connect(m_tree, &QTreeWidget::currentItemChanged, this, [this] { m_addButton->setEnabled(selectedUnit() != nullptr); });
	connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this] {
		if (selectedUnit() != nullptr)
			accept();
	});
	connect(buttons, &QDialogButtonBox::accepted, this, [this] {
		if (selectedUnit() != nullptr)
			accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	DialogLayout::install(*this, "Add from Library",
		"Rolling stock units of the bundled cases. The copy keeps the values, the traction curve and the source files of the "
		"unit.",
		body, buttons);
	m_tree->setCurrentItem(firstUnit);
	m_addButton->setEnabled(selectedUnit() != nullptr);
	DialogLayout::fitWidthToContent(*this);
}

const RollingStockLibraryUnit* RollingStockLibraryDialog::selectedUnit() const {
	const QTreeWidgetItem* item = m_tree->currentItem();
	if (item == nullptr || !item->data(0, Qt::UserRole).isValid())
		return nullptr;
	return &m_units[static_cast<std::size_t>(item->data(0, Qt::UserRole).toULongLong())];
}

bool RollingStockLibraryDialog::addsComposition() const {
	return m_compositionCheck->isChecked();
}
