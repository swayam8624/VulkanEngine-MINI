#include "platform/desktop_map_controls.hpp"

namespace lve {

struct DesktopMapControls::Impl {};

DesktopMapControls::DesktopMapControls(GLFWwindow*, std::string)
    : impl{std::make_unique<Impl>()} {}
DesktopMapControls::~DesktopMapControls() = default;
bool DesktopMapControls::available() const { return false; }
std::optional<DesktopMapAction> DesktopMapControls::pollAction() {
  return std::nullopt;
}
void DesktopMapControls::setMapName(const std::string&) {}
void DesktopMapControls::setCities(const std::vector<DesktopCityOption>&) {}
void DesktopMapControls::setSearchQuery(const std::string&) {}
void DesktopMapControls::setSearchResults(
    const std::vector<vulkax::atlas::SearchResult>&) {}
void DesktopMapControls::setStatus(const std::string&) {}
void DesktopMapControls::setRouteSummary(
    const std::string&, double, double) {}

}  // namespace lve
