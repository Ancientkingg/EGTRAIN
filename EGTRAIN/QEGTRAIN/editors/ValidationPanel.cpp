#include "editors/ValidationPanel.h"

#include "scene/SceneValidator.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QLabel>
#include <QStringList>
#include <QTableWidget>
#include <QTreeWidget>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace {

void addLoadedDataTreeItem(QTreeWidget* tree, QTreeWidgetItem* parent, const SceneLoadedData& item) {
	auto* row = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
	QString label = QString::fromStdString(item.category);
	const bool isEntityId = item.targetType == "train_unit" || item.targetType == "composition"
		|| item.targetType == "service" || item.targetType == "incident";
	if (!isEntityId && !label.contains(':')) {
		label.replace('_', ' ');
		if (!label.isEmpty())
			label[0] = label[0].toUpper();
		if (label == "Scene")
			label = "Case metadata";
		if (label == "Train units")
			label = "Rolling stock units";
	}
	row->setText(0, label);
	row->setText(1, QString::fromStdString(item.sourceFile));
	row->setText(2, QString::number(item.parsedCount));
	row->setText(3, QString::fromStdString(item.status));
	if (!item.targetType.empty()) {
		row->setData(0, ValidationPanel::kTargetTypeRole, QString::fromStdString(item.targetType));
		const QString tooltip = item.targetType == "network"
			? QStringLiteral("Activate this row to focus the existing network view.")
			: (item.targetType == "validation"
					  ? QStringLiteral("Activate this row to open the existing validation table.")
					  : (item.targetType == "train_unit_plot"
								? QStringLiteral("Activate this row to plot this rolling stock unit's tractive effort.")
								: QStringLiteral("Activate this row to open the existing editor.")));
		row->setToolTip(0, tooltip);
	}
	for (const auto& child : item.children)
		addLoadedDataTreeItem(tree, row, child);
}

} // namespace

ValidationPanel::ValidationPanel(QWidget* parent) : QObject(parent) {
	m_table = new QTableWidget(0, 6, parent);
	QStringList validationHeaders;
	validationHeaders << "Severity" << "Code" << "Message" << "File" << "Path" << "Suggested Fix";
	m_table->setHorizontalHeaderLabels(validationHeaders);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	// Tab leaves the table; the arrow keys move between its cells.
	m_table->setTabKeyNavigation(false);
	m_table->horizontalHeader()->setStretchLastSection(true);
	m_table->resizeColumnsToContents();

	m_tree = new QTreeWidget(parent);
	m_tree->setColumnCount(4);
	m_tree->setHeaderLabels(QStringList() << "Category" << "Source" << "Count" << "Status");
	m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_tree->setUniformRowHeights(true);
	m_tree->setToolTip("Activate rows to open existing editors.");
	m_tree->setAccessibleDescription("Loaded case data. Activatable rows open existing editors.");
	m_tree->header()->setStretchLastSection(true);
	connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) { activateItem(item); });

	m_statusLabel = new QLabel(parent);
}

void ValidationPanel::setDocument(SceneDocument* document) {
	m_document = document;
}

void ValidationPanel::setDiagnostics(std::vector<SceneDiagnostic>* diagnostics) {
	m_diagnostics = diagnostics ? diagnostics : &m_ownDiagnostics;
}

void ValidationPanel::setAdvancedDetails(std::function<bool()> enabled) {
	m_advancedDetails = std::move(enabled);
}

void ValidationPanel::setDurationOverride(std::function<std::optional<double>()> durationOverride) {
	m_durationOverride = std::move(durationOverride);
}

void ValidationPanel::setLoadedDataContext(std::function<LoadedDataContext()> context) {
	m_loadedDataContext = std::move(context);
}

bool ValidationPanel::advancedDetails() const {
	return m_advancedDetails && m_advancedDetails();
}

