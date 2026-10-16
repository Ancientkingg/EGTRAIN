#pragma once

namespace e2e {

// Prepares a scripted run. It must be the first thing main() does after the application names are
// set. It returns false, after a message on standard error, when the settings folder cannot be created
// or QEGTRAIN_E2E_OPEN_REQUEST_AT names none of launch, startup, chooser and running.
bool configureProcess();

} // namespace e2e
