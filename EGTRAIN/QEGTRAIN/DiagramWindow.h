#ifndef DIAGRAMWINDOW_H
#define DIAGRAMWINDOW_H

#include <QDialog>
#include <QPointer>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>

QT_CHARTS_USE_NAMESPACE

class QLabel;

// Reusable non-modal window that shows a chart with PNG export and a hover readout.
class DiagramWindow : public QDialog {
    Q_OBJECT
public:
    explicit DiagramWindow(const QString& title, QWidget* parent = nullptr);
    void setChart(QChart* chart);                                  // takes ownership
    void setTimeAxisX(bool on, long long startOffsetSeconds = 0);  // format X hover as HH:MM:SS

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;           // track mouse for hover

private slots:
    void exportPng();

private:
    QPointer<QChartView> m_view;
    QPointer<QLabel> m_readout;
    bool m_timeAxis = false;
    long long m_startOffset = 0;
};

#endif // DIAGRAMWINDOW_H
