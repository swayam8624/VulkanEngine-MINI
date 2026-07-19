#include "atlas/research/route_predictive_scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vulkax::atlas {
namespace {

double pointSegmentDistance(
    const glm::dvec3& point,
    const glm::dvec3& start,
    const glm::dvec3& end) {
  const glm::dvec3 segment = end - start;
  const double lengthSquared = glm::dot(segment, segment);
  if (lengthSquared <= 1e-9) return glm::length(point - start);
  const double amount =
      std::clamp(glm::dot(point - start, segment) / lengthSquared, 0.0, 1.0);
  return glm::length(point - (start + segment * amount));
}

}  // namespace

RoutePredictiveScheduler::RoutePredictiveScheduler(AtlasBudgetConfig budget)
    : budget{budget} {}

RoutePredictiveScheduler::RouteScore RoutePredictiveScheduler::scoreRoute(
    const AtlasTileCandidate& candidate,
    const RoutePrediction* route) const {
  if (route == nullptr || route->shape.size() < 2 || route->deviated) return {};

  std::vector<EcefPosition> routePoints;
  routePoints.reserve(route->shape.size());
  for (const auto& position : route->shape) {
    routePoints.push_back(geodeticToEcef(position));
  }

  double nearestDistance = std::numeric_limits<double>::max();
  double distanceAlongRoute = 0.0;
  double nearestDistanceAlongRoute = 0.0;
  for (size_t index = 1; index < routePoints.size(); ++index) {
    const double segmentDistance = pointSegmentDistance(
        candidate.center.meters,
        routePoints[index - 1].meters,
        routePoints[index].meters);
    const double segmentLength =
        glm::length(routePoints[index].meters - routePoints[index - 1].meters);
    if (segmentDistance < nearestDistance) {
      nearestDistance = segmentDistance;
      nearestDistanceAlongRoute = distanceAlongRoute + segmentLength * 0.5;
    }
    distanceAlongRoute += segmentLength;
  }

  const double effectiveDistance =
      std::max(0.0, nearestDistance - candidate.boundingRadiusMeters);
  const double radius = std::max(route->corridorRadiusMeters, 1.0);
  const double corridorProbability =
      std::exp(-(effectiveDistance * effectiveDistance) /
               (2.0 * radius * radius));
  const double secondsUntilNeeded =
      nearestDistanceAlongRoute /
      std::max(route->expectedSpeedMetersPerSecond, 0.1);
  const double timeProbability =
      std::clamp(
          1.0 - secondsUntilNeeded / std::max(route->lookAheadSeconds, 0.1),
          0.0,
          1.0);
  return {
      corridorProbability * (0.35 + 0.65 * timeProbability),
      secondsUntilNeeded,
  };
}

