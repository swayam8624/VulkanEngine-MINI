#pragma once

#include "atlas/navigation/navigation.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace lve {

enum class DesktopMapActionKind {
  SwitchCity,
  Search,
  Route,
  FollowRoute,
  ClearRoute
};

struct DesktopMapAction {
  DesktopMapActionKind kind = DesktopMapActionKind::Search;
  std::string query;
  std::string destinationId;
  std::string cityId;
  vulkax::atlas::TravelMode mode = vulkax::atlas::TravelMode::Walking;
};

struct DesktopCityOption {
  std::string id;
  std::string displayName;
  std::string status;
  bool selected = false;
};

class DesktopMapControls {
 public:
  struct Impl;

  DesktopMapControls(GLFWwindow* window, std::string mapName);
  ~DesktopMapControls();

  DesktopMapControls(const DesktopMapControls&) = delete;
  DesktopMapControls& operator=(const DesktopMapControls&) = delete;

  bool available() const;
  std::optional<DesktopMapAction> pollAction();
  void setMapName(const std::string& mapName);
  void setCities(const std::vector<DesktopCityOption>& cities);
  void setSearchQuery(const std::string& query);
  void setSearchResults(
      const std::vector<vulkax::atlas::SearchResult>& results);
  void setStatus(const std::string& status);
  void setRouteSummary(
      const std::string& destination,
      double distanceMeters,
      double durationSeconds);

 private:
  std::unique_ptr<Impl> impl;
};

}  // namespace lve
