#ifndef NETWORK_H
#define NETWORK_H

#include <vector>
#include <cstdint>

class Network {
public:
  // Build Watts–Strogatz: N nodes, k neighbors per node (even), rewiring prob p
  void buildWattsStrogatz(std::uint32_t N, std::uint32_t k, double p);

  // Adjacency with weights per node
  const std::vector<std::uint32_t>& neighbors(std::uint32_t node) const { return adj_[node]; }
  const std::vector<double>& weights(std::uint32_t node) const { return w_[node]; }
  std::uint32_t size() const { return static_cast<std::uint32_t>(adj_.size()); }

private:
  std::vector<std::vector<std::uint32_t>> adj_;
  std::vector<std::vector<double>> w_;
};

#endif
