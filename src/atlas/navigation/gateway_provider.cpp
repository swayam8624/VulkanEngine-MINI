#include "atlas/navigation/gateway_provider.hpp"

#include "atlas/navigation/replay_providers.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

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

SearchResult parseSearch(const Json& item) {
  return {
      item.at("id").get<std::string>(),
      item.at("name").get<std::string>(),
      item.value("subtitle", ""),
      parsePosition(item.at("position")),
      item.value("confidence", 0.0),
      item.value("category", ""),
  };
}

RouteResult parseRoute(const Json& item) {
  RouteResult result{};
  result.id = item.at("id").get<std::string>();
  result.mode = parseTravelMode(item.at("mode").get<std::string>());
  result.distanceMeters = item.value("distanceMeters", 0.0);
  result.durationSeconds = item.value("durationSeconds", 0.0);
  result.trafficDelaySeconds = item.value("trafficDelaySeconds", 0.0);
  result.trafficAware = item.value("trafficAware", false);
  result.realtimeTransit = item.value("realtimeTransit", false);
  for (const auto& point : item.value("shape", Json::array())) {
    result.shape.push_back(parsePosition(point));
  }
  for (const auto& maneuver : item.value("maneuvers", Json::array())) {
    result.maneuvers.push_back(
        {
            maneuver.value("instruction", ""),
            parsePosition(maneuver.at("position")),
            maneuver.value("distanceMeters", 0.0),
            maneuver.value("durationSeconds", 0.0),
            maneuver.value("routeShapeIndex", 0u),
            maneuver.value("validLanes", std::vector<uint8_t>{}),
            maneuver.value("activeLanes", std::vector<uint8_t>{}),
        });
  }
  return result;
}

Json positionJson(const GeodeticPosition& position) {
  return Json::array(
      {position.latitudeDegrees,
       position.longitudeDegrees,
       position.altitudeMeters});
}

Json routeRequestJson(const RouteRequest& request) {
  Json payload{
      {"origin", positionJson(request.origin)},
      {"destination", positionJson(request.destination)},
      {"mode", toString(request.mode)},
      {"alternatives", request.alternatives},
      {"useLiveTraffic", request.useLiveTraffic},
      {"locale", request.locale},
  };
  payload["intermediateStops"] = Json::array();
  for (const auto& stop : request.intermediateStops) {
    payload["intermediateStops"].push_back(positionJson(stop));
  }
  return payload;
}

Json responseJson(const HttpResponse& response) {
  if (response.status < 200 || response.status >= 300) {
    throw std::runtime_error(
        "Atlas gateway returned HTTP " + std::to_string(response.status));
  }
  return Json::parse(response.body.begin(), response.body.end());
}

std::string urlEncode(const std::string& value) {
  char* encoded =
      curl_easy_escape(
          nullptr, value.c_str(), static_cast<int>(value.size()));
  if (encoded == nullptr) throw std::runtime_error("failed to URL-encode query");
  std::string result{encoded};
  curl_free(encoded);
  return result;
}

}  // namespace

GatewayNavigationProvider::GatewayNavigationProvider(
    std::string baseUrl,
    std::shared_ptr<HttpTransport> transport)
    : baseUrl{std::move(baseUrl)}, transport{std::move(transport)} {
  if (this->baseUrl.empty() || !this->transport) {
    throw std::invalid_argument(
        "gateway navigation requires a base URL and HTTP transport");
  }
  while (this->baseUrl.ends_with('/')) this->baseUrl.pop_back();
}

std::string GatewayNavigationProvider::endpoint(
    const std::string& path) const {
  return baseUrl + path;
}

std::future<std::vector<SearchResult>>
GatewayNavigationProvider::search(SearchRequest request) {
  return std::async(
      std::launch::async,
      [this, request = std::move(request)] {
        CancellationToken cancellation;
        const auto response = responseJson(
            transport->get(
                endpoint(
                    "/v1/search?q=" +
                    urlEncode(request.query) +
                    "&limit=" + std::to_string(request.limit)),
                {},
                cancellation));
        std::vector<SearchResult> results;
        for (const auto& item : response.at("results")) {
          results.push_back(parseSearch(item));
        }
        return results;
      });
}

std::future<std::optional<SearchResult>>
GatewayNavigationProvider::reverse(
    GeodeticPosition position,
    std::string locale) {
  return std::async(
      std::launch::async,
      [this, position, locale = std::move(locale)]()
          -> std::optional<SearchResult> {
        CancellationToken cancellation;
        const auto response = responseJson(
            transport->get(
                endpoint(
                    "/v1/reverse?lat=" +
                    std::to_string(position.latitudeDegrees) +
                    "&lon=" +
                    std::to_string(position.longitudeDegrees) +
                    "&locale=" + locale),
                {},
                cancellation));
        if (response.at("result").is_null()) return std::nullopt;
        return parseSearch(response.at("result"));
      });
}

std::future<std::vector<RouteResult>>
GatewayNavigationProvider::route(RouteRequest request) {
  return std::async(
      std::launch::async,
      [this, request = std::move(request)] {
        CancellationToken cancellation;
        const auto response = responseJson(
            transport->postJson(
                endpoint("/v1/route"),
                routeRequestJson(request).dump(),
                cancellation));
        std::vector<RouteResult> routes;
        for (const auto& item : response.at("routes")) {
          routes.push_back(parseRoute(item));
        }
        return routes;
      });
}

std::future<std::optional<GeodeticPosition>>
GatewayNavigationProvider::mapMatch(
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
GatewayNavigationProvider::itineraries(RouteRequest request) {
  return std::async(
      std::launch::async,
      [this, request = std::move(request)] {
        CancellationToken cancellation;
        const auto response = responseJson(
            transport->postJson(
                endpoint("/v1/transit"),
                routeRequestJson(request).dump(),
                cancellation));
        std::vector<RouteResult> routes;
        for (const auto& item : response.at("itineraries")) {
          routes.push_back(parseRoute(item));
        }
        return routes;
      });
}

std::future<TrafficOverlay> GatewayNavigationProvider::traffic(
    GeodeticPosition southWest,
    GeodeticPosition northEast) {
  return std::async(
      std::launch::async,
      [this, southWest, northEast] {
        CancellationToken cancellation;
        const auto response = responseJson(
            transport->get(
                endpoint(
                    "/v1/traffic?south=" +
                    std::to_string(southWest.latitudeDegrees) +
                    "&west=" +
                    std::to_string(southWest.longitudeDegrees) +
                    "&north=" +
                    std::to_string(northEast.latitudeDegrees) +
                    "&east=" +
                    std::to_string(northEast.longitudeDegrees)),
                {},
                cancellation));
        TrafficOverlay overlay{};
        overlay.observedAt = std::chrono::system_clock::now();
        for (const auto& item : response.at("segments")) {
          TrafficSegment segment{};
          segment.id = item.at("id").get<std::string>();
          segment.currentSpeedKph = item.value("currentSpeedKph", 0.0);
          segment.freeFlowSpeedKph =
              item.value("freeFlowSpeedKph", 0.0);
          segment.confidence = item.value("confidence", 0.0);
          segment.closed = item.value("closed", false);
          for (const auto& point : item.value("shape", Json::array())) {
            segment.shape.push_back(parsePosition(point));
          }
          overlay.segments.push_back(std::move(segment));
        }
        return overlay;
      });
}

}  // namespace vulkax::atlas
