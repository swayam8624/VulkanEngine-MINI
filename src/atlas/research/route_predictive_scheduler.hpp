#pragma once

#include "atlas/core/geodesy.hpp"
#include "atlas/core/tile_key.hpp"
#include "atlas/navigation/navigation.hpp"

#include <cstdint>
#include <vector>

#include <glm/gtc/quaternion.hpp>

namespace vulkax::atlas {

struct AtlasBudgetConfig {
  double targetFrameMilliseconds = 16.67;
  uint64_t residentMemoryBytes = 512ull * 1024ull * 1024ull;
  uint64_t uploadBytesPerSecond = 100ull * 1024ull * 1024ull;
  uint64_t speculativeWasteBytes = 32ull * 1024ull * 1024ull;
  double diffuseErrorBudget = 0.005;
  uint32_t maximumTileChangesPerFrame = 8;
};

struct AtlasFrameContext {
  EcefPosition cameraPosition{};
  glm::dvec3 cameraVelocityMetersPerSecond{};
  uint64_t frameNumber = 0;
  double deltaSeconds = 0.0;
  double measuredFrameMilliseconds = 0.0;
};

struct AtlasCameraState {
  GeodeticPosition geodetic{};
  EcefPosition ecef{};
  glm::dquat orientation{};
  double verticalFieldOfViewRadians = 0.8726646259971648;
  uint32_t viewportWidth = 1920;
  uint32_t viewportHeight = 1080;
};

struct AtlasTileCandidate {
  AtlasTileKey key{};
  EcefPosition center{};
  double boundingRadiusMeters = 0.0;
  double screenSpaceErrorPixels = 0.0;
  double semanticImportance = 1.0;
  double visibilityConfidence = 0.0;
  double qualityGain = 0.0;
  double gpuCostMilliseconds = 0.0;
  double lightingCostMilliseconds = 0.0;
  uint64_t residentBytes = 0;
  uint64_t uploadBytes = 0;
  bool visible = false;
  bool resident = false;
  bool routeCriticalLabel = false;
};

struct AtlasTileDecision {
  AtlasTileKey key{};
  double priority = 0.0;
  double routeProbability = 0.0;
  double secondsUntilNeeded = 0.0;
  bool speculative = false;
};

struct AtlasFrameStats {
  uint32_t visibleTiles = 0;
  uint32_t selectedTiles = 0;
  uint32_t routeCriticalTiles = 0;
  uint32_t deadlineMisses = 0;
  uint64_t residentBytes = 0;
  uint64_t requestedUploadBytes = 0;
  uint64_t speculativeBytes = 0;
  uint64_t wastedPrefetchBytes = 0;
  double routeCorridorQuality = 0.0;
  double semanticUtility = 0.0;
  double frameTimeEwmaMilliseconds = 0.0;
  double budgetViolation = 0.0;
};

struct RoutePrediction {
  std::vector<GeodeticPosition> shape;
  double corridorRadiusMeters = 500.0;
  double lookAheadSeconds = 30.0;
  double expectedSpeedMetersPerSecond = 13.9;
  bool deviated = false;
};

class RoutePredictiveScheduler {
 public:
  explicit RoutePredictiveScheduler(AtlasBudgetConfig budget = {});

  std::vector<AtlasTileDecision> select(
      const AtlasFrameContext& frame,
      const std::vector<AtlasTileCandidate>& candidates,
      const RoutePrediction* route);

  const AtlasFrameStats& stats() const { return frameStats; }
  const AtlasBudgetConfig& budgetConfig() const { return budget; }
  void setBudget(AtlasBudgetConfig value) { budget = value; }

 private:
  struct RouteScore {
    double probability = 0.0;
    double secondsUntilNeeded = 0.0;
  };

  RouteScore scoreRoute(
      const AtlasTileCandidate& candidate,
      const RoutePrediction* route) const;

  AtlasBudgetConfig budget;
  AtlasFrameStats frameStats{};
  double frameTimeEwmaMilliseconds = 0.0;
};

}  // namespace vulkax::atlas
