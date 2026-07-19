#include "geobeacon/geo_scene.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace lve::geo {
namespace {

using Clock = std::chrono::steady_clock;

template <typename T>
T readScalar(const std::vector<uint8_t>& data, size_t offset) {
  if (offset + sizeof(T) > data.size()) throw std::runtime_error("GLB read exceeds binary chunk");
  T value{};
  std::memcpy(&value, data.data() + offset, sizeof(T));
  return value;
}

uint32_t representationIndex(TileRepresentation value) {
  return static_cast<uint32_t>(value);
}

TileRepresentation representationFor(uint32_t lod) {
  return static_cast<TileRepresentation>(std::min(lod, 2u));
}

double percentile95(std::vector<double> values) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  size_t index = static_cast<size_t>(std::ceil(0.95 * values.size())) - 1;
  return values[std::min(index, values.size() - 1)];
}

}  // namespace

GeoDatasetManifest GeoScene::loadManifest(const std::filesystem::path& path) {
  std::ifstream input{path};
  if (!input) throw std::runtime_error("failed to open GeoBEACON manifest: " + path.string());
  nlohmann::json root;
  input >> root;
  if (root.value("format", "") != "GeoBEACON-runtime-1") {
    throw std::runtime_error("unsupported GeoBEACON runtime manifest");
  }

  GeoDatasetManifest manifest{};
  auto origin = root.at("originWgs84");
  manifest.geodeticOrigin = {origin.at(0).get<double>(), origin.at(1).get<double>(), origin.at(2).get<double>()};
  manifest.sourceAttribution = root.value("attribution", "© OpenStreetMap contributors");
  manifest.copyrightUrl = root.value("copyrightUrl", "https://www.openstreetmap.org/copyright");
  manifest.sourceChecksum = root.at("sourceChecksum").get<std::string>();
  manifest.localBounds.min = glm::dvec3{std::numeric_limits<double>::max()};
  manifest.localBounds.max = glm::dvec3{std::numeric_limits<double>::lowest()};
  for (const auto& item : root.at("tiles")) {
    GeoTileRecord tile{};
    tile.tileId = item.at("tileId").get<uint64_t>();
    auto bounds = item.at("bounds");
    tile.bounds.min = {bounds.at(0).get<double>(), bounds.at(1).get<double>(), bounds.at(2).get<double>()};
    tile.bounds.max = {bounds.at(3).get<double>(), bounds.at(4).get<double>(), bounds.at(5).get<double>()};
    tile.semanticImportance = item.value("semanticImportance", 1.f);
    for (const auto& representation : item.at("representations")) {
      uint32_t lod = representation.at("lod").get<uint32_t>();
      if (lod >= tile.representations.size()) throw std::runtime_error("invalid GeoBEACON LOD");
      tile.representations[lod] = {
          representationFor(lod),
          representation.at("uri").get<std::string>(),
          representation.at("bytes").get<uint64_t>(),
          representation.at("vertexCount").get<uint32_t>(),
          representation.at("indexCount").get<uint32_t>(),
          representation.at("geometricError").get<float>()};
    }
    manifest.localBounds.min = glm::min(manifest.localBounds.min, tile.bounds.min);
    manifest.localBounds.max = glm::max(manifest.localBounds.max, tile.bounds.max);
    manifest.tiles.push_back(std::move(tile));
  }
  return manifest;
}

