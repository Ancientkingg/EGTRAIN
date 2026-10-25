#include <QImage>
#include <QSize>
#include <QTest>

#include <array>
#include <utility>

// The icons in the Qt resources load at their size and have an alpha channel.
class TestQtResourceImages : public QObject {
	Q_OBJECT

private slots:
	void loadsAtItsSize_data();
	void loadsAtItsSize();
};

void TestQtResourceImages::loadsAtItsSize_data() {
	QTest::addColumn<QString>("path");
	QTest::addColumn<int>("size");
	const auto add = [](const char* path, int size) { QTest::newRow(path) << QString::fromLatin1(path) << size; };
	const std::array<std::pair<const char*, int>, 8> icons{{
		{":/app/egtrain-16.png", 16},
		{":/app/egtrain-32.png", 32},
		{":/app/egtrain-48.png", 48},
		{":/app/egtrain-64.png", 64},
		{":/app/egtrain-128.png", 128},
		{":/app/egtrain-256.png", 256},
		{":/app/egtrain-512.png", 512},
		{":/app/egtrain-1024.png", 1024},
	}};
	for (const auto& [path, size] : icons)
		add(path, size);
	const std::array<const char*, 10> entity_icons{
		":/icons/passenger.svg",
		":/icons/train-passenger.svg",
		":/icons/train-sprinter.svg",
		":/icons/train-intercity.svg",
		":/icons/train-high-speed.svg",
		":/icons/train-freight.svg",
		":/icons/signal-neutral.svg",
		":/icons/signal-stop.svg",
		":/icons/signal-caution.svg",
		":/icons/signal-proceed.svg",
	};
	for (const char* path : entity_icons)
		add(path, 24);
	add(":/icons/pax_icon.png", 1200);
	const std::array<const char*, 11> command_icons{
		":/icons/run.svg",
		":/icons/pause.svg",
		":/icons/stop.svg",
		":/icons/skip-start.svg",
		":/icons/skip-end.svg",
		":/icons/zoom-in.svg",
		":/icons/zoom-out.svg",
		":/icons/fit.svg",
		":/icons/panel.svg",
		":/icons/follow.svg",
		":/icons/pick-target.svg",
	};
	for (const char* path : command_icons)
		add(path, 16);
	// The marks of a checked and of a partly checked check box in a list or a tree.
	for (const char* path : {":/icons/check-mark.svg", ":/icons/check-partial.svg"})
		add(path, 16);
}

void TestQtResourceImages::loadsAtItsSize() {
	QFETCH(QString, path);
	QFETCH(int, size);
	const QImage image(path);
	QVERIFY2(!image.isNull(), "the image does not load");
	QCOMPARE(image.size(), QSize(size, size));
	QVERIFY2(image.hasAlphaChannel(), "the image has no alpha channel");
}

QTEST_MAIN(TestQtResourceImages)
#include "test_qt_resource_images.moc"
