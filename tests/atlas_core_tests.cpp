#include "atlas/atlas_runtime.hpp"
#include "atlas/core/dataset.hpp"
#include "atlas/core/geodesy.hpp"
#include "atlas/core/sha256.hpp"
#include "atlas/core/tile_key.hpp"
#include "atlas/navigation/gateway_provider.hpp"
#include "atlas/navigation/replay_providers.hpp"
#include "atlas/renderer/route_mesh.hpp"
#include "atlas/research/route_predictive_scheduler.hpp"
#include "atlas/streaming/tile_cache.hpp"
#include "atlas/streaming/regional_pack.hpp"
#include "atlas/streaming/globe_selector.hpp"
#include "atlas/streaming/tile_source.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace {

bool close(double left, double right, double tolerance) {
  return std::abs(left - right) <= tolerance;
}

glm::dquat rotationBetween(
    const glm::dvec3& from,
    const glm::dvec3& to) {
  const glm::dvec3 normalizedFrom = glm::normalize(from);
  const glm::dvec3 normalizedTo = glm::normalize(to);
  const glm::dvec3 axis = glm::cross(normalizedFrom, normalizedTo);
  return glm::normalize(
      glm::dquat{
          1.0 + glm::dot(normalizedFrom, normalizedTo),
          axis.x,
          axis.y,
          axis.z,
      });
}

class FakeGatewayTransport final : public vulkax::atlas::HttpTransport {
 public:
  vulkax::atlas::HttpResponse get(
      const std::string& uri,
      const std::string&,
      const vulkax::atlas::CancellationToken&) override {
    if (uri.find("/v1/search") != std::string::npos) {
      return json(
          R"({"results":[{"id":"osm:w1950397","name":"India Gate","subtitle":"New Delhi","position":[28.6129,77.2295,216.0],"confidence":0.99,"category":"landmark"}]})");
    }
    if (uri.find("/v1/reverse") != std::string::npos) {
      return json(
          R"({"result":{"id":"osm:w1950397","name":"India Gate","subtitle":"New Delhi","position":[28.6129,77.2295,216.0],"confidence":0.99,"category":"landmark"}})");
    }
    if (uri.find("/v1/traffic") != std::string::npos) {
      return json(
          R"({"segments":[{"id":"traffic-1","shape":[[28.61,77.22,216.0],[28.62,77.23,216.0]],"currentSpeedKph":18.0,"freeFlowSpeedKph":45.0,"confidence":0.9,"closed":false}]})");
    }
    return {404, {}};
  }

  vulkax::atlas::HttpResponse postJson(
      const std::string& uri,
      const std::string&,
      const vulkax::atlas::CancellationToken&) override {
    if (uri.find("/v1/route") != std::string::npos) {
      return json(
          R"({"routes":[{"id":"route-delhi-1","mode":"driving","distanceMeters":3200.0,"durationSeconds":720.0,"shape":[[28.6315,77.2167,215.0],[28.6129,77.2295,216.0]]}]})");
    }
    if (uri.find("/v1/transit") != std::string::npos) {
      return json(
          R"({"itineraries":[{"id":"transit-delhi-1","mode":"transit","distanceMeters":3500.0,"durationSeconds":1100.0,"realtimeTransit":true,"shape":[[28.6315,77.2167,215.0],[28.6129,77.2295,216.0]]}]})");
    }
    return {404, {}};
  }

 private:
  static vulkax::atlas::HttpResponse json(const std::string& value) {
    return {
        200,
        std::vector<uint8_t>(value.begin(), value.end()),
        "\"fixture\"",
    };
  }
};

}  // namespace

