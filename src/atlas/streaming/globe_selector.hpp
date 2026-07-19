#pragma once

#include "atlas/research/route_predictive_scheduler.hpp"

#include <vector>

namespace vulkax::atlas {

struct GlobeSelectionConfig {
  AtlasLayer layer = AtlasLayer::Terrain;
  uint8_t minimumLevel = 0;
  uint8_t maximumLevel = 18;
  double targetScreenSpaceErrorPixels = 2.0;
  double rootGeometricErrorMeters = 4000000.0;
  uint64_t estimatedTileBytes = 256 * 1024;
  uint32_t maximumSelectedTiles = 512;
  double routeRefinementMultiplier = 0.45;
};

struct GlobeSelectionStats {
  uint32_t visitedTiles = 0;
  uint32_t horizonCulledTiles = 0;
  uint32_t frustumCulledTiles = 0;
  uint32_t refinedTiles = 0;
  uint32_t routeBiasedTiles = 0;
  uint8_t deepestSelectedLevel = 0;
};

class GlobeTileSelector {
 public:
  explicit GlobeTileSelector(GlobeSelectionConfig config = {});

  std::vector<AtlasTileCandidate> select(
      const AtlasCameraState& camera,
      const RoutePrediction* route = nullptr);

  const GlobeSelectionStats& stats() const { return selectionStats; }

 private:
  AtlasTileCandidate evaluate(
      const AtlasTileKey& key,
      const AtlasCameraState& camera,
      const RoutePrediction* route,
      bool& horizonCulled,
      bool& frustumCulled,
      bool& routeBiased) const;

  GlobeSelectionConfig config;
  GlobeSelectionStats selectionStats{};
};

}  // namespace vulkax::atlas
