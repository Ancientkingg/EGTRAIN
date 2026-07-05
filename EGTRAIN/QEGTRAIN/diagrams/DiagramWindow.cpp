#include "diagrams/DiagramWindow.h"
#include "util/TimeFormat.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QMouseEvent>
#include <QPainter>

DiagramWindow::DiagramWindow(const QString& title, QWidget* parent)
    : QDialog(parent) {
    setModal(false);
    setWindowTitle(title);
    resize(1000, 600);

    m_view = new QChartView(this);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setMouseTracking(true);
    m_view->viewport()->setMouseTracking(true);
    m_view->viewport()->installEventFilter(this);

    QPushButton* exportBtn = new QPushButton("Export PNG...", this);
    connect(exportBtn, &QPushButton::clicked, this, &DiagramWindow::exportPng);

    m_readout = new QLabel("hover over the chart", this);

    QHBoxLayout* bar = new QHBoxLayout();
    bar->addWidget(exportBtn);
    bar->addStretch();
    bar->addWidget(m_readout);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(m_view);
    layout->addLayout(bar);
}

void DiagramWindow::setChart(QChart* chart) {
    m_view->setChart(chart); // QChartView takes ownership
}

void DiagramWindow::setTimeAxisX(bool on, long long startOffsetSeconds) {
    m_timeAxis = on;
    m_startOffset = startOffsetSeconds;
}

void DiagramWindow::exportPng() {
    QString path = QFileDialog::getSaveFileName(this, "Export Diagram", "diagram.png", "PNG Image (*.png)");
    if (path.isEmpty())
        return;
    QPixmap pix = m_view->grab();
    pix.save(path, "PNG");
}

bool DiagramWindow::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == m_view->viewport() && ev->type() == QEvent::MouseMove && m_view->chart()) {
        QMouseEvent* me = static_cast<QMouseEvent*>(ev);
        // map: viewport pos -> scene pos -> chart-local pos -> data value
        QPointF scenePos = m_view->mapToScene(me->pos());
        QPointF chartPos = m_view->chart()->mapFromScene(scenePos);
        QPointF val = m_view->chart()->mapToValue(chartPos);
        QString xText = m_timeAxis
            ? QString::fromStdString(formatSimTime(static_cast<long long>(val.x()), m_startOffset))
            : QString::number(val.x(), 'f', 2);
        m_readout->setText(QString("x: %1   y: %2").arg(xText).arg(val.y(), 0, 'f', 2));
    }
    return QDialog::eventFilter(obj, ev);
}
