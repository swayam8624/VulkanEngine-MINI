#pragma once

#include "atlas/core/geodesy.hpp"

#include <chrono>
#include <cstdint>
#include <future>
#include <optional>
#include <string>
#include <vector>

namespace vulkax::atlas {

enum class TravelMode : uint8_t {
  Driving,
  Walking,
  Cycling,
  Transit
};

struct SearchRequest {
  std::string query;
  std::string locale = "en";
  std::optional<GeodeticPosition> focus;
  uint32_t limit = 10;
};

struct SearchResult {
  std::string id;
  std::string name;
  std::string subtitle;
  GeodeticPosition position{};
  double confidence = 0.0;
  std::string category;
};

struct RouteRequest {
  GeodeticPosition origin{};
  GeodeticPosition destination{};
  std::vector<GeodeticPosition> intermediateStops;
  TravelMode mode = TravelMode::Driving;
  uint32_t alternatives = 2;
  bool useLiveTraffic = true;
  std::string locale = "en";
  std::chrono::system_clock::time_point departureTime{};
};

struct Maneuver {
  std::string instruction;
  GeodeticPosition position{};
  double distanceMeters = 0.0;
  double durationSeconds = 0.0;
  uint32_t routeShapeIndex = 0;
  std::vector<uint8_t> validLanes;
  std::vector<uint8_t> activeLanes;
};

struct RouteResult {
  std::string id;
  TravelMode mode = TravelMode::Driving;
  std::vector<GeodeticPosition> shape;
  std::vector<Maneuver> maneuvers;
  double distanceMeters = 0.0;
  double durationSeconds = 0.0;
  double trafficDelaySeconds = 0.0;
  bool trafficAware = false;
  bool realtimeTransit = false;
};

struct TrafficSegment {
  std::string id;
  std::vector<GeodeticPosition> shape;
  double currentSpeedKph = 0.0;
  double freeFlowSpeedKph = 0.0;
  double confidence = 0.0;
  bool closed = false;
};

struct TrafficOverlay {
  std::vector<TrafficSegment> segments;
  std::chrono::system_clock::time_point observedAt{};
  std::chrono::seconds maxAge{120};
};

class SearchProvider {
 public:
  virtual ~SearchProvider() = default;
  virtual std::future<std::vector<SearchResult>> search(
      SearchRequest request) = 0;
  virtual std::future<std::optional<SearchResult>> reverse(
      GeodeticPosition position,
      std::string locale) = 0;
};

class RouteProvider {
 public:
  virtual ~RouteProvider() = default;
  virtual std::future<std::vector<RouteResult>> route(
      RouteRequest request) = 0;
  virtual std::future<std::optional<GeodeticPosition>> mapMatch(
      std::vector<GeodeticPosition> trace,
      TravelMode mode) = 0;
};

class TransitProvider {
 public:
  virtual ~TransitProvider() = default;
  virtual std::future<std::vector<RouteResult>> itineraries(
      RouteRequest request) = 0;
};

class TrafficProvider {
 public:
  virtual ~TrafficProvider() = default;
  virtual std::future<TrafficOverlay> traffic(
      GeodeticPosition southWest,
      GeodeticPosition northEast) = 0;
};

}  // namespace vulkax::atlas
