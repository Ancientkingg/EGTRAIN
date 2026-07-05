#ifndef DISPATCHCONTROLLER_H
#define DISPATCHCONTROLLER_H

#include "simulation/Optimisation.h"

#include <QObject>

// dispatching tool
#include "simulation/Rescheduling.h"

class DispatchController : public QObject {
	Q_OBJECT

public:
	DispatchController(QObject* parent = 0) : QObject() {}

	void setupEgtrain();

	void resetState();

	void prepareSimulation();

	void runSimulation();

	void Train_Simulation_Mixed_Signalling(double v1, double v2, double v3);

	void Train_Simulation_Mixed_Signalling_With_Passengers(double v1, double v2, double v3);

	void printLastTrainServicePathDiagram();

	void printTimetableGraph();

	void printCommonTimetableGraph();

	void setVectorSizesFromInput(int vec_size);

signals:
	void iterationFinished(int timestep);
	void simulationFinished();
};

// simulation object (global variable)
extern DispatchController simulation;

#endif // DISPATCHCONTROLLER_H
