#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lve::beacon {

enum class RenderTechnique {
  BaselineForward,
  SsboForward,
  SsboDiffuseReference,
  InstancedForward,
  CpuClusteredFixed,
  FixedClusterCostModel,
  GpuClusteredFixed,
  AdaptiveClusteredExact,
  AdaptiveClusteredBounded,
  BeaconFull
};

enum class ScenePreset {
  Tutorial,
  RepeatedGeometry,
  UniqueObjects,
  DepthHeavy,
  OcclusionHeavy
};

enum class LightDistribution {
  Tutorial,
  Uniform,
  SingleHotspot,
  MultiHotspot,
  DepthStacked,
  MovingSwarm,
  LargeRadiusAdversarial,
  CameraAttached
};

enum class GeoRenderPolicy {
  FixedLod1,
  DistanceLod,
  SemanticLod,
  GeoBeaconExact,
  GeoBeaconBounded
};

enum class GeoCacheMode { Cold, Warm };

enum class GeoCameraPath {
  OuterOrbit,
  StreetDrive,
  IntersectionDwell,
  LandmarkApproach,
  RapidTeleport
};

struct BenchmarkConfig {
  ScenePreset scene = ScenePreset::Tutorial;
  LightDistribution lightDistribution = LightDistribution::Tutorial;
  uint32_t objectCount = 3;
  uint32_t lightCount = 6;
  uint32_t frameCount = 0;
  uint32_t warmupFrames = 120;
  uint32_t randomSeed = 1337;
  uint32_t width = 800;
  uint32_t height = 600;
  RenderTechnique technique = RenderTechnique::BaselineForward;
  float clusterBuildBudgetMs = 0.7f;
  float lightingBudgetMs = 3.0f;
  float imageErrorBudget = 0.005f;
  bool benchmark = false;
  bool verbose = true;
  bool showLightBillboards = true;
  bool captureReference = false;
  bool listDevices = false;
  int32_t deviceIndex = -1;
  std::string deviceName;
  std::string deviceUuid;
  bool geoEnabled = false;
  bool atlasEnabled = false;
  std::filesystem::path atlasManifest;
  std::filesystem::path atlasPack;
  std::filesystem::path atlasNavigationReplay =
      "data/atlas/navigation_replay.json";
  GeoRenderPolicy geoPolicy = GeoRenderPolicy::GeoBeaconBounded;
  GeoCacheMode geoCacheMode = GeoCacheMode::Cold;
  GeoCameraPath geoCameraPath = GeoCameraPath::OuterOrbit;
  std::filesystem::path geoManifest = "data/connaught_place/generated/geobeacon.json";
  float geoTargetFrameMs = 16.67f;
  uint64_t geoMemoryBudgetMiB = 512;
  float geoUploadBudgetMiBPerSecond = 100.f;
  uint32_t geoMaxTileChangesPerFrame = 8;
  std::filesystem::path outputDirectory = "beacon_results";
};

BenchmarkConfig parseCommandLine(int argc, char** argv);
std::string toString(RenderTechnique technique);
std::string toString(ScenePreset scene);
std::string toString(LightDistribution distribution);
std::string toString(GeoRenderPolicy policy);
std::string toString(GeoCacheMode mode);
std::string toString(GeoCameraPath path);
std::vector<RenderTechnique> implementedTechniques();

}  // namespace lve::beacon
