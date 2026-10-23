#ifndef INFODOCKWIDGET_H
#define INFODOCKWIDGET_H

#include <QDockWidget>

/// The dock of the inspector, which tells when the user closes it.
class InfoDockWidget : public QDockWidget {
	Q_OBJECT

public:
	/// A dock with this window title. Its entry in the menus keeps this title when the window title changes with the item.
	InfoDockWidget(const QString& title, QWidget* parent = nullptr, Qt::WindowFlags flags = Qt::WindowFlags());
	~InfoDockWidget() override;

	/// Emits closed() and then closes the dock.
	void closeEvent(QCloseEvent* event) override;

signals:
	/// The dock is about to close.
	void closed();

protected:
	/// QDockWidget names its toggle action after the window title; this puts the first title back.
	void changeEvent(QEvent* event) override;

private:
	const QString m_menuName;
};

#endif // INFODOCKWIDGET_H
