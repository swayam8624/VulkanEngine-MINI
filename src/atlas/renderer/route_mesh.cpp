#include "atlas/renderer/route_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <glm/gtc/constants.hpp>

namespace vulkax::atlas {
namespace {

glm::dvec3 directionFor(const GeodeticPosition& position) {
  const double latitude = glm::radians(position.latitudeDegrees);
  const double longitude = glm::radians(position.longitudeDegrees);
  const double cosLatitude = std::cos(latitude);
  return glm::normalize(glm::dvec3{
      cosLatitude * std::cos(longitude),
      std::sin(latitude),
      cosLatitude * std::sin(longitude),
  });
}

glm::dvec3 sphericalInterpolate(
    const glm::dvec3& from,
    const glm::dvec3& to,
    double amount) {
  const double cosine = std::clamp(glm::dot(from, to), -1.0, 1.0);
  const double angle = std::acos(cosine);
  if (angle < 1e-9) return glm::normalize(glm::mix(from, to, amount));
  const double sine = std::sin(angle);
  return glm::normalize(
      (std::sin((1.0 - amount) * angle) / sine) * from +
      (std::sin(amount * angle) / sine) * to);
}

glm::vec3 ellipsoidPoint(
    const glm::dvec3& direction,
    double altitudeMeters,
    float equatorialRadius,
    float surfaceOffset) {
  const float polarRadius =
      equatorialRadius *
      static_cast<float>(
          Wgs84::semiMinorAxisMeters / Wgs84::semiMajorAxisMeters);
  const float altitude =
      static_cast<float>(
          altitudeMeters / Wgs84::semiMajorAxisMeters) *
      equatorialRadius;
  return {
      static_cast<float>(direction.x) *
          (equatorialRadius + surfaceOffset + altitude),
      static_cast<float>(direction.y) *
          (polarRadius + surfaceOffset + altitude),
      static_cast<float>(direction.z) *
          (equatorialRadius + surfaceOffset + altitude),
  };
}

}  // namespace

GlobeMesh buildRouteRibbon(
    const std::vector<GeodeticPosition>& route,
    glm::vec3 color,
    float equatorialRadius,
    float width,
    float surfaceOffset,
    double maximumSegmentAngleDegrees) {
  if (route.size() < 2) {
    throw std::invalid_argument("a globe route requires at least two points");
  }
  if (equatorialRadius <= 0.0f || width <= 0.0f ||
      maximumSegmentAngleDegrees <= 0.0) {
    throw std::invalid_argument("invalid globe route dimensions");
  }

  std::vector<glm::vec3> centerline;
  for (size_t segment = 0; segment + 1 < route.size(); ++segment) {
    const glm::dvec3 from = directionFor(route[segment]);
    const glm::dvec3 to = directionFor(route[segment + 1]);
    const double angleDegrees =
        glm::degrees(std::acos(std::clamp(glm::dot(from, to), -1.0, 1.0)));
    const uint32_t subdivisions = std::max(
        1u,
        static_cast<uint32_t>(
            std::ceil(angleDegrees / maximumSegmentAngleDegrees)));
    for (uint32_t step = segment == 0 ? 0u : 1u;
         step <= subdivisions;
         ++step) {
      const double amount =
          static_cast<double>(step) / static_cast<double>(subdivisions);
      const glm::dvec3 direction =
          sphericalInterpolate(from, to, amount);
      const double altitude = glm::mix(
          route[segment].altitudeMeters,
          route[segment + 1].altitudeMeters,
          amount);
      centerline.push_back(
          ellipsoidPoint(
              direction, altitude, equatorialRadius, surfaceOffset));
    }
  }

  GlobeMesh mesh;
  mesh.vertices.reserve(centerline.size() * 2);
  mesh.indices.reserve((centerline.size() - 1) * 6);
  for (size_t index = 0; index < centerline.size(); ++index) {
    const glm::vec3 previous =
        centerline[index == 0 ? index : index - 1];
    const glm::vec3 next =
        centerline[index + 1 < centerline.size() ? index + 1 : index];
    const glm::vec3 radial = glm::normalize(centerline[index]);
    glm::vec3 tangent = next - previous;
    tangent -= radial * glm::dot(tangent, radial);
    if (glm::dot(tangent, tangent) < 1e-10f) {
      tangent = glm::cross(
          radial,
          std::abs(radial.y) < 0.9f ? glm::vec3{0.f, 1.f, 0.f}
                                   : glm::vec3{1.f, 0.f, 0.f});
    }
    const glm::vec3 side =
        glm::normalize(glm::cross(radial, glm::normalize(tangent))) *
        (width * 0.5f);
    const float u =
        static_cast<float>(index) /
        static_cast<float>(centerline.size() - 1);
    mesh.vertices.push_back(
        {centerline[index] - side, color, radial, {u, 0.f}});
    mesh.vertices.push_back(
        {centerline[index] + side, color, radial, {u, 1.f}});
  }
  for (uint32_t index = 0;
       index + 1 < static_cast<uint32_t>(centerline.size());
       ++index) {
    const uint32_t left = index * 2;
    mesh.indices.insert(
        mesh.indices.end(),
        {left, left + 2, left + 1, left + 1, left + 2, left + 3});
  }
  return mesh;
}

}  // namespace vulkax::atlas
