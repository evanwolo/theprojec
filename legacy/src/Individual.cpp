#include "Individual.h"
#include <cmath>
#include <algorithm>

Individual::Individual(std::uint32_t id, const BeliefVec& init, const Personality& p)
  : id_(id), beliefs_(init), pers_(p) {}

void Individual::setNeighbors(const std::vector<std::uint32_t>& neigh, const std::vector<double>& weights) {
  neighbors_ = neigh;
  weights_ = weights;
}

double Individual::dist(const BeliefVec& a, const BeliefVec& b) {
  double d0 = a[0]-b[0], d1 = a[1]-b[1], d2 = a[2]-b[2], d3 = a[3]-b[3];
  return std::sqrt(d0*d0 + d1*d1 + d2*d2 + d3*d3);
}

bool Individual::passSimilarityGate(double distance) const {
  const double base = 0.8;
  const double adj = base * (1.0 + (1.0 - pers_.openness) * 0.5 + pers_.conformity * 0.3);
  return distance <= adj;
}

void Individual::step(const std::vector<Individual>& pop, double updateSpeed) {
  BeliefVec delta{0,0,0,0};

  for (std::size_t k = 0; k < neighbors_.size(); ++k) {
    const auto& nb = pop[neighbors_[k]];
    const double d = dist(beliefs_, nb.beliefs_);
    if (!passSimilarityGate(d)) continue;
    const double strength = std::exp(-std::pow(d / 0.8, 2.0));
    const double w = weights_[k];

    for (int dim = 0; dim < kDims; ++dim) {
      const double diff = nb.beliefs_[dim] - beliefs_[dim];
      const double attraction = std::tanh(diff * 0.3) * strength;
      delta[dim] += w * attraction * 0.3;
    }
  }

  for (int dim = 0; dim < kDims; ++dim) {
    const double social = delta[dim] * pers_.conformity * 0.5;
    const double change = pers_.openness * social * updateSpeed;
    const double v = beliefs_[dim] + change;
    beliefs_[dim] = std::max(-1.0, std::min(1.0, v));
  }
}
