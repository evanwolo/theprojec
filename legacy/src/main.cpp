#include "Simulation.h"
#include "Snapshot.h"
#include <iostream>
#include <sstream>

static void printHelp() {
  std::cerr << "Commands:\n"
            << "  step N             # advance N steps\n"
            << "  state              # print JSON snapshot\n"
            << "  reset [pop k p u]  # optional: population, k, rewiring p, updateSpeed\n"
            << "  quit               # exit\n";
}

int main() {
  SimConfig cfg;
  Simulation sim(cfg);

  std::ios::sync_with_stdio(false);
  std::cin.tie(nullptr);

  printHelp();

  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream iss(line);
    std::string cmd;
    if (!(iss >> cmd)) continue;

    if (cmd == "step") {
      int n = 1;
      iss >> n;
      if (n < 1) n = 1;
      sim.stepN(n);
      std::cout << toJson(sim) << "\n";
      std::cout.flush();
    } else if (cmd == "state") {
      std::cout << toJson(sim, false) << "\n";
      std::cout.flush();
    } else if (cmd == "reset") {
      std::uint32_t pop = cfg.population, k = cfg.avgConnections;
      double p = cfg.rewireProb, u = cfg.updateSpeed;
      iss >> pop >> k >> p >> u;
      if (pop == 0) pop = 200;
      SimConfig nc{pop, k, p, u, 0};
      sim.reset(nc);
      std::cout << toJson(sim) << "\n";
      std::cout.flush();
    } else if (cmd == "quit") {
      break;
    } else if (cmd == "help") {
      printHelp();
    } else {
      std::cerr << "Unknown command: " << cmd << "\n";
      printHelp();
    }
  }
  return 0;
}
