#include "platform/desktop_map_controls.hpp"

namespace lve {

struct DesktopMapControls::Impl {};

DesktopMapControls::DesktopMapControls(GLFWwindow*)
    : impl{std::make_unique<Impl>()} {}
DesktopMapControls::~DesktopMapControls() = default;
bool DesktopMapControls::available() const { return false; }
std::optional<DesktopMapAction> DesktopMapControls::pollAction() {
  return std::nullopt;
}
void DesktopMapControls::setSearchResults(
    const std::vector<vulkax::atlas::SearchResult>&) {}
void DesktopMapControls::setStatus(const std::string&) {}
void DesktopMapControls::setRouteSummary(
    const std::string&, double, double) {}

}  // namespace lve
