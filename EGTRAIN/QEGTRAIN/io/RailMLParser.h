#ifndef RAILMLPARSER_H
#define RAILMLPARSER_H

#include <iostream>
#include <nlohmann/json.hpp>

#include "io/third_party/pugixml.hpp"

bool send_external_state(nlohmann::json jsmsg, const std::string& xml, const std::string& endpoint);
std::string trafficStateMonitoring_xml(nlohmann::json jsmsg);
std::string routeChoice_xml(nlohmann::json jsmsg);


#endif
