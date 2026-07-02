#ifndef EGTRAIN_H
#define EGTRAIN_H

#include "Optimisation.h"

#include <QObject>

// dispatching tool
#include "Rescheduling.h"

class EGTRAIN : public QObject {
	Q_OBJECT

public:
	EGTRAIN(QObject* parent = 0) : QObject() {}

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
extern EGTRAIN simulation;

#endif // EGTRAIN_H
