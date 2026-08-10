#ifndef DISPATCHCONTROLLER_H
#define DISPATCHCONTROLLER_H

#include "app/GuiSimulationSnapshot.h"
#include "simulation/Optimisation.h"
#include "simulation/Rescheduling.h"

#include <QObject>

#ifdef signals
#define EGTRAIN_RESTORE_SIGNALS_KEYWORD
#undef signals
#endif
#include "scene/SceneModel.h"
#ifdef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#define signals Q_SIGNALS
#undef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#endif

class DispatchController : public QObject {
	Q_OBJECT

public:
	DispatchController(QObject* parent = 0) : QObject() {}

	std::vector<SceneDiagnostic> prepareScene(const SceneModel& scene,
			const std::string& selectedScenarioId = {},
			const SceneRunSelection& selectedOccurrences = {});

	void resetState();

	void runSimulation();

	void Train_Simulation_Mixed_Signalling_With_Passengers(double v1, double v2, double v3);

	void printLastTrainServicePathDiagram();

	void setVectorSizesFromInput(int vec_size);

	std::shared_ptr<const GuiSimulationSnapshot> takeSimulationSnapshot();

signals:
	void iterationFinished(int timestep);
	void snapshotAvailable();
	void simulationFinished();

private:
	void publishSimulationSnapshot(int timestep);

	GuiSimulationSnapshotMailbox snapshotMailbox_;
};

// simulation object (global variable)
extern DispatchController simulation;

#endif // DISPATCHCONTROLLER_H
