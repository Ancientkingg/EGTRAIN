#include "simulation/SimulationWorker.h"
#include "app/EGTRAIN.h"
#include <thread>
#include <chrono>

extern EGTRAIN simulation;

SimulationWorker* SimulationWorker::s_active = nullptr;

SimulationWorker::SimulationWorker(QObject* parent)
	: QObject(parent) {
	s_active = this;
}

SimulationWorker::~SimulationWorker() {
	if (s_active == this)
		s_active = nullptr;
}

void SimulationWorker::run() {
	emit simulationStarted();
	m_stop = false;
	m_pause = false;
	// m_delayMs is intentionally NOT reset here so the slider value set
	// before clicking Start is preserved (see MainWindow::startSimulation).
	simulation.runSimulation();
	emit simulationFinished();
}

void SimulationWorker::requestStop() {
	m_stop = true;
	m_pause = false; // unpause so the worker thread can see the stop flag
}

void SimulationWorker::requestPause() {
	m_pause = true;
}

void SimulationWorker::requestResume() {
	m_pause = false;
}

void SimulationWorker::setDelayMs(int ms) {
	m_delayMs = ms;
}
