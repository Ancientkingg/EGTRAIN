#ifndef INFODOCKWIDGET_H
#define INFODOCKWIDGET_H

#include <QDockWidget>

/// The dock of the inspector, which tells when the user closes it.
class InfoDockWidget : public QDockWidget {
	Q_OBJECT

public:
	/// A dock with this window title.
	InfoDockWidget(const QString& title, QWidget* parent = nullptr, Qt::WindowFlags flags = Qt::WindowFlags());
	~InfoDockWidget() override;

	/// Emits closed() and then closes the dock.
	void closeEvent(QCloseEvent* event) override;

signals:
	/// The dock is about to close.
	void closed();
};

#endif // INFODOCKWIDGET_H
