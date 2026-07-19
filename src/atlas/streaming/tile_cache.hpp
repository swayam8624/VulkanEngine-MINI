#pragma once

#include "atlas/streaming/tile_source.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace vulkax::atlas {

struct TileCacheEntry {
  AtlasTileKey key{};
  std::filesystem::path dataPath;
  std::string etag;
  std::string sha256;
  uint64_t bytes = 0;
  std::chrono::system_clock::time_point lastAccess{};
  bool pinned = false;
};

class TileCache {
 public:
  TileCache(std::filesystem::path root, uint64_t quotaBytes);

  std::optional<TilePayload> read(const TileRequest& request);
  void write(const TileRequest& request, const TilePayload& payload);
  void pin(const AtlasTileKey& key, bool value);
  void enforceQuota();

  uint64_t bytesUsed() const;
  uint64_t quota() const { return quotaBytes; }
  const std::filesystem::path& directory() const { return root; }

 private:
  std::filesystem::path dataPath(const AtlasTileKey& key) const;
  std::filesystem::path metadataPath(const AtlasTileKey& key) const;

  std::filesystem::path root;
  uint64_t quotaBytes;
};

}  // namespace vulkax::atlas
