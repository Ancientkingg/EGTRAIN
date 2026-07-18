#include "diagrams/DiagramWindow.h"

#include <QApplication>
#include <QLabel>
#include <QLineSeries>
#include <QValueAxis>

#include <iostream>

QT_CHARTS_USE_NAMESPACE

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static QLabel* readoutFor(DiagramWindow& window) {
	for (QLabel* label : window.findChildren<QLabel*>()) {
		if (label->text().startsWith("Hover over the chart") ||
			label->text().contains("x:") || label->text().contains("Speed (m/s)"))
			return label;
	}
	return nullptr;
}

static QChart* chartWithAxes(const QString& xTitle, const QString& yTitle, QLineSeries*& series) {
	auto* chart = new QChart();
	series = new QLineSeries();
	series->setName("Train A");
	series->append(12.345, 678.901);
	chart->addSeries(series);
	chart->createDefaultAxes();
	chart->axes(Qt::Horizontal).first()->setTitleText(xTitle);
	chart->axes(Qt::Vertical).first()->setTitleText(yTitle);
	return chart;
}

int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	bool ok = true;

	QLineSeries* speedSeries = nullptr;
	DiagramWindow speedWindow("Speed versus tractive effort");
	speedWindow.setChart(chartWithAxes("Speed (m/s)", "Tractive effort (N)", speedSeries));
	QLabel* speedReadout = readoutFor(speedWindow);
	ok &= expect(speedReadout != nullptr, "speed chart readout label");
	if (speedReadout) {
		emit speedSeries->hovered(QPointF(12.345, 678.901), true);
		ok &= expect(speedReadout->text().contains("Train A"), "hovered series name");
		ok &= expect(speedReadout->text().contains("Speed (m/s)"), "speed axis label");
		ok &= expect(speedReadout->text().contains("Tractive effort (N)"), "tractive-effort axis label");
		ok &= expect(speedReadout->text().contains("12.35"), "exact hovered x value");
		ok &= expect(speedReadout->text().contains("678.90"), "exact hovered y value");
		emit speedSeries->hovered(QPointF(12.345, 678.901), false);
		ok &= expect(!speedReadout->text().contains("Train A"), "hover exit clears series name");
		ok &= expect(speedReadout->text().contains("Speed (m/s)"), "hover exit keeps speed axis label");
		ok &= expect(speedReadout->text().contains("Tractive effort (N)"), "hover exit keeps tractive-effort axis label");
		ok &= expect(speedReadout->text().contains("12.35"), "hover exit keeps exact x value");
		ok &= expect(speedReadout->text().contains("678.90"), "hover exit keeps exact y value");
	}

	QLineSeries* timeSeries = nullptr;
	DiagramWindow timeWindow("Time versus distance");
	timeWindow.setChart(chartWithAxes("Time", "Distance (km)", timeSeries));
	timeWindow.setTimeAxisX(true, 8 * 3600);
	QLabel* timeReadout = readoutFor(timeWindow);
	ok &= expect(timeReadout != nullptr, "time chart readout label");
	if (timeReadout) {
		emit timeSeries->hovered(QPointF(90.25, 12.345), true);
		ok &= expect(timeReadout->text().contains("Train A"), "time hovered series name");
		ok &= expect(timeReadout->text().contains("Time"), "time axis label");
		ok &= expect(timeReadout->text().contains("Distance (km)"), "distance axis label");
		ok &= expect(timeReadout->text().contains("08:01:30"), "formatted time with offset");
		ok &= expect(timeReadout->text().contains("12.35"), "exact hovered distance value");
	}

	if (!ok)
		return 1;

	std::cout << "all DiagramWindow tests passed\n";
	return 0;
}
