#ifndef SIMULATIONWORKER_H
#define SIMULATIONWORKER_H

#include "simulation/RunControl.h"

#include <QObject>
#include <QThread>
#include <memory>

class DispatchController;

class SimulationWorker : public QObject {
	Q_OBJECT
public:
	// A worker without a controller runs the controller of the process.
	explicit SimulationWorker(QObject* parent = nullptr);
	// The controller has to outlive the worker.
	explicit SimulationWorker(DispatchController& controller, QObject* parent = nullptr);
	~SimulationWorker() override;

	// Static accessor so the simulation engine can find the control of the run in the window
	static SimulationWorker* active() { return s_active; }

	// The stop, pause, delay and command queue of the run of this worker. The functions below forward
	// to it. It exists from construction, so a request made before the run starts is kept.
	std::shared_ptr<RunControl> control() const { return m_control; }

	// Thread-safe accessors for the simulation engine
	bool isStopRequested() const { return m_control->isStopRequested(); }
	// Observation-only latch; set by requestStop() and never cleared.
	bool wasCancellationRequested() const { return m_control->wasCancellationRequested(); }
	bool isPauseRequested() const { return m_control->isPauseRequested(); }
	int delayMs() const { return m_control->delayMs(); }

public slots:
	// Called from the worker thread to run the full simulation.
	// The existing DispatchController::iterationFinished signal crosses threads automatically.
	void run();
	void requestStop();
	void requestPause();
	void requestResume();
	void setDelayMs(int ms);

signals:
	void simulationFinished();
	void simulationStarted();

private:
	static SimulationWorker* s_active;
	DispatchController& m_controller;
	const std::shared_ptr<RunControl> m_control = std::make_shared<RunControl>();
};

#endif // SIMULATIONWORKER_H
