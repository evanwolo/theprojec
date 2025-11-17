#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#include "Simulation.h"
#include <string>

std::string toJson(const Simulation& sim, bool includeTraits = false);

#endif
