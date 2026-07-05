#ifndef SIMULATIONWORKER_H
#define SIMULATIONWORKER_H

#include <QObject>
#include <QThread>
#include <atomic>

class SimulationWorker : public QObject {
	Q_OBJECT
public:
	explicit SimulationWorker(QObject* parent = nullptr);
	~SimulationWorker() override;

	// Static accessor so the simulation engine can check controls
	static SimulationWorker* active() { return s_active; }

	// Thread-safe accessors for the simulation engine
	bool isStopRequested() const { return m_stop; }
	bool isPauseRequested() const { return m_pause; }
	int delayMs() const { return m_delayMs; }

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
	std::atomic<bool> m_stop{false};
	std::atomic<bool> m_pause{false};
	std::atomic<int> m_delayMs{0};
};

#endif // SIMULATIONWORKER_H
