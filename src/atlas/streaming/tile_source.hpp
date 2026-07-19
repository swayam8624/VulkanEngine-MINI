#pragma once

#include "atlas/core/tile_key.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace vulkax::atlas {

struct TileRequest {
  AtlasTileKey key{};
  std::string uri;
  std::string expectedSha256;
  std::string knownEtag;
  double priority = 0.0;
  std::chrono::steady_clock::time_point deadline{};
};

struct TilePayload {
  AtlasTileKey key{};
  std::vector<uint8_t> bytes;
  std::string etag;
  std::string sha256;
  bool notModified = false;
  bool fromCache = false;
};

class CancellationToken {
 public:
  void cancel() { cancelled.store(true); }
  bool isCancelled() const { return cancelled.load(); }

 private:
  std::atomic<bool> cancelled{false};
};

class TileSource {
 public:
  virtual ~TileSource() = default;
  virtual std::future<TilePayload> request(
      TileRequest request,
      std::shared_ptr<CancellationToken> cancellation) = 0;
};

class FileTileSource final : public TileSource {
 public:
  explicit FileTileSource(std::filesystem::path root);

  std::future<TilePayload> request(
      TileRequest request,
      std::shared_ptr<CancellationToken> cancellation) override;

 private:
  std::filesystem::path root;
};

class MemoryTileSource final : public TileSource {
 public:
  void put(std::string uri, std::vector<uint8_t> bytes, std::string etag = {});

  std::future<TilePayload> request(
      TileRequest request,
      std::shared_ptr<CancellationToken> cancellation) override;

 private:
  struct Entry {
    std::vector<uint8_t> bytes;
    std::string etag;
  };
  std::unordered_map<std::string, Entry> entries;
};

struct HttpResponse {
  uint32_t status = 0;
  std::vector<uint8_t> body;
  std::string etag;
};

class HttpTransport {
 public:
  virtual ~HttpTransport() = default;
  virtual HttpResponse get(
      const std::string& uri,
      const std::string& knownEtag,
      const CancellationToken& cancellation) = 0;
  virtual HttpResponse postJson(
      const std::string& uri,
      const std::string& json,
      const CancellationToken& cancellation) = 0;
};

class HttpTileSource final : public TileSource {
 public:
  explicit HttpTileSource(std::shared_ptr<HttpTransport> transport);

  std::future<TilePayload> request(
      TileRequest request,
      std::shared_ptr<CancellationToken> cancellation) override;

 private:
  std::shared_ptr<HttpTransport> transport;
};

class CurlHttpTransport final : public HttpTransport {
 public:
  explicit CurlHttpTransport(
      std::chrono::milliseconds timeout = std::chrono::seconds{30});

  HttpResponse get(
      const std::string& uri,
      const std::string& knownEtag,
      const CancellationToken& cancellation) override;
  HttpResponse postJson(
      const std::string& uri,
      const std::string& json,
      const CancellationToken& cancellation) override;

 private:
  HttpResponse perform(
      const std::string& uri,
      const char* method,
      const std::string& requestBody,
      const std::string& knownEtag,
      const CancellationToken& cancellation);

  std::chrono::milliseconds timeout;
};

}  // namespace vulkax::atlas
