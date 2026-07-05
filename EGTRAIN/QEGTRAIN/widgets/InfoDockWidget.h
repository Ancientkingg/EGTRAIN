#ifndef INFODOCKWIDGET_H
#define INFODOCKWIDGET_H

#include <QDockWidget>

class InfoDockWidget : public QDockWidget {
	Q_OBJECT

public:
	InfoDockWidget(const QString& title, QWidget* parent = nullptr, Qt::WindowFlags flags = Qt::WindowFlags());
	~InfoDockWidget();

	// reimplemented function
	void closeEvent(QCloseEvent* event);

signals:
	void closed();
};

#endif // INFODOCKWIDGET_H