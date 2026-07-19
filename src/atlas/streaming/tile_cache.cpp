#include "atlas/streaming/tile_cache.hpp"

#include "atlas/core/sha256.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace vulkax::atlas {
namespace {

using Json = nlohmann::json;

std::string cacheStem(const AtlasTileKey& key) {
  std::string value = key.toString();
  std::replace(value.begin(), value.end(), '/', '_');
  return value;
}

std::vector<uint8_t> readBytes(const std::filesystem::path& path) {
  std::ifstream input{path, std::ios::binary | std::ios::ate};
  if (!input) return {};
  const auto end = input.tellg();
  if (end < 0) return {};
  std::vector<uint8_t> bytes(static_cast<size_t>(end));
  input.seekg(0);
  input.read(
      reinterpret_cast<char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
  if (!input && !bytes.empty()) return {};
  return bytes;
}

void atomicWrite(
    const std::filesystem::path& path,
    const uint8_t* data,
    size_t size) {
  std::filesystem::create_directories(path.parent_path());
  const auto temporary = path.string() + ".part";
  {
    std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
    if (!output) throw std::runtime_error("failed to create cache temporary");
    output.write(
        reinterpret_cast<const char*>(data),
        static_cast<std::streamsize>(size));
    output.flush();
    if (!output) throw std::runtime_error("failed to write cache temporary");
  }
  std::filesystem::rename(temporary, path);
}

}  // namespace

TileCache::TileCache(std::filesystem::path root, uint64_t quotaBytes)
    : root{std::move(root)}, quotaBytes{quotaBytes} {
  if (quotaBytes == 0) throw std::invalid_argument("tile cache quota is zero");
  std::filesystem::create_directories(this->root);
}

std::filesystem::path TileCache::dataPath(const AtlasTileKey& key) const {
  return root / (cacheStem(key) + ".tile");
}

std::filesystem::path TileCache::metadataPath(const AtlasTileKey& key) const {
  return root / (cacheStem(key) + ".json");
}

std::optional<TilePayload> TileCache::read(const TileRequest& request) {
  const auto contentPath = dataPath(request.key);
  const auto sidecarPath = metadataPath(request.key);
  if (!std::filesystem::exists(contentPath) ||
      !std::filesystem::exists(sidecarPath)) {
    return std::nullopt;
  }

  std::ifstream metadataInput{sidecarPath};
  Json metadata;
  try {
    metadataInput >> metadata;
  } catch (...) {
    return std::nullopt;
  }
  if (!request.expectedSha256.empty() &&
      metadata.value("sha256", "") != request.expectedSha256) {
    return std::nullopt;
  }
  auto bytes = readBytes(contentPath);
  if (bytes.empty() && std::filesystem::file_size(contentPath) != 0) {
    return std::nullopt;
  }
  const std::string calculatedChecksum = sha256(bytes);
  const std::string storedChecksum = metadata.value("sha256", "");
  if (!storedChecksum.empty() && storedChecksum != calculatedChecksum) {
    return std::nullopt;
  }

  metadata["lastAccess"] =
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  std::ofstream metadataOutput{sidecarPath, std::ios::trunc};
  metadataOutput << metadata.dump(2) << '\n';
  return TilePayload{
      request.key,
      std::move(bytes),
      metadata.value("etag", ""),
      calculatedChecksum,
      false,
      true,
  };
}

void TileCache::write(
    const TileRequest& request,
    const TilePayload& payload) {
  if (payload.notModified) return;
  const std::string calculatedChecksum = sha256(payload.bytes);
  if (!request.expectedSha256.empty() &&
      request.expectedSha256 != calculatedChecksum) {
    throw std::runtime_error("tile payload checksum mismatch");
  }
  if (!payload.sha256.empty() && payload.sha256 != calculatedChecksum) {
    throw std::runtime_error("tile source supplied an invalid checksum");
  }
  const auto contentPath = dataPath(request.key);
  atomicWrite(contentPath, payload.bytes.data(), payload.bytes.size());

  const Json metadata = {
      {"uri", request.uri},
      {"etag", payload.etag},
      {"sha256", calculatedChecksum},
      {"bytes", payload.bytes.size()},
      {"lastAccess",
       std::chrono::duration_cast<std::chrono::seconds>(
           std::chrono::system_clock::now().time_since_epoch())
           .count()},
      {"pinned", false},
  };
  const std::string encoded = metadata.dump(2) + "\n";
  atomicWrite(
      metadataPath(request.key),
      reinterpret_cast<const uint8_t*>(encoded.data()),
      encoded.size());
  enforceQuota();
}

void TileCache::pin(const AtlasTileKey& key, bool value) {
  const auto path = metadataPath(key);
  std::ifstream input{path};
  if (!input) return;
  Json metadata;
  input >> metadata;
  metadata["pinned"] = value;
  const std::string encoded = metadata.dump(2) + "\n";
  atomicWrite(
      path,
      reinterpret_cast<const uint8_t*>(encoded.data()),
      encoded.size());
}

uint64_t TileCache::bytesUsed() const {
  uint64_t total = 0;
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    if (entry.is_regular_file() && entry.path().extension() == ".tile") {
      total += entry.file_size();
    }
  }
  return total;
}

void TileCache::enforceQuota() {
  struct Victim {
    std::filesystem::path data;
    std::filesystem::path metadata;
    int64_t lastAccess = 0;
    uint64_t bytes = 0;
  };
  uint64_t used = bytesUsed();
  if (used <= quotaBytes) return;

  std::vector<Victim> victims;
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json") {
      continue;
    }
    std::ifstream input{entry.path()};
    Json metadata;
    try {
      input >> metadata;
    } catch (...) {
      continue;
    }
    if (metadata.value("pinned", false)) continue;
    auto data = entry.path();
    data.replace_extension(".tile");
    if (!std::filesystem::exists(data)) continue;
    victims.push_back(
        {
            data,
            entry.path(),
            metadata.value("lastAccess", int64_t{0}),
            std::filesystem::file_size(data),
        });
  }
  std::sort(
      victims.begin(),
      victims.end(),
      [](const Victim& left, const Victim& right) {
        return left.lastAccess < right.lastAccess;
      });
  for (const auto& victim : victims) {
    if (used <= quotaBytes) break;
    std::filesystem::remove(victim.data);
    std::filesystem::remove(victim.metadata);
    used -= std::min(used, victim.bytes);
  }
}

}  // namespace vulkax::atlas
