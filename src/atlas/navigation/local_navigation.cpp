#include "atlas/navigation/local_navigation.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace vulkax::atlas {
namespace {

using Json = nlohmann::json;

constexpr uint8_t drivingFlag = 1;
constexpr uint8_t walkingFlag = 2;
constexpr uint8_t cyclingFlag = 4;

double radians(double degrees) {
  return degrees * 3.14159265358979323846 / 180.0;
}

double distanceMeters(
    const GeodeticPosition& left,
    const GeodeticPosition& right) {
  constexpr double radius = 6371008.8;
  const double lat0 = radians(left.latitudeDegrees);
  const double lat1 = radians(right.latitudeDegrees);
  const double deltaLat = lat1 - lat0;
  const double deltaLon =
      radians(right.longitudeDegrees - left.longitudeDegrees);
  const double value =
      std::sin(deltaLat * 0.5) * std::sin(deltaLat * 0.5) +
      std::cos(lat0) * std::cos(lat1) *
          std::sin(deltaLon * 0.5) * std::sin(deltaLon * 0.5);
  return radius * 2.0 *
         std::atan2(std::sqrt(value), std::sqrt(std::max(0.0, 1.0 - value)));
}

std::string normalized(std::string value) {
  std::transform(
      value.begin(),
      value.end(),
      value.begin(),
      [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
      });
  return value;
}

uint8_t flagFor(TravelMode mode) {
  switch (mode) {
    case TravelMode::Driving: return drivingFlag;
    case TravelMode::Walking: return walkingFlag;
    case TravelMode::Cycling: return cyclingFlag;
    case TravelMode::Transit: return walkingFlag;
  }
  return drivingFlag;
}

double speedMetersPerSecond(TravelMode mode) {
  switch (mode) {
    case TravelMode::Driving: return 8.33;
    case TravelMode::Walking: return 1.4;
    case TravelMode::Cycling: return 4.5;
    case TravelMode::Transit: return 5.0;
  }
  return 1.4;
}

}  // namespace

struct LocalNavigationProvider::Impl {
  struct Edge {
    uint32_t target = 0;
    double distance = 0.0;
    uint8_t modes = 0;
  };

  std::vector<GeodeticPosition> nodes;
  std::vector<std::vector<Edge>> adjacency;
  std::vector<SearchResult> places;
  size_t edges = 0;

  uint32_t nearestNode(
      const GeodeticPosition& position,
      uint8_t requiredMode) const {
    double bestDistance = std::numeric_limits<double>::infinity();
    uint32_t best = 0;
    bool found = false;
    for (uint32_t index = 0; index < nodes.size(); ++index) {
      const bool supportsMode = std::any_of(
          adjacency[index].begin(),
          adjacency[index].end(),
          [&](const Edge& edge) { return (edge.modes & requiredMode) != 0; });
      if (!supportsMode) continue;
      const double distance = distanceMeters(position, nodes[index]);
      if (distance < bestDistance) {
        bestDistance = distance;
        best = index;
        found = true;
      }
    }
    if (!found) throw std::runtime_error("local navigation graph has no routable node");
    return best;
  }
};

LocalNavigationProvider::LocalNavigationProvider(
    const std::filesystem::path& dataset)
    : impl{std::make_unique<Impl>()} {
  std::ifstream input{dataset};
  if (!input) {
    throw std::runtime_error(
        "failed to open local navigation dataset: " + dataset.string());
  }
  Json root;
  input >> root;
  if (root.value("format", "") != "Vulkax-local-navigation-1") {
    throw std::runtime_error("unsupported local navigation dataset");
  }

  for (const auto& item : root.at("nodes")) {
    impl->nodes.push_back(
        {item.at(0).get<double>(), item.at(1).get<double>(), 0.0});
  }
  impl->adjacency.resize(impl->nodes.size());
  for (const auto& item : root.at("edges")) {
    const uint32_t source = item.at(0).get<uint32_t>();
    const uint32_t target = item.at(1).get<uint32_t>();
    if (source >= impl->nodes.size() || target >= impl->nodes.size()) {
      throw std::runtime_error("local navigation edge references invalid node");
    }
    impl->adjacency[source].push_back(
        {target, item.at(2).get<double>(), item.at(3).get<uint8_t>()});
    ++impl->edges;
  }
  for (const auto& item : root.at("places")) {
    const auto& position = item.at("position");
    impl->places.push_back(
        {
            item.at("id").get<std::string>(),
            item.at("name").get<std::string>(),
            item.value("subtitle", "Connaught Place, New Delhi"),
            {position.at(0).get<double>(),
             position.at(1).get<double>(),
             position.size() > 2 ? position.at(2).get<double>() : 0.0},
            1.0,
            item.value("category", "place"),
        });
  }
  if (impl->nodes.empty() || impl->edges == 0 || impl->places.empty()) {
    throw std::runtime_error("local navigation dataset is empty");
  }
}

LocalNavigationProvider::~LocalNavigationProvider() = default;
LocalNavigationProvider::LocalNavigationProvider(
    LocalNavigationProvider&&) noexcept = default;
LocalNavigationProvider& LocalNavigationProvider::operator=(
    LocalNavigationProvider&&) noexcept = default;

