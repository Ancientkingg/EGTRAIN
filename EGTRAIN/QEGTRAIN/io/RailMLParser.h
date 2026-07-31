#ifndef RAILMLPARSER_H
#define RAILMLPARSER_H

#include <iostream>
#include <zmq.hpp>
#include <zmq_addon.hpp>
#include <nlohmann/json.hpp>
#include <assert.h>

#include "io/third_party/pugixml.hpp"


int send_traffic_state5555(nlohmann::json jsmsg, std::string TSM);
int send_traffic_state5556(nlohmann::json jsmsg, std::string TSM);
std::string trafficStateMonitoring_xml(nlohmann::json jsmsg);
std::string routeChoice_xml(nlohmann::json jsmsg);


#endif