void ValidationPanel::updateStatus() {
	const SceneDiagnosticCounts counts = countDiagnostics(*m_diagnostics);
	if (advancedDetails()) {
		QString message = QStringLiteral("Validation: %1 error(s), %2 warning(s)")
							  .arg(counts.errors)
							  .arg(counts.warnings);
		if (counts.infos > 0)
			message += QStringLiteral(", %1 info").arg(counts.infos);
		m_statusLabel->setText(message);
	} else if (counts.errors > 0) {
		m_statusLabel->setText(
			QStringLiteral("Not ready: %1 validation error(s) must be fixed").arg(counts.errors));
	} else {
		m_statusLabel->clear();
	}
	Q_EMIT statusChanged();
}

void ValidationPanel::refresh() {
	if (!m_document || !m_document->isLoaded()) {
		m_diagnostics->clear();
		m_table->setRowCount(0);
		m_statusLabel->clear();
		refreshLoadedData();
		Q_EMIT refreshed();
		Q_EMIT statusChanged();
		return;
	}

	const std::optional<double> effectiveDurationOverride = m_durationOverride ? m_durationOverride() : std::nullopt;
	*m_diagnostics = validateRunnableScene(m_document->model(), {}, effectiveDurationOverride);

	std::vector<SceneDiagnostic> ordered = *m_diagnostics;
	std::stable_sort(ordered.begin(), ordered.end(), [](const SceneDiagnostic& a, const SceneDiagnostic& b) {
		return static_cast<int>(a.severity) > static_cast<int>(b.severity);
	});

	m_table->clearContents();
	m_table->setRowCount(static_cast<int>(ordered.size()));
	for (int row = 0; row < static_cast<int>(ordered.size()); ++row) {
		const SceneDiagnostic& d = ordered[static_cast<std::size_t>(row)];
		m_table->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(severityLabel(d.severity))));
		m_table->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(d.code)));
		m_table->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(d.message)));
		m_table->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(d.file)));
		m_table->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(d.path)));
		m_table->setItem(row, 5, new QTableWidgetItem(QString::fromStdString(d.suggestedFix)));
	}
	m_table->resizeColumnsToContents();

	updateStatus();
	refreshLoadedData();
	Q_EMIT refreshed();
}

