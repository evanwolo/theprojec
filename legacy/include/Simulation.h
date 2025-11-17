#ifndef SIMULATION_H
#define SIMULATION_H

#include "Individual.h"
#include "Network.h"
#include <vector>
#include <cstdint>

struct SimConfig {
  std::uint32_t population = 200;
  std::uint32_t avgConnections = 8; // k
  double rewireProb = 0.1;          // p
  double updateSpeed = 0.01;
  std::uint32_t seed = 0;
};

class Simulation {
public:
  explicit Simulation(const SimConfig& cfg);

  void reset(const SimConfig& cfg);
  void step();
  void stepN(int n);
  double polarization() const;

  const std::vector<Individual>& individuals() const { return people_; }
  std::uint32_t generation() const { return gen_; }

private:
  SimConfig cfg_;
  Network net_;
  std::vector<Individual> people_;
  std::uint32_t gen_ = 0;

  void initPopulation();
  void bindNetwork();
};

#endif
