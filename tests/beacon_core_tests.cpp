#include "beacon/beacon_research.hpp"
#include "beacon/cluster_scan_reference.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
  using namespace lve::beacon;

  auto scan = buildClusterScanReference({3, 0, 5}, 4, 7);
  assert((scan.offsets == std::vector<uint32_t>{0, 3, 3}));
  assert((scan.storedCounts == std::vector<uint32_t>{3, 0, 4}));
  assert((scan.overflow == std::vector<uint8_t>{0, 0, 1}));
  assert(scan.requiredIndices == 7);

  auto capacityOverflow = buildClusterScanReference({4, 4}, 4, 6);
  assert(capacityOverflow.storedCounts[1] == 0);
  assert(capacityOverflow.overflow[1] == 1);

  LightBoundEstimator estimator;
  GpuPointLight light{};
  light.positionRadius = {0.f, 0.f, 10.f, 2.f};
  light.colorIntensity = {1.f, 1.f, 1.f, 4.f};
  float nearBound = estimator.diffuseUpperBound(light, {-1.f, -1.f, 8.f}, {1.f, 1.f, 9.f});
  float farBound = estimator.diffuseUpperBound(light, {-1.f, -1.f, 0.f}, {1.f, 1.f, 1.f});
  assert(nearBound > farBound);

  ClusterEncodingSelector selector;
  assert(selector.choose(8, 1024) == ClusterEncoding::ExplicitList);
  assert(selector.choose(512, 1024) == ClusterEncoding::DenseBitset);
  assert(selector.estimateBytes(512, 1024, ClusterEncoding::DenseBitset) == 128);

  BudgetController controller{0.5f, 2.f, 0.005f};
  RenderStats slow{};
  slow.gpu.clusterBuildMs = 2.f;
  slow.gpu.lightingPassMs = 1.f;
  for (int i = 0; i < 12; ++i) controller.update(slow);
  assert(controller.maxSubdivisionDepth() == 1);
  assert(controller.splitPenalty() > 1.f);

  std::cout << "BEACON core tests passed\n";
  return 0;
}
