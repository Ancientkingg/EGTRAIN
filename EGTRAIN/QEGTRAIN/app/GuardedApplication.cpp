#include "app/GuardedApplication.h"

#include "app/MainWindowTestHooks.h"
#include "util/Log.h"
#include "widgets/UserPrompts.h"

#include <QEvent>
#include <QTimer>

#include <exception>
#include <utility>

namespace {

GuardedApplication::Reporter reporter;

// True from the moment a report is queued until it has been shown.
bool reportPending = false;

void showUnexpectedError(const QString& what) {
	UserPrompts([] { return mainWindowTestHooks().dialogsSuppressed; })
		.showError(QApplication::activeWindow(), QStringLiteral("Unexpected Error"),
			QStringLiteral("EGTRAIN could not finish the last action because of an internal error:\n\n%1\n\n"
						   "The case is still open. Save it to keep your edits.")
				.arg(what));
}

} // namespace

GuardedApplication::GuardedApplication(int& argc, char** argv) : QApplication(argc, argv) {}

bool GuardedApplication::notify(QObject* receiver, QEvent* event) {
	// Read before the delivery, which can delete the receiver; they are used only when the delivery throws.
	const char* className = receiver ? receiver->metaObject()->className() : "no receiver"; // NOLINT(clang-analyzer-deadcode.DeadStores)
	const int eventType = event ? static_cast<int>(event->type()) : 0;						// NOLINT(clang-analyzer-deadcode.DeadStores)
	QString what;
	try {
		return QApplication::notify(receiver, event);
	} catch (const std::exception& error) {
		what = QString::fromUtf8(error.what());
	} catch (...) {
		what = QStringLiteral("unknown error");
	}
	EG_LOG(AppUi, Error) << "Unexpected error in " << className << " (event " << eventType << "): " << what.toStdString() << '\n';
	if (!reportPending) {
		reportPending = true;
		QTimer::singleShot(0, this, [what] {
			if (reporter)
				reporter(what);
			else
				showUnexpectedError(what);
			reportPending = false;
		});
	}
	return false;
}

void GuardedApplication::setReporter(Reporter value) {
	reporter = std::move(value);
}
