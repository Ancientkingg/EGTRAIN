#include "infoQDockWidget.h"

infoQDockWidget::infoQDockWidget(const QString& title, QWidget* parent, Qt::WindowFlags flags)
	: QDockWidget(parent, flags) {
	setWindowTitle(title);
}

infoQDockWidget::~infoQDockWidget() {
}

// reimplemented function
void infoQDockWidget::closeEvent(QCloseEvent* event) {
	// send signal to MainWindow (which will hide all sub widgets on dock widget)
	emit closed();

	// call default function
	QDockWidget::closeEvent(event);
}