LveModel::Builder GeoScene::loadGlb(const std::filesystem::path& path) {
  std::ifstream input{path, std::ios::binary | std::ios::ate};
  if (!input) throw std::runtime_error("failed to open tile GLB: " + path.string());
  size_t size = static_cast<size_t>(input.tellg());
  input.seekg(0);
  std::vector<uint8_t> bytes(size);
  input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (bytes.size() < 28 || std::memcmp(bytes.data(), "glTF", 4) != 0 || readScalar<uint32_t>(bytes, 4) != 2) {
    throw std::runtime_error("invalid GLB header: " + path.string());
  }
  uint32_t jsonLength = readScalar<uint32_t>(bytes, 12);
  if (readScalar<uint32_t>(bytes, 16) != 0x4E4F534A) throw std::runtime_error("GLB JSON chunk missing");
  nlohmann::json document = nlohmann::json::parse(
      reinterpret_cast<const char*>(bytes.data() + 20),
      reinterpret_cast<const char*>(bytes.data() + 20 + jsonLength));
  size_t binaryHeader = 20 + jsonLength;
  uint32_t binaryLength = readScalar<uint32_t>(bytes, binaryHeader);
  if (readScalar<uint32_t>(bytes, binaryHeader + 4) != 0x004E4942) {
    throw std::runtime_error("GLB binary chunk missing");
  }
  size_t binaryStart = binaryHeader + 8;
  if (binaryStart + binaryLength > bytes.size()) throw std::runtime_error("truncated GLB binary chunk");

  const auto& primitive = document.at("meshes").at(0).at("primitives").at(0);
  uint32_t positionAccessorIndex = primitive.at("attributes").at("POSITION").get<uint32_t>();
  uint32_t normalAccessorIndex = primitive.at("attributes").at("NORMAL").get<uint32_t>();
  uint32_t colorAccessorIndex = primitive.at("attributes").at("COLOR_0").get<uint32_t>();
  uint32_t indexAccessorIndex = primitive.at("indices").get<uint32_t>();
  const auto& accessors = document.at("accessors");
  const auto& views = document.at("bufferViews");
  const auto& positionAccessor = accessors.at(positionAccessorIndex);
  const auto& normalAccessor = accessors.at(normalAccessorIndex);
  const auto& colorAccessor = accessors.at(colorAccessorIndex);
  const auto& indexAccessor = accessors.at(indexAccessorIndex);
  const auto& vertexView = views.at(positionAccessor.at("bufferView").get<uint32_t>());
  const auto& indexView = views.at(indexAccessor.at("bufferView").get<uint32_t>());
  uint32_t vertexCount = positionAccessor.at("count").get<uint32_t>();
  uint32_t indexCount = indexAccessor.at("count").get<uint32_t>();
  size_t vertexBase = binaryStart + vertexView.value("byteOffset", 0u);
  size_t stride = vertexView.value("byteStride", 40u);
  size_t positionOffset = positionAccessor.value("byteOffset", 0u);
  size_t normalOffset = normalAccessor.value("byteOffset", 0u);
  size_t colorOffset = colorAccessor.value("byteOffset", 0u);
  size_t indexBase = binaryStart + indexView.value("byteOffset", 0u) + indexAccessor.value("byteOffset", 0u);

  LveModel::Builder builder{};
  builder.vertices.reserve(vertexCount);
  for (uint32_t i = 0; i < vertexCount; ++i) {
    size_t base = vertexBase + static_cast<size_t>(i) * stride;
    LveModel::Vertex vertex{};
    vertex.position = {
        readScalar<float>(bytes, base + positionOffset),
        readScalar<float>(bytes, base + positionOffset + 4),
        readScalar<float>(bytes, base + positionOffset + 8)};
    vertex.normal = {
        readScalar<float>(bytes, base + normalOffset),
        readScalar<float>(bytes, base + normalOffset + 4),
        readScalar<float>(bytes, base + normalOffset + 8)};
    vertex.color = {
        readScalar<float>(bytes, base + colorOffset),
        readScalar<float>(bytes, base + colorOffset + 4),
        readScalar<float>(bytes, base + colorOffset + 8)};
    builder.vertices.push_back(vertex);
  }
  builder.indices.reserve(indexCount);
  for (uint32_t i = 0; i < indexCount; ++i) {
    builder.indices.push_back(readScalar<uint32_t>(bytes, indexBase + static_cast<size_t>(i) * 4));
  }
  return builder;
}

GeoScene::GeoScene(
    LveDevice& device,
    std::filesystem::path manifestPath,
    beacon::GeoRenderPolicy policy,
    beacon::GeoCacheMode cacheMode,
    GeoBudgetConfig budget,
    bool forceMaximumLod)
    : device{device},
      manifestPath{std::move(manifestPath)},
      baseDirectory{this->manifestPath.parent_path()},
      dataset{loadManifest(this->manifestPath)},
      policy{policy},
      cacheMode{cacheMode},
      budget{budget},
      forceMaximumLod{forceMaximumLod} {
  runtime.reserve(dataset.tiles.size());
  for (size_t i = 0; i < dataset.tiles.size(); ++i) runtime.push_back(std::make_unique<TileRuntime>());
  uint32_t workerCount = std::max(1u, std::min(4u, std::thread::hardware_concurrency() / 2u));
  for (uint32_t i = 0; i < workerCount; ++i) workers.emplace_back(&GeoScene::workerMain, this);
  uploadTokens = cacheMode == beacon::GeoCacheMode::Warm
                     ? static_cast<double>(budget.gpuMemoryBudgetBytes)
                     : 0.0;
}

