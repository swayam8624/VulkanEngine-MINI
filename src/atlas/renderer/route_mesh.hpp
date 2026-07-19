#pragma once

#include "atlas/core/geodesy.hpp"
#include "atlas/renderer/globe_mesh.hpp"

#include <vector>

namespace vulkax::atlas {

GlobeMesh buildRouteRibbon(
    const std::vector<GeodeticPosition>& route,
    glm::vec3 color = {0.05f, 0.95f, 0.45f},
    float equatorialRadius = 10.0f,
    float width = 0.045f,
    float surfaceOffset = 0.055f,
    double maximumSegmentAngleDegrees = 0.2);

}  // namespace vulkax::atlas
