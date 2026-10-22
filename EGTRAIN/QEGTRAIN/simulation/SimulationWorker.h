#ifndef SIMULATIONWORKER_H
#define SIMULATIONWORKER_H

#include "simulation/RunControl.h"

#include <QObject>
#include <QThread>
#include <memory>

class DispatchController;

/// Runs one simulation of a DispatchController on the thread that the worker lives on, and holds the RunControl of that run.
///
/// The window makes a new worker for each run, moves it to a new QThread and calls run() when the thread starts. The worker
/// constructed last is the active one, through which DispatchController::runSimulation finds the control of its run.
class SimulationWorker : public QObject {
	Q_OBJECT
public:
	/// A worker without a controller runs the controller of the process, the global simulation.
	explicit SimulationWorker(QObject* parent = nullptr);
	/// The controller has to outlive the worker.
	explicit SimulationWorker(DispatchController& controller, QObject* parent = nullptr);
	/// Leaves no active worker when this one is the active worker.
	~SimulationWorker() override;

	/// The worker constructed last, or nullptr once it is destroyed. A run without a window has none and makes its own control.
	static SimulationWorker* active() { return s_active; }

	/// The stop, pause, delay and command queue of the run of this worker. The functions below forward
	/// to it. It exists from construction, so a request made before the run starts is kept.
	std::shared_ptr<RunControl> control() const { return m_control; }

	/// RunControl::isStopRequested of control(). Safe on any thread.
	bool isStopRequested() const { return m_control->isStopRequested(); }
	/// Observation-only latch; set by requestStop() and never cleared.
	bool wasCancellationRequested() const { return m_control->wasCancellationRequested(); }
	/// RunControl::isPauseRequested of control(). Safe on any thread.
	bool isPauseRequested() const { return m_control->isPauseRequested(); }
	/// RunControl::delayMs of control(), in milliseconds. Safe on any thread.
	int delayMs() const { return m_control->delayMs(); }

public slots:
	/// Runs the simulation of the controller on the calling thread, from simulationStarted to simulationFinished. The window
	/// calls it on the thread of the worker when that thread starts.
	void run();
	/// RunControl::requestStop of control(). Safe on any thread.
	void requestStop();
	/// RunControl::requestPause of control(). Safe on any thread.
	void requestPause();
	/// RunControl::requestResume of control(). Safe on any thread.
	void requestResume();
	/// RunControl::setDelayMs of control(), in milliseconds. Safe on any thread.
	void setDelayMs(int ms);

signals:
	/// Emitted by run() on the thread of the run when DispatchController::runSimulation has returned, also for a run that
	/// was stopped or turned down.
	void simulationFinished();
	/// Emitted by run() on the thread of the run before the simulation starts.
	void simulationStarted();

private:
	static SimulationWorker* s_active;
	DispatchController& m_controller;
	const std::shared_ptr<RunControl> m_control = std::make_shared<RunControl>();
};

#endif // SIMULATIONWORKER_H