GeoScene::~GeoScene() {
  stopping = true;
  condition.notify_all();
  for (auto& worker : workers) {
    if (worker.joinable()) worker.join();
  }
  vkQueueWaitIdle(device.graphicsQueue());
}

void GeoScene::workerMain() {
  while (!stopping) {
    LoadRequest request{};
    {
      std::unique_lock lock{mutex};
      condition.wait(lock, [&] { return stopping || !requests.empty(); });
      if (stopping) return;
      request = requests.front();
      requests.pop_front();
    }
    CompletedLoad result{};
    result.tileIndex = request.tileIndex;
    result.lod = request.lod;
    result.generation = request.generation;
    result.queuedAt = request.queuedAt;
    try {
      auto path = baseDirectory / dataset.tiles[request.tileIndex].representations[request.lod].uri;
      result.builder = std::make_unique<LveModel::Builder>(loadGlb(path));
    } catch (const std::exception& error) {
      result.error = error.what();
    }
    {
      std::lock_guard lock{mutex};
      completed.push_back(std::move(result));
    }
  }
}

bool GeoScene::visible(const GeoTileRecord& tile, const glm::mat4& viewProjection) const {
  glm::vec3 center = glm::vec3{(tile.bounds.min + tile.bounds.max) * 0.5};
  glm::vec3 extent = glm::vec3{(tile.bounds.max - tile.bounds.min) * 0.5};
  glm::vec4 clip = viewProjection * glm::vec4{center, 1.f};
  float radius = glm::length(extent);
  if (clip.w <= 0.001f) return false;
  float allowance = radius / std::max(clip.w, 0.001f);
  glm::vec3 ndc = glm::vec3{clip} / clip.w;
  return ndc.x >= -1.f - allowance && ndc.x <= 1.f + allowance &&
         ndc.y >= -1.f - allowance && ndc.y <= 1.f + allowance &&
         ndc.z >= -allowance && ndc.z <= 1.f + allowance;
}

double GeoScene::distanceTo(const GeoTileRecord& tile, const glm::vec3& position) const {
  glm::dvec3 p{position};
  glm::dvec3 nearest = glm::clamp(p, tile.bounds.min, tile.bounds.max);
  return glm::length(p - nearest);
}

uint32_t GeoScene::desiredLod(const GeoTileRecord& tile, double distance) const {
  if (forceMaximumLod) return 2;
  if (policy == beacon::GeoRenderPolicy::FixedLod1) return 1;
  double importance = policy == beacon::GeoRenderPolicy::DistanceLod ? 1.0 : tile.semanticImportance;
  double adjusted = distance / std::max(importance, 0.25);
  if (adjusted < 260.0) return 2;
  if (adjusted < 700.0) return 1;
  return 0;
}

void GeoScene::queueLoad(uint32_t tileIndex, uint32_t lod, uint64_t frameNumber) {
  auto& state = *runtime[tileIndex];
  if ((state.residency == TileResidency::Queued || state.residency == TileResidency::LoadingCpu ||
       state.residency == TileResidency::AwaitingUpload) &&
      state.requestedLod == lod) {
    return;
  }
  state.requestedLod = lod;
  state.generation++;
  state.residency = TileResidency::Queued;
  state.lastUsedFrame = frameNumber;
  {
    std::lock_guard lock{mutex};
    requests.push_back({tileIndex, lod, state.generation, Clock::now()});
  }
  condition.notify_one();
  frameStats.requestedTiles++;
}

