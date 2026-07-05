#include "widgets/InfoDockWidget.h"

InfoDockWidget::InfoDockWidget(const QString& title, QWidget* parent, Qt::WindowFlags flags)
	: QDockWidget(parent, flags) {
	setWindowTitle(title);
}

InfoDockWidget::~InfoDockWidget() {
}

// reimplemented function
void InfoDockWidget::closeEvent(QCloseEvent* event) {
	// send signal to MainWindow (which will hide all sub widgets on dock widget)
	emit closed();

	// call default function
	QDockWidget::closeEvent(event);
}