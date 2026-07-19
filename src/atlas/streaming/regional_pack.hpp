#pragma once

#include "atlas/core/dataset.hpp"
#include "atlas/navigation/navigation.hpp"
#include "atlas/streaming/tile_source.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace vulkax::atlas {

class AtlasPackWriter {
 public:
  explicit AtlasPackWriter(const std::filesystem::path& path);
  ~AtlasPackWriter();

  AtlasPackWriter(const AtlasPackWriter&) = delete;
  AtlasPackWriter& operator=(const AtlasPackWriter&) = delete;

  void begin();
  void commit();
  void rollback();
  void setMetadata(std::string key, std::string value);
  void putTile(const TileRequest& request, const TilePayload& payload);
  void putPoi(const SearchResult& result);
  void putAsset(
      std::string name,
      std::span<const uint8_t> bytes,
      std::string sha256);

 private:
  struct Impl;
  Impl* impl = nullptr;
};

class AtlasPackTileSource final : public TileSource {
 public:
  explicit AtlasPackTileSource(std::filesystem::path path);

  std::future<TilePayload> request(
      TileRequest request,
      std::shared_ptr<CancellationToken> cancellation) override;

  std::vector<SearchResult> searchOffline(
      const std::string& query,
      uint32_t limit) const;
  std::optional<std::vector<uint8_t>> asset(const std::string& name) const;
  std::optional<std::string> metadata(const std::string& key) const;

 private:
  std::filesystem::path path;
};

}  // namespace vulkax::atlas
