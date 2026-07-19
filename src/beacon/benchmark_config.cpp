#include "beacon/benchmark_config.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string_view>

namespace lve::beacon {
namespace {

bool consumeValue(int& index, int argc, char** argv, std::string_view name, std::string& value) {
  std::string_view arg{argv[index]};
  std::string prefix = std::string{name} + "=";
  if (arg.rfind(prefix, 0) == 0) {
    value = std::string{arg.substr(prefix.size())};
    return true;
  }
  if (arg == name) {
    if (index + 1 >= argc) {
      throw std::runtime_error("missing value for " + std::string{name});
    }
    value = argv[++index];
    return true;
  }
  return false;
}

uint32_t parseU32(const std::string& value, std::string_view name) {
  char* end = nullptr;
  unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
  if (*end != '\0') {
    throw std::runtime_error("invalid integer for " + std::string{name} + ": " + value);
  }
  return static_cast<uint32_t>(parsed);
}

float parseFloat(const std::string& value, std::string_view name) {
  char* end = nullptr;
  float parsed = std::strtof(value.c_str(), &end);
  if (*end != '\0') {
    throw std::runtime_error("invalid float for " + std::string{name} + ": " + value);
  }
  return parsed;
}

bool parseBool(const std::string& value, std::string_view name) {
  if (value == "true" || value == "1" || value == "yes" || value == "on") return true;
  if (value == "false" || value == "0" || value == "no" || value == "off") return false;
  throw std::runtime_error("invalid boolean for " + std::string{name} + ": " + value);
}

RenderTechnique parseTechnique(const std::string& value) {
  if (value == "baseline") return RenderTechnique::BaselineForward;
  if (value == "ssbo") return RenderTechnique::SsboForward;
  if (value == "ssbo-diffuse") return RenderTechnique::SsboDiffuseReference;
  if (value == "instanced") return RenderTechnique::InstancedForward;
  if (value == "cpu-clustered") return RenderTechnique::CpuClusteredFixed;
  if (value == "fixed-cluster-cost-model") return RenderTechnique::FixedClusterCostModel;
  if (value == "gpu-clustered") return RenderTechnique::GpuClusteredFixed;
  if (value == "adaptive-exact") return RenderTechnique::AdaptiveClusteredExact;
  if (value == "adaptive-bounded") return RenderTechnique::AdaptiveClusteredBounded;
  if (value == "beacon") return RenderTechnique::BeaconFull;
  throw std::runtime_error("unknown render technique: " + value);
}

ScenePreset parseScene(const std::string& value) {
  if (value == "tutorial") return ScenePreset::Tutorial;
  if (value == "repeated") return ScenePreset::RepeatedGeometry;
  if (value == "unique") return ScenePreset::UniqueObjects;
  if (value == "depth-heavy") return ScenePreset::DepthHeavy;
  if (value == "occlusion-heavy") return ScenePreset::OcclusionHeavy;
  throw std::runtime_error("unknown scene preset: " + value);
}

LightDistribution parseDistribution(const std::string& value) {
  if (value == "tutorial") return LightDistribution::Tutorial;
  if (value == "uniform") return LightDistribution::Uniform;
  if (value == "single-hotspot") return LightDistribution::SingleHotspot;
  if (value == "multi-hotspot") return LightDistribution::MultiHotspot;
  if (value == "depth-stacked") return LightDistribution::DepthStacked;
  if (value == "moving-swarm") return LightDistribution::MovingSwarm;
  if (value == "adversarial") return LightDistribution::LargeRadiusAdversarial;
  if (value == "camera-attached") return LightDistribution::CameraAttached;
  throw std::runtime_error("unknown light distribution: " + value);
}

GeoRenderPolicy parseGeoPolicy(const std::string& value) {
  if (value == "fixed-lod1") return GeoRenderPolicy::FixedLod1;
  if (value == "distance-lod") return GeoRenderPolicy::DistanceLod;
  if (value == "semantic-lod") return GeoRenderPolicy::SemanticLod;
  if (value == "geo-beacon-exact") return GeoRenderPolicy::GeoBeaconExact;
  if (value == "geo-beacon-bounded") return GeoRenderPolicy::GeoBeaconBounded;
  throw std::runtime_error("unknown GeoBEACON policy: " + value);
}

GeoCacheMode parseGeoCacheMode(const std::string& value) {
  if (value == "cold") return GeoCacheMode::Cold;
  if (value == "warm") return GeoCacheMode::Warm;
  throw std::runtime_error("unknown GeoBEACON cache mode: " + value);
}

GeoCameraPath parseGeoCameraPath(const std::string& value) {
  if (value == "outer-orbit") return GeoCameraPath::OuterOrbit;
  if (value == "street-drive") return GeoCameraPath::StreetDrive;
  if (value == "intersection-dwell") return GeoCameraPath::IntersectionDwell;
  if (value == "landmark-approach") return GeoCameraPath::LandmarkApproach;
  if (value == "rapid-teleport") return GeoCameraPath::RapidTeleport;
  throw std::runtime_error("unknown GeoBEACON camera path: " + value);
}

}  // namespace

BenchmarkConfig parseCommandLine(int argc, char** argv) {
  BenchmarkConfig config{};
  for (int i = 1; i < argc; ++i) {
    std::string value;
    std::string_view arg{argv[i]};
    if (arg == "--benchmark") {
      config.benchmark = true;
    } else if (arg == "--atlas") {
      config.atlasEnabled = true;
    } else if (arg == "--geo") {
      config.geoEnabled = true;
    } else if (arg == "--list-devices") {
      config.listDevices = true;
    } else if (arg == "--quiet") {
      config.verbose = false;
    } else if (consumeValue(i, argc, argv, "--technique", value)) {
      config.technique = parseTechnique(value);
    } else if (consumeValue(i, argc, argv, "--scene", value)) {
      config.scene = parseScene(value);
    } else if (consumeValue(i, argc, argv, "--light-distribution", value)) {
      config.lightDistribution = parseDistribution(value);
    } else if (consumeValue(i, argc, argv, "--objects", value)) {
      config.objectCount = parseU32(value, "--objects");
    } else if (consumeValue(i, argc, argv, "--lights", value)) {
      config.lightCount = parseU32(value, "--lights");
    } else if (consumeValue(i, argc, argv, "--frames", value)) {
      config.frameCount = parseU32(value, "--frames");
      config.benchmark = true;
    } else if (consumeValue(i, argc, argv, "--warmup-frames", value)) {
      config.warmupFrames = parseU32(value, "--warmup-frames");
    } else if (consumeValue(i, argc, argv, "--seed", value)) {
      config.randomSeed = parseU32(value, "--seed");
    } else if (consumeValue(i, argc, argv, "--width", value)) {
      config.width = parseU32(value, "--width");
    } else if (consumeValue(i, argc, argv, "--height", value)) {
      config.height = parseU32(value, "--height");
    } else if (consumeValue(i, argc, argv, "--cluster-build-budget-ms", value)) {
      config.clusterBuildBudgetMs = parseFloat(value, "--cluster-build-budget-ms");
    } else if (consumeValue(i, argc, argv, "--lighting-budget-ms", value)) {
      config.lightingBudgetMs = parseFloat(value, "--lighting-budget-ms");
    } else if (consumeValue(i, argc, argv, "--quality-error", value)) {
      config.imageErrorBudget = parseFloat(value, "--quality-error");
    } else if (consumeValue(i, argc, argv, "--show-light-billboards", value)) {
      config.showLightBillboards = parseBool(value, "--show-light-billboards");
    } else if (consumeValue(i, argc, argv, "--capture-reference", value)) {
      config.captureReference = parseBool(value, "--capture-reference");
    } else if (consumeValue(i, argc, argv, "--device-index", value)) {
      config.deviceIndex = static_cast<int32_t>(parseU32(value, "--device-index"));
    } else if (consumeValue(i, argc, argv, "--device-name", value)) {
      config.deviceName = value;
    } else if (consumeValue(i, argc, argv, "--device-uuid", value)) {
      config.deviceUuid = value;
    } else if (consumeValue(i, argc, argv, "--geo-manifest", value)) {
      config.geoManifest = value;
      config.geoEnabled = true;
    } else if (consumeValue(i, argc, argv, "--atlas-manifest", value)) {
      config.atlasManifest = value;
      config.atlasEnabled = true;
    } else if (consumeValue(i, argc, argv, "--atlas-pack", value)) {
      config.atlasPack = value;
      config.atlasEnabled = true;
    } else if (consumeValue(i, argc, argv, "--atlas-navigation-replay", value)) {
      config.atlasNavigationReplay = value;
      config.atlasEnabled = true;
    } else if (consumeValue(i, argc, argv, "--geo-policy", value)) {
      config.geoPolicy = parseGeoPolicy(value);
      config.geoEnabled = true;
    } else if (consumeValue(i, argc, argv, "--geo-cache-mode", value)) {
      config.geoCacheMode = parseGeoCacheMode(value);
    } else if (consumeValue(i, argc, argv, "--geo-camera-path", value)) {
      config.geoCameraPath = parseGeoCameraPath(value);
    } else if (consumeValue(i, argc, argv, "--geo-budget-frame-ms", value)) {
      config.geoTargetFrameMs = parseFloat(value, "--geo-budget-frame-ms");
    } else if (consumeValue(i, argc, argv, "--geo-budget-memory-mib", value)) {
      config.geoMemoryBudgetMiB = parseU32(value, "--geo-budget-memory-mib");
    } else if (consumeValue(i, argc, argv, "--geo-budget-upload-mibps", value)) {
      config.geoUploadBudgetMiBPerSecond = parseFloat(value, "--geo-budget-upload-mibps");
    } else if (consumeValue(i, argc, argv, "--geo-max-tile-changes", value)) {
      config.geoMaxTileChangesPerFrame = parseU32(value, "--geo-max-tile-changes");
    } else if (consumeValue(i, argc, argv, "--output", value)) {
      config.outputDirectory = value;
    } else {
      throw std::runtime_error("unknown argument: " + std::string{arg});
    }
  }
  return config;
}

std::string toString(RenderTechnique technique) {
  switch (technique) {
    case RenderTechnique::BaselineForward: return "baseline";
    case RenderTechnique::SsboForward: return "ssbo";
    case RenderTechnique::SsboDiffuseReference: return "ssbo-diffuse";
    case RenderTechnique::InstancedForward: return "instanced";
    case RenderTechnique::CpuClusteredFixed: return "cpu-clustered";
    case RenderTechnique::FixedClusterCostModel: return "fixed-cluster-cost-model";
    case RenderTechnique::GpuClusteredFixed: return "gpu-clustered";
    case RenderTechnique::AdaptiveClusteredExact: return "adaptive-exact";
    case RenderTechnique::AdaptiveClusteredBounded: return "adaptive-bounded";
    case RenderTechnique::BeaconFull: return "beacon";
  }
  return "unknown";
}

std::string toString(ScenePreset scene) {
  switch (scene) {
    case ScenePreset::Tutorial: return "tutorial";
    case ScenePreset::RepeatedGeometry: return "repeated";
    case ScenePreset::UniqueObjects: return "unique";
    case ScenePreset::DepthHeavy: return "depth-heavy";
    case ScenePreset::OcclusionHeavy: return "occlusion-heavy";
  }
  return "unknown";
}

std::string toString(LightDistribution distribution) {
  switch (distribution) {
    case LightDistribution::Tutorial: return "tutorial";
    case LightDistribution::Uniform: return "uniform";
    case LightDistribution::SingleHotspot: return "single-hotspot";
    case LightDistribution::MultiHotspot: return "multi-hotspot";
    case LightDistribution::DepthStacked: return "depth-stacked";
    case LightDistribution::MovingSwarm: return "moving-swarm";
    case LightDistribution::LargeRadiusAdversarial: return "adversarial";
    case LightDistribution::CameraAttached: return "camera-attached";
  }
  return "unknown";
}

std::string toString(GeoRenderPolicy policy) {
  switch (policy) {
    case GeoRenderPolicy::FixedLod1: return "fixed-lod1";
    case GeoRenderPolicy::DistanceLod: return "distance-lod";
    case GeoRenderPolicy::SemanticLod: return "semantic-lod";
    case GeoRenderPolicy::GeoBeaconExact: return "geo-beacon-exact";
    case GeoRenderPolicy::GeoBeaconBounded: return "geo-beacon-bounded";
  }
  return "unknown";
}

std::string toString(GeoCacheMode mode) {
  return mode == GeoCacheMode::Warm ? "warm" : "cold";
}

std::string toString(GeoCameraPath path) {
  switch (path) {
    case GeoCameraPath::OuterOrbit: return "outer-orbit";
    case GeoCameraPath::StreetDrive: return "street-drive";
    case GeoCameraPath::IntersectionDwell: return "intersection-dwell";
    case GeoCameraPath::LandmarkApproach: return "landmark-approach";
    case GeoCameraPath::RapidTeleport: return "rapid-teleport";
  }
  return "unknown";
}

std::vector<RenderTechnique> implementedTechniques() {
  return {
      RenderTechnique::BaselineForward,
      RenderTechnique::SsboForward,
      RenderTechnique::SsboDiffuseReference,
      RenderTechnique::InstancedForward,
      RenderTechnique::CpuClusteredFixed,
      RenderTechnique::FixedClusterCostModel,
      RenderTechnique::GpuClusteredFixed,
      RenderTechnique::AdaptiveClusteredExact,
      RenderTechnique::AdaptiveClusteredBounded,
      RenderTechnique::BeaconFull};
}

}  // namespace lve::beacon
