#ifndef INDIVIDUAL_H
#define INDIVIDUAL_H

#include "BeliefTypes.h"
#include <vector>
#include <cstdint>

class Individual {
public:
  Individual(std::uint32_t id, const BeliefVec& init, const Personality& p);

  std::uint32_t id() const { return id_; }
  const BeliefVec& beliefs() const { return beliefs_; }
  const Personality& personality() const { return pers_; }

  void setNeighbors(const std::vector<std::uint32_t>& neigh, const std::vector<double>& weights);
  void step(const std::vector<Individual>& pop, double updateSpeed);

private:
  std::uint32_t id_;
  BeliefVec beliefs_;
  Personality pers_;
  std::vector<std::uint32_t> neighbors_;
  std::vector<double> weights_;

  static double dist(const BeliefVec& a, const BeliefVec& b);
  bool passSimilarityGate(double distance) const;
};

#endif