std::vector<AtlasTileDecision> RoutePredictiveScheduler::select(
    const AtlasFrameContext& frame,
    const std::vector<AtlasTileCandidate>& candidates,
    const RoutePrediction* route) {
  frameStats = {};
  if (frame.measuredFrameMilliseconds > 0.0) {
    frameTimeEwmaMilliseconds =
        frameTimeEwmaMilliseconds == 0.0
            ? frame.measuredFrameMilliseconds
            : frameTimeEwmaMilliseconds * 0.9 +
                  frame.measuredFrameMilliseconds * 0.1;
  }
  frameStats.frameTimeEwmaMilliseconds = frameTimeEwmaMilliseconds;

  struct Ranked {
    AtlasTileDecision decision;
    const AtlasTileCandidate* candidate = nullptr;
    double utility = 0.0;
  };
  std::vector<Ranked> ranked;
  ranked.reserve(candidates.size());

  for (const auto& candidate : candidates) {
    if (candidate.visible) frameStats.visibleTiles++;
    if (candidate.resident) frameStats.residentBytes += candidate.residentBytes;
    const RouteScore routeScore = scoreRoute(candidate, route);
    const bool routeCritical =
        routeScore.probability >= 0.6 || candidate.routeCriticalLabel;
    if (routeCritical) frameStats.routeCriticalTiles++;

    const double visualImportance =
        std::max(0.0, candidate.semanticImportance) *
        std::max(0.0, candidate.qualityGain) *
        (0.25 + std::max(0.0, candidate.visibilityConfidence));
    const double routeImportance =
        routeScore.probability * (routeCritical ? 3.0 : 1.5);
    const double deadlineWeight =
        routeScore.secondsUntilNeeded <= 1.0
            ? 3.0
            : 1.0 + 1.0 / routeScore.secondsUntilNeeded;
    const double utility =
        visualImportance + routeImportance * deadlineWeight +
        std::max(0.0, candidate.screenSpaceErrorPixels) * 0.02;
    const double cost =
        1.0 +
        static_cast<double>(candidate.uploadBytes) / (1024.0 * 1024.0) +
        candidate.gpuCostMilliseconds * 8.0 +
        candidate.lightingCostMilliseconds * 8.0;
    const bool speculative = !candidate.visible && routeScore.probability > 0.05;
    ranked.push_back(
        {
            {candidate.key,
             utility / cost,
             routeScore.probability,
             routeScore.secondsUntilNeeded,
             speculative},
            &candidate,
            utility,
        });
  }

  std::stable_sort(
      ranked.begin(),
      ranked.end(),
      [](const Ranked& left, const Ranked& right) {
        if (left.decision.priority != right.decision.priority) {
          return left.decision.priority > right.decision.priority;
        }
        return left.decision.key < right.decision.key;
      });

  const double framePressure =
      frameTimeEwmaMilliseconds > 0.0
          ? frameTimeEwmaMilliseconds /
                std::max(budget.targetFrameMilliseconds, 0.1)
          : 0.0;
  const uint32_t changeLimit =
      framePressure > 1.05
          ? std::max(1u, budget.maximumTileChangesPerFrame / 2)
          : budget.maximumTileChangesPerFrame;
  const uint64_t frameUploadBudget = static_cast<uint64_t>(
      static_cast<double>(budget.uploadBytesPerSecond) *
      std::max(frame.deltaSeconds, 1.0 / 120.0));

  std::vector<AtlasTileDecision> selected;
  selected.reserve(changeLimit);
  for (const auto& entry : ranked) {
    if (selected.size() >= changeLimit || entry.decision.priority <= 0.0) break;
    const auto& candidate = *entry.candidate;
    if (candidate.resident) continue;
    if (frameStats.residentBytes + candidate.residentBytes >
        budget.residentMemoryBytes) {
      continue;
    }
    if (frameStats.requestedUploadBytes + candidate.uploadBytes >
        frameUploadBudget) {
      if (entry.decision.secondsUntilNeeded <= frame.deltaSeconds) {
        frameStats.deadlineMisses++;
      }
      continue;
    }
    if (entry.decision.speculative &&
        frameStats.speculativeBytes + candidate.uploadBytes >
            budget.speculativeWasteBytes) {
      continue;
    }

    selected.push_back(entry.decision);
    frameStats.selectedTiles++;
    frameStats.residentBytes += candidate.residentBytes;
    frameStats.requestedUploadBytes += candidate.uploadBytes;
    frameStats.semanticUtility += entry.utility;
    if (entry.decision.speculative) {
      frameStats.speculativeBytes += candidate.uploadBytes;
    }
    frameStats.routeCorridorQuality +=
        entry.decision.routeProbability * candidate.qualityGain;
  }

  const double memoryViolation =
      frameStats.residentBytes > budget.residentMemoryBytes ? 1.0 : 0.0;
  const double timeViolation = framePressure > 1.0 ? framePressure - 1.0 : 0.0;
  frameStats.budgetViolation = std::max(memoryViolation, timeViolation);
  return selected;
}

}  // namespace vulkax::atlas
