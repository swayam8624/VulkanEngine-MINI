#include "atlas/streaming/tile_source.hpp"

#include "atlas/core/sha256.hpp"
#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace vulkax::atlas {
namespace {

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
  std::ifstream input{path, std::ios::binary | std::ios::ate};
  if (!input) throw std::runtime_error("tile file is unavailable: " + path.string());
  const auto end = input.tellg();
  if (end < 0) throw std::runtime_error("failed to size tile file: " + path.string());
  std::vector<uint8_t> bytes(static_cast<size_t>(end));
  input.seekg(0);
  input.read(
      reinterpret_cast<char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
  if (!input && !bytes.empty()) {
    throw std::runtime_error("failed to read tile file: " + path.string());
  }
  return bytes;
}

void ensureActive(const std::shared_ptr<CancellationToken>& cancellation) {
  if (cancellation && cancellation->isCancelled()) {
    throw std::runtime_error("tile request cancelled");
  }
}

void initializeCurl() {
  static std::once_flag once;
  std::call_once(once, [] {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
      throw std::runtime_error("failed to initialize libcurl");
    }
  });
}

size_t appendBody(
    char* data,
    size_t size,
    size_t count,
    void* userData) {
  auto& body = *static_cast<std::vector<uint8_t>*>(userData);
  const size_t bytes = size * count;
  body.insert(
      body.end(),
      reinterpret_cast<uint8_t*>(data),
      reinterpret_cast<uint8_t*>(data) + bytes);
  return bytes;
}

size_t parseHeader(
    char* data,
    size_t size,
    size_t count,
    void* userData) {
  const size_t bytes = size * count;
  std::string header{data, bytes};
  std::string normalized = header;
  std::transform(
      normalized.begin(),
      normalized.end(),
      normalized.begin(),
      [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
      });
  if (normalized.rfind("etag:", 0) == 0) {
    auto& etag = *static_cast<std::string*>(userData);
    const size_t begin = header.find_first_not_of(" \t", 5);
    const size_t end = header.find_last_not_of(" \t\r\n");
    etag =
        begin == std::string::npos ? std::string{}
                                   : header.substr(begin, end - begin + 1);
  }
  return bytes;
}

int cancelTransfer(
    void* userData,
    curl_off_t,
    curl_off_t,
    curl_off_t,
    curl_off_t) {
  return static_cast<const CancellationToken*>(userData)->isCancelled()
             ? 1
             : 0;
}

}  // namespace

FileTileSource::FileTileSource(std::filesystem::path root)
    : root{std::move(root)} {}

std::future<TilePayload> FileTileSource::request(
    TileRequest request,
    std::shared_ptr<CancellationToken> cancellation) {
  return std::async(
      std::launch::async,
      [root = root,
       request = std::move(request),
       cancellation = std::move(cancellation)] {
        ensureActive(cancellation);
        std::filesystem::path relative{request.uri};
        if (relative.is_absolute()) {
          throw std::runtime_error("file tile URI must be relative");
        }
        const auto path = std::filesystem::weakly_canonical(root / relative);
        const auto canonicalRoot = std::filesystem::weakly_canonical(root);
        const auto relativeToRoot = path.lexically_relative(canonicalRoot);
        if (relativeToRoot.empty() || relativeToRoot.native().starts_with("..")) {
          throw std::runtime_error("file tile URI escapes its source root");
        }
        auto bytes = readFile(path);
        ensureActive(cancellation);
        return TilePayload{request.key, std::move(bytes)};
      });
}

void MemoryTileSource::put(
    std::string uri,
    std::vector<uint8_t> bytes,
    std::string etag) {
  entries[std::move(uri)] = {std::move(bytes), std::move(etag)};
}

std::future<TilePayload> MemoryTileSource::request(
    TileRequest request,
    std::shared_ptr<CancellationToken> cancellation) {
  return std::async(
      std::launch::deferred,
      [this,
       request = std::move(request),
       cancellation = std::move(cancellation)] {
        ensureActive(cancellation);
        const auto found = entries.find(request.uri);
        if (found == entries.end()) {
          throw std::runtime_error("memory tile is unavailable: " + request.uri);
        }
        if (!request.knownEtag.empty() &&
            request.knownEtag == found->second.etag) {
          return TilePayload{
              request.key, {}, found->second.etag, {}, true, false};
        }
        return TilePayload{
            request.key,
            found->second.bytes,
            found->second.etag,
            {},
            false,
            false,
        };
      });
}

