#ifndef GEOCODING_H
#define GEOCODING_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <time.h>
#include <vector>
#include <array>

#include "simulation/Infrastructure.h"

using namespace std;

void readStationInfo();

string parse_station(string line);

#endif // GEOCODING_H
