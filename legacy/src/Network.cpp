#include "Network.h"
#include <random>
#include <unordered_set>
#include <algorithm>

void Network::buildWattsStrogatz(std::uint32_t N, std::uint32_t k, double p) {
  if (k % 2) ++k;
  adj_.assign(N, {});
  w_.assign(N, {});
  std::mt19937_64 rng(std::random_device{}());
  std::uniform_real_distribution<double> U(0.0, 1.0);
  std::uniform_int_distribution<std::uint32_t> Uni(0, N - 1);

  // ring lattice
  const std::uint32_t half = k / 2;
  for (std::uint32_t i = 0; i < N; ++i) {
    std::unordered_set<std::uint32_t> seen;
    for (std::uint32_t d = 1; d <= half; ++d) {
      std::uint32_t j = (i + d) % N;
      if (!seen.count(j)) {
        adj_[i].push_back(j);
        w_[i].push_back(0.5 + 0.3 * U(rng));
        seen.insert(j);
      }
    }
  }

  // rewiring
  for (std::uint32_t i = 0; i < N; ++i) {
    for (std::size_t e = 0; e < adj_[i].size(); ++e) {
      if (U(rng) < p) {
        std::uint32_t newT;
        do { newT = Uni(rng); } while (newT == i);
        adj_[i][e] = newT;
      }
    }
    // dedupe + remove self
    std::unordered_set<std::uint32_t> uniq;
    std::vector<std::uint32_t> nbrs;
    std::vector<double> weights;
    for (std::size_t e = 0; e < adj_[i].size(); ++e) {
      auto t = adj_[i][e];
      if (t == i) continue;
      if (uniq.insert(t).second) {
        nbrs.push_back(t);
        weights.push_back(w_[i][e]);
      }
    }
    adj_[i].swap(nbrs);
    w_[i].swap(weights);
  }
}
