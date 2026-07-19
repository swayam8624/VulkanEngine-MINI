#include "atlas/core/geodesy.hpp"
#include "atlas/core/tile_key.hpp"
#include "atlas/research/route_predictive_scheduler.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace vulkax::atlas;

struct Options {
  std::filesystem::path output = "atlas_results";
  uint32_t frames = 600;
  uint32_t seed = 1337;
  std::optional<AtlasControlPolicy> policy;
};

Options parseOptions(int argc, char** argv) {
  Options options{};
  for (int index = 1; index < argc; ++index) {
    const std::string argument{argv[index]};
    auto value = [&]() -> std::string {
      if (index + 1 >= argc) {
        throw std::invalid_argument("missing value for " + argument);
      }
      return argv[++index];
    };
    if (argument == "--output") {
      options.output = value();
    } else if (argument == "--frames") {
      options.frames = static_cast<uint32_t>(std::stoul(value()));
    } else if (argument == "--seed") {
      options.seed = static_cast<uint32_t>(std::stoul(value()));
    } else if (argument == "--policy") {
      options.policy = parseAtlasControlPolicy(value());
    } else if (argument == "--help") {
      std::cout
          << "atlas-benchmark [--output PATH] [--frames N] [--seed N] "
             "[--policy POLICY]\n";
      std::exit(0);
    } else {
      throw std::invalid_argument("unknown argument: " + argument);
    }
  }
  if (options.frames < 2) throw std::invalid_argument("frames must be at least two");
  return options;
}

GeodeticPosition interpolate(
    const GeodeticPosition& from,
    const GeodeticPosition& to,
    double amount) {
  return {
      glm::mix(from.latitudeDegrees, to.latitudeDegrees, amount),
      glm::mix(from.longitudeDegrees, to.longitudeDegrees, amount),
      glm::mix(from.altitudeMeters, to.altitudeMeters, amount),
  };
}

std::vector<GeodeticPosition> makeRoute(uint32_t samples) {
  const GeodeticPosition start{28.6315, 77.2167, 215.0};
  const GeodeticPosition middle{28.6230, 77.2200, 215.0};
  const GeodeticPosition end{28.6129, 77.2295, 216.0};
  std::vector<GeodeticPosition> route;
  route.reserve(samples);
  for (uint32_t index = 0; index < samples; ++index) {
    const double amount =
        static_cast<double>(index) / static_cast<double>(samples - 1);
    route.push_back(
        amount < 0.5
            ? interpolate(start, middle, amount * 2.0)
            : interpolate(middle, end, (amount - 0.5) * 2.0));
  }
  return route;
}

std::vector<AtlasTileCandidate> makeCandidates(
    const std::vector<GeodeticPosition>& route) {
  std::map<AtlasTileKey, AtlasTileCandidate> unique;
  auto add = [&](GeodeticPosition position, double semantic, bool label) {
    const auto center = geodeticToEcef(position);
    const auto key =
        directionToTile(center.meters, 16, AtlasLayer::Buildings);
    AtlasTileCandidate candidate{};
    candidate.key = key;
    candidate.center = center;
    candidate.boundingRadiusMeters = 130.0;
    candidate.screenSpaceErrorPixels = 8.0;
    candidate.semanticImportance = semantic;
    candidate.qualityGain = 1.0;
    candidate.gpuCostMilliseconds = 0.02;
    candidate.lightingCostMilliseconds = 0.015;
    candidate.residentBytes = 64 * 1024;
    candidate.uploadBytes = 64 * 1024;
    candidate.routeCriticalLabel = label;
    unique[key] = candidate;
  };
  for (size_t index = 0; index < route.size(); ++index) {
    add(route[index], index % 25 == 0 ? 2.5 : 1.25, index % 40 == 0);
    GeodeticPosition side = route[index];
    side.longitudeDegrees += index % 2 == 0 ? 0.012 : -0.012;
    add(side, 0.55, false);
  }
  for (int latitude = -5; latitude <= 5; ++latitude) {
    for (int longitude = -5; longitude <= 5; ++longitude) {
      add(
          {28.622 + latitude * 0.006,
           77.221 + longitude * 0.006,
           215.0},
          0.4,
          false);
    }
  }
  std::vector<AtlasTileCandidate> candidates;
  candidates.reserve(unique.size());
  for (auto& [_, candidate] : unique) {
    candidates.push_back(std::move(candidate));
  }
  return candidates;
}

struct PolicySummary {
  uint64_t deadlineMisses = 0;
  uint64_t requestedBytes = 0;
  uint64_t wastedBytes = 0;
  double utility = 0.0;
  double corridorQuality = 0.0;
};

