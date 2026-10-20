#ifndef RTTPXML_H
#define RTTPXML_H

#include <iostream>
#include <nlohmann/json.hpp>

#include "io/third_party/pugixml.hpp"

std::string trafficStateMonitoring_xml(nlohmann::json jsmsg);
std::string routeChoice_xml(nlohmann::json jsmsg);
void read_rttp_train_view(const std::string& rttp);
void read_rttp_infra_view(const std::string& rttp);


#endif
