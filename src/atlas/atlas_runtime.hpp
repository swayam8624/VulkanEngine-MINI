#pragma once

#include "atlas/core/dataset.hpp"
#include "atlas/research/route_predictive_scheduler.hpp"
#include "atlas/streaming/tile_cache.hpp"
#include "atlas/streaming/tile_source.hpp"

#include <deque>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vulkax::atlas {

struct AtlasRuntimeStats {
  AtlasFrameStats scheduler{};
  uint32_t pendingRequests = 0;
  uint32_t completedRequests = 0;
  uint32_t failedRequests = 0;
  uint32_t cancelledRequests = 0;
  uint32_t cacheHits = 0;
  uint32_t cacheMisses = 0;
  uint64_t downloadedBytes = 0;
};

class AtlasRuntime {
 public:
  AtlasRuntime(
      AtlasDatasetManifest dataset,
      std::shared_ptr<TileSource> source,
      std::shared_ptr<TileCache> cache,
      AtlasBudgetConfig budget = {});
  ~AtlasRuntime();

  AtlasRuntime(const AtlasRuntime&) = delete;
  AtlasRuntime& operator=(const AtlasRuntime&) = delete;

  void update(
      const AtlasFrameContext& frame,
      const std::vector<AtlasTileCandidate>& candidates,
      const RoutePrediction* route);

  std::vector<TilePayload> takeReadyTiles();
  void cancelAll();

  const AtlasDatasetManifest& manifest() const { return dataset; }
  const AtlasRuntimeStats& stats() const { return runtimeStats; }
  RoutePredictiveScheduler& scheduler() { return tileScheduler; }

 private:
  struct PendingRequest {
    TileRequest request;
    std::shared_ptr<CancellationToken> cancellation;
    std::future<TilePayload> future;
    uint64_t lastWantedFrame = 0;
  };

  std::string contentUri(const AtlasTileKey& key) const;
  void requestTile(
      const AtlasTileDecision& decision,
      uint64_t frameNumber);
  void integrateCompleted(uint64_t frameNumber);
  void cancelStale(uint64_t frameNumber);

  AtlasDatasetManifest dataset;
  std::shared_ptr<TileSource> source;
  std::shared_ptr<TileCache> cache;
  RoutePredictiveScheduler tileScheduler;
  AtlasRuntimeStats runtimeStats{};
  std::unordered_map<std::string, PendingRequest> pending;
  std::unordered_map<std::string, uint32_t> failureCounts;
  std::unordered_map<std::string, uint64_t> retryAfterFrame;
  std::deque<TilePayload> ready;
};

}  // namespace vulkax::atlas
