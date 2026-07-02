#ifndef INFOQDOCKWIDGET_H
#define INFOQDOCKWIDGET_H

#include <QDockWidget>

class infoQDockWidget : public QDockWidget {
	Q_OBJECT

public:
	infoQDockWidget(const QString& title, QWidget* parent = nullptr, Qt::WindowFlags flags = Qt::WindowFlags());
	~infoQDockWidget();

	// reimplemented function
	void closeEvent(QCloseEvent* event);

signals:
	void closed();
};

#endif // INFOQDOCKWIDGET_H