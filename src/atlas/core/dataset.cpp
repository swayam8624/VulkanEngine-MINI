#include "atlas/core/dataset.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <set>
#include <stdexcept>

namespace vulkax::atlas {
namespace {

using Json = nlohmann::json;

Json toJson(const AtlasDatasetManifest& manifest) {
  Json root = {
      {"format", "Vulkax-Atlas-dataset"},
      {"formatVersion", manifest.formatVersion},
      {"datasetId", manifest.datasetId},
      {"displayName", manifest.displayName},
      {"generatedAt", manifest.generatedAt},
      {"verticalDatum", manifest.verticalDatum},
      {"defaultView",
       {manifest.defaultView.latitudeDegrees,
        manifest.defaultView.longitudeDegrees,
        manifest.defaultView.altitudeMeters}},
      {"attribution", manifest.attribution},
      {"copyrightUrl", manifest.copyrightUrl},
      {"capabilities",
       {
           {"requiresNetwork", manifest.capabilities.requiresNetwork},
           {"offlineRouting", manifest.capabilities.supportsOfflineRouting},
           {"offlineSearch", manifest.capabilities.supportsOfflineSearch},
           {"transit", manifest.capabilities.supportsTransit},
           {"traffic", manifest.capabilities.supportsTraffic},
       }},
  };
  root["layers"] = Json::array();
  for (const auto& layer : manifest.layers) {
    root["layers"].push_back(
        {
            {"layer", toString(layer.layer)},
            {"contentTemplate", layer.contentTemplate},
            {"minimumLevel", layer.minimumLevel},
            {"maximumLevel", layer.maximumLevel},
            {"rootGeometricErrorMeters", layer.rootGeometricErrorMeters},
            {"optional", layer.optional},
        });
  }
  root["sources"] = Json::array();
  for (const auto& source : manifest.sources) {
    root["sources"].push_back(
        {
            {"name", source.name},
            {"url", source.url},
            {"license", source.license},
            {"checksumSha256", source.checksumSha256},
            {"capturedAt", source.capturedAt},
        });
  }
  return root;
}

}  // namespace

AtlasLayer parseAtlasLayer(const std::string& value) {
  if (value == "terrain") return AtlasLayer::Terrain;
  if (value == "buildings") return AtlasLayer::Buildings;
  if (value == "roads") return AtlasLayer::Roads;
  if (value == "land-use") return AtlasLayer::LandUse;
  if (value == "water") return AtlasLayer::Water;
  if (value == "vegetation") return AtlasLayer::Vegetation;
  if (value == "poi") return AtlasLayer::Poi;
  if (value == "labels") return AtlasLayer::Labels;
  if (value == "transit") return AtlasLayer::Transit;
  if (value == "traffic") return AtlasLayer::Traffic;
  throw std::runtime_error("unknown Atlas layer: " + value);
}

AtlasDatasetManifest loadDatasetManifest(const std::filesystem::path& path) {
  std::ifstream input{path};
  if (!input) {
    throw std::runtime_error("failed to open Atlas dataset: " + path.string());
  }
  Json root;
  input >> root;
  if (root.value("format", "") != "Vulkax-Atlas-dataset") {
    throw std::runtime_error("not a Vulkax Atlas dataset manifest");
  }

  AtlasDatasetManifest manifest{};
  manifest.formatVersion = root.at("formatVersion").get<uint32_t>();
  manifest.datasetId = root.at("datasetId").get<std::string>();
  manifest.displayName = root.at("displayName").get<std::string>();
  manifest.generatedAt = root.at("generatedAt").get<std::string>();
  manifest.verticalDatum =
      root.value("verticalDatum", "WGS84-ellipsoidal");
  const auto& view = root.at("defaultView");
  manifest.defaultView = {
      view.at(0).get<double>(),
      view.at(1).get<double>(),
      view.at(2).get<double>(),
  };
  manifest.attribution =
      root.value("attribution", "(c) OpenStreetMap contributors");
  manifest.copyrightUrl =
      root.value("copyrightUrl", "https://www.openstreetmap.org/copyright");

  if (root.contains("capabilities")) {
    const auto& capabilities = root.at("capabilities");
    manifest.capabilities.requiresNetwork =
        capabilities.value("requiresNetwork", false);
    manifest.capabilities.supportsOfflineRouting =
        capabilities.value("offlineRouting", false);
    manifest.capabilities.supportsOfflineSearch =
        capabilities.value("offlineSearch", false);
    manifest.capabilities.supportsTransit =
        capabilities.value("transit", false);
    manifest.capabilities.supportsTraffic =
        capabilities.value("traffic", false);
  }

  for (const auto& item : root.at("layers")) {
    manifest.layers.push_back(
        {
            parseAtlasLayer(item.at("layer").get<std::string>()),
            item.at("contentTemplate").get<std::string>(),
            item.at("minimumLevel").get<uint8_t>(),
            item.at("maximumLevel").get<uint8_t>(),
            item.at("rootGeometricErrorMeters").get<double>(),
            item.value("optional", false),
        });
  }
  for (const auto& item : root.at("sources")) {
    manifest.sources.push_back(
        {
            item.at("name").get<std::string>(),
            item.value("url", ""),
            item.at("license").get<std::string>(),
            item.value("checksumSha256", ""),
            item.value("capturedAt", ""),
        });
  }
  validateDatasetManifest(manifest);
  return manifest;
}

void saveDatasetManifest(
    const AtlasDatasetManifest& manifest,
    const std::filesystem::path& path) {
  validateDatasetManifest(manifest);
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output{path};
  if (!output) {
    throw std::runtime_error("failed to write Atlas dataset: " + path.string());
  }
  output << toJson(manifest).dump(2) << '\n';
}

void validateDatasetManifest(const AtlasDatasetManifest& manifest) {
  if (manifest.formatVersion != 2) {
    throw std::runtime_error("Atlas dataset formatVersion must be 2");
  }
  if (manifest.datasetId.empty() || manifest.displayName.empty()) {
    throw std::runtime_error("Atlas dataset identity is incomplete");
  }
  if (manifest.layers.empty()) {
    throw std::runtime_error("Atlas dataset has no content layers");
  }
  if (manifest.sources.empty()) {
    throw std::runtime_error("Atlas dataset has no licensed sources");
  }
  if (manifest.attribution.empty() || manifest.copyrightUrl.empty()) {
    throw std::runtime_error("Atlas dataset attribution is required");
  }
  std::set<AtlasLayer> uniqueLayers;
  for (const auto& layer : manifest.layers) {
    if (!uniqueLayers.insert(layer.layer).second) {
      throw std::runtime_error("Atlas dataset contains a duplicate layer");
    }
    if (layer.maximumLevel < layer.minimumLevel ||
        layer.maximumLevel > 30 || layer.contentTemplate.empty()) {
      throw std::runtime_error("Atlas dataset layer is invalid");
    }
  }
  for (const auto& source : manifest.sources) {
    if (source.name.empty() || source.license.empty()) {
      throw std::runtime_error("Atlas dataset source lacks name or license");
    }
  }
  geodeticToEcef(manifest.defaultView);
}

std::optional<AtlasLayerManifest> findLayer(
    const AtlasDatasetManifest& manifest,
    AtlasLayer layer) {
  for (const auto& candidate : manifest.layers) {
    if (candidate.layer == layer) return candidate;
  }
  return std::nullopt;
}

}  // namespace vulkax::atlas
