#include "editors/ValidationPanel.h"

#include "editors/CountText.h"
#include "scene/SceneValidator.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QLabel>
#include <QStringList>
#include <QTableWidget>
#include <QTreeWidget>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>

namespace {

// The table columns that only advanced details show.
constexpr int kCodeColumn = 1;
constexpr int kFileColumn = 3;
constexpr int kPathColumn = 4;

// What the status column says for a status of the loaded data or of the run.
QString loadedStatusText(const QString& status) {
	if (status == QLatin1String("Parsed") || status == QLatin1String("Loaded"))
		return QStringLiteral("OK");
	if (status == QLatin1String("Missing optional"))
		return QStringLiteral("Not present");
	if (status == QLatin1String("Not built"))
		return QStringLiteral("Not run");
	return status;
}

// The name of a category of the loaded data on screen.
QString categoryText(const std::string& category) {
	static const std::pair<const char*, const char*> names[] = {
		{"scene", "Case metadata"},
		{"train_units", "Rolling stock units"},
		{"import_provenance", "Original files"},
		{"original_parameter_source", "Parameter file"},
		{"original_tractive_effort_source", "Traction file"},
		{"train_unit_parameters", "Physical values"},
		{"tractive_effort_curve", "Traction curve"},
	};
	for (const auto& [key, name] : names)
		if (category == key)
			return QString::fromLatin1(name);
	QString label = QString::fromStdString(category);
	label.replace('_', ' ');
	if (!label.isEmpty())
		label[0] = label[0].toUpper();
	return label;
}

// `named` says that the category of the item is a name, such as the name of a scenario, and not a category key.
void addLoadedDataTreeItem(QTreeWidget* tree, QTreeWidgetItem* parent, const SceneLoadedData& item, bool advanced,
	bool named = false) {
	// The file and the parsed objects below each file are details, and so is a count of problems that is zero.
	const bool problemCount = item.category == "skipped" || item.category == "unresolved_references" || item.category == "validation";
	if (!advanced && (item.category == "source_file" || item.category == "parsed_objects" || (problemCount && item.parsedCount == 0)))
		return;
	auto* row = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
	QString label = QString::fromStdString(item.category);
	const bool isEntityId = item.targetType == "train_unit" || item.targetType == "composition"
		|| item.targetType == "service" || item.targetType == "incident";
	if (!named && !isEntityId && !label.contains(':'))
		label = categoryText(item.category);
	// A row that stands for one thing, such as a service or a file, has no count.
	static const char* const singleFacts[] = {"scene", "source_file", "train_unit_parameters", "original_parameter_source",
		"original_tractive_effort_source"};
	const bool single = isEntityId || label.startsWith(QLatin1String("Incident: ")) || item.targetType == "train_unit_plot"
		|| std::any_of(std::begin(singleFacts), std::end(singleFacts), [&](const char* key) { return item.category == key; });
	row->setText(0, label);
	row->setText(1, QString::fromStdString(item.sourceFile));
	row->setText(2, single ? QString() : QString::number(item.parsedCount));
	row->setText(3, loadedStatusText(QString::fromStdString(item.status)));
	if (!item.targetType.empty()) {
		row->setData(0, ValidationPanel::kTargetTypeRole, QString::fromStdString(item.targetType));
		const QString tooltip = item.targetType == "network" ? QStringLiteral("Double-click to show on the network")
			: item.targetType == "train_unit_plot"			 ? QStringLiteral("Double-click to plot")
															 : QStringLiteral("Double-click to open");
		row->setToolTip(0, tooltip);
	}
	for (const auto& child : item.children)
		addLoadedDataTreeItem(tree, row, child, advanced, item.category == "available_scenarios");
}

} // namespace

ValidationPanel::ValidationPanel(QWidget* parent) : QObject(parent) {
	m_table = new QTableWidget(0, 6, parent);
	QStringList validationHeaders;
	validationHeaders << "Severity" << "Code" << "Message" << "File" << "Path" << "Fix";
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
	m_tree->setAccessibleDescription("Loaded case data. Double-click a row to open it.");
	m_tree->header()->setStretchLastSection(true);
	connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) { activateItem(item); });

	m_statusLabel = new QLabel(parent);
	showAdvancedColumns();
}

void ValidationPanel::setDocument(SceneDocument* document) {
	m_document = document;
}

void ValidationPanel::setDiagnostics(std::vector<SceneDiagnostic>* diagnostics) {
	m_diagnostics = diagnostics ? diagnostics : &m_ownDiagnostics;
}

