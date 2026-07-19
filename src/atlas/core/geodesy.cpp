#include "atlas/core/geodesy.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::atlas {
namespace {

constexpr double pi = 3.1415926535897932384626433832795;

double radians(double degrees) {
  return degrees * pi / 180.0;
}

double degrees(double radiansValue) {
  return radiansValue * 180.0 / pi;
}

}  // namespace

glm::dvec3 LocalFrame::toLocal(const EcefPosition& position) const {
  const glm::dvec3 delta = position.meters - origin.meters;
  return {glm::dot(delta, east), glm::dot(delta, north), glm::dot(delta, up)};
}

EcefPosition LocalFrame::toEcef(const glm::dvec3& localMeters) const {
  return {origin.meters + east * localMeters.x + north * localMeters.y +
          up * localMeters.z};
}

EcefPosition geodeticToEcef(const GeodeticPosition& position) {
  if (!std::isfinite(position.latitudeDegrees) ||
      !std::isfinite(position.longitudeDegrees) ||
      !std::isfinite(position.altitudeMeters) ||
      position.latitudeDegrees < -90.0 || position.latitudeDegrees > 90.0) {
    throw std::invalid_argument("invalid WGS84 geodetic position");
  }

  const double latitude = radians(position.latitudeDegrees);
  const double longitude = radians(position.longitudeDegrees);
  const double sinLatitude = std::sin(latitude);
  const double cosLatitude = std::cos(latitude);
  const double normalRadius =
      Wgs84::semiMajorAxisMeters /
      std::sqrt(1.0 - Wgs84::firstEccentricitySquared *
                          sinLatitude * sinLatitude);

  return {{
      (normalRadius + position.altitudeMeters) * cosLatitude *
          std::cos(longitude),
      (normalRadius + position.altitudeMeters) * cosLatitude *
          std::sin(longitude),
      (normalRadius * (1.0 - Wgs84::firstEccentricitySquared) +
       position.altitudeMeters) *
          sinLatitude,
  }};
}

GeodeticPosition ecefToGeodetic(const EcefPosition& position) {
  const double x = position.meters.x;
  const double y = position.meters.y;
  const double z = position.meters.z;
  const double horizontal = std::hypot(x, y);
  if (!std::isfinite(horizontal) || !std::isfinite(z) ||
      glm::length(position.meters) < 1.0) {
    throw std::invalid_argument("invalid ECEF position");
  }

  const double longitude = std::atan2(y, x);
  double latitude =
      std::atan2(z, horizontal * (1.0 - Wgs84::firstEccentricitySquared));
  double altitude = 0.0;
  for (int iteration = 0; iteration < 12; ++iteration) {
    const double sinLatitude = std::sin(latitude);
    const double normalRadius =
        Wgs84::semiMajorAxisMeters /
        std::sqrt(1.0 - Wgs84::firstEccentricitySquared *
                            sinLatitude * sinLatitude);
    altitude = horizontal / std::max(std::cos(latitude), 1e-15) -
               normalRadius;
    const double denominator =
        horizontal *
        (1.0 - Wgs84::firstEccentricitySquared * normalRadius /
                   (normalRadius + altitude));
    const double nextLatitude = std::atan2(z, denominator);
    if (std::abs(nextLatitude - latitude) < 1e-14) {
      latitude = nextLatitude;
      break;
    }
    latitude = nextLatitude;
  }

  return {
      degrees(latitude),
      degrees(longitude),
      altitude,
  };
}

LocalFrame makeLocalFrame(const GeodeticPosition& origin) {
  const double latitude = radians(origin.latitudeDegrees);
  const double longitude = radians(origin.longitudeDegrees);
  const double sinLatitude = std::sin(latitude);
  const double cosLatitude = std::cos(latitude);
  const double sinLongitude = std::sin(longitude);
  const double cosLongitude = std::cos(longitude);

  return {
      geodeticToEcef(origin),
      {-sinLongitude, cosLongitude, 0.0},
      {-sinLatitude * cosLongitude, -sinLatitude * sinLongitude,
       cosLatitude},
      {cosLatitude * cosLongitude, cosLatitude * sinLongitude, sinLatitude},
  };
}

glm::vec3 cameraRelative(
    const EcefPosition& worldPosition,
    const EcefPosition& cameraPosition) {
  return glm::vec3{worldPosition.meters - cameraPosition.meters};
}

bool isBeyondEllipsoidHorizon(
    const EcefPosition& camera,
    const EcefPosition& objectCenter,
    double objectRadiusMeters) {
  const double radius = Wgs84::semiMajorAxisMeters;
  const double cameraDistance = glm::length(camera.meters);
  if (cameraDistance <= radius || objectRadiusMeters >= radius) return false;

  const glm::dvec3 cameraDirection = camera.meters / cameraDistance;
  const double horizonPlaneDistance = radius * radius / cameraDistance;
  const double projectedObject = glm::dot(objectCenter.meters, cameraDirection);
  return projectedObject + std::max(0.0, objectRadiusMeters) <
         horizonPlaneDistance;
}

}  // namespace vulkax::atlas
