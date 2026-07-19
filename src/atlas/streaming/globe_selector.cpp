#include "atlas/streaming/globe_selector.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>

namespace vulkax::atlas {
namespace {

GeodeticPosition directionToGeodetic(const glm::dvec3& direction) {
  const glm::dvec3 normalized = glm::normalize(direction);
  return {
      glm::degrees(
          std::atan2(
              normalized.z,
              std::hypot(normalized.x, normalized.y))),
      glm::degrees(std::atan2(normalized.y, normalized.x)),
      0.0,
  };
}

double tileAngularRadius(const AtlasTileKey& key) {
  const double dimension = static_cast<double>(1u << key.level);
  const glm::dvec2 centerUv{
      ((static_cast<double>(key.x) + 0.5) / dimension) * 2.0 - 1.0,
      ((static_cast<double>(key.y) + 0.5) / dimension) * 2.0 - 1.0,
  };
  const glm::dvec3 center = cubeToDirection(key.face, centerUv);
  double maximumAngle = 0.0;
  for (double y : {static_cast<double>(key.y),
                   static_cast<double>(key.y + 1)}) {
    for (double x : {static_cast<double>(key.x),
                     static_cast<double>(key.x + 1)}) {
      const glm::dvec2 uv{
          (x / dimension) * 2.0 - 1.0,
          (y / dimension) * 2.0 - 1.0,
      };
      maximumAngle = std::max(
          maximumAngle,
          std::acos(
              std::clamp(
                  glm::dot(center, cubeToDirection(key.face, uv)),
                  -1.0,
                  1.0)));
    }
  }
  return maximumAngle;
}

double routeDistanceMeters(
    const EcefPosition& center,
    const RoutePrediction* route) {
  if (route == nullptr || route->shape.empty()) {
    return std::numeric_limits<double>::infinity();
  }
  double distance = std::numeric_limits<double>::infinity();
  for (const auto& point : route->shape) {
    distance = std::min(
        distance,
        glm::length(geodeticToEcef(point).meters - center.meters));
  }
  return distance;
}

}  // namespace

GlobeTileSelector::GlobeTileSelector(GlobeSelectionConfig config)
    : config{config} {
  if (config.maximumLevel > 30 ||
      config.minimumLevel > config.maximumLevel ||
      config.targetScreenSpaceErrorPixels <= 0.0 ||
      config.rootGeometricErrorMeters <= 0.0 ||
      config.maximumSelectedTiles < 6) {
    throw std::invalid_argument("invalid globe tile selection configuration");
  }
}

AtlasTileCandidate GlobeTileSelector::evaluate(
    const AtlasTileKey& key,
    const AtlasCameraState& camera,
    const RoutePrediction* route,
    bool& horizonCulled,
    bool& frustumCulled,
    bool& routeBiased) const {
  const glm::dvec3 direction = tileCenterDirection(key);
  const EcefPosition center =
      geodeticToEcef(directionToGeodetic(direction));
  const double angularRadius = tileAngularRadius(key);
  const double boundingRadius =
      Wgs84::semiMajorAxisMeters * std::sin(angularRadius) * 1.1;
  const glm::dvec3 cameraToCenter =
      center.meters - camera.ecef.meters;
  const double distance = std::max(
      1.0, glm::length(cameraToCenter) - boundingRadius);

  horizonCulled =
      isBeyondEllipsoidHorizon(camera.ecef, center, boundingRadius);
  frustumCulled = false;
  if (!horizonCulled && glm::length(cameraToCenter) > 1e-6) {
    const glm::dvec3 forward =
        glm::normalize(camera.orientation * glm::dvec3{0.0, 0.0, 1.0});
    const double centerAngle = std::acos(
        std::clamp(
            glm::dot(forward, glm::normalize(cameraToCenter)),
            -1.0,
            1.0));
    const double sphereAngle =
        std::asin(
            std::clamp(
                boundingRadius /
                    std::max(glm::length(cameraToCenter), boundingRadius),
                0.0,
                1.0));
    const double horizontalFieldOfView =
        2.0 *
        std::atan(
            std::tan(camera.verticalFieldOfViewRadians * 0.5) *
            (static_cast<double>(camera.viewportWidth) /
             std::max(1u, camera.viewportHeight)));
    const double halfDiagonal =
        0.5 * std::hypot(
                  camera.verticalFieldOfViewRadians, horizontalFieldOfView);
    frustumCulled = centerAngle - sphereAngle > halfDiagonal;
  }

  const double geometricError =
      config.rootGeometricErrorMeters /
      static_cast<double>(uint64_t{1} << key.level);
  double sse = screenSpaceError(
      geometricError,
      distance,
      static_cast<double>(camera.viewportHeight),
      camera.verticalFieldOfViewRadians);
  const double routeDistance = routeDistanceMeters(center, route);
  routeBiased =
      route != nullptr && !route->deviated &&
      routeDistance <= route->corridorRadiusMeters + boundingRadius;
  if (routeBiased) {
    sse /= std::max(config.routeRefinementMultiplier, 0.05);
  }

  AtlasTileCandidate candidate{};
  candidate.key = key;
  candidate.center = center;
  candidate.boundingRadiusMeters = boundingRadius;
  candidate.screenSpaceErrorPixels = sse;
  candidate.semanticImportance = routeBiased ? 2.0 : 1.0;
  candidate.visibilityConfidence =
      horizonCulled || frustumCulled ? 0.0 : 1.0;
  candidate.qualityGain = sse;
  candidate.residentBytes = config.estimatedTileBytes;
  candidate.uploadBytes = config.estimatedTileBytes;
  candidate.visible = !horizonCulled && !frustumCulled;
  candidate.routeCriticalLabel = routeBiased;
  return candidate;
}

std::vector<AtlasTileCandidate> GlobeTileSelector::select(
    const AtlasCameraState& camera,
    const RoutePrediction* route) {
  selectionStats = {};
  struct Pending {
    AtlasTileCandidate candidate;
    double priority = 0.0;
    bool operator<(const Pending& other) const {
      return priority < other.priority;
    }
  };
  std::priority_queue<Pending> queue;
  for (CubeFace face :
       {CubeFace::PositiveX,
        CubeFace::NegativeX,
        CubeFace::PositiveY,
        CubeFace::NegativeY,
        CubeFace::PositiveZ,
        CubeFace::NegativeZ}) {
    bool horizon = false;
    bool frustum = false;
    bool routeBiased = false;
    auto candidate = evaluate(
        {face, 0, 0, 0, config.layer},
        camera,
        route,
        horizon,
        frustum,
        routeBiased);
    selectionStats.visitedTiles++;
    selectionStats.horizonCulledTiles += horizon ? 1u : 0u;
    selectionStats.frustumCulledTiles += frustum ? 1u : 0u;
    if (candidate.visible) {
      queue.push(
          {candidate,
           candidate.screenSpaceErrorPixels *
               candidate.semanticImportance});
    }
  }

  std::vector<AtlasTileCandidate> selected;
  selected.reserve(config.maximumSelectedTiles);
  while (!queue.empty() &&
         selected.size() < config.maximumSelectedTiles) {
    Pending pending = queue.top();
    queue.pop();
    const auto& candidate = pending.candidate;
    const bool refine =
        candidate.key.level < config.minimumLevel ||
        (candidate.key.level < config.maximumLevel &&
         candidate.screenSpaceErrorPixels >
             config.targetScreenSpaceErrorPixels);
    if (refine &&
        selected.size() + queue.size() + 4 <=
            config.maximumSelectedTiles) {
      selectionStats.refinedTiles++;
      for (const auto& child : candidate.key.children()) {
        bool horizon = false;
        bool frustum = false;
        bool routeBiased = false;
        auto childCandidate = evaluate(
            child, camera, route, horizon, frustum, routeBiased);
        selectionStats.visitedTiles++;
        selectionStats.horizonCulledTiles += horizon ? 1u : 0u;
        selectionStats.frustumCulledTiles += frustum ? 1u : 0u;
        selectionStats.routeBiasedTiles += routeBiased ? 1u : 0u;
        if (childCandidate.visible) {
          queue.push(
              {childCandidate,
               childCandidate.screenSpaceErrorPixels *
                   childCandidate.semanticImportance});
        }
      }
    } else {
      selectionStats.deepestSelectedLevel =
          std::max(
              selectionStats.deepestSelectedLevel,
              candidate.key.level);
      selected.push_back(candidate);
    }
  }
  return selected;
}

}  // namespace vulkax::atlas