HttpTileSource::HttpTileSource(std::shared_ptr<HttpTransport> transport)
    : transport{std::move(transport)} {
  if (!this->transport) {
    throw std::invalid_argument("HTTP tile source requires a transport");
  }
}

std::future<TilePayload> HttpTileSource::request(
    TileRequest request,
    std::shared_ptr<CancellationToken> cancellation) {
  if (!cancellation) cancellation = std::make_shared<CancellationToken>();
  return std::async(
      std::launch::async,
      [transport = transport,
       request = std::move(request),
       cancellation = std::move(cancellation)] {
        ensureActive(cancellation);
        const HttpResponse response =
            transport->get(request.uri, request.knownEtag, *cancellation);
        ensureActive(cancellation);
        if (response.status == 304) {
          return TilePayload{
              request.key, {}, response.etag, {}, true, false};
        }
        if (response.status < 200 || response.status >= 300) {
          throw std::runtime_error(
              "HTTP tile request failed with status " +
              std::to_string(response.status));
        }
        return TilePayload{
            request.key,
            response.body,
            response.etag,
            sha256(response.body),
            false,
            false,
        };
      });
}

CurlHttpTransport::CurlHttpTransport(std::chrono::milliseconds timeout)
    : timeout{timeout} {
  if (timeout.count() <= 0) {
    throw std::invalid_argument("HTTP timeout must be positive");
  }
  initializeCurl();
}

HttpResponse CurlHttpTransport::get(
    const std::string& uri,
    const std::string& knownEtag,
    const CancellationToken& cancellation) {
  return perform(uri, "GET", {}, knownEtag, cancellation);
}

HttpResponse CurlHttpTransport::postJson(
    const std::string& uri,
    const std::string& json,
    const CancellationToken& cancellation) {
  return perform(uri, "POST", json, {}, cancellation);
}

HttpResponse CurlHttpTransport::perform(
    const std::string& uri,
    const char* method,
    const std::string& requestBody,
    const std::string& knownEtag,
    const CancellationToken& cancellation) {
  if (cancellation.isCancelled()) {
    throw std::runtime_error("HTTP request cancelled");
  }
  CURL* handle = curl_easy_init();
  if (handle == nullptr) throw std::runtime_error("failed to create curl handle");
  struct CurlCleanup {
    CURL* handle;
    ~CurlCleanup() { curl_easy_cleanup(handle); }
  } cleanup{handle};

  HttpResponse response{};
  curl_easy_setopt(handle, CURLOPT_URL, uri.c_str());
  curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 5L);
  curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, timeout.count());
  curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, timeout.count());
  curl_easy_setopt(handle, CURLOPT_USERAGENT, "VulkaxAtlas/0.24");
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, appendBody);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response.body);
  curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, parseHeader);
  curl_easy_setopt(handle, CURLOPT_HEADERDATA, &response.etag);
  curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, cancelTransfer);
  curl_easy_setopt(
      handle,
      CURLOPT_XFERINFODATA,
      const_cast<CancellationToken*>(&cancellation));
  curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);

  curl_slist* headers = nullptr;
  if (!knownEtag.empty()) {
    headers =
        curl_slist_append(
            headers, ("If-None-Match: " + knownEtag).c_str());
  }
  if (std::string{method} == "POST") {
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(handle, CURLOPT_POST, 1L);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, requestBody.data());
    curl_easy_setopt(
        handle,
        CURLOPT_POSTFIELDSIZE_LARGE,
        static_cast<curl_off_t>(requestBody.size()));
  }
  struct HeaderCleanup {
    curl_slist* headers;
    ~HeaderCleanup() { curl_slist_free_all(headers); }
  } headerCleanup{headers};
  if (headers != nullptr) curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);

  const CURLcode result = curl_easy_perform(handle);
  if (result != CURLE_OK) {
    throw std::runtime_error(
        "HTTP request failed: " + std::string{curl_easy_strerror(result)});
  }
  long status = 0;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
  response.status = static_cast<uint32_t>(status);
  return response;
}

}  // namespace vulkax::atlas