void GeoScene::schedule(
    const glm::vec3& cameraPosition, const glm::mat4& viewProjection, uint64_t frameNumber) {
  struct Candidate {
    uint32_t index;
    uint32_t lod;
    double score;
  };
  std::vector<Candidate> candidates;
  frameStats.visibleTiles = 0;
  frameStats.semanticUtility = 0.f;
  for (uint32_t index = 0; index < dataset.tiles.size(); ++index) {
    auto& tile = dataset.tiles[index];
    auto& state = *runtime[index];
    state.isVisible = visible(tile, viewProjection);
    if (!state.isVisible) continue;
    frameStats.visibleTiles++;
    state.lastVisibleFrame = frameNumber;
    double distance = distanceTo(tile, cameraPosition);
    uint32_t lod = desiredLod(tile, distance);
    double screenProxy = 1.0 / std::max(1.0, distance * distance);
    double quality = static_cast<double>(lod + 1);
    double bytes = std::max<uint64_t>(1, tile.representations[lod].bytes);
    double score = tile.semanticImportance * screenProxy * quality / bytes;
    candidates.push_back({index, lod, score});
    frameStats.semanticUtility += static_cast<float>(tile.semanticImportance * quality);
  }
  std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
    return a.score > b.score;
  });

  uint32_t changes = 0;
  uint64_t selectedBytes = 0;
  for (const auto& candidate : candidates) {
    auto& state = *runtime[candidate.index];
    auto& representation = dataset.tiles[candidate.index].representations[candidate.lod];
    if (selectedBytes + representation.bytes > budget.gpuMemoryBudgetBytes) continue;
    selectedBytes += representation.bytes;
    bool needsLoad = state.residency != TileResidency::Resident || state.residentLod != candidate.lod;
    bool lifetimeAllows = state.lifetimeTicks >= 30 || state.residency != TileResidency::Resident ||
                          candidate.lod > state.residentLod;
    if (needsLoad && lifetimeAllows && changes < budget.maxTileChangesPerFrame) {
      if (state.residency == TileResidency::Resident) {
        if (candidate.lod > state.residentLod) frameStats.upgradedTiles++;
        else frameStats.downgradedTiles++;
      }
      queueLoad(candidate.index, candidate.lod, frameNumber);
      changes++;
    } else if (state.residency == TileResidency::Resident) {
      state.lifetimeTicks++;
      state.lastUsedFrame = frameNumber;
    }
  }
  frameStats.representationChurn = changes;
}

void GeoScene::integrateUploads(float frameTime, uint64_t frameNumber) {
  uploadTokens += budget.uploadBudgetMiBPerSecond * 1024.0 * 1024.0 * frameTime;
  uploadTokens = std::min(uploadTokens, static_cast<double>(budget.gpuMemoryBudgetBytes));
  uint32_t changes = 0;
  std::vector<double> latencies;
  while (changes < budget.maxTileChangesPerFrame) {
    CompletedLoad result{};
    {
      std::lock_guard lock{mutex};
      if (completed.empty()) break;
      uint64_t bytes = dataset.tiles[completed.front().tileIndex]
                           .representations[completed.front().lod]
                           .bytes;
      if (cacheMode != beacon::GeoCacheMode::Warm && uploadTokens < bytes) break;
      result = std::move(completed.front());
      completed.pop_front();
      uploadTokens -= std::min(uploadTokens, static_cast<double>(bytes));
    }
    auto& state = *runtime[result.tileIndex];
    if (result.generation != state.generation) continue;
    if (!result.error.empty() || result.builder == nullptr || result.builder->vertices.empty()) {
      state.residency = TileResidency::Failed;
      frameStats.failedTiles++;
      continue;
    }
    uint64_t bytes = dataset.tiles[result.tileIndex].representations[result.lod].bytes;
    std::unique_ptr<LveModel> model;
    try {
      model = std::make_unique<LveModel>(device, *result.builder);
    } catch (const std::exception&) {
      state.residency = TileResidency::Failed;
      frameStats.failedTiles++;
      continue;
    }
    if (state.model != nullptr) retired.push_back({frameNumber + 3, std::move(state.model)});
    state.model = std::move(model);
    state.residentLod = result.lod;
    state.residency = TileResidency::Resident;
    state.lifetimeTicks = 0;
    state.lastUsedFrame = frameNumber;
    frameStats.uploadedBytes += bytes;
    latencies.push_back(std::chrono::duration<double, std::milli>(Clock::now() - result.queuedAt).count());
    changes++;
  }
  if (!latencies.empty()) frameStats.streamingLatencyP95Ms = percentile95(std::move(latencies));
  retired.erase(
      std::remove_if(
          retired.begin(),
          retired.end(),
          [&](const RetiredModel& entry) {
            return entry.retireAfterFrame <= frameNumber;
          }),
      retired.end());
}

