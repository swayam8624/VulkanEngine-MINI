#include "atlas/atlas_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace vulkax::atlas {
namespace {

void replaceAll(
    std::string& value,
    const std::string& needle,
    const std::string& replacement) {
  size_t offset = 0;
  while ((offset = value.find(needle, offset)) != std::string::npos) {
    value.replace(offset, needle.size(), replacement);
    offset += replacement.size();
  }
}

}  // namespace

AtlasRuntime::AtlasRuntime(
    AtlasDatasetManifest dataset,
    std::shared_ptr<TileSource> source,
    std::shared_ptr<TileCache> cache,
    AtlasBudgetConfig budget)
    : dataset{std::move(dataset)},
      source{std::move(source)},
      cache{std::move(cache)},
      tileScheduler{budget} {
  validateDatasetManifest(this->dataset);
  if (!this->source) {
    throw std::invalid_argument("Atlas runtime requires a tile source");
  }
}

AtlasRuntime::~AtlasRuntime() {
  cancelAll();
  for (auto& [_, request] : pending) {
    request.future.wait();
  }
}

std::string AtlasRuntime::contentUri(const AtlasTileKey& key) const {
  const auto layer = findLayer(dataset, key.layer);
  if (!layer) {
    throw std::runtime_error(
        "Atlas dataset does not provide layer " +
        std::string{toString(key.layer)});
  }
  std::string uri = layer->contentTemplate;
  replaceAll(uri, "{face}", toString(key.face));
  replaceAll(uri, "{level}", std::to_string(key.level));
  replaceAll(uri, "{x}", std::to_string(key.x));
  replaceAll(uri, "{y}", std::to_string(key.y));
  return uri;
}

void AtlasRuntime::requestTile(
    const AtlasTileDecision& decision,
    uint64_t frameNumber) {
  const std::string identity = decision.key.toString();
  if (const auto found = pending.find(identity); found != pending.end()) {
    found->second.lastWantedFrame = frameNumber;
    return;
  }
  if (retryAfterFrame[identity] > frameNumber) return;

  TileRequest request{};
  request.key = decision.key;
  request.uri = contentUri(decision.key);
  request.priority = decision.priority;
  request.deadline =
      std::chrono::steady_clock::now() +
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(
              std::max(0.0, decision.secondsUntilNeeded)));

  if (cache) {
    if (auto cached = cache->read(request)) {
      ready.push_back(std::move(*cached));
      runtimeStats.cacheHits++;
      return;
    }
    runtimeStats.cacheMisses++;
  }

  auto cancellation = std::make_shared<CancellationToken>();
  auto future = source->request(request, cancellation);
  pending.emplace(
      identity,
      PendingRequest{
          std::move(request),
          std::move(cancellation),
          std::move(future),
          frameNumber,
      });
}

void AtlasRuntime::integrateCompleted(uint64_t frameNumber) {
  for (auto iterator = pending.begin(); iterator != pending.end();) {
    auto& pendingRequest = iterator->second;
    const auto status = pendingRequest.future.wait_for(std::chrono::seconds{0});
    if (status == std::future_status::timeout) {
      ++iterator;
      continue;
    }

    const std::string identity = iterator->first;
    try {
      TilePayload payload = pendingRequest.future.get();
      if (payload.notModified && cache) {
        auto cached = cache->read(pendingRequest.request);
        if (!cached) {
          throw std::runtime_error(
              "tile source returned not-modified without cached content");
        }
        payload = std::move(*cached);
        runtimeStats.cacheHits++;
      } else {
        runtimeStats.downloadedBytes += payload.bytes.size();
        if (cache) cache->write(pendingRequest.request, payload);
      }
      ready.push_back(std::move(payload));
      runtimeStats.completedRequests++;
      failureCounts.erase(identity);
      retryAfterFrame.erase(identity);
    } catch (...) {
      if (pendingRequest.cancellation->isCancelled()) {
        runtimeStats.cancelledRequests++;
      } else {
        runtimeStats.failedRequests++;
        const uint32_t failures =
            std::min(10u, ++failureCounts[identity]);
        retryAfterFrame[identity] =
            frameNumber + std::min<uint64_t>(600, 1ull << failures);
      }
    }
    iterator = pending.erase(iterator);
  }
}

void AtlasRuntime::cancelStale(uint64_t frameNumber) {
  constexpr uint64_t staleFrames = 30;
  for (auto& [_, request] : pending) {
    if (frameNumber > request.lastWantedFrame + staleFrames) {
      request.cancellation->cancel();
    }
  }
}

void AtlasRuntime::update(
    const AtlasFrameContext& frame,
    const std::vector<AtlasTileCandidate>& candidates,
    const RoutePrediction* route) {
  runtimeStats.completedRequests = 0;
  runtimeStats.failedRequests = 0;
  runtimeStats.cancelledRequests = 0;
  runtimeStats.cacheHits = 0;
  runtimeStats.cacheMisses = 0;
  runtimeStats.downloadedBytes = 0;

  integrateCompleted(frame.frameNumber);
  const auto decisions = tileScheduler.select(frame, candidates, route);
  runtimeStats.scheduler = tileScheduler.stats();
  for (const auto& decision : decisions) {
    requestTile(decision, frame.frameNumber);
  }
  cancelStale(frame.frameNumber);
  runtimeStats.pendingRequests = static_cast<uint32_t>(pending.size());
}

std::vector<TilePayload> AtlasRuntime::takeReadyTiles() {
  std::vector<TilePayload> output;
  output.reserve(ready.size());
  while (!ready.empty()) {
    output.push_back(std::move(ready.front()));
    ready.pop_front();
  }
  return output;
}

void AtlasRuntime::cancelAll() {
  for (auto& [_, request] : pending) request.cancellation->cancel();
}

}  // namespace vulkax::atlas
