#include "io/Snapshot.h"
#include <sstream>
#include <iomanip>

std::string kernelToJson(const Kernel& kernel, bool includeTraits) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(4);
    
    auto m = kernel.computeMetrics();
    
    os << "{";
    os << "\"generation\":" << kernel.generation() << ",";
    os << "\"metrics\":{";
    os << "\"polarizationMean\":" << m.polarizationMean << ",";
    os << "\"polarizationStd\":" << m.polarizationStd << ",";
    os << "\"avgOpenness\":" << m.avgOpenness << ",";
    os << "\"avgConformity\":" << m.avgConformity;
    os << "},";
    
    os << "\"agents\":[";
    const auto& agents = kernel.agents();
    for (std::size_t i = 0; i < agents.size(); ++i) {
        const auto& a = agents[i];
        os << "{";
        os << "\"id\":" << a.id << ",";
        os << "\"region\":" << a.region << ",";
        os << "\"lang\":" << static_cast<int>(a.primaryLang) << ",";
        os << "\"beliefs\":[" << a.B[0] << "," << a.B[1] << "," 
           << a.B[2] << "," << a.B[3] << "]";
        
        if (includeTraits) {
            os << ",\"traits\":{";
            os << "\"openness\":" << a.openness << ",";
            os << "\"conformity\":" << a.conformity << ",";
            os << "\"assertiveness\":" << a.assertiveness << ",";
            os << "\"sociality\":" << a.sociality;
            os << "}";
        }
        
        os << "}";
        if (i + 1 < agents.size()) os << ",";
    }
    os << "]";
    os << "}";
    
    return os.str();
}

void logMetrics(const Kernel& kernel, std::ostream& out) {
    auto m = kernel.computeMetrics();
    out << kernel.generation() << ","
        << m.polarizationMean << ","
        << m.polarizationStd << ","
        << m.avgOpenness << ","
        << m.avgConformity << ","
        << m.globalWelfare << ","
        << m.globalInequality << ","
        << m.globalHardship << "\n";
}
