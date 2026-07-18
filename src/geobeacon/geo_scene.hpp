#pragma once

#include "beacon/benchmark_config.hpp"
#include "lve_device.hpp"
#include "lve_model.hpp"

#include <glm/glm.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lve::geo {

enum class TileResidency : uint8_t {
  Unloaded,
  Queued,
  LoadingCpu,
  AwaitingUpload,
  Resident,
  EvictPending,
  Failed
};

enum class TileRepresentation : uint8_t { Lod0Proxy, Lod1Semantic, Lod2Detailed };

enum class SemanticClass : uint8_t {
  Ground,
  Building,
  Landmark,
  PrimaryRoad,
  Intersection,
  SecondaryRoad,
  Pedestrian,
  Vegetation,
  Water
};

struct Aabb3d {
  glm::dvec3 min{};
  glm::dvec3 max{};
};

struct GeoRepresentationInfo {
  TileRepresentation representation{};
  std::filesystem::path uri;
  uint64_t bytes = 0;
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;
  float geometricError = 0.f;
};

struct GeoTileRecord {
  uint64_t tileId = 0;
  int32_t parentIndex = -1;
  std::array<int32_t, 4> childIndices{-1, -1, -1, -1};
  Aabb3d bounds{};
  std::array<GeoRepresentationInfo, 3> representations{};
  float semanticImportance = 1.f;
};

struct GeoDatasetManifest {
  glm::dvec3 geodeticOrigin{};
  Aabb3d localBounds{};
  std::vector<GeoTileRecord> tiles;
  std::string sourceAttribution;
  std::string copyrightUrl;
  std::string sourceChecksum;
};

struct GeoBudgetConfig {
  float targetFrameMs = 16.67f;
  uint64_t gpuMemoryBudgetBytes = 512ull * 1024ull * 1024ull;
  float uploadBudgetMiBPerSecond = 100.f;
  float lightingErrorBudget = 0.005f;
  uint32_t maxTileChangesPerFrame = 8;
};

struct GeoFrameStats {
  uint32_t visibleTiles = 0;
  uint32_t residentTiles = 0;
  uint32_t requestedTiles = 0;
  uint32_t upgradedTiles = 0;
  uint32_t downgradedTiles = 0;
  uint32_t failedTiles = 0;
  uint32_t representationChurn = 0;
  uint64_t residentBytes = 0;
  uint64_t uploadedBytes = 0;
  double streamingLatencyP95Ms = 0.0;
  float semanticUtility = 0.f;
  float semanticWeightedError = 0.f;
  float budgetViolation = 0.f;
};

struct GeoDrawItem {
  LveModel* model = nullptr;
  glm::mat4 transform{1.f};
  uint64_t tileId = 0;
  TileRepresentation representation = TileRepresentation::Lod0Proxy;
};

class GeoScene {
 public:
  GeoScene(
      LveDevice& device,
      std::filesystem::path manifestPath,
      beacon::GeoRenderPolicy policy,
      beacon::GeoCacheMode cacheMode,
      GeoBudgetConfig budget,
      bool forceMaximumLod = false);
  ~GeoScene();

  GeoScene(const GeoScene&) = delete;
  GeoScene& operator=(const GeoScene&) = delete;

  void update(
      const glm::vec3& cameraPosition,
      const glm::mat4& viewProjection,
      float frameTime,
      uint64_t frameNumber,
      double measuredFrameMs);
  std::vector<GeoDrawItem> drawItems() const;
  const GeoDatasetManifest& manifest() const { return dataset; }
  const GeoFrameStats& stats() const { return frameStats; }
  const GeoBudgetConfig& budgetConfig() const { return budget; }
  beacon::GeoRenderPolicy renderPolicy() const { return policy; }
  void setPolicy(beacon::GeoRenderPolicy value) { policy = value; }

 private:
  struct TileRuntime {
    TileResidency residency = TileResidency::Unloaded;
    uint32_t residentLod = 0;
    uint32_t requestedLod = 0;
    uint64_t generation = 0;
    uint64_t lastVisibleFrame = 0;
    uint64_t lastUsedFrame = 0;
    uint32_t lifetimeTicks = 0;
    bool isVisible = false;
    std::unique_ptr<LveModel> model;
  };

  struct LoadRequest {
    uint32_t tileIndex = 0;
    uint32_t lod = 0;
    uint64_t generation = 0;
    std::chrono::steady_clock::time_point queuedAt{};
  };

  struct CompletedLoad {
    uint32_t tileIndex = 0;
    uint32_t lod = 0;
    uint64_t generation = 0;
    std::chrono::steady_clock::time_point queuedAt{};
    std::unique_ptr<LveModel::Builder> builder;
    std::string error;
  };

  struct RetiredModel {
    uint64_t retireAfterFrame = 0;
    std::unique_ptr<LveModel> model;
  };

  static GeoDatasetManifest loadManifest(const std::filesystem::path& path);
  static LveModel::Builder loadGlb(const std::filesystem::path& path);
  void workerMain();
  void queueLoad(uint32_t tileIndex, uint32_t lod, uint64_t frameNumber);
  void integrateUploads(float frameTime, uint64_t frameNumber);
  void schedule(const glm::vec3& cameraPosition, const glm::mat4& viewProjection, uint64_t frameNumber);
  void enforceMemoryBudget(uint64_t frameNumber);
  bool visible(const GeoTileRecord& tile, const glm::mat4& viewProjection) const;
  double distanceTo(const GeoTileRecord& tile, const glm::vec3& position) const;
  uint32_t desiredLod(const GeoTileRecord& tile, double distance) const;

  LveDevice& device;
  std::filesystem::path manifestPath;
  std::filesystem::path baseDirectory;
  GeoDatasetManifest dataset;
  beacon::GeoRenderPolicy policy;
  beacon::GeoCacheMode cacheMode;
  GeoBudgetConfig budget;
  bool forceMaximumLod = false;
  GeoFrameStats frameStats{};
  std::vector<std::unique_ptr<TileRuntime>> runtime;
  std::vector<RetiredModel> retired;
  std::deque<LoadRequest> requests;
  std::deque<CompletedLoad> completed;
  std::vector<std::thread> workers;
  mutable std::mutex mutex;
  std::condition_variable condition;
  std::atomic<bool> stopping{false};
  double uploadTokens = 0.0;
  double frameTimeEwmaMs = 0.0;
  double controllerAccumulator = 0.0;
};

}  // namespace lve::geo
