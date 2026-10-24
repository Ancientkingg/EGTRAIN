#include "widgets/InfoDockWidget.h"

#include <QAction>
#include <QEvent>

InfoDockWidget::InfoDockWidget(const QString& title, QWidget* parent, Qt::WindowFlags flags)
	: QDockWidget(parent, flags), m_menuName(title) {
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

void InfoDockWidget::changeEvent(QEvent* event) {
	QDockWidget::changeEvent(event);
	if (event->type() == QEvent::WindowTitleChange)
		toggleViewAction()->setText(m_menuName);
}
