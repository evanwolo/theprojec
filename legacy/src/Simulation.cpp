#include "Simulation.h"
#include <random>
#include <numeric>
#include <cmath>

static BeliefVec jittered(const std::array<double, kDims>& base, std::mt19937_64& rng) {
  std::uniform_real_distribution<double> J(-0.15, 0.15);
  BeliefVec b{};
  for (int i = 0; i < kDims; ++i) b[i] = std::max(-1.0, std::min(1.0, base[i] + J(rng)));
  return b;
}

Simulation::Simulation(const SimConfig& cfg) { reset(cfg); }

void Simulation::reset(const SimConfig& cfg) {
  cfg_ = cfg;
  gen_ = 0;
  initPopulation();
  net_.buildWattsStrogatz(cfg_.population, cfg_.avgConnections, cfg_.rewireProb);
  bindNetwork();
}

void Simulation::initPopulation() {
  std::mt19937_64 rng(cfg_.seed ? cfg_.seed : std::random_device{}());
  std::uniform_real_distribution<double> U(0.0, 1.0);
  const std::array<double, kDims> c0{0.8, 0.7, 0.7, 0.6};
  const std::array<double, kDims> c1{-0.8, 0.8, -0.7, 0.8};
  const std::array<double, kDims> c2{0.7, -0.8, 0.8, -0.6};
  const std::array<double, kDims> c3{-0.7, -0.6, -0.8, 0.7};
  std::array<const std::array<double, kDims>*, 4> bases{&c0,&c1,&c2,&c3};

  people_.clear();
  people_.reserve(cfg_.population);
  for (std::uint32_t i = 0; i < cfg_.population; ++i) {
    int cluster = static_cast<int>(U(rng) * 4.0);
    if (cluster < 0) cluster = 0;
    if (cluster > 3) cluster = 3;
    BeliefVec b = jittered(*bases[cluster], rng);
    Personality p{0.2 + U(rng) * 0.5, 0.2 + U(rng) * 0.6, 0.3 + U(rng) * 0.5};
    people_.emplace_back(i, b, p);
  }
}

void Simulation::bindNetwork() {
  for (std::uint32_t i = 0; i < cfg_.population; ++i) {
    people_[i].setNeighbors(net_.neighbors(i), net_.weights(i));
  }
}

void Simulation::step() {
  // Compute next state by stepping into a shadow copy, then swap
  std::vector<Individual> next = people_;
  for (std::uint32_t i = 0; i < people_.size(); ++i) {
    next[i].step(people_, cfg_.updateSpeed);
  }
  people_.swap(next);
  ++gen_;
}

void Simulation::stepN(int n) {
  for (int i = 0; i < n; ++i) step();
}

double Simulation::polarization() const {
  // Mean of per-dimension variances
  std::array<double, kDims> mean{0,0,0,0};
  for (const auto& ind : people_) for (int d = 0; d < kDims; ++d) mean[d] += ind.beliefs()[d];
  for (int d = 0; d < kDims; ++d) mean[d] /= static_cast<double>(people_.size());
  std::array<double, kDims> var{0,0,0,0};
  for (const auto& ind : people_) {
    for (int d = 0; d < kDims; ++d) {
      const double diff = ind.beliefs()[d] - mean[d];
      var[d] += diff * diff;
    }
  }
  double avg = 0.0;
  for (int d = 0; d < kDims; ++d) avg += var[d] / static_cast<double>(people_.size());
  return avg / static_cast<double>(kDims);
}
