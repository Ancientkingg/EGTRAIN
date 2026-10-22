#include "app/GuardedApplication.h"
#include "util/Log.h"

#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <iostream>
#include <stdexcept>
#include <string>

#ifdef Q_OS_MACOS
// On macOS (seen with Qt 6.8.3) an exception does not pass the frames of QtWidgets, QApplication::notify among them: the
// program ends in std::terminate before the guard sees it.
int main() {
	std::cout << "skipped: an exception cannot pass the frames of QtWidgets on macOS\n";
	return 77;
}
#else
namespace {

bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

// Throws from the handler of a user event.
class ThrowingObject : public QObject {
public:
	bool event(QEvent* event) override {
		if (event->type() == QEvent::User)
			throw std::runtime_error("injected");
		return QObject::event(event);
	}
};

} // namespace

int main(int argc, char** argv) {
	GuardedApplication application(argc, argv);
	QStringList errors;
	eglog::setSink([&errors](eglog::Category, eglog::Level level, const std::string& text) {
		if (level == eglog::Level::Error)
			errors << QString::fromStdString(text);
	});
	QStringList reports;
	GuardedApplication::setReporter([&reports](const QString& what) { reports << what; });
	const auto settle = [&reports](int count) {
		for (int i = 0; i < 100 && reports.size() < count; ++i)
			QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
	};
	bool ok = true;

	ThrowingObject thrower;
	QEvent first(QEvent::User);
	ok &= expect(!QCoreApplication::sendEvent(&thrower, &first), "an event whose handler throws is not accepted");
	ok &= expect(errors.size() == 1 && errors.front().contains("injected"), "the exception is logged once at error level");
	QEvent second(QEvent::User);
	QCoreApplication::sendEvent(&thrower, &second);
	ok &= expect(errors.size() == 2 && reports.isEmpty(), "a second exception is logged and nothing is reported yet");
	settle(1);
	QCoreApplication::processEvents();
	ok &= expect(reports == QStringList{"injected"}, "one report for the exceptions that came while it waited");

	QTimer timer;
	timer.setSingleShot(true);
	// A condition the compiler cannot see through: MSVC warns about unreachable code after a slot that always throws.
	const bool throwFromTimer = !reports.isEmpty();
	QObject::connect(&timer, &QTimer::timeout, [throwFromTimer] {
		if (throwFromTimer)
			throw std::runtime_error("from a timer");
	});
	timer.start(0);
	settle(2);
	ok &= expect(reports.size() == 2 && reports.back() == "from a timer", "an exception in a timer slot is reported");

	eglog::setSink({});
	GuardedApplication::setReporter({});
	if (!ok)
		return 1;
	std::cout << "all guarded application tests passed\n";
	return 0;
}
#endif
