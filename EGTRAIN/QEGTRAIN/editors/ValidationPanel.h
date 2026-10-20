#ifndef VALIDATIONPANEL_H
#define VALIDATIONPANEL_H

#include "editors/SceneDocument.h"
#include "scene/SceneDiagnostic.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <optional>
#include <string>
#include <vector>

class QLabel;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

// What the validation of the scene reports, and what the scene and its run made of the loaded data. The table lists the
// diagnostics, the status label gives their count, and the tree shows the case, its source files and the state of the run.
// The window puts the table and the tree in docks and the label in its status bar.
class ValidationPanel : public QObject {
	Q_OBJECT

public:
	// The kind of scene item that a row of the tree stands for. A row with a kind opens an editor or a view when it is activated.
	static constexpr int kTargetTypeRole = Qt::UserRole;

	// What the tree shows besides the scene: where the scene came from and the state of the run.
	struct LoadedDataContext {
		QString sceneDir;
		bool sceneIsBundle = false;
		std::optional<int> bundleVersion;
		QString runtimeStatus = QStringLiteral("Not built");
		std::vector<SceneDiagnostic> runtimeDiagnostics;
		std::string appliedScenarioId;
		bool resultsAvailable = false;
	};

	// The table, the tree and the label are made as children of parent, until the window moves them.
	explicit ValidationPanel(QWidget* parent);

	void setDocument(SceneDocument* document);
	// refresh() writes the diagnostics to this vector, where the window and the other panels read them, and the panel shows
	// them. The panel uses its own until it is given another; null brings its own back. The vector must outlive the panel.
	void setDiagnostics(std::vector<SceneDiagnostic>* diagnostics);
	// Tells whether the status gives the counts of every severity. Without it the status shows errors only.
	void setAdvancedDetails(std::function<bool()> enabled);
	// The duration that the next run uses in place of the one of the scene, if it has one. Without it the scene is validated
	// with its own duration.
	void setDurationOverride(std::function<std::optional<double>()> durationOverride);
	// Gives the context of the tree whenever it is written. Without it the scene came from nowhere and no run was made.
	void setLoadedDataContext(std::function<LoadedDataContext()> context);

	QTableWidget* table() const { return m_table; }
	QTreeWidget* tree() const { return m_tree; }
	QLabel* statusLabel() const { return m_statusLabel; }

	// Validates the scene of the document, shows the diagnostics and the loaded data, and says so with refreshed(). With no
	// scene it clears them.
	void refresh();
	// Writes the tree again.
	void refreshLoadedData();
	// Writes the status from the diagnostics again.
	void updateStatus();
	// Emits targetActivated() for a row of the tree that has a kind.
	void activateItem(QTreeWidgetItem* item);
	// Selects the first row of the table that belongs to this scene file.
	void selectDiagnosticOfFile(const QString& file);

Q_SIGNALS:
	// The status was written or cleared. What shows the diagnostics follows.
	void statusChanged();
	// refresh() has written the table, the status and the tree. What depends on the diagnostics follows.
	void refreshed();
	// A row of the tree with this kind was activated.
	void targetActivated(const QString& targetType, QTreeWidgetItem* item);

private:
	bool advancedDetails() const;

	QPointer<SceneDocument> m_document;
	std::vector<SceneDiagnostic> m_ownDiagnostics;
	std::vector<SceneDiagnostic>* m_diagnostics = &m_ownDiagnostics;
	std::function<bool()> m_advancedDetails;
	std::function<std::optional<double>()> m_durationOverride;
	std::function<LoadedDataContext()> m_loadedDataContext;

	QTableWidget* m_table = nullptr;
	QTreeWidget* m_tree = nullptr;
	QLabel* m_statusLabel = nullptr;
};

#endif // VALIDATIONPANEL_H
