#include "simulation/SimulationWorker.h"
#include "app/DispatchController.h"
#include <thread>
#include <chrono>

SimulationWorker* SimulationWorker::s_active = nullptr;

SimulationWorker::SimulationWorker(QObject* parent)
	: SimulationWorker(simulation, parent) {}

SimulationWorker::SimulationWorker(DispatchController& controller, QObject* parent)
	: QObject(parent), m_controller(controller) {
	s_active = this;
}

SimulationWorker::~SimulationWorker() {
	if (s_active == this)
		s_active = nullptr;
}

void SimulationWorker::run() {
	emit simulationStarted();
	// Each run uses a new worker, so a stop or pause requested before or during
	// simulationStarted is kept. The delay keeps the slider value set before
	// clicking Start (see MainWindow::startSimulation).
	m_controller.runSimulation();
	emit simulationFinished();
}

void SimulationWorker::requestStop() {
	m_control->requestStop();
}

void SimulationWorker::requestPause() {
	m_control->requestPause();
}

void SimulationWorker::requestResume() {
	m_control->requestResume();
}

void SimulationWorker::setDelayMs(int ms) {
	m_control->setDelayMs(ms);
}