void ValidationPanel::setAdvancedDetails(std::function<bool()> enabled) {
	m_advancedDetails = std::move(enabled);
	showAdvancedColumns();
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

QString ValidationPanel::statusText(const SceneDiagnosticCounts& counts, bool advanced) {
	const QString errors = countText(counts.errors, QStringLiteral("error"), QStringLiteral("errors"));
	if (!advanced)
		return counts.errors > 0 ? QStringLiteral("Cannot run: ") + errors : QString();
	QStringList parts;
	if (counts.errors > 0)
		parts << errors;
	if (counts.warnings > 0)
		parts << countText(counts.warnings, QStringLiteral("warning"), QStringLiteral("warnings"));
	if (counts.infos > 0)
		parts << countText(counts.infos, QStringLiteral("info"), QStringLiteral("info"));
	return parts.join(QStringLiteral(", "));
}

void ValidationPanel::showAdvancedColumns() {
	const bool advanced = advancedDetails();
	for (const int column : {kCodeColumn, kFileColumn, kPathColumn})
		m_table->setColumnHidden(column, !advanced);
}

void ValidationPanel::updateStatus() {
	m_statusLabel->setText(statusText(countDiagnostics(*m_diagnostics), advancedDetails()));
	// The window calls this when advanced details are switched, so the columns and the rows of the tree follow here.
	showAdvancedColumns();
	if (m_treeAdvanced && *m_treeAdvanced != advancedDetails())
		refreshLoadedData();
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
		QString severity = QString::fromStdString(severityLabel(d.severity));
		if (!severity.isEmpty())
			severity[0] = severity[0].toUpper();
		const QString file = QString::fromStdString(d.file);
		auto* severityItem = new QTableWidgetItem(severity);
		severityItem->setData(kFileRole, file);
		m_table->setItem(row, 0, severityItem);
		m_table->setItem(row, kCodeColumn, new QTableWidgetItem(QString::fromStdString(d.code)));
		m_table->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(d.message)));
		m_table->setItem(row, kFileColumn, new QTableWidgetItem(file));
		m_table->setItem(row, kPathColumn, new QTableWidgetItem(QString::fromStdString(d.path)));
		m_table->setItem(row, 5, new QTableWidgetItem(QString::fromStdString(d.suggestedFix)));
		// The code, the file and the path, one per line, for a table that does not show their columns.
		QStringList details{QStringLiteral("Code: %1").arg(QString::fromStdString(d.code))};
		if (!file.isEmpty())
			details << QStringLiteral("File: %1").arg(file);
		if (!d.path.empty())
			details << QStringLiteral("Path: %1").arg(QString::fromStdString(d.path));
		for (int column = 0; column < m_table->columnCount(); ++column)
			m_table->item(row, column)->setToolTip(details.join(QLatin1Char('\n')));
	}
	m_table->resizeColumnsToContents();

	updateStatus();
	refreshLoadedData();
	Q_EMIT refreshed();
}

void ValidationPanel::refreshLoadedData() {
	m_tree->clear();
	m_treeAdvanced.reset();
	if (!m_document || !m_document->isLoaded())
		return;
	const bool advanced = advancedDetails();
	m_treeAdvanced = advanced;

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
	caseRoot->setText(0, "Case");
	caseRoot->setText(1, context.sceneDir);
	caseRoot->setText(3, caseStatus);
	// A row of one fact leaves the count empty.
	addRow(caseRoot, "Name", QString::fromStdString(model.name), QString(), QStringLiteral("OK"));
	addRow(caseRoot, "Description",
		model.description.empty() ? QStringLiteral("(none)") : QString::fromStdString(model.description),
		QString(), QStringLiteral("OK"));
	if (advanced) {
		addRow(caseRoot, "Format version", QString::number(model.schemaVersion),
			QString::number(kCurrentSceneSchemaVersion), QStringLiteral("OK"));
		addRow(caseRoot, "Saved with",
			model.savedWithAppVersion.empty() ? QStringLiteral("-") : QString::fromStdString(model.savedWithAppVersion),
			QString(), model.savedWithAppVersion.empty() ? QStringLiteral("Not present") : QStringLiteral("OK"));
		if (context.sceneIsBundle)
			addRow(caseRoot, "Bundle version", context.bundleVersion ? QString::number(*context.bundleVersion) : QStringLiteral("-"),
				QString::number(kCurrentSceneBundleVersion),
				context.bundleVersion ? QStringLiteral("OK") : QStringLiteral("Missing"));

		auto* sourceFiles = addRow(caseRoot, "Source files discovered", QString(),
			QString::number(static_cast<int>(model.sourceFiles.size())), QStringLiteral("OK"));
		for (const auto& source : model.sourceFiles) {
			addRow(sourceFiles, QString::fromStdString(source), QString::fromStdString(source), QString(),
				QStringLiteral("OK"));
		}
	}
	for (const auto& item : model.loadedData) {
		addLoadedDataTreeItem(m_tree, caseRoot, item, advanced);
	}
	const QString runStatus = loadedStatusText(context.runtimeStatus);
	auto* run = addRow(caseRoot, "Run", QString(), QString(), runStatus);
	const bool runtimeReady = context.runtimeStatus == "Ready" || context.runtimeStatus == "Running"
		|| context.runtimeStatus == "Completed";
	const bool runAttempted = context.runtimeStatus != "Not built";
	const QString notRun = QStringLiteral("Not run");
	addRow(run, "Network", QString(), QString(), runtimeReady ? QStringLiteral("Ready") : notRun);
	addRow(run, "Trains", QString(), QString(), runtimeReady ? QStringLiteral("Ready") : notRun);
	addRow(run, "Scenario", context.appliedScenarioId.empty() ? QStringLiteral("(none)") : QString::fromStdString(context.appliedScenarioId),
		QString(), runAttempted ? runStatus : notRun);
	addRow(run, "Status", QString(), QString(), runStatus);
	auto* messages = addRow(run, "Messages", QString(),
		context.runtimeDiagnostics.empty() ? QString() : QString::number(static_cast<int>(context.runtimeDiagnostics.size())), runStatus);
	for (const auto& diagnostic : context.runtimeDiagnostics) {
		const QString severity = diagnostic.severity == SceneSeverity::Error ? QStringLiteral("Failed")
																			 : (diagnostic.severity == SceneSeverity::Warning ? QStringLiteral("Warning") : QStringLiteral("Ready"));
		const QString detail = QString::fromStdString(diagnostic.code) + ": "
			+ QString::fromStdString(diagnostic.message);
		addRow(messages, detail, QString::fromStdString(diagnostic.file), QString(), severity);
	}
	addRow(run, "Results", QString(), QString(), context.resultsAvailable ? QStringLiteral("Ready") : notRun);
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
		QTableWidgetItem* cell = m_table->item(row, 0);
		if (cell && cell->data(kFileRole).toString() == file) {
			m_table->selectRow(row);
			break;
		}
	}
}
