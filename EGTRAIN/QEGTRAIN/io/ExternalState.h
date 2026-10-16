#ifndef EXTERNALSTATE_H
#define EXTERNALSTATE_H

#include <nlohmann/json.hpp>
#include <string>

// Sends jsmsg, with xml added as its member "xml", to the module that listens on endpoint.
// Returns true when the module answered.
bool send_external_state(nlohmann::json jsmsg, const std::string& xml, const std::string& endpoint);

#endif
