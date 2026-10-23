#ifndef CAPACITYWINDOW_H
#define CAPACITYWINDOW_H

#include "diagrams/CapacityAnalysis.h"
#include "diagrams/CompletedRunData.h"

#include <QDialog>
#include <QString>

class QPushButton;

/// The key figures of a chain for a header, as the panel of CapacityWindow gives them: "cycle 3097.5 s, 86.0% of 1 h", or
/// "cycle 3097.5 s" without a valid period.
QString capacityCycleText(const CapacityAnalysisResult& result);

/// The window of the capacity analysis of a chain of trains (diagrams/CapacityAnalysis.h). The pairs, the compression and
/// the critical blocks lie in tabs at the left. A panel at the right holds the cycle, its share of the period and the
/// critical block, the button that opens the compressed blocking time and the export. Not modal; the caller connects
/// the two buttons.
class CapacityWindow : public QDialog {
public:
	/// Shows `result` with `context` in the header and `contextTip` below it in the tooltip. The cells that name blocks
	/// have their labels in `labels` as tooltips.
	CapacityWindow(const CapacityAnalysisResult& result, const QString& context, const QString& contextTip,
		const SceneSectionLabels& labels, QWidget* parent = nullptr);
	/// The CSV button of the panel.
	QPushButton* exportButton() const { return m_exportButton; }
	/// The button that opens the compressed blocking time.
	QPushButton* diagramButton() const { return m_diagramButton; }

private:
	QPushButton* m_exportButton = nullptr;
	QPushButton* m_diagramButton = nullptr;
};

#endif // CAPACITYWINDOW_H