std::future<std::vector<SearchResult>> LocalNavigationProvider::search(
    SearchRequest request) {
  return std::async(
      std::launch::deferred,
      [places = impl->places, request = std::move(request)] {
        const std::string query = normalized(request.query);
        struct Match {
          SearchResult result;
          int score = 0;
        };
        std::vector<Match> matches;
        for (auto place : places) {
          const std::string name = normalized(place.name);
          const std::string subtitle = normalized(place.subtitle);
          const size_t position = name.find(query);
          if (!query.empty() && position == std::string::npos &&
              subtitle.find(query) == std::string::npos) {
            continue;
          }
          int score = query.empty() ? 0 : (position == 0 ? 100 : 50);
          if (name == query) score += 100;
          if (request.focus) {
            place.confidence =
                1.0 / (1.0 + distanceMeters(*request.focus, place.position) / 500.0);
            score += static_cast<int>(place.confidence * 20.0);
          }
          matches.push_back({std::move(place), score});
        }
        std::stable_sort(
            matches.begin(),
            matches.end(),
            [](const Match& left, const Match& right) {
              if (left.score != right.score) return left.score > right.score;
              return left.result.name < right.result.name;
            });
        std::vector<SearchResult> results;
        const size_t limit = std::min<size_t>(request.limit, matches.size());
        results.reserve(limit);
        for (size_t index = 0; index < limit; ++index) {
          results.push_back(std::move(matches[index].result));
        }
        return results;
      });
}

std::future<std::optional<SearchResult>> LocalNavigationProvider::reverse(
    GeodeticPosition position,
    std::string) {
  return std::async(
      std::launch::deferred,
      [places = impl->places, position]() -> std::optional<SearchResult> {
        if (places.empty()) return std::nullopt;
        return *std::min_element(
            places.begin(),
            places.end(),
            [&](const SearchResult& left, const SearchResult& right) {
              return distanceMeters(position, left.position) <
                     distanceMeters(position, right.position);
            });
      });
}

std::future<std::vector<RouteResult>> LocalNavigationProvider::route(
    RouteRequest request) {
  return std::async(
      std::launch::deferred,
      [this, request = std::move(request)] {
        const uint8_t mode = flagFor(request.mode);
        const uint32_t source = impl->nearestNode(request.origin, mode);
        const uint32_t destination =
            impl->nearestNode(request.destination, mode);
        const size_t count = impl->nodes.size();
        std::vector<double> distance(count, std::numeric_limits<double>::infinity());
        std::vector<uint32_t> previous(
            count, std::numeric_limits<uint32_t>::max());
        using QueueItem = std::pair<double, uint32_t>;
        std::priority_queue<
            QueueItem, std::vector<QueueItem>, std::greater<QueueItem>>
            queue;
        distance[source] = 0.0;
        queue.push({0.0, source});
        while (!queue.empty()) {
          const auto [currentDistance, node] = queue.top();
          queue.pop();
          if (currentDistance != distance[node]) continue;
          if (node == destination) break;
          for (const auto& edge : impl->adjacency[node]) {
            if ((edge.modes & mode) == 0) continue;
            const double candidate = currentDistance + edge.distance;
            if (candidate < distance[edge.target]) {
              distance[edge.target] = candidate;
              previous[edge.target] = node;
              queue.push({candidate, edge.target});
            }
          }
        }
        if (!std::isfinite(distance[destination])) {
          return std::vector<RouteResult>{};
        }

        std::vector<uint32_t> path;
        for (uint32_t node = destination;; node = previous[node]) {
          path.push_back(node);
          if (node == source) break;
          if (previous[node] == std::numeric_limits<uint32_t>::max()) {
            return std::vector<RouteResult>{};
          }
        }
        std::reverse(path.begin(), path.end());

        RouteResult route{};
        route.id = "local-connaught-place";
        route.mode = request.mode;
        route.distanceMeters = distance[destination];
        route.durationSeconds =
            route.distanceMeters / speedMetersPerSecond(request.mode);
        route.shape.reserve(path.size() + 2);
        route.shape.push_back(request.origin);
        for (uint32_t node : path) route.shape.push_back(impl->nodes[node]);
        route.shape.push_back(request.destination);
        route.maneuvers.push_back(
            {"Proceed toward destination",
             request.origin,
             route.distanceMeters,
             route.durationSeconds,
             0});
        route.maneuvers.push_back(
            {"Arrive at destination",
             request.destination,
             0.0,
             0.0,
             static_cast<uint32_t>(route.shape.size() - 1)});
        return std::vector<RouteResult>{std::move(route)};
      });
}

std::future<std::optional<GeodeticPosition>>
LocalNavigationProvider::mapMatch(
    std::vector<GeodeticPosition> trace,
    TravelMode mode) {
  return std::async(
      std::launch::deferred,
      [this, trace = std::move(trace), mode]()
          -> std::optional<GeodeticPosition> {
        if (trace.empty()) return std::nullopt;
        const uint32_t node =
            impl->nearestNode(trace.back(), flagFor(mode));
        return impl->nodes[node];
      });
}

size_t LocalNavigationProvider::placeCount() const {
  return impl->places.size();
}

size_t LocalNavigationProvider::nodeCount() const {
  return impl->nodes.size();
}

size_t LocalNavigationProvider::edgeCount() const {
  return impl->edges;
}

}  // namespace vulkax::atlas
