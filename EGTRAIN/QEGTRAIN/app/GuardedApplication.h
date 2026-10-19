#ifndef GUARDEDAPPLICATION_H
#define GUARDEDAPPLICATION_H

#include <QApplication>
#include <QString>

#include <functional>

/// The application object of the window. An exception that leaves the delivery of an event is logged and reported to
/// the user, and the event loop goes on. One report is shown at a time: an exception that comes while a report waits or
/// is open is only logged.
class GuardedApplication : public QApplication {
public:
	/// Receives the text of the exception. The default shows an "Unexpected Error" box.
	using Reporter = std::function<void(const QString& what)>;

	/// The arguments of QApplication.
	GuardedApplication(int& argc, char** argv);

	/// Delivers event to receiver as QApplication does. An exception is logged at error level in the category app.ui,
	/// a report is queued when none waits, and false is returned.
	bool notify(QObject* receiver, QEvent* event) override;

	/// Replaces the reporter of the process; an empty one restores the default. For tests.
	static void setReporter(Reporter reporter);
};

#endif // GUARDEDAPPLICATION_H
