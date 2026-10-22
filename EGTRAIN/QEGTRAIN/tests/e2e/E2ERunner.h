#pragma once

namespace e2e {

// Prepares a scripted run. It must be the first thing main() does after the application names are
// set. It returns false, after a message on standard error, when the settings folder cannot be created
// or QEGTRAIN_E2E_OPEN_REQUEST_AT names none of launch, startup, chooser and running.
bool configureProcess();

// Gives the application a dark palette and dark tooltips, as a dark system setting does, when
// QEGTRAIN_E2E_DARK_PALETTE is set. main() calls it right after it makes the application object.
void configureApplication();

// Whether the run is the creator acceptance scenario, which starts without a case study.
bool wantsCreatorAcceptance();

// Opens a window without a scene for the creator acceptance scenario and runs the event loop. It returns the
// exit code of the loop, or 1, after a message on standard error, when QEGTRAIN_E2E_OUT names no folder that can
// be made. The application object must exist.
int runCreatorAcceptanceMain();

} // namespace e2e
