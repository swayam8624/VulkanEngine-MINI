#include "beacon/cluster_scan_reference.hpp"

#include <algorithm>
#include <limits>

namespace lve::beacon {

ClusterScanReference buildClusterScanReference(
    const std::vector<uint32_t>& candidateCounts,
    uint32_t maxLightsPerCluster,
    uint64_t lightIndexCapacity) {
  ClusterScanReference result{};
  result.offsets.resize(candidateCounts.size());
  result.storedCounts.resize(candidateCounts.size());
  result.overflow.resize(candidateCounts.size());
  uint64_t cursor = 0;
  for (size_t index = 0; index < candidateCounts.size(); ++index) {
    result.offsets[index] =
        static_cast<uint32_t>(std::min<uint64_t>(cursor, std::numeric_limits<uint32_t>::max()));
    uint32_t stored = std::min(candidateCounts[index], maxLightsPerCluster);
    bool capacityOverflow = cursor + stored > lightIndexCapacity;
    result.storedCounts[index] = capacityOverflow ? 0u : stored;
    result.overflow[index] =
        candidateCounts[index] > stored || capacityOverflow ? uint8_t{1} : uint8_t{0};
    cursor += stored;
  }
  result.requiredIndices = cursor;
  return result;
}

}  // namespace lve::beacon
