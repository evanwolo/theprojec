#ifndef KERNEL_SNAPSHOT_H
#define KERNEL_SNAPSHOT_H

#include "kernel/Kernel.h"
#include <string>
#include <iosfwd>

// JSON export for kernel state
std::string kernelToJson(const Kernel& kernel, bool includeTraits = false);

// CSV metrics logging
void logMetrics(const Kernel& kernel, std::ostream& out);

#endif
