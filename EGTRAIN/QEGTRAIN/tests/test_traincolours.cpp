#include "diagrams/TrainColors.h"

#include <QStringList>
#include <QTest>

#include <array>
#include <utility>

namespace {

const std::array<QColor, 6> kPalette{QColor("#2a78d6"), QColor("#c2448a"), QColor("#4c8a12"), QColor("#6127b1"), QColor("#e35b6b"),
	QColor("#842a65")};
const QColor kGrey("#8a97a1");
const QColor kHidden("#c3cbd1");

// Trains named T0, T1 and so on, each a service of its own.
QVector<TrainColourInput> numberedTrains(int count) {
	QVector<TrainColourInput> trains;
	for (int i = 0; i < count; ++i) trains.append({QString("T%1").arg(i), QString(), true, QString()});
	return trains;
}

// The trains of Assignment: 22 trains in the services IC1723 and S19825 of 6 trains and IC2025 and S9827 of 5.
QVector<TrainColourInput> assignmentTrains() {
	QVector<TrainColourInput> trains;
	const std::array<std::pair<const char*, int>, 4> services{{{"IC1723", 6}, {"S19825", 6}, {"IC2025", 5}, {"S9827", 5}}};
	for (const auto& [service, count] : services)
		for (int i = 1; i <= count; ++i) trains.append({QString("%1-%2").arg(service).arg(i), QString(service), true, QString()});
	return trains;
}

} // namespace

class TestTrainColours : public QObject {
	Q_OBJECT

private slots:
	void coloursPerTrain();
	void colourPerService();
	void greyWithSelectedTrain();
	void sceneColoursWin();
	void copiesTakeColourOfTheirTrain();
};

// Six shown trains or fewer take the palette in their order; while the window has six trains or fewer a train keeps its
// colour when another is hidden, and with more trains the shown ones take the palette in their order.
void TestTrainColours::coloursPerTrain() {
	const TrainColourPlan three = trainColourPlan(numberedTrains(3), {});
	// 3 trains get the palette entries 0 to 2.
	QCOMPARE(three.mode, TrainColourPlan::Mode::PerTrain);
	QCOMPARE(three.colours.size(), qsizetype(3));
	QCOMPARE(three.colours.value("T0"), kPalette[0]);
	QCOMPARE(three.colours.value("T1"), kPalette[1]);
	QCOMPARE(three.colours.value("T2"), kPalette[2]);
	QVector<TrainColourInput> six = numberedTrains(6);
	six[1].shown = false;
	const TrainColourPlan fiveOfSix = trainColourPlan(six, {});
	// Of six trains a hidden one is light grey and the others keep their colours.
	QCOMPARE(fiveOfSix.colours.value("T1"), kHidden);
	QCOMPARE(fiveOfSix.colours.value("T2"), kPalette[2]);
	QCOMPARE(fiveOfSix.colours.value("T5"), kPalette[5]);
	QVector<TrainColourInput> sixInTwo = numberedTrains(6);
	for (int i = 0; i < 6; ++i) sixInTwo[i].service = i < 3 ? "A" : "B";
	const TrainColourPlan two = trainColourPlan(sixInTwo, {});
	// Six trains in two services take a colour each.
	QCOMPARE(two.mode, TrainColourPlan::Mode::PerTrain);
	QCOMPARE(two.colours.value("T1"), kPalette[1]);
	QCOMPARE(two.colours.value("T5"), kPalette[5]);
	QVector<TrainColourInput> many = numberedTrains(47);
	for (int i = 0; i < 47; ++i) many[i].shown = i == 3 || i == 20 || i == 46;
	const TrainColourPlan threeOf47 = trainColourPlan(many, {});
	// 3 shown trains of 47 take the palette entries 0 to 2 in their order, and the hidden ones are light grey.
	QCOMPARE(threeOf47.mode, TrainColourPlan::Mode::PerTrain);
	QCOMPARE(threeOf47.colours.value("T3"), kPalette[0]);
	QCOMPARE(threeOf47.colours.value("T20"), kPalette[1]);
	QCOMPARE(threeOf47.colours.value("T46"), kPalette[2]);
	QCOMPARE(threeOf47.colours.value("T0"), kHidden);
	QCOMPARE(threeOf47.colours.size(), qsizetype(47));
}

// More than six shown trains in six services or fewer take a colour for each service; while the window has six services
// or fewer a service keeps its colour when another service is hidden.
void TestTrainColours::colourPerService() {
	const TrainColourPlan assignment = trainColourPlan(assignmentTrains(), {});
	// 22 trains in 4 services get one colour for each service.
	QCOMPARE(assignment.mode, TrainColourPlan::Mode::PerService);
	QCOMPARE(assignment.colours.value("IC1723-1"), kPalette[0]);
	QCOMPARE(assignment.colours.value("IC1723-6"), kPalette[0]);
	QCOMPARE(assignment.colours.value("S19825-3"), kPalette[1]);
	QCOMPARE(assignment.colours.value("IC2025-5"), kPalette[2]);
	QCOMPARE(assignment.colours.value("S9827-1"), kPalette[3]);
	QVector<TrainColourInput> withoutS19825 = assignmentTrains();
	for (TrainColourInput& train : withoutS19825) train.shown = !train.id.startsWith("S19825");
	const TrainColourPlan sixteen = trainColourPlan(withoutS19825, {});
	// Hiding a service keeps the colours of the other services.
	QCOMPARE(sixteen.mode, TrainColourPlan::Mode::PerService);
	QCOMPARE(sixteen.colours.value("IC2025-1"), kPalette[2]);
	QCOMPARE(sixteen.colours.value("S9827-5"), kPalette[3]);
	QCOMPARE(sixteen.colours.value("S19825-1"), kHidden);
	QVector<TrainColourInput> sevenServices = numberedTrains(7);
	const TrainColourPlan seven = trainColourPlan(sevenServices, {});
	// 7 trains in 7 services, each train a service of its own, are grey.
	QCOMPARE(seven.mode, TrainColourPlan::Mode::Grey);
	QCOMPARE(seven.colours.value("T0"), kGrey);
}

