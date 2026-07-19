#pragma once

#include "atlas/core/geodesy.hpp"
#include "atlas/core/tile_key.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vulkax::atlas {

struct AtlasDataSource {
  std::string name;
  std::string url;
  std::string license;
  std::string checksumSha256;
  std::string capturedAt;
};

struct AtlasLayerManifest {
  AtlasLayer layer = AtlasLayer::Terrain;
  std::string contentTemplate;
  uint8_t minimumLevel = 0;
  uint8_t maximumLevel = 0;
  double rootGeometricErrorMeters = 0.0;
  bool optional = false;
};

struct AtlasCapabilities {
  bool requiresNetwork = false;
  bool supportsOfflineRouting = false;
  bool supportsOfflineSearch = false;
  bool supportsTransit = false;
  bool supportsTraffic = false;
};

struct AtlasDatasetManifest {
  uint32_t formatVersion = 2;
  std::string datasetId;
  std::string displayName;
  std::string generatedAt;
  std::string verticalDatum = "WGS84-ellipsoidal";
  GeodeticPosition defaultView{};
  std::string attribution = "(c) OpenStreetMap contributors";
  std::string copyrightUrl = "https://www.openstreetmap.org/copyright";
  std::vector<AtlasLayerManifest> layers;
  std::vector<AtlasDataSource> sources;
  AtlasCapabilities capabilities{};
};

struct AtlasRegionalPack {
  std::string packId;
  std::string datasetId;
  std::string displayName;
  double southDegrees = 0.0;
  double westDegrees = 0.0;
  double northDegrees = 0.0;
  double eastDegrees = 0.0;
  std::filesystem::path archivePath;
  std::string checksumSha256;
  uint64_t bytes = 0;
};

AtlasDatasetManifest loadDatasetManifest(const std::filesystem::path& path);
void saveDatasetManifest(
    const AtlasDatasetManifest& manifest,
    const std::filesystem::path& path);
void validateDatasetManifest(const AtlasDatasetManifest& manifest);
std::optional<AtlasLayerManifest> findLayer(
    const AtlasDatasetManifest& manifest,
    AtlasLayer layer);

AtlasLayer parseAtlasLayer(const std::string& value);

}  // namespace vulkax::atlas
