#include "diagrams/TrainColors.h"

#include <QStringList>

#include <array>
#include <iostream>
#include <utility>

namespace {

bool expect(bool condition, const char* message) {
	if (!condition) std::cerr << "failed: " << message << "\n";
	return condition;
}

const std::array<QColor, 6> kPalette{QColor("#2a78d6"), QColor("#c2448a"), QColor("#4c8a12"), QColor("#6127b1"), QColor("#e35b6b"),
	QColor("#842a65")};
const QColor kGrey("#8a97a1");
const QColor kHidden("#c3cbd1");

// Trains named T0, T1 and so on, each a service of its own.
QVector<TrainColourInput> numberedTrains(int count) {
	QVector<TrainColourInput> trains;
	for (int i = 0; i < count; ++i) trains.append({QString("T%1").arg(i), QString(), true});
	return trains;
}

// The trains of Assignment: 22 trains in the services IC1723 and S19825 of 6 trains and IC2025 and S9827 of 5.
QVector<TrainColourInput> assignmentTrains() {
	QVector<TrainColourInput> trains;
	const std::array<std::pair<const char*, int>, 4> services{{{"IC1723", 6}, {"S19825", 6}, {"IC2025", 5}, {"S9827", 5}}};
	for (const auto& [service, count] : services)
		for (int i = 1; i <= count; ++i) trains.append({QString("%1-%2").arg(service).arg(i), QString(service), true});
	return trains;
}

// Six shown trains or fewer take the palette in their order; while the window has six trains or fewer a train keeps its
// colour when another is hidden, and with more trains the shown ones take the palette in their order.
bool exercisePerTrain() {
	bool ok = true;
	const TrainColourPlan three = trainColourPlan(numberedTrains(3), {});
	ok &= expect(three.mode == TrainColourPlan::Mode::PerTrain && three.colours.size() == 3 && three.colours.value("T0") == kPalette[0]
			&& three.colours.value("T1") == kPalette[1] && three.colours.value("T2") == kPalette[2],
		"3 trains get the palette entries 0 to 2");
	QVector<TrainColourInput> six = numberedTrains(6);
	six[1].shown = false;
	const TrainColourPlan fiveOfSix = trainColourPlan(six, {});
	ok &= expect(fiveOfSix.colours.value("T1") == kHidden && fiveOfSix.colours.value("T2") == kPalette[2]
			&& fiveOfSix.colours.value("T5") == kPalette[5],
		"of six trains a hidden one is light grey and the others keep their colours");
	QVector<TrainColourInput> sixInTwo = numberedTrains(6);
	for (int i = 0; i < 6; ++i) sixInTwo[i].service = i < 3 ? "A" : "B";
	const TrainColourPlan two = trainColourPlan(sixInTwo, {});
	ok &= expect(two.mode == TrainColourPlan::Mode::PerTrain && two.colours.value("T1") == kPalette[1] && two.colours.value("T5") == kPalette[5],
		"six trains in two services take a colour each");
	QVector<TrainColourInput> many = numberedTrains(47);
	for (int i = 0; i < 47; ++i) many[i].shown = i == 3 || i == 20 || i == 46;
	const TrainColourPlan threeOf47 = trainColourPlan(many, {});
	ok &= expect(threeOf47.mode == TrainColourPlan::Mode::PerTrain && threeOf47.colours.value("T3") == kPalette[0]
			&& threeOf47.colours.value("T20") == kPalette[1] && threeOf47.colours.value("T46") == kPalette[2]
			&& threeOf47.colours.value("T0") == kHidden && threeOf47.colours.size() == 47,
		"3 shown trains of 47 take the palette entries 0 to 2 in their order, and the hidden ones are light grey");
	return ok;
}

// More than six shown trains in six services or fewer take a colour for each service; while the window has six services
// or fewer a service keeps its colour when another service is hidden.
bool exercisePerService() {
	bool ok = true;
	const TrainColourPlan assignment = trainColourPlan(assignmentTrains(), {});
	ok &= expect(assignment.mode == TrainColourPlan::Mode::PerService && assignment.colours.value("IC1723-1") == kPalette[0]
			&& assignment.colours.value("IC1723-6") == kPalette[0] && assignment.colours.value("S19825-3") == kPalette[1]
			&& assignment.colours.value("IC2025-5") == kPalette[2] && assignment.colours.value("S9827-1") == kPalette[3],
		"22 trains in 4 services get one colour for each service");
	QVector<TrainColourInput> withoutS19825 = assignmentTrains();
	for (TrainColourInput& train : withoutS19825) train.shown = !train.id.startsWith("S19825");
	const TrainColourPlan sixteen = trainColourPlan(withoutS19825, {});
	ok &= expect(sixteen.mode == TrainColourPlan::Mode::PerService && sixteen.colours.value("IC2025-1") == kPalette[2]
			&& sixteen.colours.value("S9827-5") == kPalette[3] && sixteen.colours.value("S19825-1") == kHidden,
		"hiding a service keeps the colours of the other services");
	QVector<TrainColourInput> sevenServices = numberedTrains(7);
	const TrainColourPlan seven = trainColourPlan(sevenServices, {});
	ok &= expect(seven.mode == TrainColourPlan::Mode::Grey && seven.colours.value("T0") == kGrey,
		"7 trains in 7 services, each train a service of its own, are grey");
	return ok;
}

// More shown trains in more than six services are grey, and the selected train takes the first colour of the palette.
bool exerciseGrey() {
	bool ok = true;
	const TrainColourPlan many = trainColourPlan(numberedTrains(47), {}, "T10");
	bool grey = true;
	for (int i = 0; i < 47; ++i)
		if (i != 10) grey &= many.colours.value(QString("T%1").arg(i)) == kGrey;
	ok &= expect(many.mode == TrainColourPlan::Mode::Grey && grey && many.colours.value("T10") == kPalette[0],
		"47 trains are grey with the selected one blue");
	QVector<TrainColourInput> hiddenSelected = numberedTrains(47);
	hiddenSelected[10].shown = false;
	ok &= expect(trainColourPlan(hiddenSelected, {}, "T10").colours.value("T10") == kHidden && trainColourPlan(numberedTrains(47), {}, "X").colours.size() == 47,
		"a hidden or unknown selected train takes no colour");
	return ok;
}

// The colour of the service of a train in the case wins over the palette, for a train, a service and the selected train.
bool exerciseSceneColours() {
	bool ok = true;
	const QColor red(200, 30, 30);
	const TrainColourPlan three = trainColourPlan(numberedTrains(3), {{"T1", red}, {"T2", QColor()}});
	ok &= expect(three.colours.value("T0") == kPalette[0] && three.colours.value("T1") == red && three.colours.value("T2") == kPalette[2],
		"a service colour of the case wins over the palette, and the next train keeps its place in the palette");
	QHash<QString, QColor> s19825;
	for (int i = 1; i <= 6; ++i) s19825.insert(QString("S19825-%1").arg(i), red);
	const TrainColourPlan assignment = trainColourPlan(assignmentTrains(), s19825);
	ok &= expect(assignment.colours.value("S19825-1") == red && assignment.colours.value("S19825-6") == red
			&& assignment.colours.value("IC2025-1") == kPalette[2],
		"a service colour of the case wins over the palette colour of its service");
	const TrainColourPlan many = trainColourPlan(numberedTrains(47), {{"T10", red}, {"T11", red}}, "T10");
	ok &= expect(many.colours.value("T10") == red && many.colours.value("T11") == kGrey,
		"among many grey trains the selected one takes the colour of its service in the case");
	QVector<TrainColourInput> hidden = numberedTrains(3);
	hidden[1].shown = false;
	ok &= expect(trainColourPlan(hidden, {{"T1", red}}).colours.value("T1") == kHidden, "a hidden train is light grey also with a colour in the case");
	return ok;
}

} // namespace

int main() {
	bool ok = exercisePerTrain();
	ok &= exercisePerService();
	ok &= exerciseGrey();
	ok &= exerciseSceneColours();
	std::cout << (ok ? "test_traincolours passed\n" : "test_traincolours FAILED\n");
	return ok ? 0 : 1;
}
