#pragma once

#include "atlas/navigation/navigation.hpp"

#include <filesystem>
#include <vector>

namespace vulkax::atlas {

class ReplayNavigationProvider final
    : public SearchProvider,
      public RouteProvider,
      public TransitProvider,
      public TrafficProvider {
 public:
  explicit ReplayNavigationProvider(const std::filesystem::path& fixture);

  std::future<std::vector<SearchResult>> search(
      SearchRequest request) override;
  std::future<std::optional<SearchResult>> reverse(
      GeodeticPosition position,
      std::string locale) override;
  std::future<std::vector<RouteResult>> route(
      RouteRequest request) override;
  std::future<std::optional<GeodeticPosition>> mapMatch(
      std::vector<GeodeticPosition> trace,
      TravelMode mode) override;
  std::future<std::vector<RouteResult>> itineraries(
      RouteRequest request) override;
  std::future<TrafficOverlay> traffic(
      GeodeticPosition southWest,
      GeodeticPosition northEast) override;

 private:
  std::vector<SearchResult> searchResults;
  std::vector<RouteResult> roadRoutes;
  std::vector<RouteResult> transitRoutes;
  TrafficOverlay trafficOverlay;
};

const char* toString(TravelMode mode);
TravelMode parseTravelMode(const std::string& value);

}  // namespace vulkax::atlas