void ValidationPanel::refreshLoadedData() {
	m_tree->clear();
	if (!m_document || !m_document->isLoaded())
		return;

	const LoadedDataContext context = m_loadedDataContext ? m_loadedDataContext() : LoadedDataContext();
	SceneModel& model = m_document->mutableModel();
	refreshLoadedDataSummary(model);
	refreshLoadedDataDiagnostics(model, *m_diagnostics);
	auto addRow = [](QTreeWidgetItem* owner, const QString& category, const QString& source,
					  const QString& count, const QString& status) {
		return new QTreeWidgetItem(owner, QStringList{category, source, count, status});
	};
	const SceneDiagnosticCounts validationCounts = countDiagnostics(*m_diagnostics);
	const SceneDiagnosticCounts runtimeCounts = countDiagnostics(context.runtimeDiagnostics);
	const bool dataInvalid = std::any_of(model.loadedData.begin(), model.loadedData.end(),
		[](const SceneLoadedData& row) { return row.status == "Invalid"; });
	const bool dataWarning = std::any_of(model.loadedData.begin(), model.loadedData.end(),
		[](const SceneLoadedData& row) { return row.status == "Warning"; });
	const QString caseStatus = validationCounts.errors > 0 || runtimeCounts.errors > 0 || dataInvalid
		? QStringLiteral("Invalid")
		: (validationCounts.warnings > 0 || runtimeCounts.warnings > 0 || dataWarning
				  ? QStringLiteral("Warning")
				  : QStringLiteral("Ready"));
	auto* caseRoot = new QTreeWidgetItem(m_tree);
	caseRoot->setText(0, "Case Study");
	caseRoot->setText(1, context.sceneDir);
	caseRoot->setText(2, "1");
	caseRoot->setText(3, caseStatus);
	caseRoot->setToolTip(0, "Loaded case-study review.");
	addRow(caseRoot, "Name", QString::fromStdString(model.name), "1", "Parsed");
	addRow(caseRoot, "Description",
		model.description.empty() ? QStringLiteral("(none)") : QString::fromStdString(model.description),
		"1", "Parsed");
	addRow(caseRoot, "Source path", context.sceneDir, "1", "Loaded");
	addRow(caseRoot, "Canonical schema version", QString::number(model.schemaVersion),
		QString::number(kCurrentSceneSchemaVersion), "Parsed");
	addRow(caseRoot, "Saved with app version",
		model.savedWithAppVersion.empty()
			? QStringLiteral("(not recorded)")
			: QString::fromStdString(model.savedWithAppVersion),
		"1", model.savedWithAppVersion.empty() ? QStringLiteral("Missing optional") : QStringLiteral("Loaded"));
	if (context.sceneIsBundle)
		addRow(caseRoot, "Bundle format version", context.bundleVersion ? QString::number(*context.bundleVersion) : QStringLiteral("(not recorded)"),
			QString::number(kCurrentSceneBundleVersion),
			context.bundleVersion ? QStringLiteral("Loaded") : QStringLiteral("Missing"));

	auto* sourceFiles = addRow(caseRoot, "Source files discovered", QString(),
		QString::number(static_cast<int>(model.sourceFiles.size())), "Loaded");
	for (const auto& source : model.sourceFiles) {
		addRow(sourceFiles, QString::fromStdString(source), QString::fromStdString(source), "1",
			QStringLiteral("Loaded"));
	}
	for (const auto& item : model.loadedData) {
		addLoadedDataTreeItem(m_tree, caseRoot, item);
	}
	auto* runtimeAndResults = addRow(caseRoot, "Runtime and results", QString(), QString(),
		context.runtimeStatus);
	const bool runtimeReady = context.runtimeStatus == "Ready" || context.runtimeStatus == "Running"
		|| context.runtimeStatus == "Completed";
	const bool runAttempted = context.runtimeStatus != "Not built";
	addRow(runtimeAndResults, "Runtime infrastructure", QString(), "1",
		runtimeReady ? QStringLiteral("Ready") : QStringLiteral("Not built"));
	addRow(runtimeAndResults, "Runtime rolling stock", QString(), "1",
		runtimeReady ? QStringLiteral("Ready") : QStringLiteral("Not built"));
	addRow(runtimeAndResults, "Applied scenario", context.appliedScenarioId.empty() ? QStringLiteral("(none)") : QString::fromStdString(context.appliedScenarioId), "1",
		runAttempted ? context.runtimeStatus : QStringLiteral("Not built"));
	addRow(runtimeAndResults, "Current run", QString(), "1", context.runtimeStatus);
	auto* runtime = addRow(runtimeAndResults, "Runtime", QString(),
		QString::number(static_cast<int>(context.runtimeDiagnostics.size())), context.runtimeStatus);
	for (const auto& diagnostic : context.runtimeDiagnostics) {
		const QString severity = diagnostic.severity == SceneSeverity::Error ? QStringLiteral("Failed")
																			 : (diagnostic.severity == SceneSeverity::Warning ? QStringLiteral("Warning") : QStringLiteral("Ready"));
		const QString detail = QString::fromStdString(diagnostic.code) + ": "
			+ QString::fromStdString(diagnostic.message);
		addRow(runtime, detail, QString::fromStdString(diagnostic.file), "1", severity);
	}
	addRow(runtimeAndResults, "Results", QString(), context.resultsAvailable ? "1" : "0",
		context.resultsAvailable ? QStringLiteral("Ready") : QStringLiteral("Not built"));
	caseRoot->setExpanded(true);
	m_tree->resizeColumnToContents(0);
	m_tree->resizeColumnToContents(1);
	m_tree->setColumnWidth(1, std::min(m_tree->columnWidth(1), 360));
}

void ValidationPanel::activateItem(QTreeWidgetItem* item) {
	if (!item)
		return;
	const QString targetType = item->data(0, kTargetTypeRole).toString();
	if (!targetType.isEmpty())
		Q_EMIT targetActivated(targetType, item);
}

void ValidationPanel::selectDiagnosticOfFile(const QString& file) {
	for (int row = 0; row < m_table->rowCount(); ++row) {
		QTableWidgetItem* cell = m_table->item(row, 3);
		if (cell && cell->text() == file) {
			m_table->selectRow(row);
			break;
		}
	}
}
