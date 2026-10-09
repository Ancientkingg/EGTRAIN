// The scene model has a member named "signals", so its header comes before any Qt header.
#include "scene/SceneModel.h"

#include "diagrams/TimetableTableWindow.h"
#include "diagrams/TrainColors.h"
#include "diagrams/TrainFilterButton.h"

#include <QApplication>
#include <QFile>
#include <QScreen>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>

#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition) std::cerr << "failed: " << message << "\n";
	return condition;
}

static TrainRunResult runTrain(const char* trainId, const char* serviceId) {
	TrainRunResult train;
	train.trainId = trainId;
	train.serviceId = serviceId;
	return train;
}

// The map that the result windows receive: one entry for each train of a service with a valid colour.
static bool exerciseTrainColorsForRun() {
	SceneModel scene;
	for (const char* color : {"#3C8DD2", "", "not a colour"}) {
		SceneService service;
		service.id = std::string("service-") + std::to_string(scene.services.size());
		service.visualizationColor = color;
		scene.services.push_back(service);
	}
	RunResults run;
	run.trains = {runTrain("service-0-1", "service-0"), runTrain("service-0-2", "service-0"),
		runTrain("service-1-1", "service-1"), runTrain("service-2-1", "service-2"),
		runTrain("legacy-train", ""), runTrain("orphan-1", "service-9")};
	const QHash<QString, QColor> colors = trainColorsForRun(run, scene);
	bool ok = expect(colors.size() == 2, "only the trains of the coloured service have a colour");
	ok &= expect(colors.value("service-0-1") == QColor(60, 141, 210) && colors.value("service-0-2") == QColor(60, 141, 210),
		"every occurrence of a service has its colour");
	ok &= expect(!colors.contains("service-1-1") && !colors.contains("service-2-1") && !colors.contains("legacy-train")
			&& !colors.contains("orphan-1"),
		"no colour, an invalid colour, no service and an unknown service give no entry");
	ok &= expect(trainColorsForRun(RunResults(), scene).isEmpty(), "an empty run gives an empty map");
	return ok;
}

// The filter swatch of one train, or an invalid colour when the row has no icon.
static QColor swatchColor(const QListWidget* list, int row) {
	const QIcon icon = list->item(row)->icon();
	return icon.isNull() ? QColor() : icon.pixmap(12, 12).toImage().pixelColor(6, 6);
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	QFile stylesheet(QStringLiteral(EGTRAIN_DIALOG_QSS));
	if (!stylesheet.open(QIODevice::ReadOnly)) return 1;
	app.setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
	std::vector<TimetableResultRow> rows(1);
	rows.front().trainId = "train-a";
	rows.front().stationId = "station-b";
	TimetableTableWindow window(std::move(rows), 0, {});
	QFont scaled = window.font();
	scaled.setPointSizeF(18);
	window.setFont(scaled);
	RunProvenance provenance;
	provenance.caseName = "Case A";
	provenance.appliedScenario = "Scenario B";
	window.setRunProvenance(std::move(provenance));
	window.show();
	app.processEvents();
	const auto* table = window.findChild<QTableWidget*>();
	const auto* context = window.findChild<QLabel*>("timetableContext");
	const QRect screen = window.screen()->availableGeometry();
	bool ok = window.width() <= screen.width() * 9 / 10
		&& window.height() <= screen.height() * 4 / 5
		&& context && context->isVisible() && context->text().contains("Case A")
		&& context->textFormat() == Qt::PlainText
		&& table && table->rowCount() == 1 && table->item(0, 0)->text() == "train-a"
		&& window.findChild<TrainFilterButton*>()->isVisible();
	if (!ok) std::cerr << "timetable presentation or data changed\n";

	std::vector<TimetableResultRow> twoTrains(2);
	twoTrains[0].trainId = "train-a";
	twoTrains[1].trainId = "train-b";
	TimetableTableWindow colored(std::move(twoTrains), 0, {});
	const auto* list = colored.findChild<TrainFilterButton*>()->menu()->findChild<QListWidget*>();
	ok &= expect(list && list->count() == 2 && !swatchColor(list, 0).isValid() && !swatchColor(list, 1).isValid(),
		"the train filter has no swatches without colours");
	colored.setTrainColors({{"train-a", QColor(60, 141, 210)}, {"train-c", QColor(1, 2, 3)}, {"train-b", QColor()}});
	ok &= expect(list && list->count() == 2 && list->item(0)->text() == "train-a"
			&& swatchColor(list, 0) == QColor(60, 141, 210) && !swatchColor(list, 1).isValid(),
		"a train of a coloured service has its swatch and another train has none");
	ok &= exerciseTrainColorsForRun();
	return ok ? 0 : 1;
}
