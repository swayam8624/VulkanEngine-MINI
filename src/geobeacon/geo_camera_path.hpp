#pragma once

#include "beacon/benchmark_config.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace lve::geo {

struct GeoCameraSample {
  glm::vec3 position{};
  glm::vec3 rotation{};
};

GeoCameraSample sampleCameraPath(
    beacon::GeoCameraPath path,
    uint64_t simulationTick,
    float fixedTimeStep = 1.f / 60.f);

}  // namespace lve::geo
