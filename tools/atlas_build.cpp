#include "atlas/core/dataset.hpp"
#include "atlas/core/sha256.hpp"
#include "atlas/streaming/regional_pack.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Json = nlohmann::json;
using namespace vulkax::atlas;

Json readJson(const std::filesystem::path& path) {
  std::ifstream input{path};
  if (!input) throw std::runtime_error("failed to open " + path.string());
  Json value;
  input >> value;
  return value;
}

std::vector<uint8_t> readBytes(const std::filesystem::path& path) {
  std::ifstream input{path, std::ios::binary | std::ios::ate};
  if (!input) throw std::runtime_error("failed to open " + path.string());
  const auto end = input.tellg();
  if (end < 0) throw std::runtime_error("failed to size " + path.string());
  std::vector<uint8_t> bytes(static_cast<size_t>(end));
  input.seekg(0);
  input.read(
      reinterpret_cast<char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
  if (!input && !bytes.empty()) {
    throw std::runtime_error("failed to read " + path.string());
  }
  return bytes;
}

AtlasDatasetManifest manifestFromRegion(const Json& region) {
  AtlasDatasetManifest manifest{};
  manifest.datasetId = region.at("id").get<std::string>();
  manifest.displayName = region.at("name").get<std::string>();
  manifest.generatedAt = region.at("sourceCapturedAt").get<std::string>();
  manifest.verticalDatum =
      region.value("verticalDatum", "WGS84-ellipsoidal");
  const auto& view = region.at("defaultView");
  manifest.defaultView = {
      view.at(0).get<double>(),
      view.at(1).get<double>(),
      view.at(2).get<double>(),
  };
  manifest.capabilities.requiresNetwork = false;
  manifest.capabilities.supportsOfflineRouting = true;
  manifest.capabilities.supportsOfflineSearch = true;
  manifest.capabilities.supportsTransit = true;
  manifest.capabilities.supportsTraffic = true;
  for (AtlasLayer layer : {
           AtlasLayer::Terrain,
           AtlasLayer::Buildings,
           AtlasLayer::Roads,
           AtlasLayer::LandUse,
           AtlasLayer::Water,
           AtlasLayer::Vegetation,
           AtlasLayer::Poi,
           AtlasLayer::Labels,
           AtlasLayer::Transit,
           AtlasLayer::Traffic,
       }) {
    const bool optional =
        layer == AtlasLayer::Transit || layer == AtlasLayer::Traffic;
    manifest.layers.push_back(
        {
            layer,
            "content/" + std::string{toString(layer)} +
                "/{face}/{level}/{x}/{y}.glb",
            0,
            static_cast<uint8_t>(
                layer == AtlasLayer::Terrain ? 18 : 20),
            layer == AtlasLayer::Terrain ? 2000000.0 : 500000.0,
            optional,
        });
  }
  for (const auto& source : region.at("sources")) {
    manifest.sources.push_back(
        {
            source.at("name").get<std::string>(),
            source.value("url", ""),
            source.at("license").get<std::string>(),
            source.value("checksumSha256", ""),
            source.value("capturedAt", manifest.generatedAt),
        });
  }
  return manifest;
}

void generateManifest(
    const std::filesystem::path& regionPath,
    const std::filesystem::path& outputPath) {
  const Json region = readJson(regionPath);
  AtlasDatasetManifest manifest = manifestFromRegion(region);
  saveDatasetManifest(manifest, outputPath);

  Json tileset{
      {"asset", {{"version", "1.1"}, {"tilesetVersion", "Vulkax-Atlas-2"}}},
      {"geometricError", 2000000.0},
      {"root",
       {
           {"boundingVolume",
            {{"box",
              {0.0, 0.0, 0.0,
               6378137.0, 0.0, 0.0,
               0.0, 6378137.0, 0.0,
               0.0, 0.0, 6378137.0}}}},
           {"geometricError", 2000000.0},
           {"refine", "REPLACE"},
           {"children", Json::array()},
       }},
      {"extras",
       {
           {"datasetId", manifest.datasetId},
           {"attribution", manifest.attribution},
           {"cubeFaceRoots", 6},
       }},
  };
  for (CubeFace face : {
           CubeFace::PositiveX,
           CubeFace::NegativeX,
           CubeFace::PositiveY,
           CubeFace::NegativeY,
           CubeFace::PositiveZ,
           CubeFace::NegativeZ,
       }) {
    tileset["root"]["children"].push_back(
        {
            {"boundingVolume",
             {{"box",
               {0.0, 0.0, 0.0,
                6378137.0, 0.0, 0.0,
                0.0, 6378137.0, 0.0,
                0.0, 0.0, 6378137.0}}}},
            {"geometricError", 2000000.0},
            {"refine", "REPLACE"},
            {"implicitTiling",
             {
                 {"subdivisionScheme", "QUADTREE"},
                 {"subtreeLevels", 5},
                 {"availableLevels", 19},
                 {"subtrees",
                  {{"uri",
                    "subtrees/" + std::string{toString(face)} +
                        "/{level}/{x}/{y}.subtree"}}},
             }},
            {"content",
             {{"uri",
               "content/terrain/" + std::string{toString(face)} +
                   "/{level}/{x}/{y}.glb"}}},
            {"extras", {{"cubeFace", toString(face)}}},
        });
  }
  std::ofstream output{outputPath.parent_path() / "tileset.json"};
  output << tileset.dump(2) << '\n';
  std::cout << "generated Atlas dataset " << manifest.datasetId << " at "
            << outputPath << '\n';
}

void validateManifest(const std::filesystem::path& path) {
  const auto manifest = loadDatasetManifest(path);
  std::cout << "valid Atlas dataset " << manifest.datasetId << " with "
            << manifest.layers.size() << " layers and "
            << manifest.sources.size() << " licensed sources\n";
}

void packGeoBeacon(
    const std::filesystem::path& geoManifestPath,
    const std::filesystem::path& packPath) {
  const Json root = readJson(geoManifestPath);
  if (root.value("format", "") != "GeoBEACON-runtime-1") {
    throw std::runtime_error("input is not a GeoBEACON runtime manifest");
  }
  const auto& originJson = root.at("originWgs84");
  const GeodeticPosition origin{
      originJson.at(0).get<double>(),
      originJson.at(1).get<double>(),
      originJson.at(2).get<double>(),
  };
  const LocalFrame localFrame = makeLocalFrame(origin);
  const auto base = geoManifestPath.parent_path();
  AtlasPackWriter writer{packPath};
  writer.begin();
  writer.setMetadata("format", "Vulkax-Atlas-pack-1");
  writer.setMetadata("datasetId", "connaught-place-atlas");
  writer.setMetadata("sourceChecksum", root.at("sourceChecksum").get<std::string>());
  writer.setMetadata(
      "attribution",
      root.value("attribution", "(c) OpenStreetMap contributors"));

  uint64_t tileCount = 0;
  for (const auto& tile : root.at("tiles")) {
    const auto& bounds = tile.at("bounds");
    const glm::dvec3 localCenter{
        (bounds.at(0).get<double>() + bounds.at(3).get<double>()) * 0.5,
        (bounds.at(2).get<double>() + bounds.at(5).get<double>()) * 0.5,
        -(bounds.at(1).get<double>() + bounds.at(4).get<double>()) * 0.5,
    };
    const EcefPosition center = localFrame.toEcef(localCenter);
    for (const auto& representation : tile.at("representations")) {
      const uint8_t lod = representation.at("lod").get<uint8_t>();
      const AtlasTileKey key = directionToTile(
          center.meters,
          static_cast<uint8_t>(18 + lod),
          AtlasLayer::Buildings);
      const auto relative = representation.at("uri").get<std::string>();
      TileRequest request{key, relative};
      TilePayload payload{};
      payload.key = key;
      payload.bytes = readBytes(base / relative);
      payload.etag =
          std::to_string(payload.bytes.size()) + "-" + std::to_string(lod);
      payload.sha256 = sha256(payload.bytes);
      writer.putTile(request, payload);
      tileCount++;
    }
  }
  writer.commit();
  std::cout << "packed " << tileCount << " GeoBEACON representations into "
            << packPath << '\n';
}

void usage() {
  std::cerr
      << "Usage:\n"
      << "  atlas-build generate-manifest <region.json> <atlas-dataset.json>\n"
      << "  atlas-build validate <atlas-dataset.json>\n"
      << "  atlas-build pack-geobeacon <geobeacon.json> <output.vxa>\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 4 && std::string{argv[1]} == "generate-manifest") {
      generateManifest(argv[2], argv[3]);
      return 0;
    }
    if (argc == 3 && std::string{argv[1]} == "validate") {
      validateManifest(argv[2]);
      return 0;
    }
    if (argc == 4 && std::string{argv[1]} == "pack-geobeacon") {
      packGeoBeacon(argv[2], argv[3]);
      return 0;
    }
    usage();
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "atlas-build: " << error.what() << '\n';
    return 1;
  }
}