int main() {
  using namespace vulkax::atlas;

  const GeodeticPosition delhi{28.6139, 77.2090, 216.0};
  const std::vector<uint8_t> abc{'a', 'b', 'c'};
  assert(
      sha256(abc) ==
      "ba7816bf8f01cfea414140de5dae2223"
      "b00361a396177a9cb410ff61f20015ad");
  const EcefPosition delhiEcef = geodeticToEcef(delhi);
  const GeodeticPosition delhiRoundTrip = ecefToGeodetic(delhiEcef);
  assert(close(delhi.latitudeDegrees, delhiRoundTrip.latitudeDegrees, 1e-9));
  assert(close(delhi.longitudeDegrees, delhiRoundTrip.longitudeDegrees, 1e-9));
  assert(close(delhi.altitudeMeters, delhiRoundTrip.altitudeMeters, 1e-5));

  const LocalFrame local = makeLocalFrame(delhi);
  const EcefPosition eastPoint = local.toEcef({100.0, 0.0, 0.0});
  const glm::dvec3 localRoundTrip = local.toLocal(eastPoint);
  assert(close(localRoundTrip.x, 100.0, 1e-8));
  assert(close(localRoundTrip.y, 0.0, 1e-8));
  assert(close(localRoundTrip.z, 0.0, 1e-8));

  for (const auto direction : {
           glm::dvec3{1.0, 0.2, -0.4},
           glm::dvec3{-1.0, 0.2, 0.4},
           glm::dvec3{0.1, 1.0, -0.2},
           glm::dvec3{0.1, -1.0, 0.2},
           glm::dvec3{0.1, 0.2, 1.0},
           glm::dvec3{-0.1, 0.2, -1.0},
       }) {
    const auto address = directionToCube(direction);
    const auto roundTrip = cubeToDirection(address.face, address.uv);
    assert(glm::dot(glm::normalize(direction), roundTrip) > 0.999999999);
  }

  AtlasTileKey key =
      directionToTile(glm::dvec3{1.0, 0.0, 0.0}, 8, AtlasLayer::Terrain);
  assert(key.valid());
  assert(key.parent().level == 7);
  assert(key.children()[0].parent() == key);
  assert(screenSpaceError(16.0, 1000.0, 1080.0, 1.0) > 0.0);

  const auto temporary =
      std::filesystem::temp_directory_path() / "vulkax-atlas-core-tests";
  std::filesystem::remove_all(temporary);
  std::filesystem::create_directories(temporary);

  AtlasDatasetManifest manifest{};
  manifest.datasetId = "test-region";
  manifest.displayName = "Test Region";
  manifest.generatedAt = "2026-07-19T00:00:00Z";
  manifest.defaultView = delhi;
  manifest.layers.push_back(
      {AtlasLayer::Terrain,
       "content/terrain/{face}/{level}/{x}/{y}.glb",
       0,
       18,
       1000000.0,
       false});
  manifest.sources.push_back(
      {"OpenStreetMap",
       "https://www.openstreetmap.org",
       "ODbL-1.0",
       "test-checksum",
       "2026-07-19T00:00:00Z"});
  const auto manifestPath = temporary / "atlas-dataset.json";
  saveDatasetManifest(manifest, manifestPath);
  const auto loaded = loadDatasetManifest(manifestPath);
  assert(loaded.datasetId == manifest.datasetId);
  assert(findLayer(loaded, AtlasLayer::Terrain).has_value());

  MemoryTileSource memory;
  memory.put("tiles/test", {1, 2, 3, 4}, "etag-1");
  TileRequest request{key, "tiles/test"};
  auto payload =
      memory.request(request, std::make_shared<CancellationToken>()).get();
  assert(payload.bytes == std::vector<uint8_t>({1, 2, 3, 4}));

  payload.sha256 = sha256(payload.bytes);
  TileCache cache{temporary / "cache", 1024};
  cache.write(request, payload);
  const auto cached = cache.read(request);
  assert(cached.has_value());
  assert(cached->fromCache);
  assert(cached->bytes == payload.bytes);

  const auto packPath = temporary / "test-region.vxa";
  {
    AtlasPackWriter pack{packPath};
    pack.begin();
    pack.setMetadata("format", "Vulkax-Atlas-pack-1");
    pack.setMetadata("datasetId", manifest.datasetId);
    pack.putTile(request, payload);
    pack.putPoi(
        {"osm:n123",
         "India Gate",
         "New Delhi",
         delhi,
         0.95,
         "landmark"});
    const std::vector<uint8_t> graph{9, 8, 7};
    pack.putAsset("routing/valhalla.tar", graph, "graph-checksum");
    pack.commit();
  }
  AtlasPackTileSource packSource{packPath};
  const auto packed =
      packSource.request(request, std::make_shared<CancellationToken>()).get();
  assert(packed.bytes == payload.bytes);
  const auto poiResults = packSource.searchOffline("gate", 5);
  assert(poiResults.size() == 1);
  assert(poiResults.front().name == "India Gate");
  assert(
      packSource.asset("routing/valhalla.tar") ==
      std::optional<std::vector<uint8_t>>({9, 8, 7}));
  assert(packSource.metadata("datasetId") == manifest.datasetId);

  AtlasBudgetConfig budget{};
  budget.uploadBytesPerSecond = 1024 * 1024;
  budget.maximumTileChangesPerFrame = 2;
  RoutePredictiveScheduler scheduler{budget};
  const GeodeticPosition routeEnd{28.62, 77.22, 216.0};
  RoutePrediction route{{delhi, routeEnd}, 1000.0, 60.0, 10.0, false};
  AtlasTileCandidate onRoute{};
  onRoute.key = key;
  onRoute.center = geodeticToEcef(delhi);
  onRoute.semanticImportance = 2.0;
  onRoute.visibilityConfidence = 1.0;
  onRoute.qualityGain = 2.0;
  onRoute.uploadBytes = 1024;
  onRoute.residentBytes = 4096;
  AtlasTileCandidate offRoute = onRoute;
  offRoute.key.x += 1;
  offRoute.center = geodeticToEcef({40.7128, -74.0060, 10.0});
  offRoute.semanticImportance = 0.5;

  AtlasFrameContext frame{};
  frame.deltaSeconds = 1.0 / 60.0;
  frame.measuredFrameMilliseconds = 10.0;
  const auto decisions = scheduler.select(frame, {offRoute, onRoute}, &route);
  assert(!decisions.empty());
  assert(decisions.front().key == onRoute.key);
  assert(decisions.front().routeProbability > 0.5);

  auto runtimeSource = std::make_shared<MemoryTileSource>();
  runtimeSource->put(
      "content/terrain/px/8/" + std::to_string(key.x) + "/" +
          std::to_string(key.y) + ".glb",
      std::vector<uint8_t>{4, 3, 2, 1});
  AtlasRuntime runtime{manifest, runtimeSource, nullptr, budget};
  runtime.update(frame, {onRoute}, &route);
  assert(runtime.stats().pendingRequests == 1);
  frame.frameNumber++;
  runtime.update(frame, {onRoute}, &route);
  const auto runtimeReady = runtime.takeReadyTiles();
  assert(runtimeReady.size() == 1);
  assert(runtimeReady.front().bytes == std::vector<uint8_t>({4, 3, 2, 1}));

  const auto replayFixture =
      std::filesystem::path{ENGINE_DIR} /
      "tests/fixtures/navigation_replay.json";
  ReplayNavigationProvider replay{replayFixture};
  const auto replaySearch = replay.search({"gate", "en", delhi, 5}).get();
  assert(replaySearch.size() == 1);
  assert(replaySearch.front().name == "India Gate");
  RouteRequest routeRequest{};
  routeRequest.origin = delhi;
  routeRequest.destination = routeEnd;
  routeRequest.mode = TravelMode::Driving;
  const auto replayRoutes = replay.route(routeRequest).get();
  assert(replayRoutes.size() == 1);
  assert(replayRoutes.front().trafficAware);
  routeRequest.mode = TravelMode::Transit;
  assert(replay.itineraries(routeRequest).get().front().realtimeTransit);
  assert(
      replay.traffic(delhi, routeEnd).get().segments.front().currentSpeedKph ==
      18.0);
  const auto routeRibbon =
      buildRouteRibbon(replayRoutes.front().shape);
  assert(routeRibbon.vertices.size() >= 6);
  assert(routeRibbon.indices.size() >= 12);
  for (const auto& vertex : routeRibbon.vertices) {
    assert(glm::length(vertex.position) > 10.0f);
    assert(close(glm::length(vertex.normal), 1.0, 1e-5));
  }

  auto gatewayTransport = std::make_shared<FakeGatewayTransport>();
  GatewayNavigationProvider gateway{
      "http://atlas.test/", gatewayTransport};
  assert(gateway.search({"India Gate", "en", delhi, 5}).get().size() == 1);
  assert(gateway.reverse(delhi, "en").get()->name == "India Gate");
  routeRequest.mode = TravelMode::Driving;
  assert(gateway.route(routeRequest).get().front().id == "route-delhi-1");
  routeRequest.mode = TravelMode::Transit;
  assert(gateway.itineraries(routeRequest).get().front().realtimeTransit);
  assert(gateway.traffic(delhi, routeEnd).get().segments.size() == 1);

  AtlasCameraState atlasCamera{};
  atlasCamera.geodetic = {0.0, 0.0, 10000000.0};
  atlasCamera.ecef = geodeticToEcef(atlasCamera.geodetic);
  atlasCamera.orientation =
      rotationBetween(
          glm::dvec3{0.0, 0.0, 1.0},
          glm::normalize(-atlasCamera.ecef.meters));
  GlobeSelectionConfig selectionConfig{};
  selectionConfig.maximumLevel = 8;
  selectionConfig.maximumSelectedTiles = 96;
  GlobeTileSelector selector{selectionConfig};
  const auto selectedTiles = selector.select(atlasCamera, &route);
  assert(!selectedTiles.empty());
  assert(selectedTiles.size() <= selectionConfig.maximumSelectedTiles);
  assert(selector.stats().visitedTiles >= selectedTiles.size());
  for (const auto& candidate : selectedTiles) {
    assert(candidate.key.valid());
    assert(candidate.visible);
    assert(candidate.screenSpaceErrorPixels > 0.0);
  }

  std::filesystem::remove_all(temporary);
  std::cout << "Vulkax Atlas core tests passed\n";
  return 0;
}
