#include "geobeacon/geo_camera_path.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace lve::geo {
namespace {

glm::vec3 lookAtRotation(const glm::vec3& position, const glm::vec3& target) {
  glm::vec3 direction = glm::normalize(target - position);
  return {-std::asin(std::clamp(direction.y, -1.f, 1.f)),
          std::atan2(direction.x, direction.z),
          0.f};
}

GeoCameraSample sampleLinear(glm::vec3 start, glm::vec3 end, glm::vec3 target, float phase) {
  float smooth = phase * phase * (3.f - 2.f * phase);
  glm::vec3 position = glm::mix(start, end, smooth);
  return {position, lookAtRotation(position, target)};
}

}  // namespace

GeoCameraSample sampleCameraPath(
    beacon::GeoCameraPath path,
    uint64_t simulationTick,
    float fixedTimeStep) {
  float time = static_cast<float>(simulationTick) * fixedTimeStep;
  constexpr glm::vec3 center{0.f, -20.f, 0.f};
  switch (path) {
    case beacon::GeoCameraPath::OuterOrbit: {
      float angle = time * 0.12f;
      glm::vec3 position{900.f * std::sin(angle), -260.f, 900.f * std::cos(angle)};
      return {position, lookAtRotation(position, center)};
    }
    case beacon::GeoCameraPath::StreetDrive: {
      float phase = std::fmod(time / 30.f, 1.f);
      glm::vec3 position{
          -700.f + 1400.f * phase, -4.5f, 35.f * std::sin(phase * glm::two_pi<float>())};
      glm::vec3 target = position + glm::vec3{80.f, 2.f, 0.f};
      return {position, lookAtRotation(position, target)};
    }
    case beacon::GeoCameraPath::IntersectionDwell: {
      float angle = time * 0.2f;
      glm::vec3 position{90.f * std::sin(angle), -35.f, 90.f * std::cos(angle)};
      return {position, lookAtRotation(position, center)};
    }
    case beacon::GeoCameraPath::LandmarkApproach: {
      float phase = std::fmod(time / 24.f, 1.f);
      return sampleLinear({-650.f, -80.f, -500.f}, {-75.f, -18.f, -70.f}, center, phase);
    }
    case beacon::GeoCameraPath::RapidTeleport: {
      constexpr std::array<glm::vec3, 5> positions{{
          {-750.f, -100.f, -650.f},
          {720.f, -80.f, -540.f},
          {650.f, -120.f, 620.f},
          {-680.f, -70.f, 580.f},
          {0.f, -40.f, 0.f},
      }};
      uint32_t index = static_cast<uint32_t>(time / 2.f) % positions.size();
      glm::vec3 position = positions[index];
      return {position, lookAtRotation(position, center)};
    }
  }
  return {};
}

}  // namespace lve::geo