void GeoScene::enforceMemoryBudget(uint64_t frameNumber) {
  frameStats.residentBytes = 0;
  frameStats.residentTiles = 0;
  for (uint32_t i = 0; i < runtime.size(); ++i) {
    if (runtime[i]->residency == TileResidency::Resident) {
      frameStats.residentTiles++;
      frameStats.residentBytes += dataset.tiles[i].representations[runtime[i]->residentLod].bytes;
    }
  }
  while (frameStats.residentBytes > budget.gpuMemoryBudgetBytes) {
    uint32_t victim = std::numeric_limits<uint32_t>::max();
    uint64_t oldest = std::numeric_limits<uint64_t>::max();
    for (uint32_t i = 0; i < runtime.size(); ++i) {
      auto& state = *runtime[i];
      if (state.residency == TileResidency::Resident && !state.isVisible && state.lastUsedFrame < oldest) {
        oldest = state.lastUsedFrame;
        victim = i;
      }
    }
    if (victim == std::numeric_limits<uint32_t>::max()) break;
    auto& state = *runtime[victim];
    uint64_t bytes = dataset.tiles[victim].representations[state.residentLod].bytes;
    retired.push_back({frameNumber + 3, std::move(state.model)});
    state.residency = TileResidency::Unloaded;
    frameStats.residentBytes -= bytes;
    frameStats.residentTiles--;
  }
  frameStats.budgetViolation = frameStats.residentBytes > budget.gpuMemoryBudgetBytes ? 1.f : 0.f;
}

void GeoScene::update(
    const glm::vec3& cameraPosition,
    const glm::mat4& viewProjection,
    float frameTime,
    uint64_t frameNumber,
    double measuredFrameMs) {
  frameStats = {};
  if (measuredFrameMs > 0.0) {
    frameTimeEwmaMs = frameTimeEwmaMs == 0.0 ? measuredFrameMs
                                             : frameTimeEwmaMs * 0.9 + measuredFrameMs * 0.1;
  }
  controllerAccumulator += frameTime;
  if (controllerAccumulator >= 0.1 || frameNumber == 0) {
    controllerAccumulator = std::fmod(controllerAccumulator, 0.1);
    if ((policy == beacon::GeoRenderPolicy::GeoBeaconExact ||
         policy == beacon::GeoRenderPolicy::GeoBeaconBounded) &&
        frameTimeEwmaMs > budget.targetFrameMs * 1.05) {
      budget.maxTileChangesPerFrame = std::max(1u, budget.maxTileChangesPerFrame - 1);
    } else if (frameTimeEwmaMs > 0.0 && frameTimeEwmaMs < budget.targetFrameMs * 0.8) {
      budget.maxTileChangesPerFrame = std::min(8u, budget.maxTileChangesPerFrame + 1);
    }
    schedule(cameraPosition, viewProjection, frameNumber);
  } else {
    for (uint32_t index = 0; index < runtime.size(); ++index) {
      auto& state = *runtime[index];
      if (state.isVisible) {
        frameStats.visibleTiles++;
        uint32_t quality = state.residency == TileResidency::Resident ? state.residentLod + 1u : 0u;
        frameStats.semanticUtility += dataset.tiles[index].semanticImportance * quality;
      }
    }
  }
  integrateUploads(frameTime, frameNumber);
  enforceMemoryBudget(frameNumber);
}

std::vector<GeoDrawItem> GeoScene::drawItems() const {
  std::vector<GeoDrawItem> items;
  items.reserve(frameStats.visibleTiles);
  for (uint32_t index = 0; index < runtime.size(); ++index) {
    const auto& state = *runtime[index];
    if (!state.isVisible || state.residency != TileResidency::Resident || state.model == nullptr) continue;
    items.push_back(
        {state.model.get(), glm::mat4{1.f}, dataset.tiles[index].tileId, representationFor(state.residentLod)});
  }
  return items;
}

}  // namespace lve::geo
