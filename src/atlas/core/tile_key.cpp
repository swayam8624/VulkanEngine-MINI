#include "atlas/core/tile_key.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace vulkax::atlas {

bool AtlasTileKey::valid() const {
  if (level > 30) return false;
  const uint32_t dimension = 1u << level;
  return x < dimension && y < dimension;
}

AtlasTileKey AtlasTileKey::parent() const {
  if (!valid() || level == 0) return *this;
  return {face, static_cast<uint8_t>(level - 1), x / 2, y / 2, layer};
}

std::array<AtlasTileKey, 4> AtlasTileKey::children() const {
  if (!valid() || level >= 30) {
    throw std::out_of_range("Atlas tile cannot be subdivided");
  }
  const uint8_t childLevel = static_cast<uint8_t>(level + 1);
  return {{
      {face, childLevel, x * 2, y * 2, layer},
      {face, childLevel, x * 2 + 1, y * 2, layer},
      {face, childLevel, x * 2, y * 2 + 1, layer},
      {face, childLevel, x * 2 + 1, y * 2 + 1, layer},
  }};
}

std::string AtlasTileKey::toString() const {
  std::ostringstream output;
  output << vulkax::atlas::toString(layer) << "/"
         << vulkax::atlas::toString(face) << "/" << static_cast<int>(level)
         << "/" << x << "/" << y;
  return output.str();
}

CubeTileAddress directionToCube(const glm::dvec3& input) {
  const double length = glm::length(input);
  if (!std::isfinite(length) || length < 1e-15) {
    throw std::invalid_argument("cube direction must be non-zero");
  }
  const glm::dvec3 direction = input / length;
  const glm::dvec3 absolute = glm::abs(direction);
  CubeTileAddress result{};
  if (absolute.x >= absolute.y && absolute.x >= absolute.z) {
    if (direction.x >= 0.0) {
      result.face = CubeFace::PositiveX;
      result.uv = {-direction.z / absolute.x, direction.y / absolute.x};
    } else {
      result.face = CubeFace::NegativeX;
      result.uv = {direction.z / absolute.x, direction.y / absolute.x};
    }
  } else if (absolute.y >= absolute.x && absolute.y >= absolute.z) {
    if (direction.y >= 0.0) {
      result.face = CubeFace::PositiveY;
      result.uv = {direction.x / absolute.y, -direction.z / absolute.y};
    } else {
      result.face = CubeFace::NegativeY;
      result.uv = {direction.x / absolute.y, direction.z / absolute.y};
    }
  } else if (direction.z >= 0.0) {
    result.face = CubeFace::PositiveZ;
    result.uv = {direction.x / absolute.z, direction.y / absolute.z};
  } else {
    result.face = CubeFace::NegativeZ;
    result.uv = {-direction.x / absolute.z, direction.y / absolute.z};
  }
  result.uv = glm::clamp(result.uv, glm::dvec2{-1.0}, glm::dvec2{1.0});
  return result;
}

glm::dvec3 cubeToDirection(CubeFace face, const glm::dvec2& uv) {
  glm::dvec3 direction{};
  switch (face) {
    case CubeFace::PositiveX: direction = {1.0, uv.y, -uv.x}; break;
    case CubeFace::NegativeX: direction = {-1.0, uv.y, uv.x}; break;
    case CubeFace::PositiveY: direction = {uv.x, 1.0, -uv.y}; break;
    case CubeFace::NegativeY: direction = {uv.x, -1.0, uv.y}; break;
    case CubeFace::PositiveZ: direction = {uv.x, uv.y, 1.0}; break;
    case CubeFace::NegativeZ: direction = {-uv.x, uv.y, -1.0}; break;
  }
  return glm::normalize(direction);
}

AtlasTileKey directionToTile(
    const glm::dvec3& direction,
    uint8_t level,
    AtlasLayer layer) {
  if (level > 30) throw std::out_of_range("Atlas tile level exceeds 30");
  const auto cube = directionToCube(direction);
  const uint32_t dimension = 1u << level;
  const glm::dvec2 normalized = (cube.uv + 1.0) * 0.5;
  const uint32_t x = std::min(
      dimension - 1,
      static_cast<uint32_t>(std::floor(normalized.x * dimension)));
  const uint32_t y = std::min(
      dimension - 1,
      static_cast<uint32_t>(std::floor(normalized.y * dimension)));
  return {cube.face, level, x, y, layer};
}

glm::dvec3 tileCenterDirection(const AtlasTileKey& key) {
  if (!key.valid()) throw std::invalid_argument("invalid Atlas tile key");
  const double dimension = static_cast<double>(1u << key.level);
  const glm::dvec2 uv{
      ((static_cast<double>(key.x) + 0.5) / dimension) * 2.0 - 1.0,
      ((static_cast<double>(key.y) + 0.5) / dimension) * 2.0 - 1.0,
  };
  return cubeToDirection(key.face, uv);
}

double screenSpaceError(
    double geometricErrorMeters,
    double distanceMeters,
    double viewportHeightPixels,
    double verticalFieldOfViewRadians) {
  if (geometricErrorMeters <= 0.0) return 0.0;
  const double safeDistance = std::max(distanceMeters, 1e-6);
  const double denominator =
      2.0 * safeDistance * std::tan(verticalFieldOfViewRadians * 0.5);
  return geometricErrorMeters * viewportHeightPixels / denominator;
}

const char* toString(CubeFace face) {
  switch (face) {
    case CubeFace::PositiveX: return "px";
    case CubeFace::NegativeX: return "nx";
    case CubeFace::PositiveY: return "py";
    case CubeFace::NegativeY: return "ny";
    case CubeFace::PositiveZ: return "pz";
    case CubeFace::NegativeZ: return "nz";
  }
  return "unknown";
}

const char* toString(AtlasLayer layer) {
  switch (layer) {
    case AtlasLayer::Terrain: return "terrain";
    case AtlasLayer::Buildings: return "buildings";
    case AtlasLayer::Roads: return "roads";
    case AtlasLayer::LandUse: return "land-use";
    case AtlasLayer::Water: return "water";
    case AtlasLayer::Vegetation: return "vegetation";
    case AtlasLayer::Poi: return "poi";
    case AtlasLayer::Labels: return "labels";
    case AtlasLayer::Transit: return "transit";
    case AtlasLayer::Traffic: return "traffic";
  }
  return "unknown";
}

}  // namespace vulkax::atlas
