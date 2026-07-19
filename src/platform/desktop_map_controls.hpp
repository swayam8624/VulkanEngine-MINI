#pragma once

#include "atlas/navigation/navigation.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace lve {

enum class DesktopMapActionKind {
  Search,
  Route,
  FollowRoute,
  ClearRoute
};

struct DesktopMapAction {
  DesktopMapActionKind kind = DesktopMapActionKind::Search;
  std::string query;
  std::string destinationId;
  vulkax::atlas::TravelMode mode = vulkax::atlas::TravelMode::Walking;
};

class DesktopMapControls {
 public:
  struct Impl;

  explicit DesktopMapControls(GLFWwindow* window);
  ~DesktopMapControls();

  DesktopMapControls(const DesktopMapControls&) = delete;
  DesktopMapControls& operator=(const DesktopMapControls&) = delete;

  bool available() const;
  std::optional<DesktopMapAction> pollAction();
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
