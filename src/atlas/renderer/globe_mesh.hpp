#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace vulkax::atlas {

struct GlobeVertex {
  glm::vec3 position{};
  glm::vec3 color{};
  glm::vec3 normal{};
  glm::vec2 uv{};
};

struct GlobeMesh {
  std::vector<GlobeVertex> vertices;
  std::vector<uint32_t> indices;
};

GlobeMesh buildWgs84Ellipsoid(
    uint32_t longitudeSegments = 192,
    uint32_t latitudeSegments = 96,
    float equatorialRadius = 10.0f);

}  // namespace vulkax::atlas