PolicySummary runPolicy(
    AtlasControlPolicy policy,
    const Options& options,
    const std::vector<GeodeticPosition>& route,
    const std::vector<AtlasTileCandidate>& sourceCandidates,
    std::ofstream& frames) {
  AtlasBudgetConfig budget{};
  budget.targetFrameMilliseconds = 16.67;
  budget.residentMemoryBytes = 4 * 1024 * 1024;
  budget.uploadBytesPerSecond = 4 * 1024 * 1024;
  budget.maximumTileChangesPerFrame = 2;
  budget.speculativeWasteBytes = 2 * 1024 * 1024;
  RoutePredictiveScheduler scheduler{budget, policy};
  std::set<AtlasTileKey> resident;
  std::set<AtlasTileKey> everNeeded;
  PolicySummary summary{};
  EcefPosition previousCamera = geodeticToEcef(route.front());

  for (uint32_t frameIndex = 0; frameIndex < options.frames; ++frameIndex) {
    const size_t routeIndex =
        std::min(
            route.size() - 1,
            static_cast<size_t>(
                (static_cast<uint64_t>(frameIndex) *
                 static_cast<uint64_t>(route.size() - 1)) /
                static_cast<uint64_t>(options.frames - 1)));
    const EcefPosition camera = geodeticToEcef(route[routeIndex]);
    AtlasFrameContext frame{};
    frame.cameraPosition = camera;
    frame.cameraVelocityMetersPerSecond =
        (camera.meters - previousCamera.meters) * 60.0;
    frame.frameNumber = frameIndex;
    frame.deltaSeconds = 1.0 / 60.0;
    frame.measuredFrameMilliseconds =
        12.0 + static_cast<double>((frameIndex + options.seed) % 11) * 0.15;
    previousCamera = camera;

    std::vector<AtlasTileCandidate> candidates = sourceCandidates;
    for (auto& candidate : candidates) {
      const double distance =
          glm::length(candidate.center.meters - camera.meters);
      candidate.visible = distance < 1400.0;
      candidate.visibilityConfidence =
          candidate.visible ? std::max(0.1, 1.0 - distance / 1400.0) : 0.0;
      candidate.qualityGain = 1.0 + 8.0 / (1.0 + distance / 250.0);
      candidate.screenSpaceErrorPixels = candidate.qualityGain * 2.0;
      candidate.resident = resident.contains(candidate.key);
    }

    RoutePrediction prediction{};
    prediction.shape.assign(route.begin() + routeIndex, route.end());
    prediction.corridorRadiusMeters = 350.0;
    prediction.lookAheadSeconds = 20.0;
    prediction.expectedSpeedMetersPerSecond =
        std::max(2.0, glm::length(frame.cameraVelocityMetersPerSecond));

    uint32_t frameMisses = 0;
    for (size_t lookAhead = 0; lookAhead <= 12; lookAhead += 4) {
      const size_t neededIndex =
          std::min(route.size() - 1, routeIndex + lookAhead);
      const AtlasTileKey needed =
          directionToTile(
              geodeticToEcef(route[neededIndex]).meters,
              16,
              AtlasLayer::Buildings);
      everNeeded.insert(needed);
      if (!resident.contains(needed)) frameMisses++;
    }

    const auto selected = scheduler.select(
        frame,
        candidates,
        policy == AtlasControlPolicy::DistanceOnly ||
                policy == AtlasControlPolicy::GeometryOnly ||
                policy == AtlasControlPolicy::LightingOnly
            ? nullptr
            : &prediction);
    for (const auto& decision : selected) resident.insert(decision.key);

    const auto& stats = scheduler.stats();
    summary.deadlineMisses += frameMisses;
    summary.requestedBytes += stats.requestedUploadBytes;
    summary.utility += stats.semanticUtility;
    summary.corridorQuality += stats.routeCorridorQuality;
    frames << frameIndex << ',' << toString(policy)
           << ",analytical-scheduler-simulation," << frameMisses << ','
           << summary.deadlineMisses << ',' << selected.size() << ','
           << resident.size() << ',' << stats.requestedUploadBytes << ','
           << stats.speculativeBytes << ',' << stats.semanticUtility << ','
           << stats.routeCorridorQuality << ',' << stats.budgetViolation
           << '\n';
  }

  for (const auto& key : resident) {
    if (!everNeeded.contains(key)) summary.wastedBytes += 64 * 1024;
  }
  return summary;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parseOptions(argc, argv);
    std::filesystem::create_directories(options.output);
    const auto route = makeRoute(241);
    const auto candidates = makeCandidates(route);
    std::ofstream frames{options.output / "frames.csv"};
    frames
        << "frame,policy,measurementClass,frameDeadlineMisses,"
           "totalDeadlineMisses,selectedTiles,residentTiles,"
           "requestedUploadBytes,speculativeBytes,semanticUtility,"
           "routeCorridorQuality,budgetViolation\n";

    const std::vector<AtlasControlPolicy> policies =
        options.policy
            ? std::vector<AtlasControlPolicy>{*options.policy}
            : std::vector<AtlasControlPolicy>{
                  AtlasControlPolicy::DistanceOnly,
                  AtlasControlPolicy::VelocityOnly,
                  AtlasControlPolicy::RouteOnly,
                  AtlasControlPolicy::RouteSemantics,
                  AtlasControlPolicy::GeometryOnly,
                  AtlasControlPolicy::LightingOnly,
                  AtlasControlPolicy::FullAtlas,
              };
    nlohmann::json summary{
        {"format", "Vulkax-Atlas-research-summary-1"},
        {"measurementClass", "analytical-scheduler-simulation"},
        {"framesPerPolicy", options.frames},
        {"seed", options.seed},
        {"candidateTiles", candidates.size()},
        {"policies", nlohmann::json::array()},
    };
    for (AtlasControlPolicy policy : policies) {
      const PolicySummary result =
          runPolicy(policy, options, route, candidates, frames);
      summary["policies"].push_back(
          {
              {"policy", toString(policy)},
              {"deadlineMisses", result.deadlineMisses},
              {"requestedBytes", result.requestedBytes},
              {"wastedPrefetchBytes", result.wastedBytes},
              {"semanticUtility", result.utility},
              {"routeCorridorQuality", result.corridorQuality},
          });
      std::cout << std::left << std::setw(18) << toString(policy)
                << " deadline-misses=" << result.deadlineMisses
                << " wasted-bytes=" << result.wastedBytes << '\n';
    }
    std::ofstream summaryFile{options.output / "summary.json"};
    summaryFile << summary.dump(2) << '\n';
    std::cout << "Atlas benchmark wrote " << options.output << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "atlas-benchmark: " << error.what() << '\n';
    return 1;
  }
}
