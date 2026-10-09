#include "diagrams/TimetableTableWindow.h"
#include "diagrams/TrainFilterButton.h"

#include <QApplication>
#include <QFile>
#include <QScreen>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>

#include <iostream>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QFile stylesheet(QStringLiteral(EGTRAIN_DIALOG_QSS));
    if (!stylesheet.open(QIODevice::ReadOnly)) return 1;
    app.setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
    std::vector<TimetableResultRow> rows(1);
    rows.front().trainId = "train-a";
    rows.front().stationId = "station-b";
    TimetableTableWindow window(std::move(rows), 0, {});
    QFont scaled = window.font();
    scaled.setPointSizeF(18);
    window.setFont(scaled);
    RunProvenance provenance;
    provenance.caseName = "Case A";
    provenance.appliedScenario = "Scenario B";
    window.setRunProvenance(std::move(provenance));
    window.show();
    app.processEvents();
    const auto* table = window.findChild<QTableWidget*>();
    const auto* context = window.findChild<QLabel*>("timetableContext");
    const QRect screen = window.screen()->availableGeometry();
    const bool ok = window.width() <= screen.width() * 9 / 10
        && window.height() <= screen.height() * 4 / 5
        && context && context->isVisible() && context->text().contains("Case A")
        && context->textFormat() == Qt::PlainText
        && table && table->rowCount() == 1 && table->item(0, 0)->text() == "train-a"
        && window.findChild<TrainFilterButton*>()->isVisible();
    if (!ok) std::cerr << "timetable presentation or data changed\n";
    return ok ? 0 : 1;
}
