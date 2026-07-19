#include "atlas/navigation/replay_providers.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace vulkax::atlas {
namespace {

using Json = nlohmann::json;

GeodeticPosition parsePosition(const Json& value) {
  return {
      value.at(0).get<double>(),
      value.at(1).get<double>(),
      value.size() > 2 ? value.at(2).get<double>() : 0.0,
  };
}

std::string lower(std::string value) {
  std::transform(
      value.begin(),
      value.end(),
      value.begin(),
      [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
      });
  return value;
}

RouteResult parseRoute(const Json& item) {
  RouteResult route{};
  route.id = item.at("id").get<std::string>();
  route.mode = parseTravelMode(item.at("mode").get<std::string>());
  route.distanceMeters = item.at("distanceMeters").get<double>();
  route.durationSeconds = item.at("durationSeconds").get<double>();
  route.trafficDelaySeconds = item.value("trafficDelaySeconds", 0.0);
  route.trafficAware = item.value("trafficAware", false);
  route.realtimeTransit = item.value("realtimeTransit", false);
  for (const auto& point : item.at("shape")) {
    route.shape.push_back(parsePosition(point));
  }
  for (const auto& itemManeuver : item.value("maneuvers", Json::array())) {
    route.maneuvers.push_back(
        {
            itemManeuver.at("instruction").get<std::string>(),
            parsePosition(itemManeuver.at("position")),
            itemManeuver.value("distanceMeters", 0.0),
            itemManeuver.value("durationSeconds", 0.0),
            itemManeuver.value("routeShapeIndex", 0u),
            itemManeuver.value("validLanes", std::vector<uint8_t>{}),
            itemManeuver.value("activeLanes", std::vector<uint8_t>{}),
        });
  }
  return route;
}

}  // namespace

const char* toString(TravelMode mode) {
  switch (mode) {
    case TravelMode::Driving: return "driving";
    case TravelMode::Walking: return "walking";
    case TravelMode::Cycling: return "cycling";
    case TravelMode::Transit: return "transit";
  }
  return "unknown";
}

TravelMode parseTravelMode(const std::string& value) {
  if (value == "driving") return TravelMode::Driving;
  if (value == "walking") return TravelMode::Walking;
  if (value == "cycling") return TravelMode::Cycling;
  if (value == "transit") return TravelMode::Transit;
  throw std::runtime_error("unknown Atlas travel mode: " + value);
}

ReplayNavigationProvider::ReplayNavigationProvider(
    const std::filesystem::path& fixture) {
  std::ifstream input{fixture};
  if (!input) {
    throw std::runtime_error(
        "failed to open Atlas navigation replay: " + fixture.string());
  }
  Json root;
  input >> root;
  if (root.value("format", "") != "Vulkax-Atlas-navigation-replay-1") {
    throw std::runtime_error("unsupported Atlas navigation replay");
  }

  for (const auto& item : root.value("search", Json::array())) {
    searchResults.push_back(
        {
            item.at("id").get<std::string>(),
            item.at("name").get<std::string>(),
            item.value("subtitle", ""),
            parsePosition(item.at("position")),
            item.value("confidence", 0.0),
            item.value("category", ""),
        });
  }
  for (const auto& item : root.value("routes", Json::array())) {
    roadRoutes.push_back(parseRoute(item));
  }
  for (const auto& item : root.value("transit", Json::array())) {
    transitRoutes.push_back(parseRoute(item));
  }
  for (const auto& item : root.value("traffic", Json::array())) {
    TrafficSegment segment{};
    segment.id = item.at("id").get<std::string>();
    segment.currentSpeedKph = item.value("currentSpeedKph", 0.0);
    segment.freeFlowSpeedKph = item.value("freeFlowSpeedKph", 0.0);
    segment.confidence = item.value("confidence", 0.0);
    segment.closed = item.value("closed", false);
    for (const auto& point : item.at("shape")) {
      segment.shape.push_back(parsePosition(point));
    }
    trafficOverlay.segments.push_back(std::move(segment));
  }
  trafficOverlay.observedAt = std::chrono::system_clock::now();
}

std::future<std::vector<SearchResult>> ReplayNavigationProvider::search(
    SearchRequest request) {
  return std::async(
      std::launch::deferred,
      [results = searchResults, request = std::move(request)] {
        const std::string query = lower(request.query);
        std::vector<SearchResult> matches;
        for (const auto& result : results) {
          if (query.empty() || lower(result.name).find(query) != std::string::npos ||
              lower(result.subtitle).find(query) != std::string::npos) {
            matches.push_back(result);
          }
        }
        std::stable_sort(
            matches.begin(),
            matches.end(),
            [](const SearchResult& left, const SearchResult& right) {
              return left.confidence > right.confidence;
            });
        if (matches.size() > request.limit) matches.resize(request.limit);
        return matches;
      });
}

std::future<std::optional<SearchResult>> ReplayNavigationProvider::reverse(
    GeodeticPosition position,
    std::string) {
  return std::async(
      std::launch::deferred,
      [results = searchResults, position]() -> std::optional<SearchResult> {
        if (results.empty()) return std::nullopt;
        const EcefPosition target = geodeticToEcef(position);
        return *std::min_element(
            results.begin(),
            results.end(),
            [&](const SearchResult& left, const SearchResult& right) {
              return glm::length(
                         geodeticToEcef(left.position).meters - target.meters) <
                     glm::length(
                         geodeticToEcef(right.position).meters - target.meters);
            });
      });
}

std::future<std::vector<RouteResult>> ReplayNavigationProvider::route(
    RouteRequest request) {
  return std::async(
      std::launch::deferred,
      [routes = roadRoutes, request = std::move(request)] {
        std::vector<RouteResult> matches;
        for (const auto& route : routes) {
          if (route.mode == request.mode) matches.push_back(route);
        }
        const size_t maximum = static_cast<size_t>(request.alternatives) + 1;
        if (matches.size() > maximum) matches.resize(maximum);
        return matches;
      });
}

std::future<std::optional<GeodeticPosition>>
ReplayNavigationProvider::mapMatch(
    std::vector<GeodeticPosition> trace,
    TravelMode) {
  return std::async(
      std::launch::deferred,
      [trace = std::move(trace)]() -> std::optional<GeodeticPosition> {
        if (trace.empty()) return std::nullopt;
        return trace.back();
      });
}

std::future<std::vector<RouteResult>>
ReplayNavigationProvider::itineraries(RouteRequest) {
  return std::async(
      std::launch::deferred,
      [routes = transitRoutes] { return routes; });
}

std::future<TrafficOverlay> ReplayNavigationProvider::traffic(
    GeodeticPosition,
    GeodeticPosition) {
  return std::async(
      std::launch::deferred,
      [overlay = trafficOverlay] { return overlay; });
}

}  // namespace vulkax::atlas
