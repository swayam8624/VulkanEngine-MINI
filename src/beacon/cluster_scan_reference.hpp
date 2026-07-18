#pragma once

#include <cstdint>
#include <vector>

namespace lve::beacon {

struct ClusterScanReference {
  std::vector<uint32_t> offsets;
  std::vector<uint32_t> storedCounts;
  std::vector<uint8_t> overflow;
  uint64_t requiredIndices = 0;
};

ClusterScanReference buildClusterScanReference(
    const std::vector<uint32_t>& candidateCounts,
    uint32_t maxLightsPerCluster,
    uint64_t lightIndexCapacity);

}  // namespace lve::beacon
