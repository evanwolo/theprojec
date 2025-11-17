#include "Snapshot.h"
#include <sstream>
#include <iomanip>

[[maybe_unused]]
static void appendEsc(std::ostream& os, const std::string& s) {
  os << '"';
  for (char c : s) {
    if (c == '"' || c == '\\') os << '\\';
    os << c;
  }
  os << '"';
}

std::string toJson(const Simulation& sim, bool includeTraits) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(4);
  os << "{";
  os << "\"generation\":" << sim.generation() << ",";
  os << "\"polarization\":" << sim.polarization() << ",";
  os << "\"individuals\":[";
  const auto& v = sim.individuals();
  for (std::size_t i = 0; i < v.size(); ++i) {
    const auto& ind = v[i];
    os << "{";
    os << "\"id\":" << ind.id() << ",";
    os << "\"beliefs\":[" << ind.beliefs()[0] << "," << ind.beliefs()[1] << "," << ind.beliefs()[2] << "," << ind.beliefs()[3] << "]";
    if (includeTraits) {
      os << ",\"traits\":{";
      os << "\"openness\":" << ind.personality().openness << ",";
      os << "\"charisma\":" << ind.personality().charisma << ",";
      os << "\"conformity\":" << ind.personality().conformity << "}";
    }
    os << "}";
    if (i + 1 < v.size()) os << ",";
  }
  os << "]";
  os << "}";
  return os.str();
}
