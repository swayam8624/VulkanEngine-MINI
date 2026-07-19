#pragma once

#include <glm/glm.hpp>

namespace vulkax::atlas {

struct GeodeticPosition {
  double latitudeDegrees = 0.0;
  double longitudeDegrees = 0.0;
  double altitudeMeters = 0.0;
};

struct EcefPosition {
  glm::dvec3 meters{};
};

struct LocalFrame {
  EcefPosition origin;
  glm::dvec3 east{};
  glm::dvec3 north{};
  glm::dvec3 up{};

  glm::dvec3 toLocal(const EcefPosition& position) const;
  EcefPosition toEcef(const glm::dvec3& localMeters) const;
};

struct Wgs84 {
  static constexpr double semiMajorAxisMeters = 6378137.0;
  static constexpr double flattening = 1.0 / 298.257223563;
  static constexpr double semiMinorAxisMeters =
      semiMajorAxisMeters * (1.0 - flattening);
  static constexpr double firstEccentricitySquared =
      flattening * (2.0 - flattening);
};

EcefPosition geodeticToEcef(const GeodeticPosition& position);
GeodeticPosition ecefToGeodetic(const EcefPosition& position);
LocalFrame makeLocalFrame(const GeodeticPosition& origin);
glm::vec3 cameraRelative(
    const EcefPosition& worldPosition,
    const EcefPosition& cameraPosition);

bool isBeyondEllipsoidHorizon(
    const EcefPosition& camera,
    const EcefPosition& objectCenter,
    double objectRadiusMeters);

}  // namespace vulkax::atlas
