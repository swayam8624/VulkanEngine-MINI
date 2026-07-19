#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <glm/glm.hpp>

namespace vulkax::atlas {

enum class CubeFace : uint8_t {
  PositiveX,
  NegativeX,
  PositiveY,
  NegativeY,
  PositiveZ,
  NegativeZ
};

enum class AtlasLayer : uint8_t {
  Terrain,
  Buildings,
  Roads,
  LandUse,
  Water,
  Vegetation,
  Poi,
  Labels,
  Transit,
  Traffic
};

struct AtlasTileKey {
  CubeFace face = CubeFace::PositiveX;
  uint8_t level = 0;
  uint32_t x = 0;
  uint32_t y = 0;
  AtlasLayer layer = AtlasLayer::Terrain;

  bool valid() const;
  AtlasTileKey parent() const;
  std::array<AtlasTileKey, 4> children() const;
  std::string toString() const;

  auto operator<=>(const AtlasTileKey&) const = default;
};

struct CubeTileAddress {
  CubeFace face = CubeFace::PositiveX;
  glm::dvec2 uv{};
};

CubeTileAddress directionToCube(const glm::dvec3& direction);
glm::dvec3 cubeToDirection(CubeFace face, const glm::dvec2& uv);
AtlasTileKey directionToTile(
    const glm::dvec3& direction,
    uint8_t level,
    AtlasLayer layer);
glm::dvec3 tileCenterDirection(const AtlasTileKey& key);
double screenSpaceError(
    double geometricErrorMeters,
    double distanceMeters,
    double viewportHeightPixels,
    double verticalFieldOfViewRadians);

const char* toString(CubeFace face);
const char* toString(AtlasLayer layer);

}  // namespace vulkax::atlas
