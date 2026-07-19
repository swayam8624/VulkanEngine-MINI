#pragma once

#include "atlas/navigation/navigation.hpp"
#include "atlas/streaming/tile_source.hpp"

#include <memory>

namespace vulkax::atlas {

class GatewayNavigationProvider final
    : public SearchProvider,
      public RouteProvider,
      public TransitProvider,
      public TrafficProvider {
 public:
  GatewayNavigationProvider(
      std::string baseUrl,
      std::shared_ptr<HttpTransport> transport);

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
  std::string endpoint(const std::string& path) const;

  std::string baseUrl;
  std::shared_ptr<HttpTransport> transport;
};

}  // namespace vulkax::atlas
