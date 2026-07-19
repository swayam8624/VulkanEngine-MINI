#include "atlas/renderer/globe_mesh.hpp"

#include "atlas/core/geodesy.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <glm/gtc/constants.hpp>

namespace vulkax::atlas {

GlobeMesh buildWgs84Ellipsoid(
    uint32_t longitudeSegments,
    uint32_t latitudeSegments,
    float equatorialRadius) {
  if (longitudeSegments < 8 || latitudeSegments < 4 ||
      equatorialRadius <= 0.0f) {
    throw std::invalid_argument("invalid WGS84 ellipsoid tessellation");
  }

  GlobeMesh mesh;
  mesh.vertices.reserve(
      static_cast<size_t>(longitudeSegments + 1) *
      static_cast<size_t>(latitudeSegments + 1));
  mesh.indices.reserve(
      static_cast<size_t>(longitudeSegments) *
      static_cast<size_t>(latitudeSegments) * 6);

  const float polarRadius =
      equatorialRadius *
      static_cast<float>(
          Wgs84::semiMinorAxisMeters / Wgs84::semiMajorAxisMeters);
  for (uint32_t latitudeIndex = 0;
       latitudeIndex <= latitudeSegments;
       ++latitudeIndex) {
    const float v =
        static_cast<float>(latitudeIndex) /
        static_cast<float>(latitudeSegments);
    const float latitude =
        (v - 0.5f) * glm::pi<float>();
    const float cosLatitude = std::cos(latitude);
    const float sinLatitude = std::sin(latitude);
    for (uint32_t longitudeIndex = 0;
         longitudeIndex <= longitudeSegments;
         ++longitudeIndex) {
      const float u =
          static_cast<float>(longitudeIndex) /
          static_cast<float>(longitudeSegments);
      const float longitude =
          (u * 2.0f - 1.0f) * glm::pi<float>();
      const float cosLongitude = std::cos(longitude);
      const float sinLongitude = std::sin(longitude);
      const glm::vec3 position{
          equatorialRadius * cosLatitude * cosLongitude,
          polarRadius * sinLatitude,
          equatorialRadius * cosLatitude * sinLongitude,
      };
      const glm::vec3 normal = glm::normalize(
          glm::vec3{
              position.x / (equatorialRadius * equatorialRadius),
              position.y / (polarRadius * polarRadius),
              position.z / (equatorialRadius * equatorialRadius),
          });

      const bool latitudeLine =
          latitudeIndex % std::max(1u, latitudeSegments / 18) == 0;
      const bool longitudeLine =
          longitudeIndex % std::max(1u, longitudeSegments / 36) == 0;
      const float polarAmount = std::abs(sinLatitude);
      glm::vec3 color =
          glm::mix(glm::vec3{0.025f, 0.18f, 0.33f},
                   glm::vec3{0.08f, 0.42f, 0.62f},
                   1.0f - polarAmount);
      if (polarAmount > 0.88f) {
        color = glm::mix(color, glm::vec3{0.75f, 0.88f, 0.92f},
                         (polarAmount - 0.88f) / 0.12f);
      }
      if (latitudeLine || longitudeLine) color *= 1.18f;
      mesh.vertices.push_back(
          {position, glm::clamp(color, 0.0f, 1.0f), normal, {u, v}});
    }
  }

  const uint32_t stride = longitudeSegments + 1;
  for (uint32_t latitudeIndex = 0;
       latitudeIndex < latitudeSegments;
       ++latitudeIndex) {
    for (uint32_t longitudeIndex = 0;
         longitudeIndex < longitudeSegments;
         ++longitudeIndex) {
      const uint32_t topLeft = latitudeIndex * stride + longitudeIndex;
      const uint32_t bottomLeft = topLeft + stride;
      mesh.indices.insert(
          mesh.indices.end(),
          {
              topLeft,
              bottomLeft,
              topLeft + 1,
              topLeft + 1,
              bottomLeft,
              bottomLeft + 1,
          });
    }
  }
  return mesh;
}

}  // namespace vulkax::atlas