// More shown trains in more than six services are grey, and the selected train takes the first colour of the palette.
void TestTrainColours::greyWithSelectedTrain() {
	const TrainColourPlan many = trainColourPlan(numberedTrains(47), {}, "T10");
	bool grey = true;
	for (int i = 0; i < 47; ++i)
		if (i != 10) grey &= many.colours.value(QString("T%1").arg(i)) == kGrey;
	// 47 trains are grey with the selected one blue.
	QCOMPARE(many.mode, TrainColourPlan::Mode::Grey);
	QVERIFY(grey);
	QCOMPARE(many.colours.value("T10"), kPalette[0]);
	QVector<TrainColourInput> hiddenSelected = numberedTrains(47);
	hiddenSelected[10].shown = false;
	// A hidden or unknown selected train takes no colour.
	QCOMPARE(trainColourPlan(hiddenSelected, {}, "T10").colours.value("T10"), kHidden);
	QCOMPARE(trainColourPlan(numberedTrains(47), {}, "X").colours.size(), qsizetype(47));
}

// The colour of the service of a train in the case wins over the palette, for a train, a service and the selected train.
void TestTrainColours::sceneColoursWin() {
	const QColor red(200, 30, 30);
	const TrainColourPlan three = trainColourPlan(numberedTrains(3), {{"T1", red}, {"T2", QColor()}});
	// A service colour of the case wins over the palette, and the next train keeps its place in the palette.
	QCOMPARE(three.colours.value("T0"), kPalette[0]);
	QCOMPARE(three.colours.value("T1"), red);
	QCOMPARE(three.colours.value("T2"), kPalette[2]);
	QHash<QString, QColor> s19825;
	for (int i = 1; i <= 6; ++i) s19825.insert(QString("S19825-%1").arg(i), red);
	const TrainColourPlan assignment = trainColourPlan(assignmentTrains(), s19825);
	// A service colour of the case wins over the palette colour of its service.
	QCOMPARE(assignment.colours.value("S19825-1"), red);
	QCOMPARE(assignment.colours.value("S19825-6"), red);
	QCOMPARE(assignment.colours.value("IC2025-1"), kPalette[2]);
	const TrainColourPlan many = trainColourPlan(numberedTrains(47), {{"T10", red}, {"T11", red}}, "T10");
	// Among many grey trains the selected one takes the colour of its service in the case.
	QCOMPARE(many.colours.value("T10"), red);
	QCOMPARE(many.colours.value("T11"), kGrey);
	QVector<TrainColourInput> hidden = numberedTrains(3);
	hidden[1].shown = false;
	// A hidden train is light grey also with a colour in the case.
	QCOMPARE(trainColourPlan(hidden, {{"T1", red}}).colours.value("T1"), kHidden);
}

// A copy, such as the first train of a capacity cycle one period later, takes the colour of its train and counts as
// that train.
void TestTrainColours::copiesTakeColourOfTheirTrain() {
	QVector<TrainColourInput> cycle = numberedTrains(31);
	cycle.append({"T0 (next period)", QString(), true, "T0"});
	for (TrainColourInput& train : cycle) train.shown = train.id.startsWith("T0");
	const TrainColourPlan two = trainColourPlan(cycle, {});
	// Of 31 trains the first train and its copy, both shown, take the first colour.
	QCOMPARE(two.mode, TrainColourPlan::Mode::PerTrain);
	QCOMPARE(two.colours.value("T0"), kPalette[0]);
	QCOMPARE(two.colours.value("T0 (next period)"), kPalette[0]);
	cycle[0].shown = false;
	const TrainColourPlan copyOnly = trainColourPlan(cycle, {});
	// A shown copy of a hidden train takes the colour of its train, which stays light grey.
	QCOMPARE(copyOnly.colours.value("T0"), kHidden);
	QCOMPARE(copyOnly.colours.value("T0 (next period)"), kPalette[0]);
	QVector<TrainColourInput> six = numberedTrains(6);
	six.append({"T2 (next period)", QString(), true, "T2"});
	const TrainColourPlan sixAndCopy = trainColourPlan(six, {});
	// Six trains and a copy keep a colour each, and the copy takes that of its train.
	QCOMPARE(sixAndCopy.mode, TrainColourPlan::Mode::PerTrain);
	QCOMPARE(sixAndCopy.colours.value("T2 (next period)"), kPalette[2]);
	QCOMPARE(sixAndCopy.colours.value("T5"), kPalette[5]);
	six.back().shown = false;
	// A hidden copy is light grey.
	QCOMPARE(trainColourPlan(six, {}).colours.value("T2 (next period)"), kHidden);
	QVector<TrainColourInput> many = numberedTrains(47);
	many.append({"T10 (next period)", QString(), true, "T10"});
	const TrainColourPlan selectedCopy = trainColourPlan(many, {}, "T10 (next period)");
	// Selecting a copy among many grey trains colours the copy and its train.
	QCOMPARE(selectedCopy.mode, TrainColourPlan::Mode::Grey);
	QCOMPARE(selectedCopy.colours.value("T10"), kPalette[0]);
	QCOMPARE(selectedCopy.colours.value("T10 (next period)"), kPalette[0]);
	QCOMPARE(selectedCopy.colours.value("T11"), kGrey);
}

QTEST_APPLESS_MAIN(TestTrainColours)
#include "test_traincolours.moc"
