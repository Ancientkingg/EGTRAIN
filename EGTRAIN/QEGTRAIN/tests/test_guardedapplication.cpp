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
#include <QTest>

namespace {

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

class TestGuardedApplication : public QObject {
	Q_OBJECT

private slots:
	void cleanup();
	void reportsExceptions();
};

// The sink and the reporter capture locals of the slot, which a failed check leaves early.
void TestGuardedApplication::cleanup() {
	eglog::setSink({});
	GuardedApplication::setReporter({});
}

void TestGuardedApplication::reportsExceptions() {
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

	ThrowingObject thrower;
	QEvent first(QEvent::User);
	QVERIFY2(!QCoreApplication::sendEvent(&thrower, &first), "an event whose handler throws is not accepted");
	// the exception is logged once at error level
	QCOMPARE(errors.size(), 1);
	QVERIFY(errors.front().contains("injected"));
	QEvent second(QEvent::User);
	QCoreApplication::sendEvent(&thrower, &second);
	// a second exception is logged and nothing is reported yet
	QCOMPARE(errors.size(), 2);
	QVERIFY(reports.isEmpty());
	settle(1);
	QCoreApplication::processEvents();
	QVERIFY2(reports == QStringList{"injected"}, "one report for the exceptions that came while it waited");

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
	// an exception in a timer slot is reported
	QCOMPARE(reports.size(), 2);
	QCOMPARE(reports.back(), QStringLiteral("from a timer"));
}

int main(int argc, char** argv) {
	GuardedApplication application(argc, argv);
	TestGuardedApplication test;
	return QTest::qExec(&test, argc, argv);
}

#include "test_guardedapplication.moc"
#endif
